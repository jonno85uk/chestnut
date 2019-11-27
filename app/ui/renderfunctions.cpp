#include "renderfunctions.h"

#include <QOpenGLFramebufferObject>
#include <QApplication>
#include <QDesktopWidget>
#include <QDebug>

#include "project/clip.h"
#include "project/sequence.h"
#include "project/media.h"
#include "project/effect.h"
#include "project/footage.h"
#include "project/transition.h"
#include "panels/panelmanager.h"

#include "ui/collapsiblewidget.h"

#include "playback/audio.h"
#include "playback/playback.h"

#include "io/math.h"
#include "io/config.h"
#include "io/avtogl.h"

#include "panels/timeline.h"
#include "panels/viewer.h"

extern "C" {
#include <libavformat/avformat.h>
}

#define GL_DEFAULT_BLEND glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE)

GLuint draw_clip(QOpenGLContext* ctx, QOpenGLFramebufferObject* fbo, const GLuint texture, const bool clear)
{
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0, 1, 0, 1, -1, 1);

  GLint current_fbo = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &current_fbo);

  fbo->bind();

  if (clear) {
    glClear(GL_COLOR_BUFFER_BIT);
  }

  // get current blend mode
  GLint src_rgb, src_alpha, dst_rgb, dst_alpha;
  glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_alpha);
  glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_alpha);

  ctx->functions()->GL_DEFAULT_BLEND;

  glBindTexture(GL_TEXTURE_2D, texture);
  glBegin(GL_QUADS);
  glTexCoord2f(0, 0); // top left
  glVertex2f(0, 0); // top left
  glTexCoord2f(1, 0); // top right
  glVertex2f(1, 0); // top right
  glTexCoord2f(1, 1); // bottom right
  glVertex2f(1, 1); // bottom right
  glTexCoord2f(0, 1); // bottom left
  glVertex2f(0, 1); // bottom left
  glEnd();
  glBindTexture(GL_TEXTURE_2D, 0);

  ctx->functions()->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_fbo);

  // restore previous blendFunc
  ctx->functions()->glBlendFuncSeparate(src_rgb, dst_rgb, src_alpha, dst_alpha);

  glPopMatrix();
  return fbo->texture();
}

void process_effect(QOpenGLContext* ctx,
                    ClipPtr& c,
                    EffectPtr& e,
                    double timecode,
                    GLTextureCoords& coords,
                    GLuint& composite_texture,
                    bool& fbo_switcher,
                    bool& texture_failed,
                    int data)
{
  if (!e->is_enabled()) {
    return;
  }

  if (e->hasCapability(Capability::COORDS)) {
    e->process_coords(timecode, coords, data);
  }

  if (( e->hasCapability(Capability::SHADER) && shaders_are_enabled) || e->hasCapability(Capability::SUPERIMPOSE)) {
    e->startEffect();
    if ((e->hasCapability(Capability::SHADER) && shaders_are_enabled) && e->is_glsl_linked()) {
      for (auto i = 0; i < e->glsl_.iterations_; ++i) {
        e->process_shader(timecode, coords, i);
        composite_texture = draw_clip(ctx, c->fbo[fbo_switcher], composite_texture, true);
        fbo_switcher = !fbo_switcher;
      }
    }

    if (e->hasCapability(Capability::SUPERIMPOSE)) {
      GLuint superimpose_texture = e->process_superimpose(timecode);
      if (superimpose_texture == 0) {
        qWarning() << "Superimpose texture was nullptr, retrying...";
        texture_failed = true;
      } else {
        composite_texture = draw_clip(ctx, c->fbo[!fbo_switcher], superimpose_texture, false);
      }
    }

    e->endEffect();
  }
}


// FIXME: refactor this long function
GLuint compose_sequence(Viewer* viewer,
                        QOpenGLContext* ctx,
                        SequencePtr seq,
                        QVector<ClipPtr> &nests,
                        const bool video,
                        const bool render_audio,
                        EffectPtr &gizmos,
                        bool &texture_failed,
                        const bool rendering,
                        const bool use_effects)
{
  GLint current_fbo = 0;
  if (video) {
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &current_fbo);
  }
  auto lcl_seq = seq;
  auto playhead = lcl_seq->playhead_;

  if (!nests.isEmpty()) {
    for(auto nest_clip : nests) {
      lcl_seq = nest_clip->timeline_info.media->object<Sequence>();
      playhead += nest_clip->timeline_info.clip_in - nest_clip->timelineInWithTransition();
      playhead = refactor_frame_number(playhead, nest_clip->sequence->frameRate(), lcl_seq->frameRate());
    }

    if (video && (nests.last()->fbo != nullptr) ) {
      nests.last()->fbo[0]->bind();
      glClear(GL_COLOR_BUFFER_BIT);
      ctx->functions()->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_fbo);
    }
  }

  auto audio_track_count = 0;

  QVector<ClipPtr> current_clips;

  for (auto clp : lcl_seq->clips()) {
    // if clip starts within one second and/or hasn't finished yet
    if (clp == nullptr) {
      qWarning() << "Clip instance is null";
    }

    if (!seq->trackEnabled(clp->timeline_info.track_)) {
      continue;
    }
    if ((clp->mediaType() == ClipType::VISUAL) != video) {
      continue;
    }
    auto clip_is_active = false;

    if ( (clp->timeline_info.media != nullptr) && (clp->timeline_info.media->type() == MediaType::FOOTAGE) ) {
      auto ftg = clp->timeline_info.media->object<Footage>();
      if (!ftg->has_preview_) {
        // TODO: should the render really be prevented just because the audio waveform preview hasn't been generated?
        qDebug() << "Waiting on preview (audio/video) to be generated for Footage, fileName =" << ftg->location();
        continue;
      }

      if (!( (clp->timeline_info.track_ >= 0) && !is_audio_device_set())) {
        if (ftg->ready_) {
          const auto found = ftg->has_stream_from_file_index(clp->timeline_info.media_stream);

          if (found && clp->isActive(playhead)) {
            // if thread is already working, we don't want to touch this,
            // but we also don't want to hang the UI thread
            clp->open(!rendering);
            clip_is_active = true;
            if (clp->timeline_info.track_ >= 0) {
              audio_track_count++;
            }
          } else if (clp->is_open) {
            clp->close(false);
          }
        } else {
          qWarning() << "Media '" + ftg->name() + "' was not ready, retrying...";
          texture_failed = true;
        }
      }
    } else {
      if (clp->isActive(playhead)) {
        clp->open(!rendering);
        clip_is_active = true;
      } else if (clp->is_open) {
        clp->close(false);
      }
    }

    if (clip_is_active) {
      bool added = false;
      for (int j=0; j<current_clips.size(); ++j) {
        if (!current_clips.at(j)){
          qDebug() << "Clip is Null";
          continue;
        }
        if (current_clips.at(j)->timeline_info.track_ < clp->timeline_info.track_) {
          current_clips.insert(j, clp);
          added = true;
          break;
        }
      }//for

      if (!added) {
        current_clips.append(clp);
      }
    }
  }//for

  const auto half_width = lcl_seq->width()/2;
  const auto half_height = lcl_seq->height()/2;

  if (video) {
    glPushMatrix();
    glLoadIdentity();
    glOrtho(-half_width, half_width, -half_height, half_height, -1, 10);
  }

  for (auto& clp : current_clips) {
    if (clp == nullptr) {
      qDebug() << "Null Clip instance";
      continue;
    }
    if (!clp->mediaOpen()) {
      qWarning() << "Tried to display clip '" << clp->name() << "' but it's closed";
      texture_failed = true;
    } else {
      if (clp->mediaType() == ClipType::VISUAL) {
        ctx->functions()->GL_DEFAULT_BLEND;
        glColor4f(1.0, 1.0, 1.0, 1.0);

        GLuint textureID = 0;
        const int video_width = clp->width();
        const int video_height = clp->height();

        if (clp->timeline_info.media != nullptr) {
          switch (clp->timeline_info.media->type()) {
            case MediaType::FOOTAGE:
              // set up opengl texture
              if ( (clp->media_handling_.stream_ == nullptr)
                   || (clp->media_handling_.stream_->codecpar == nullptr) ) {
                qCritical() << "Media stream is null";
                break;
              }
              if (clp->texture == nullptr) {
                clp->texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
                clp->texture->setSize(clp->media_handling_.stream_->codecpar->width,
                                      clp->media_handling_.stream_->codecpar->height);
                clp->texture->setFormat(get_gl_tex_fmt_from_av(clp->pix_fmt));
                clp->texture->setMipLevels(clp->texture->maximumMipLevels());
                clp->texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
                clp->texture->allocateStorage(get_gl_pix_fmt_from_av(clp->pix_fmt), QOpenGLTexture::UInt8);
              }
              clp->frame(playhead, texture_failed);
              textureID = clp->texture->textureId();
              break;
            case MediaType::SEQUENCE:
              textureID = -1;
              break;
            default:
              qWarning() << "Unhandled sequence type" << static_cast<int>(clp->timeline_info.media->type());
              break;
          }
        }

        if ( (textureID == 0) && (clp->timeline_info.media != nullptr) ) {
          qWarning() << "Texture hasn't been created yet";
          texture_failed = true;
        } else if (playhead >= clp->timelineInWithTransition()) {
          glPushMatrix();

          // start preparing cache
          if (clp->fbo == nullptr) {
            clp->fbo = new QOpenGLFramebufferObject* [2];
            clp->fbo[0] = new QOpenGLFramebufferObject(video_width, video_height);
            clp->fbo[1] = new QOpenGLFramebufferObject(video_width, video_height);
          }

          bool fbo_switcher = false;

          glViewport(0, 0, video_width, video_height);

          GLuint composite_texture;

          if (clp->timeline_info.media == nullptr) {
            clp->fbo[fbo_switcher]->bind();
            glClear(GL_COLOR_BUFFER_BIT);
            ctx->functions()->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_fbo);
            composite_texture = clp->fbo[fbo_switcher]->texture();
          } else {
            // for nested sequences
            if (clp->timeline_info.media->type()== MediaType::SEQUENCE) {
              nests.append(clp);
              textureID = compose_sequence(viewer, ctx, seq, nests, video, render_audio, gizmos, texture_failed, rendering);
              nests.removeLast();
              fbo_switcher = true;
            }

            composite_texture = draw_clip(ctx, clp->fbo[fbo_switcher], textureID, true);
          }

          fbo_switcher = !fbo_switcher;

          // set up default coords
          GLTextureCoords coords;
          coords.grid_size = 1;

          coords.vertices_[0].x_ = -video_width/2;
          coords.vertices_[3].x_ = -video_width/2;
          coords.vertices_[0].y_ = -video_height/2;
          coords.vertices_[1].y_ = -video_height/2;

          coords.vertices_[1].x_ = video_width/2;
          coords.vertices_[2].x_ = video_width/2;
          coords.vertices_[3].y_ = video_height/2;
          coords.vertices_[2].y_ = video_height/2;

          coords.vertices_[3].z_ = 1;
          coords.vertices_[2].z_ = 1;
          coords.vertices_[0].z_ = 1;
          coords.vertices_[1].z_ = 1;

          coords.texture_[0].y_ = 0.0;
          coords.texture_[1].y_ = 0.0;
          coords.texture_[0].x_ = 0.0;
          coords.texture_[3].x_ = 0.0;

          coords.texture_[3].y_ = 1.0;
          coords.texture_[2].y_ = 1.0;
          coords.texture_[1].x_ = 1.0;
          coords.texture_[2].x_ = 1.0;

          // set up autoscale
          if (clp->timeline_info.autoscale && ( (video_width != lcl_seq->width()) && (video_height != lcl_seq->height()))) {
            auto width_multiplier = static_cast<GLfloat>(lcl_seq->width()) / video_width;
            auto height_multiplier = static_cast<GLfloat>(lcl_seq->height()) / video_height;
            auto scale_multiplier = qMin(width_multiplier, height_multiplier);
            glScalef(scale_multiplier, scale_multiplier, 1);
          }

          EffectPtr first_gizmo_effect = nullptr;
          EffectPtr selected_effect = nullptr;
          const double timecode = clp->timecode(playhead);

          // EFFECT CODE START
          if (use_effects || render_audio) {
            for (auto& eff : clp->effects) {
              if (!eff) {
                continue;
              }
              process_effect(ctx, clp, eff, timecode, coords, composite_texture, fbo_switcher, texture_failed, TA_NO_TRANSITION);

              if (eff->are_gizmos_enabled()) {
                if (first_gizmo_effect == nullptr) {
                  first_gizmo_effect = eff;
                }
                if (eff->container->selected) {
                  selected_effect = eff;
                }
              }
            }//for

            if (selected_effect != nullptr) {
              gizmos = selected_effect;
            } else if (clp->isSelected(true)) {
              gizmos = first_gizmo_effect;
            }

            if (clp->openingTransition() != nullptr) {
              const int transition_progress = playhead - clp->timelineInWithTransition();
              if (transition_progress < clp->openingTransition()->get_length()) {
                EffectPtr trans(clp->openingTransition());
                process_effect(ctx, clp, trans,
                               static_cast<double>(transition_progress) / clp->openingTransition()->get_length(),
                               coords, composite_texture, fbo_switcher, texture_failed, TA_OPENING_TRANSITION);
              }
            }

            if (clp->closingTransition() != nullptr) {
              const int transition_progress = playhead - (clp->timelineOutWithTransition() - clp->closingTransition()->get_length());
              if ( (transition_progress >= 0) && (transition_progress < clp->closingTransition()->get_length()) ) {
                EffectPtr trans(clp->closingTransition());
                process_effect(ctx, clp, trans,
                               static_cast<double>(transition_progress) / clp->closingTransition()->get_length(),
                               coords, composite_texture, fbo_switcher, texture_failed, TA_CLOSING_TRANSITION);
              }
            }
          }
          // EFFECT CODE END

          if (!nests.isEmpty()) {
            nests.last()->fbo[0]->bind();
          }
          glViewport(0, 0, lcl_seq->width(), lcl_seq->height());

          glBindTexture(GL_TEXTURE_2D, composite_texture);

          glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

          glBegin(GL_QUADS);

          if (coords.grid_size <= 1) {
            constexpr auto z = 0;
            glTexCoord2f(coords.texture_[0].x_, coords.texture_[0].y_); // top left
            glVertex3i(coords.vertices_[0].x_, coords.vertices_[0].y_, z); // top left
            glTexCoord2f(coords.texture_[1].x_, coords.texture_[1].y_); // top right
            glVertex3i(coords.vertices_[1].x_, coords.vertices_[1].y_, z); // top right
            glTexCoord2f(coords.texture_[2].x_, coords.texture_[2].y_); // bottom right
            glVertex3i(coords.vertices_[2].x_, coords.vertices_[2].y_, z); // bottom right
            glTexCoord2f(coords.texture_[3].x_, coords.texture_[3].y_); // bottom left
            glVertex3i(coords.vertices_[3].x_, coords.vertices_[3].y_, z); // bottom left
          } else {
            const auto rows = coords.grid_size;
            const auto cols = coords.grid_size;

            for (auto k = 0; k < rows; ++k) {
              const auto row_prog = static_cast<float>(k)/rows;
              const auto next_row_prog = static_cast<float>(k+1)/rows;
              for (auto j = 0; j < cols; ++j) {
                const auto col_prog = static_cast<float>(j)/cols;
                const auto next_col_prog = static_cast<float>(j + 1)/cols;

                const auto vertexTLX = float_lerp(coords.vertices_[0].x_, coords.vertices_[3].x_, row_prog);
                const auto vertexTRX = float_lerp(coords.vertices_[1].x_, coords.vertices_[2].x_, row_prog);
                const auto vertexBLX = float_lerp(coords.vertices_[0].x_, coords.vertices_[3].x_, next_row_prog);
                const auto vertexBRX = float_lerp(coords.vertices_[1].x_, coords.vertices_[2].x_, next_row_prog);

                const auto vertexTLY = float_lerp(coords.vertices_[0].y_, coords.vertices_[1].y_, col_prog);
                const auto vertexTRY = float_lerp(coords.vertices_[0].y_, coords.vertices_[1].y_, next_col_prog);
                const auto vertexBLY = float_lerp(coords.vertices_[3].y_, coords.vertices_[2].y_, col_prog);
                const auto vertexBRY = float_lerp(coords.vertices_[3].y_, coords.vertices_[2].y_, next_col_prog);

                glTexCoord2f(float_lerp(coords.texture_[0].x_, coords.texture_[1].x_, col_prog),
                    float_lerp(coords.texture_[0].y_, coords.texture_[3].y_, row_prog)); // top left
                glVertex2f(float_lerp(vertexTLX, vertexTRX, col_prog), float_lerp(vertexTLY, vertexBLY, row_prog)); // top left
                glTexCoord2f(float_lerp(coords.texture_[0].x_, coords.texture_[1].x_, next_col_prog),
                    float_lerp(coords.texture_[1].y_, coords.texture_[2].y_, row_prog)); // top right
                glVertex2f(float_lerp(vertexTLX, vertexTRX, next_col_prog), float_lerp(vertexTRY, vertexBRY, row_prog)); // top right
                glTexCoord2f(float_lerp(coords.texture_[3].x_, coords.texture_[2].x_, next_col_prog),
                    float_lerp(coords.texture_[1].y_, coords.texture_[2].y_, next_row_prog)); // bottom right
                glVertex2f(float_lerp(vertexBLX, vertexBRX, next_col_prog), float_lerp(vertexTRY, vertexBRY, next_row_prog)); // bottom right
                glTexCoord2f(float_lerp(coords.texture_[3].x_, coords.texture_[2].x_, col_prog),
                    float_lerp(coords.texture_[0].y_, coords.texture_[3].y_, next_row_prog)); // bottom left
                glVertex2f(float_lerp(vertexBLX, vertexBRX, col_prog), float_lerp(vertexTLY, vertexBLY, next_row_prog)); // bottom left
              }//for
            }//for
          }

          glEnd();

          glBindTexture(GL_TEXTURE_2D, 0); // unbind texture

          // prepare gizmos
          if (panels::PanelManager::sequenceViewer().usingEffects()
              && ( (gizmos != nullptr) && nests.isEmpty() )
              && ( (gizmos == first_gizmo_effect) || (gizmos == selected_effect) ) )  {
            gizmos->gizmo_draw(timecode, coords); // set correct gizmo coords
            gizmos->gizmo_world_to_screen();      // convert gizmo coords to screen coords
          }

          if (!nests.isEmpty()) {
            ctx->functions()->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_fbo);
          }

          glPopMatrix();
        }
      } else {
        if (render_audio || (global::config.enable_audio_scrubbing && audio_scrub)) {
          if (clp->timeline_info.media != nullptr && clp->timeline_info.media->type() == MediaType::SEQUENCE) {
            nests.append(clp);
            compose_sequence(viewer, ctx, seq, nests, video, render_audio, gizmos, texture_failed, rendering);
            nests.removeLast();
          } else {
            if (clp->lock.tryLock()) {
              // clip is not caching, start caching audio
              clp->cache(playhead, clp->audio_playback.reset, !render_audio, nests);
              clp->lock.unlock();
            }
          }
        }

        // visually update all the keyframe values
        if (clp->sequence == seq) { // only if you can currently see them
          double ts = (playhead - clp->timelineInWithTransition() + clp->clipInWithTransition()) / lcl_seq->frameRate();
          for (const auto& eff : clp->effects) {
            Q_ASSERT(eff);
            for (int j=0; j < eff->row_count(); ++j) {
              const auto eff_row = eff->row(j);
              for (int k = 0; k < eff_row->fieldCount(); ++k) {
                eff_row->field(k)->validate_keyframe_data(ts);
              }
            }
          }//for
        }
      }
    }
  }//for

  if (audio_track_count == 0 && viewer != nullptr) {
    viewer->play_wake();
  }

  if (video) {
    glPopMatrix();
  }

  if (!nests.isEmpty() && (nests.last()->fbo != nullptr) ) {
    // returns nested clip's texture
    return nests.last()->fbo[0]->texture();
  }

  return 0;
}

void compose_audio(Viewer* viewer, SequencePtr seq, bool render_audio)
{
  //FIXME: ......
  QVector<ClipPtr> nests;
  bool texture_failed;
  EffectPtr gizmos;
  compose_sequence(viewer, nullptr, seq, nests, false, render_audio, gizmos, texture_failed, audio_rendering);
}
