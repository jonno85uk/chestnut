/* 
 * Olive. Olive is a free non-linear video editor for Windows, macOS, and Linux.
 * Copyright (C) 2018  {{ organization }}
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 *along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "viewerwidget.h"

#include <QPainter>
#include <QAudioOutput>
#include <QOpenGLShaderProgram>
#include <QtMath>
#include <QOpenGLFramebufferObject>
#include <QMouseEvent>
#include <QMimeData>
#include <QDrag>
#include <QMenu>
#include <QOffscreenSurface>
#include <QFileDialog>
#include <QPolygon>
#include <QDesktopWidget>
#include <QInputDialog>
#include <QApplication>
#include <QScreen>
#include <QMessageBox>
#include <iostream>

#include "panels/panelmanager.h"
#include "project/sequence.h"
#include "project/clip.h"
#include "project/effect.h"
#include "project/transition.h"
#include "playback/playback.h"
#include "playback/audio.h"
#include "project/footage.h"
#include "io/config.h"
#include "debug.h"
#include "io/math.h"
#include "ui/collapsiblewidget.h"
#include "project/undo.h"
#include "project/media.h"
#include "ui/viewercontainer.h"
#include "io/avtogl.h"
#include "ui/timelinewidget.h"
#include "ui/renderfunctions.h"
#include "ui/renderthread.h"
#include "ui/viewerwindow.h"
#include "ui/mainwindow.h"
#include "io/exportthread.h"



extern "C" {
#include <libavformat/avformat.h>
}

constexpr int SURFACE_DEPTH = 24;
constexpr int SURFACE_SAMPLES = 16;
constexpr double DEFAULT_WAVEFORM_ZOOM = 1.0;
constexpr float GIZMO_Z = 0.0;
constexpr auto FRAME_NAME_FILTER = "Portable Network Graphic (*.png);;JPEG (*.jpg);;Windows Bitmap (*.bmp);;"
                                   "Portable Pixmap (*.ppm);;X11 Bitmap (*.xbm);;X11 Pixmap (*.xpm)";

using panels::PanelManager;

ViewerWidget::ViewerWidget(QWidget *parent) :
  QOpenGLWidget(parent),
  waveform(false),
  waveform_zoom(DEFAULT_WAVEFORM_ZOOM),
  waveform_scroll(0),
  dragging(false),
  gizmos(nullptr),
  selected_gizmo(nullptr),
  window(nullptr)
{
  setMouseTracking(true);
  setFocusPolicy(Qt::ClickFocus);

  QSurfaceFormat format;
  format.setDepthBufferSize(SURFACE_DEPTH);
  format.setSamples(SURFACE_SAMPLES);
  setFormat(format);

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(show_context_menu()));

  renderer = new RenderThread();
  renderer->start(QThread::HighPriority);
  connect(renderer, SIGNAL(ready()), this, SLOT(queue_repaint()));
  connect(renderer, SIGNAL(finished()), renderer, SLOT(deleteLater()));
  connect(renderer, &RenderThread::frameGrabbed, this, &ViewerWidget::frameGrabbed);
  connect(&PanelManager::histogram(), &panels::HistogramViewer::drawClippedPixels, renderer, &RenderThread::drawClippedPixels);
}

ViewerWidget::~ViewerWidget()
{
  if (window != nullptr) {
    window->close();
    delete window;
  }

  if (renderer != nullptr) {
    renderer->cancel();
    delete renderer;
  }
  delete_function();
}

void ViewerWidget::delete_function()
{
  if (viewer != nullptr && viewer->getSequence()) {
    viewer->getSequence()->closeActiveClips();
  }
}

void ViewerWidget::set_waveform_scroll(int s) {
  if (waveform) {
    waveform_scroll = s;
    update();
  }
}

void ViewerWidget::show_context_menu()
{
  QMenu menu(this);

  QAction* save_frame_as_image = menu.addAction(tr("Save Frame as Image..."));
  connect(save_frame_as_image, SIGNAL(triggered(bool)), this, SLOT(save_frame()));

  QMenu* fullscreen_menu = menu.addMenu(tr("Show Fullscreen"));
  QList<QScreen*> screens = QGuiApplication::screens();
  if (window != nullptr && window->isVisible()) {
    fullscreen_menu->addAction(tr("Disable"));
  }
  for (int i = 0; i < screens.size(); i++) {
    QAction* screen_action = fullscreen_menu->addAction(tr("Screen %1: %2x%3").arg(
                                                          QString::number(i),
                                                          QString::number(screens.at(i)->size().width()),
                                                          QString::number(screens.at(i)->size().height())));
    Q_ASSERT(screen_action);
    screen_action->setData(i);
  }
  connect(fullscreen_menu, SIGNAL(triggered(QAction*)), this, SLOT(fullscreen_menu_action(QAction*)));

  QMenu zoom_menu(tr("Zoom"));
  QAction* fit_zoom = zoom_menu.addAction(tr("Fit"));
  Q_ASSERT(fit_zoom);
  connect(fit_zoom, SIGNAL(triggered(bool)), this, SLOT(set_fit_zoom()));
  zoom_menu.addAction("10%")->setData(0.1);
  zoom_menu.addAction("25%")->setData(0.25);
  zoom_menu.addAction("50%")->setData(0.5);
  zoom_menu.addAction("75%")->setData(0.75);
  zoom_menu.addAction("100%")->setData(1.0);
  zoom_menu.addAction("150%")->setData(1.5);
  zoom_menu.addAction("200%")->setData(2.0);
  zoom_menu.addAction("400%")->setData(4.0);
  QAction* custom_zoom = zoom_menu.addAction(tr("Custom"));
  Q_ASSERT(custom_zoom);
  connect(custom_zoom, SIGNAL(triggered(bool)), this, SLOT(set_custom_zoom()));
  connect(&zoom_menu, SIGNAL(triggered(QAction*)), this, SLOT(set_menu_zoom(QAction*)));
  menu.addMenu(&zoom_menu);

  if (!viewer->is_main_sequence()) {
    menu.addAction(tr("Close Media"), viewer, SLOT(close_media()));
  }

  menu.exec(QCursor::pos());
}

void ViewerWidget::save_frame()
{
  QFileDialog fd(this);
  fd.setAcceptMode(QFileDialog::AcceptSave);
  fd.setFileMode(QFileDialog::AnyFile);
  fd.setWindowTitle(tr("Save Frame"));
  fd.setNameFilter(FRAME_NAME_FILTER);

  if (fd.exec()) {
    frame_file_name_ = fd.selectedFiles().front();
    const QString selected_ext(fd.selectedNameFilter().mid(fd.selectedNameFilter().indexOf(QRegExp("\\*.[a-z][a-z][a-z]")) + 1, 4));
    if (!frame_file_name_.endsWith(selected_ext,  Qt::CaseInsensitive)) {
      frame_file_name_ += selected_ext;
    }
    save_frame_ = true;
    renderer->start_render(context(), viewer->getSequence(), true);
  }
}

void ViewerWidget::queue_repaint()
{
  update();
}

void ViewerWidget::fullscreen_menu_action(QAction *action)
{
  if (window == nullptr) {
    QMessageBox::critical(this, tr("Error"), tr("Failed to create viewer window"));
  } else {
    if (action->data().isNull()) {
      window->hide();
    } else {
      QScreen* selected_screen = QGuiApplication::screens().at(action->data().toInt());
      window->showFullScreen();
      window->setGeometry(selected_screen->geometry());

      // HACK: window seems to show with distorted texture on first showing, so we queue an update after it's shown
      QTimer::singleShot(100, window, SLOT(update()));
    }
  }
}

void ViewerWidget::set_fit_zoom() {
  container->fit = true;
  container->adjust();
}

void ViewerWidget::set_custom_zoom()
{
  bool ok;
  double d = QInputDialog::getDouble(this,
                                     tr("Viewer Zoom"),
                                     tr("Set Custom Zoom Value:"),
                                     container->zoom*100, 0, 2147483647, 2, &ok);
  if (ok) {
    container->fit = false;
    container->zoom = d * 0.01;
    container->adjust();
  }
}

void ViewerWidget::set_menu_zoom(QAction* action)
{
  const QVariant& actionData = action->data();
  if (!actionData.isNull()) {
    container->fit = false;
    container->zoom = actionData.toDouble();
    container->adjust();
  }
}


void ViewerWidget::frameGrabbed(QImage img)
{
  if (save_frame_) {
    img.save(frame_file_name_);
    save_frame_ = false;
    frame_file_name_.clear();
  }
}

void ViewerWidget::retry()
{
  update();
}

void ViewerWidget::initializeGL()
{
  initializeOpenGLFunctions();

  connect(context(), SIGNAL(aboutToBeDestroyed()), this, SLOT(context_destroy()), Qt::DirectConnection);

  window = new ViewerWindow(context());
}

void ViewerWidget::frame_update()
{
  if (auto sqn = viewer->getSequence()) {
    const auto render_audio = (viewer->playing || audio_rendering);
    // send context to other thread for drawing
    if (waveform) {
      update();
    } else {
      doneCurrent();

      auto grab = false;
      if (PanelManager::histogram().isVisible()) { //FIXME: isVisible is not correct usage
        grab = true;
        connect(renderer, &RenderThread::frameGrabbed, &PanelManager::histogram(), &panels::HistogramViewer::frameGrabbed);
      } else {
        // not visible
        disconnect(renderer, &RenderThread::frameGrabbed, &PanelManager::histogram(), &panels::HistogramViewer::frameGrabbed);
      }

      if (PanelManager::colorScope().isVisible()) {
        grab = true;
        connect(renderer, &RenderThread::frameGrabbed, &PanelManager::colorScope(), &panels::ScopeViewer::frameGrabbed);
      } else {
        disconnect(renderer, &RenderThread::frameGrabbed, &PanelManager::colorScope(), &panels::ScopeViewer::frameGrabbed);
      }

      renderer->start_render(context(), sqn, grab);
    }

    // render the audio
    compose_audio(viewer, sqn, render_audio);
  }
}

RenderThread *ViewerWidget::get_renderer()
{
  return renderer;
}

void ViewerWidget::seek_from_click(int x)
{
  viewer->seek(getFrameFromScreenPoint(waveform_zoom, x + waveform_scroll));
}


void ViewerWidget::context_destroy()
{
  makeCurrent();
  if ((viewer->getSequence() != nullptr) && viewer->getSequence()) {
    viewer->getSequence()->closeActiveClips();
  }
  if (window != nullptr) {
    delete window;
  }
  //QMetaObject::invokeMethod(renderer, "delete_ctx", Qt::QueuedConnection);
  renderer->delete_ctx();
  doneCurrent();
}

EffectGizmoPtr ViewerWidget::get_gizmo_from_mouse(int x, int y)
{
  if (gizmos == nullptr) {
    return nullptr;
  }

  double multiplier = static_cast<double>(viewer->getSequence()->width()) / width();
  QPoint mouse_pos(qRound(x * multiplier), qRound((height() - y) * multiplier));
  int dot_size = 2 * qRound(GIZMO_DOT_SIZE * multiplier);
  int target_size = 2 * qRound(GIZMO_TARGET_SIZE * multiplier);
  for (int i = 0; i < gizmos->gizmo_count(); i++) {
    EffectGizmoPtr g = gizmos->gizmo(i);
    Q_ASSERT(g);

    switch (g->get_type()) {
      case GizmoType::DOT:
        if (mouse_pos.x() > g->screen_pos[0].x() - dot_size
            && mouse_pos.y() > g->screen_pos[0].y() - dot_size
            && mouse_pos.x() < g->screen_pos[0].x() + dot_size
            && mouse_pos.y() < g->screen_pos[0].y() + dot_size) {
          return g;
        }
        break;
      case GizmoType::POLY:
        if (QPolygon(g->screen_pos).containsPoint(mouse_pos, Qt::OddEvenFill)) {
          return g;
        }
        break;
      case GizmoType::TARGET:
        if (mouse_pos.x() > g->screen_pos[0].x() - target_size
            && mouse_pos.y() > g->screen_pos[0].y() - target_size
            && mouse_pos.x() < g->screen_pos[0].x() + target_size
            && mouse_pos.y() < g->screen_pos[0].y() + target_size) {
          return g;
        }
        break;
      default:
        qWarning() << "Unhandled Gizmo type" << static_cast<int>(g->get_type());
        break;
    }

  }
  return nullptr;
}

void ViewerWidget::move_gizmos(QMouseEvent *event, bool done)
{
  if (selected_gizmo != nullptr) {
    const auto multiplier = static_cast<double>(viewer->getSequence()->width()) / width();

    const auto x_movement = qRound((event->pos().x() - drag_start_x) * multiplier);
    const auto y_movement = qRound((event->pos().y() - drag_start_y) * multiplier);

    gizmos->gizmo_move(selected_gizmo,
                       x_movement, y_movement,
                       gizmos->parent_clip->timecode(gizmos->parent_clip->sequence->playhead_),
                       done);

    gizmo_x_mvmt += x_movement;
    gizmo_y_mvmt += y_movement;

    drag_start_x = event->pos().x();
    drag_start_y = event->pos().y();

    gizmos->field_changed();
  }
}

void ViewerWidget::mousePressEvent(QMouseEvent* event)
{
  Q_ASSERT(event);
  if (waveform) {
    seek_from_click(event->x());
  } else if ( (event->buttons() & Qt::MiddleButton) || (PanelManager::timeLine().tool == TimelineToolType::HAND) ) {
    container->dragScrollPress(event->pos());
  } else {
    drag_start_x = event->pos().x();
    drag_start_y = event->pos().y();

    gizmo_x_mvmt = 0;
    gizmo_y_mvmt = 0;

    selected_gizmo = get_gizmo_from_mouse(event->pos().x(), event->pos().y());

    if (selected_gizmo != nullptr) {
      selected_gizmo->set_previous_value();
    }
  }

  if (event->buttons() & Qt::LeftButton) {
    dragging = true;
  }
}

void ViewerWidget::mouseMoveEvent(QMouseEvent* event)
{
  Q_ASSERT(event);
  unsetCursor();
  if (PanelManager::timeLine().tool == TimelineToolType::HAND) {
    setCursor(Qt::OpenHandCursor);
  }

  if (dragging) {
    if (waveform) {
      seek_from_click(event->x());
    } else if ( (event->buttons() & Qt::MiddleButton) || PanelManager::timeLine().tool == TimelineToolType::HAND) {
      container->dragScrollMove(event->pos());
    } else if (gizmos == nullptr) {
      auto drag = new QDrag(this);
      auto mimeData = new QMimeData;
      mimeData->setText("h"); // QMimeData will fail without some kind of data
      drag->setMimeData(mimeData);
      drag->exec();
      dragging = false;
    } else {
      move_gizmos(event, false);
    }
  } else if (EffectGizmoPtr g = get_gizmo_from_mouse(event->pos().x(), event->pos().y())) {
    if (g->get_cursor() > -1) {
      setCursor(static_cast<enum Qt::CursorShape>(g->get_cursor()));
    }
  }
}

void ViewerWidget::mouseReleaseEvent(QMouseEvent *event)
{
  Q_ASSERT(event);
  if (dragging && (gizmos != nullptr) && !( (event->button() == Qt::MiddleButton)
                                            || (PanelManager::timeLine().tool == TimelineToolType::HAND))) {
    move_gizmos(event, true);
  }
  dragging = false;
}

void ViewerWidget::close_window()
{
  if (window != nullptr) {
    window->hide();
  }
}

void ViewerWidget::draw_waveform_func()
{
  QPainter p(this);
  if (viewer->getSequence()->workarea_.using_) {
    int in_x = getScreenPointFromFrame(waveform_zoom, viewer->getSequence()->workarea_.in_) - waveform_scroll;
    int out_x = getScreenPointFromFrame(waveform_zoom, viewer->getSequence()->workarea_.out_) - waveform_scroll;

    p.fillRect(QRect(in_x, 0, out_x - in_x, height()), QColor(255, 255, 255, 64));
    p.setPen(Qt::white);
    p.drawLine(in_x, 0, in_x, height());
    p.drawLine(out_x, 0, out_x, height());
  }
  QRect wr = rect();
  wr.setX(wr.x() - waveform_scroll);

  p.setPen(Qt::green);
  auto stream = waveform_ms.lock();
  if (stream == nullptr) {
    qCritical() << "No stream to draw waveform";
    return;
  }
  draw_waveform(waveform_clip, stream, waveform_clip->timeline_info.out, p, wr,
                waveform_scroll, width() + waveform_scroll, waveform_zoom);
  p.setPen(Qt::red);
  const auto playhead_x = getScreenPointFromFrame(waveform_zoom, viewer->getSequence()->playhead_) - waveform_scroll;
  p.drawLine(playhead_x, 0, playhead_x, height());
}

void ViewerWidget::draw_title_safe_area()
{
  double halfWidth = 0.5;
  double halfHeight = 0.5;
  const double viewportAr = static_cast<double>(width()) / height();
  const double halfAr = viewportAr * 0.5;

  if (e_config.use_custom_title_safe_ratio && (e_config.custom_title_safe_ratio > 0) ) {
    if (e_config.custom_title_safe_ratio > viewportAr) {
      halfHeight = (e_config.custom_title_safe_ratio / viewportAr) * 0.5;
    } else {
      halfWidth = (viewportAr / e_config.custom_title_safe_ratio) * 0.5;
    }
  }

  glLoadIdentity();
  glOrtho(-halfWidth, halfWidth, halfHeight, -halfHeight, 0, 1);

  glColor4f(0.66f, 0.66f, 0.66f, 1.0f);
  glBegin(GL_LINES);

  // action safe rectangle
  glVertex2d(-0.45, -0.45);
  glVertex2d(0.45, -0.45);
  glVertex2d(0.45, -0.45);
  glVertex2d(0.45, 0.45);
  glVertex2d(0.45, 0.45);
  glVertex2d(-0.45, 0.45);
  glVertex2d(-0.45, 0.45);
  glVertex2d(-0.45, -0.45);

  // title safe rectangle
  glVertex2d(-0.4, -0.4);
  glVertex2d(0.4, -0.4);
  glVertex2d(0.4, -0.4);
  glVertex2d(0.4, 0.4);
  glVertex2d(0.4, 0.4);
  glVertex2d(-0.4, 0.4);
  glVertex2d(-0.4, 0.4);
  glVertex2d(-0.4, -0.4);

  // horizontal centers
  glVertex2d(-0.45, 0);
  glVertex2d(-0.375, 0);
  glVertex2d(0.45, 0);
  glVertex2d(0.375, 0);

  // vertical centers
  glVertex2d(0, -0.45);
  glVertex2d(0, -0.375);
  glVertex2d(0, 0.45);
  glVertex2d(0, 0.375);

  glEnd();

  // center cross
  glLoadIdentity();
  glOrtho(-halfAr, halfAr, 0.5, -0.5, -1, 1);

  glBegin(GL_LINES);

  glVertex2d(-0.05, 0);
  glVertex2d(0.05, 0);
  glVertex2d(0, -0.05);
  glVertex2d(0, 0.05);

  glEnd();
}

void ViewerWidget::drawDot(const EffectGizmoPtr& g)
{
  const float dot_size = GIZMO_DOT_SIZE / width() * viewer->getSequence()->width();
  glBegin(GL_QUADS);
  glVertex3f(g->screen_pos[0].x() - dot_size, g->screen_pos[0].y() - dot_size, GIZMO_Z);
  glVertex3f(g->screen_pos[0].x() + dot_size, g->screen_pos[0].y() - dot_size, GIZMO_Z);
  glVertex3f(g->screen_pos[0].x() + dot_size, g->screen_pos[0].y() + dot_size, GIZMO_Z);
  glVertex3f(g->screen_pos[0].x() - dot_size, g->screen_pos[0].y() + dot_size, GIZMO_Z);
  glEnd();
}

void ViewerWidget::drawLines(const EffectGizmoPtr& g)
{
  glBegin(GL_LINES);
  for (int k = 1; k < g->get_point_count(); k++) {
    glVertex3f(g->screen_pos[k-1].x(), g->screen_pos[k - 1].y(), GIZMO_Z);
    glVertex3f(g->screen_pos[k].x(), g->screen_pos[k].y(), GIZMO_Z);
  }
  glVertex3f(g->screen_pos[g->get_point_count() - 1].x(), g->screen_pos[g->get_point_count() - 1].y(), GIZMO_Z);
  glVertex3f(g->screen_pos[0].x(), g->screen_pos[0].y(), GIZMO_Z);
  glEnd();
}

void ViewerWidget::drawTarget(const EffectGizmoPtr& g)
{
  const float target_size = GIZMO_TARGET_SIZE / width() * viewer->getSequence()->width();
  glBegin(GL_LINES);
  glVertex3f(g->screen_pos[0].x() - target_size, g->screen_pos[0].y() - target_size, GIZMO_Z);
  glVertex3f(g->screen_pos[0].x() + target_size, g->screen_pos[0].y() - target_size, GIZMO_Z);

  glVertex3f(g->screen_pos[0].x() + target_size, g->screen_pos[0].y()-  target_size, GIZMO_Z);
  glVertex3f(g->screen_pos[0].x() + target_size, g->screen_pos[0].y() + target_size, GIZMO_Z);

  glVertex3f(g->screen_pos[0].x() + target_size, g->screen_pos[0].y() + target_size, GIZMO_Z);
  glVertex3f(g->screen_pos[0].x() - target_size, g->screen_pos[0].y() + target_size, GIZMO_Z);

  glVertex3f(g->screen_pos[0].x() - target_size, g->screen_pos[0].y() + target_size, GIZMO_Z);
  glVertex3f(g->screen_pos[0].x() - target_size, g->screen_pos[0].y() - target_size, GIZMO_Z);

  glVertex3f(g->screen_pos[0].x() - target_size, g->screen_pos[0].y(), GIZMO_Z);
  glVertex3f(g->screen_pos[0].x() + target_size, g->screen_pos[0].y(), GIZMO_Z);

  glVertex3f(g->screen_pos[0].x(), g->screen_pos[0].y() - target_size, GIZMO_Z);
  glVertex3f(g->screen_pos[0].x(), g->screen_pos[0].y() + target_size, GIZMO_Z);
  glEnd();
}

void ViewerWidget::draw_gizmos()
{
  float color[4];
  glGetFloatv(GL_CURRENT_COLOR, color);

  glPushMatrix();
  glLoadIdentity();
  glOrtho(0, viewer->getSequence()->width(), viewer->getSequence()->height(), 0, -1, 10);

  for (int j=0;j<gizmos->gizmo_count();j++) {
    EffectGizmoPtr g = gizmos->gizmo(j);
    glColor4f(static_cast<GLfloat>(g->color.redF()),
              static_cast<GLfloat>(g->color.greenF()),
              static_cast<GLfloat>(g->color.blueF()),
              1.0);
    switch (g->get_type()) {
      case GizmoType::DOT: // draw dot
        drawDot(g);
        break;
      case GizmoType::POLY: // draw lines
        drawLines(g);
        break;
      case GizmoType::TARGET: // draw target
        drawTarget(g);
        break;
      default:
        qWarning() << "Unhandled Gizmo type" << static_cast<int>(g->get_type());
        break;
    }
  }
  glPopMatrix();

  glColor4f(color[0], color[1], color[2], color[3]);
}

void ViewerWidget::paintGL()
{
  if (waveform) {
    draw_waveform_func();
  } else {
    renderer->mutex.lock();

    makeCurrent();

    // clear to solid black

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_MULTISAMPLE);

    // set color multipler to straight white
    glColor4f(1.0, 1.0, 1.0, 1.0);

    glEnable(GL_TEXTURE_2D);

    // set screen coords to widget size
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1);

    // draw texture from render thread

    glBindTexture(GL_TEXTURE_2D, renderer->texColorBuffer);

    glBegin(GL_QUADS);

    glVertex2f(0, 0);
    glTexCoord2f(0, 0);
    glVertex2f(0, 1);
    glTexCoord2f(1, 0);
    glVertex2f(1, 1);
    glTexCoord2f(1, 1);
    glVertex2f(1, 0);
    glTexCoord2f(0, 1);

    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);

    // draw title/action safe area
    if (e_config.show_title_safe_area) {
      draw_title_safe_area();
    }

    gizmos = renderer->gizmos;
    if (gizmos != nullptr) {
      draw_gizmos();
    }

    glDisable(GL_TEXTURE_2D);

    if (window != nullptr && window->isVisible()) {
      window->set_texture(renderer->texColorBuffer,
                          static_cast<double>(viewer->getSequence()->width()) / viewer->getSequence()->height(),
                          &renderer->mutex);
    }

    renderer->mutex.unlock();

    if (renderer->did_texture_fail()) {
      doneCurrent();
      renderer->start_render(context(), viewer->getSequence());
    }
  }
}
