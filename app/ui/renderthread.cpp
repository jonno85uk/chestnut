#include "renderthread.h"

#include <QApplication>
#include <QImage>
#include <QOpenGLFunctions>
#include <QMutexLocker>

#include "ui/renderfunctions.h"
#include "playback/playback.h"
#include "project/sequence.h"
#include "panels/panelmanager.h"

RenderThread::RenderThread()
{
  surface.create();
}

RenderThread::~RenderThread()
{
  surface.destroy();
}

void RenderThread::run()
{
  QMutexLocker locker(&mutex);
  while (running) {
    if (!queued) {
      waitCond.wait(&mutex);
    }
    if (!running) {
      break;
    }
    queued = false;

    if (share_ctx == nullptr) {
      qCritical() << "Shared Context instance is NULL";
      continue;
    }
    if (ctx != nullptr) {
      ctx->makeCurrent(&surface);

      // gen fbo
      if (frameBuffer == 0) {
        delete_fbo();
        ctx->functions()->glGenFramebuffers(1, &frameBuffer);
      }

      // bind
      ctx->functions()->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer);

      if (auto sequenceNow = seq.lock()) {
        // gen texture
        if ( (texColorBuffer == 0) || (tex_width != sequenceNow->width()) || (tex_height != sequenceNow->height()) ) {
          delete_texture();
          glGenTextures(1, &texColorBuffer);
          glBindTexture(GL_TEXTURE_2D, texColorBuffer);
          glTexImage2D(GL_TEXTURE_2D,
                       0,
                       GL_RGB,
                       sequenceNow->width(),
                       sequenceNow->height(),
                       0,
                       GL_RGB,
                       GL_UNSIGNED_BYTE,
                       nullptr);
          tex_width = sequenceNow->width();
          tex_height = sequenceNow->height();
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          ctx->functions()->glFramebufferTexture2D(GL_FRAMEBUFFER,
                                                   GL_COLOR_ATTACHMENT0,
                                                   GL_TEXTURE_2D,
                                                   texColorBuffer,
                                                   0);
          glBindTexture(GL_TEXTURE_2D, 0);
        }

        // draw
        paint();

        // flush changes
        glFinish();

        // release
        ctx->functions()->glBindFramebuffer(GL_FRAMEBUFFER, 0);

        emit ready();
      } else {
        qCritical() << "Sequence instance is NULL";
      }
    } else {
      qCritical() << "Context instance is NULL";
    }
  }

  delete_ctx();
}

void RenderThread::paint()
{
  glLoadIdentity();

  texture_failed = false;

  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  glClearColor(0, 0, 0, 0);
  glMatrixMode(GL_MODELVIEW);

  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glEnable(GL_DEPTH);

  QVector<ClipPtr> nests;
  if (const SequencePtr& sequenceNow = seq.lock()) {
    compose_sequence(nullptr, ctx, sequenceNow, nests, true, false, gizmos, texture_failed, false,
                     (exporting_ || panels::PanelManager::sequenceViewer().usingEffects()));

    if (frame_grabbing_) {
      if (texture_failed) {
        // texture failed, try again
        queued = true;
      } else {
        // used when saving a frame as a picture
        QImage img(tex_width, tex_height, QImage::Format_RGBA8888);
        ctx->functions()->glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer);
        glReadPixels(0, 0, tex_width, tex_height, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        ctx->functions()->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        emit frameGrabbed(img);
        frame_grabbing_ = false;
      }
    } else if (pix_buf_ != nullptr) {
      // used on exporting sequence
      ctx->functions()->glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer);
      glReadPixels(0, 0, tex_width, tex_height, GL_RGBA, GL_UNSIGNED_BYTE, pix_buf_);
      ctx->functions()->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      pix_buf_ = nullptr;
    }

    glDisable(GL_DEPTH);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
  }
}


void RenderThread::setAsExporting(const bool value)
{
  exporting_ = value;
}

void RenderThread::start_render(QOpenGLContext *share, SequenceWPtr s, const bool grab, GLvoid* pixel_buffer)
{
  seq = std::move(s);

  // stall any dependent actions
  texture_failed = true;

  if ( (share != nullptr) && ( (ctx == nullptr) || (ctx->shareContext() != share_ctx) )) {
    share_ctx = share;
    delete_ctx();
    ctx = new QOpenGLContext();
    ctx->setFormat(share_ctx->format());
    ctx->setShareContext(share_ctx);
    ctx->create();
    ctx->moveToThread(this);
  }

  frame_grabbing_ = grab;
  pix_buf_ = pixel_buffer;

  queued = true;
  waitCond.wakeAll();
}

bool RenderThread::did_texture_fail()
{
  return texture_failed;
}

void RenderThread::cancel()
{
  running = false;
  waitCond.wakeAll();
  wait();
}

void RenderThread::delete_texture()
{
  if (texColorBuffer > 0) {
    ctx->functions()->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glDeleteTextures(1, &texColorBuffer);
  }
  texColorBuffer = 0;
}

void RenderThread::delete_fbo()
{
  if (frameBuffer > 0) {
    ctx->functions()->glDeleteFramebuffers(1, &frameBuffer);
  }
  frameBuffer = 0;
}

void RenderThread::delete_ctx()
{
  if (ctx != nullptr) {
    delete_texture();
    delete_fbo();
    ctx->doneCurrent();
    delete ctx;
  }
  ctx = nullptr;
}

// FIXME: this does nothing
void RenderThread::drawClippedPixels(const bool state) noexcept
{
  draw_clipped_ = state;
}
