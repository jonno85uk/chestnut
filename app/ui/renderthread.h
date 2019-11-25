#ifndef RENDERTHREAD_H
#define RENDERTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>

#include "project/sequence.h"
#include "project/effect.h"

class RenderThread : public QThread {
    Q_OBJECT
  public:
    QMutex mutex;
    GLuint texColorBuffer {0};
    EffectPtr gizmos {nullptr};

    RenderThread();
    virtual ~RenderThread() override;

    RenderThread(const RenderThread& ) = delete;
    RenderThread(const RenderThread&& ) = delete;
    RenderThread& operator=(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&&) = delete;

    void paint();
    void setAsExporting(const bool value);
    void start_render(QOpenGLContext* share, SequenceWPtr s, const bool grab=false, GLvoid *pixel_buffer=nullptr);
    bool did_texture_fail();
    void cancel();
  protected:
    void run() override;
  public slots:
    // cleanup functions
    void delete_ctx();
    void drawClippedPixels(const bool state) noexcept;
  signals:
    void ready();
    void frameGrabbed(const QImage& img);
  private:
    // cleanup functions
    void delete_texture();
    void delete_fbo();

    GLuint frameBuffer {0};
    QWaitCondition waitCond;
    QOffscreenSurface surface;
    QOpenGLContext* share_ctx {nullptr};
    QOpenGLContext* ctx {nullptr};
    SequenceWPtr seq;
    int divider {0};
    int tex_width {-1};
    int tex_height {-1};
    bool queued {false};
    bool texture_failed {false};
    bool running {true};
    bool frame_grabbing_ {false};
    std::atomic_bool draw_clipped_{false};
    GLvoid* pix_buf_{nullptr};
    std::atomic_bool exporting_ {false};
};

#endif // RENDERTHREAD_H
