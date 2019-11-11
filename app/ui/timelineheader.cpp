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
#include "timelineheader.h"

#include <QPainter>
#include <QMouseEvent>
#include <QScrollBar>
#include <QtMath>
#include <QMenu>
#include <QAction>

#include "ui/mainwindow.h"
#include "panels/panelmanager.h"
#include "project/sequence.h"
#include "project/undo.h"
#include "io/config.h"
#include "debug.h"


constexpr int CLICK_RANGE = 5;
constexpr int PLAYHEAD_SIZE = 6;
constexpr int LINE_MIN_PADDING = 50;
constexpr int SUBLINE_MIN_PADDING = 50; //TODO: play with this
constexpr int MARKER_SIZE = 4;
constexpr int MARKER_OUTLINE_WIDTH = 3;

using panels::PanelManager;

bool center_scroll_to_playhead(QScrollBar* bar, double zoom, long playhead) {
  // returns true is the scroll was changed, false if not
  int target_scroll = qMin(bar->maximum(), qMax(0, getScreenPointFromFrame(zoom, playhead)-(bar->width()>>1)));
  if (target_scroll == bar->value()) {
    return false;
  }
  bar->setValue(target_scroll);
  return true;
}

TimelineHeader::TimelineHeader(QWidget *parent) :
  QWidget(parent),
  fm(font())
{
  height_actual = fm.height();
  setCursor(Qt::ArrowCursor);
  setMouseTracking(true);
  setFocusPolicy(Qt::ClickFocus);
  show_text(true);

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(show_context_menu(const QPoint &)));
}


TimelineHeader::~TimelineHeader()
{
  viewer_ = nullptr;
}

void TimelineHeader::set_scroll(int s)
{
  scroll = s;
  update();
}

long TimelineHeader::getHeaderFrameFromScreenPoint(int x)
{
  return getFrameFromScreenPoint(zoom, x + scroll) + in_visible;
}

int TimelineHeader::getHeaderScreenPointFromFrame(long frame)
{
  return getScreenPointFromFrame(zoom, frame - in_visible) - scroll;
}

void TimelineHeader::set_playhead(int mouse_x)
{
  long frame = getHeaderFrameFromScreenPoint(mouse_x);
  if (snapping) {
    PanelManager::timeLine().snap_to_timeline(&frame, false, true, true);
  }
  if (auto sqn = viewer_->getSequence(); sqn->playhead_ != frame) {
    viewer_->seek(frame);
  }
}

void TimelineHeader::set_visible_in(long i)
{
  in_visible = i;
  update();
}

void TimelineHeader::set_in_point(long new_in)
{
  if (auto sqn = viewer_->getSequence()) {
    auto new_out = sqn->workarea_.out_;
    if (new_out == new_in) {
      new_in--;
    } else if (new_out < new_in) {
      new_out = sqn->endFrame();
    }

    e_undo_stack.push(new SetTimelineInOutCommand(sqn, true, new_in, new_out));
    update_parents();
  }
}

void TimelineHeader::set_out_point(long new_out) {
  if (auto sqn = viewer_->getSequence()) {
    auto new_in = sqn->workarea_.in_;
    if (new_out == new_in) {
      new_out++;
    } else if ( (new_in > new_out) || (new_in < 0) ) {
      new_in = 0;
    }

    e_undo_stack.push(new SetTimelineInOutCommand(sqn, true, new_in, new_out));
    update_parents();
  }
}

void TimelineHeader::set_scrollbar_max(QScrollBar* bar, long sequence_end_frame, int offset)
{
  bar->setMaximum(qMax(0, getScreenPointFromFrame(zoom, sequence_end_frame) - offset));
}

void TimelineHeader::show_text(bool enable)
{
  text_enabled = enable;
  const auto height = enable ? height_actual * 2 : height_actual;
  setFixedHeight(height);
  update();
}

void TimelineHeader::mousePressEvent(QMouseEvent* event)
{
  auto sqn = viewer_->getSequence();
  if (sqn == nullptr) {
    return;
  }
  if (event->buttons() & Qt::LeftButton) {
    if (resizing_workarea) {
      sequence_end = sqn->endFrame();
    } else {
      MarkerPtr mark;
      const bool shift = (event->modifiers() & Qt::ShiftModifier);
      bool clicked_on_marker = false;
      for (int i = 0; i < sqn->markers_.size(); i++) {
        mark = sqn->markers_.at(i);
        if (mark == nullptr) {
          continue;
        }
        const int marker_pos = getHeaderScreenPointFromFrame(mark->frame);
        if ( (event->pos().x() > (marker_pos - MARKER_SIZE)) && (event->pos().x() < (marker_pos + MARKER_SIZE) )) {
          bool found = false;
          for (int j = 0; j < selected_markers.size(); j++) {
            if (selected_markers.at(j) != i) {
              continue;
            }
            if (shift) {
              selected_markers.removeAt(j);
            }
            found = true;
            break;
          }

          if (!found) {
            if (!shift) {
              selected_markers.clear();
            }
            selected_markers.append(i);
          }
          clicked_on_marker = true;
          update();
          break;
        }
      }//for

      if (clicked_on_marker) {
        selected_marker_original_times.resize(selected_markers.size());
        for (int i = 0; i < selected_markers.size(); i++) {
          mark = sqn->markers_.at(selected_markers.at(i));
          if (mark) {
            selected_marker_original_times[i] = mark->frame;
          }
        }
        drag_start = event->pos().x();
        dragging_markers = true;
      } else {
        if (!selected_markers.empty()) {
          selected_markers.clear();
          update();
        }
        set_playhead(event->pos().x());
      }
    }
    dragging = true;
  }
}

void TimelineHeader::mouseMoveEvent(QMouseEvent* event) {
  auto sqn = viewer_->getSequence();
  if (sqn == nullptr) {
    return;
  }
  if (dragging) {
    if (resizing_workarea) {
      long frame = getHeaderFrameFromScreenPoint(event->pos().x());
      if (snapping) {
        PanelManager::timeLine().snap_to_timeline(&frame, true, true, false);
      }

      if (resizing_workarea_in) {
        temp_workarea_in = qMax(qMin(temp_workarea_out - 1, frame), 0L);
      } else {
        temp_workarea_out = qMin(qMax(temp_workarea_in + 1, frame), sequence_end);
      }

      update_parents();
    } else if (dragging_markers) {
      long frame_movement = getHeaderFrameFromScreenPoint(event->pos().x()) - getHeaderFrameFromScreenPoint(drag_start);

      // snap markers
      for (int i = 0; i<selected_markers.size(); i++) {
        long fmv = selected_marker_original_times.at(i) + frame_movement;
        if (snapping && PanelManager::timeLine().snap_to_timeline(&fmv, true, false, true)) {
          frame_movement = fmv - selected_marker_original_times.at(i);
          break;
        }
      }

      // validate markers (ensure none go below 0)
      long validator;
      for (int i = 0; i < selected_markers.size(); i++) {
        validator = selected_marker_original_times.at(i) + frame_movement;
        if (validator < 0) {
          frame_movement -= validator;
        }
      }

      // move markers
      for (int i = 0; i < selected_markers.size(); ++i) {
        if (MarkerPtr mark = sqn->markers_.at(selected_markers.at(i))) {
          mark->frame = selected_marker_original_times.at(i) + frame_movement;
        }
      }

      update_parents();
    } else {
      set_playhead(event->pos().x());
    }
  } else {
    resizing_workarea = false;
    unsetCursor();
    if (sqn != nullptr && sqn->workarea_.using_) {
      const long min_frame = getHeaderFrameFromScreenPoint(event->pos().x() - CLICK_RANGE) - 1;
      const long max_frame = getHeaderFrameFromScreenPoint(event->pos().x() + CLICK_RANGE) + 1;
      if (sqn->workarea_.in_ > min_frame && sqn->workarea_.in_ < max_frame) {
        resizing_workarea = true;
        resizing_workarea_in = true;
      } else if (sqn->workarea_.out_ > min_frame && sqn->workarea_.out_ < max_frame) {
        resizing_workarea = true;
        resizing_workarea_in = false;
      }
      if (resizing_workarea) {
        temp_workarea_in = sqn->workarea_.in_;
        temp_workarea_out = sqn->workarea_.out_;
        setCursor(Qt::SizeHorCursor);
      }
    }
  }
}

void TimelineHeader::mouseReleaseEvent(QMouseEvent*)
{
  auto sqn = viewer_->getSequence();
  if (sqn == nullptr) {
    return;
  }
  dragging = false;
  if (resizing_workarea) {
    e_undo_stack.push(new SetTimelineInOutCommand(sqn, true, temp_workarea_in, temp_workarea_out));
  } else if (dragging_markers && !selected_markers.empty()) {
    bool moved = false;
    auto ca = new ComboAction();
    for (int i = 0; i < selected_markers.size(); i++) {
      if (MarkerPtr m = sqn->markers_.at(selected_markers.at(i))) {
        if (selected_marker_original_times.at(i) != m->frame) {
          ca->append(new MoveMarkerAction(m, selected_marker_original_times.at(i), m->frame));
          moved = true;
        }
      }
    }
    if (moved) {
      e_undo_stack.push(ca);
    } else {
      delete ca;
    }
  }

  resizing_workarea = false;
  dragging = false;
  dragging_markers = false;
  PanelManager::timeLine().snapped = false;
  update_parents();
}

void TimelineHeader::focusOutEvent(QFocusEvent*)
{
  selected_markers.clear();
  update();
}

void TimelineHeader::update_parents()
{
  viewer_->update_parents();
}

void TimelineHeader::update_zoom(double z)
{
  zoom = z;
  update();
}

double TimelineHeader::get_zoom()
{
  return zoom;
}

void TimelineHeader::delete_markers()
{
  if (auto sqn = viewer_->getSequence()) {
    if (!selected_markers.empty()) {
      auto dma = new DeleteMarkerAction(sqn);
      for (int i = 0; i < selected_markers.size(); i++) {
        dma->markers.append(selected_markers.at(i));
      }
      e_undo_stack.push(dma);
      update_parents();
    }
  }
}

void TimelineHeader::paintEvent(QPaintEvent*)
{
  auto sqn = viewer_->getSequence();
  if ( (sqn == nullptr) || (zoom <= 0.0) ) {
    return;
  }
  QPainter p(this);
  const int yoff = (text_enabled) ? height()/2 : 0;

  const double interval = sqn->frameRate();
  int textWidth = 0;
  int lastTextBoundary = INT_MIN;

  int i = 0;
  int lastLineX = INT_MIN;

  int sublineCount = 1;
  int sublineTest = qRound(interval*zoom);
  int sublineInterval = 1;
  while ( (sublineTest > SUBLINE_MIN_PADDING) && (sublineInterval >= 1) ) {
    sublineCount *= 2;
    sublineInterval = qRound(interval/sublineCount);
    sublineTest = qRound(sublineInterval * zoom);
  }
  sublineCount = qMin(sublineCount, qRound(interval));

  int text_x;
  int fullTextWidth;
  QString timecode;

  while (true) {
    const long frame = qRound(interval*i);
    const int lineX = qRound(frame*zoom) - scroll;

    if (lineX > width()) {
      break;
    }

    // draw text
    bool draw_text = false;
    if (text_enabled && (lineX-textWidth > lastTextBoundary) ) {
      timecode = frame_to_timecode(frame + in_visible, global::config.timecode_view, sqn->frameRate());
      fullTextWidth = fm.horizontalAdvance(timecode);
      textWidth = fullTextWidth >> 1;
      text_x = lineX - textWidth;
      lastTextBoundary = lineX + textWidth;
      if (lastTextBoundary >= 0) {
        draw_text = true;
      }
    }

    if (lineX > (lastLineX + LINE_MIN_PADDING) ) {
      if (draw_text) {
        p.setPen(Qt::white);
        p.drawText(QRect(text_x, 0, fullTextWidth, yoff), timecode);
      }

      // draw line markers
      p.setPen(Qt::gray);
      p.drawLine(lineX, yoff, lineX, height());

      // draw sub-line markers
      for (int j = 1; j < sublineCount; j++) {
        const int sublineX = lineX + (qRound(j * interval / sublineCount) * zoom);
        p.drawLine(sublineX, yoff, sublineX, yoff+(height()/4));
      }

      lastLineX = lineX;
    }

    // TODO wastes cycles here, could just bring it up to 0
    i++;
  }//while

  // draw in/out selection
  int in_x;
  if (sqn->workarea_.using_) {
    in_x = getHeaderScreenPointFromFrame((resizing_workarea ? temp_workarea_in : sqn->workarea_.in_));
    const int out_x = getHeaderScreenPointFromFrame((resizing_workarea ? temp_workarea_out : sqn->workarea_.out_));
    p.fillRect(QRect(in_x, 0, out_x-in_x, height()), sqn->workarea_.enabled_ ? QColor(0, 192, 255, 128)
                                                                             : QColor(255, 255, 255, 64));
    p.setPen(Qt::white);
    p.drawLine(in_x, 0, in_x, height());
    p.drawLine(out_x, 0, out_x, height());
  }

  // draw markers
  for (int j=0; j < sqn->markers_.size(); ++j) {
    const MarkerPtr& m = sqn->markers_.at(j);
    if (m == nullptr) {
      continue;
    }
    const int marker_x = getHeaderScreenPointFromFrame(m->frame);
    const QPoint points[5] = {
      QPoint(marker_x, height() - 1),
      QPoint(marker_x + MARKER_SIZE, height() - MARKER_SIZE - 1),
      QPoint(marker_x + MARKER_SIZE, yoff),
      QPoint(marker_x - MARKER_SIZE, yoff),
      QPoint(marker_x - MARKER_SIZE, height() - MARKER_SIZE - 1)
    };

    bool selected = false;
    for (int k = 0; k < selected_markers.size(); ++k) {
      if (selected_markers.at(k) == j) {
        selected = true;
        break;
      }
    }

    if (selected) {
      QColor invert;
      invert.setRed(255 - m->color_.red());
      invert.setGreen(255 - m->color_.green());
      invert.setBlue(255 - m->color_.blue());
      QPen pen(invert);
      pen.setWidth(MARKER_OUTLINE_WIDTH);
      p.setPen(pen);
    } else {
      p.setPen(m->color_);
    }
    p.setBrush(m->color_);
    p.drawPolygon(points, 5);
  }

  // draw playhead triangle
  p.setRenderHint(QPainter::Antialiasing);

  in_x = getHeaderScreenPointFromFrame(sqn->playhead_);
  const QPoint start(in_x, height() + 2);
  QPainterPath path;
  path.moveTo(start + QPoint(1, 0));
  path.lineTo(in_x - PLAYHEAD_SIZE, yoff);
  path.lineTo(in_x + PLAYHEAD_SIZE + 1, yoff);
  path.lineTo(start);
  p.fillPath(path, Qt::red);
}

void TimelineHeader::show_context_menu(const QPoint &pos)
{
  QMenu menu(this);

  MainWindow::instance().make_inout_menu(menu);

  menu.exec(mapToGlobal(pos));
}

void TimelineHeader::resized_scroll_listener(double d)
{
  update_zoom(zoom * d);
}
