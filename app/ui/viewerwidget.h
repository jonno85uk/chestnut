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
#ifndef VIEWERWIDGET_H
#define VIEWERWIDGET_H

#include <QOpenGLWidget>
#include <QMatrix4x4>
#include <QOpenGLTexture>
#include <QTimer>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>

#include "project/effect.h"
#include "project/clip.h"
#include "chestnut.h"


constexpr double DEFAULT_WAVEFORM_ZOOM = 1.0;

class Viewer;
class Clip;
class FootageStream;
class QOpenGLFramebufferObject;
class EffectGizmo;
class ViewerContainer;
struct GLTextureCoords;
class RenderThread;
class ViewerWindow;

class ViewerWidget : public QOpenGLWidget, QOpenGLFunctions
{
    Q_OBJECT
public:
    Viewer* viewer {nullptr};
    ViewerContainer* container {nullptr};
    bool waveform {false};
    ClipPtr waveform_clip {nullptr};
    project::FootageStreamWPtr waveform_ms;
    double waveform_zoom {DEFAULT_WAVEFORM_ZOOM};
    int waveform_scroll {0};

    explicit ViewerWidget(QWidget *parent = nullptr);
    virtual ~ViewerWidget() override;
    ViewerWidget() = delete;
    ViewerWidget(const ViewerWidget&) = delete;
    ViewerWidget(const ViewerWidget&&) = delete;
    ViewerWidget& operator=(const ViewerWidget&) = delete;
    ViewerWidget& operator=(const ViewerWidget&&) = delete;

    void delete_function();
    void close_window();

    void initializeGL() override;

    void frame_update();
    RenderThread* get_renderer() noexcept;
    void enableGizmos(const bool enable) noexcept;
public slots:
    void set_waveform_scroll(int s);
protected:
    virtual void paintGL() override;
    virtual void mousePressEvent(QMouseEvent *event) override;
    virtual void mouseMoveEvent(QMouseEvent *event) override;
    virtual void mouseReleaseEvent(QMouseEvent *event) override;
private:
    bool dragging {false};
    EffectPtr gizmos {nullptr};
    chestnut::Coordinate_T drag_start_;
    chestnut::Coordinate_T gizmo_mvmt_;
    EffectGizmoPtr selected_gizmo {nullptr};
    RenderThread* renderer {nullptr};
    ViewerWindow* window {nullptr};
    QString frame_file_name_;
    bool save_frame_ {false};
    bool gizmos_enabled_ {false};

    void draw_waveform_func();
    void draw_title_safe_area();
    void draw_gizmos();
    EffectGizmoPtr get_gizmo_from_mouse(const int x, const int y);
    void move_gizmos(QMouseEvent *event, bool done);
    void seek_from_click(int x);

    void drawDot(const EffectGizmoPtr& g) const;
    void drawLines(const EffectGizmoPtr& g) const;
    void drawTarget(const EffectGizmoPtr& g) const;

private slots:
    void context_destroy();
    void retry();
    void show_context_menu();
    void save_frame();
    void queue_repaint();
    void fullscreen_menu_action(QAction* action);
    void set_fit_zoom();
    void set_custom_zoom();
    void set_menu_zoom(QAction *action);
    void frameGrabbed(QImage img);

};

#endif // VIEWERWIDGET_H
