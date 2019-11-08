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
#include "exportthread.h"

#include <QApplication>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <utility>
#include <thread>

#include "project/sequence.h"
#include "panels/panelmanager.h"
#include "ui/viewerwidget.h"
#include "ui/renderfunctions.h"
#include "playback/playback.h"
#include "playback/audio.h"
#include "dialogs/exportdialog.h"
#include "ui/mainwindow.h"
#include "debug.h"
#include "coderconstants.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
}

#ifdef QT_DEBUG
constexpr auto X264_PRESET = "ultrafast";
#else
constexpr auto X264_PRESET = "medium";
#endif

using panels::PanelManager;

constexpr int WAIT_TIMEOUT_MILLIS = 10000;
constexpr auto ERR_LEN = 256;
constexpr auto INTERNAL_PIXEL_FORMAT = AV_PIX_FMT_RGBA;

namespace  {
  std::array<char, ERR_LEN> err;
  std::pair<double, AVRational> NTSC_24P {23.976, {24000, 1001}};
  std::pair<double, AVRational> NTSC_30P {29.97, {30000, 1001}};
  std::pair<double, AVRational> NTSC_60P {59.94, {60000, 1001}};
}

ExportThread::ExportThread()
  : QThread(nullptr)
{
  surface.create();
}

bool ExportThread::encode(AVFormatContext* ofmt_ctx,
                          AVCodecContext* codec_ctx,
                          AVFrame* frame,
                          AVPacket* packet,
                          AVStream* stream,
                          bool rescale)
{
  auto ret = avcodec_send_frame(codec_ctx, frame);
  if (ret < 0) {
    av_strerror(ret, err.data(), ERR_LEN);
    qCritical() << "Failed to send frame to encoder." << err.data();
    ed->export_error = tr("failed to send frame to encoder (%1)").arg(err.data());
    return false;
  }

  while (ret >= 0) {
    ret = avcodec_receive_packet(codec_ctx, packet);
    if (ret == AVERROR(EAGAIN)) {
      return true;
    } else if (ret < 0) {
      if (ret != AVERROR_EOF) {
        av_strerror(ret, err.data(), ERR_LEN);
        qCritical() << "Failed to receive packet from encoder, code=" << err.data();
        ed->export_error = tr("failed to receive packet from encoder (%1)").arg(QString::number(ret));
      }
      return false;
    }

    packet->stream_index = stream->index;
    if (rescale) {
      av_packet_rescale_ts(packet, codec_ctx->time_base, stream->time_base);
    }

    av_interleaved_write_frame(ofmt_ctx, packet);
    av_packet_unref(packet);
  }
  return true;
}

void ExportThread::setupScaler()
{
  int interp_meth;
  // Do not scale if the sequence dimensions are the same as export dimensions. Nothing to do.
  if ( (global::sequence->width() == video_params_.width_) &&
       (global::sequence->height() == video_params_.height_) ) {
    interp_meth = SWS_FAST_BILINEAR;
  } else {
    interp_meth = convertInterpolationType(video_params_.interpol_);
  }

  sws_ctx = sws_getContext(
              global::sequence->width(),
              global::sequence->height(),
              INTERNAL_PIXEL_FORMAT,
              video_params_.width_,
              video_params_.height_,
              vcodec_ctx->pix_fmt,
              interp_meth,
              nullptr,
              nullptr,
              nullptr
              );

  Q_ASSERT(sws_ctx);
  sws_frame = av_frame_alloc();
  Q_ASSERT(sws_frame);
  sws_frame->format = vcodec_ctx->pix_fmt;
  sws_frame->width = video_params_.width_;
  sws_frame->height = video_params_.height_;
  av_frame_get_buffer(sws_frame, 0);

}

//FIXME: setup is too naive/basic
bool ExportThread::setupVideo()
{
  Q_ASSERT(fmt_ctx);
  Q_ASSERT(global::sequence);

  // if video is disabled, no setup necessary
  if (!video_params_.enabled) {
    return true;
  }

  // find video encoder
  vcodec = avcodec_find_encoder(static_cast<AVCodecID>(video_params_.codec_));
  if (!vcodec) {
    qCritical() << "Could not find video encoder";
    ed->export_error = tr("could not video encoder for %1").arg(QString::number(video_params_.codec_));
    return false;
  }

  // create video stream
  video_stream = avformat_new_stream(fmt_ctx, vcodec);
  Q_ASSERT(video_stream);
  video_stream->id = 0;
  if (!video_stream) {
    qCritical() << "Could not allocate video stream";
    ed->export_error = tr("could not allocate video stream");
    return false;
  }

  // allocate context
  vcodec_ctx = avcodec_alloc_context3(vcodec);
  if (!vcodec_ctx) {
    qCritical() << "Could not allocate video encoding context";
    ed->export_error = tr("could not allocate video encoding context");
    return false;
  }

  // setup context
  vcodec_ctx->codec_id = static_cast<AVCodecID>(video_params_.codec_);
  vcodec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
  vcodec_ctx->width = video_params_.width_;
  vcodec_ctx->height = video_params_.height_;
  vcodec_ctx->pix_fmt = vcodec->pix_fmts[0]; // maybe be breakable code
  setupFrameRate(*vcodec_ctx, video_params_.frame_rate_);
  if (video_params_.compression_type_ == CompressionType::CBR) {
    const int64_t brate = llround(video_params_.bitrate_ * 1E6);
    vcodec_ctx->bit_rate = brate;
    vcodec_ctx->rc_min_rate = brate;
    vcodec_ctx->rc_max_rate = brate;
  }
  vcodec_ctx->time_base = av_inv_q(vcodec_ctx->framerate);
  video_stream->time_base = vcodec_ctx->time_base;
  vcodec_ctx->gop_size = video_params_.gop_length_;
  vcodec_ctx->thread_count = static_cast<int>(std::thread::hardware_concurrency());
  vcodec_ctx->thread_type = FF_THREAD_SLICE;

  AVDictionary* opts = nullptr;
  if (video_params_.closed_gop_) {
    // Technically incorrect "closed gop" refers to last frame in each GOP being a P-frame
    // Terminology being used to identify a fixed structure and libavcodec dynamically changing structure
    // effectively disabling scene change detection
    auto ret = av_dict_set(&opts, "sc_threshold", "1000000000", 0);
    if (ret < 0) {
      av_strerror(ret, err.data(), ERR_LEN);
      qCritical() << "Failed to set closed-gop, code=" << err.data();
      ed->export_error = tr("Failed to set closed-gop (%1)").arg(err.data());
      return false;
    }
  }

  const std::string fmt_name(fmt_ctx->oformat->name);
  if ( (fmt_name == "mp4") || (fmt_name == "mov") ) {
    // moov atom at start for mp4/mov files
    auto ret = av_dict_set( &opts, "movflags", "faststart", 0);
    if (ret < 0) {
      av_strerror(ret, err.data(), ERR_LEN);
      qCritical() << "Failed to set moov atom at start, code=" << err.data();
      ed->export_error = tr("Failed to set moov atom at start (%1)").arg(err.data());
      return false;
    }
  }

  // Do bare minimum before avcodec_open2
  switch (vcodec_ctx->codec_id) {
    case AV_CODEC_ID_H264:
      setupH264Encoder(*vcodec_ctx, video_params_);
      break;
    case AV_CODEC_ID_MPEG2VIDEO:
      setupMPEG2Encoder(*vcodec_ctx, *video_stream, video_params_);
      break;
    case AV_CODEC_ID_DNXHD:
      setupDNXHDEncoder(*vcodec_ctx, video_params_);
      break;
    case AV_CODEC_ID_MPEG4:
      setupMPEG4Encoder(*vcodec_ctx, video_params_);
    default:
      // Nothing defined for these codecs yet
      break;
  }

  auto ret = avcodec_open2(vcodec_ctx, vcodec, &opts);
  if (ret < 0) {
    av_strerror(ret, err.data(), ERR_LEN);
    qCritical() << "Could not open output video encoder." << err.data();
    ed->export_error = tr("could not open output video encoder (%1)").arg(err.data());
    return false;
  }

  if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
    vcodec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  vcodec_ctx->sample_aspect_ratio = {1, 1};
  vcodec_ctx->max_b_frames = video_params_.b_frames_;

  // copy video encoder parameters to output stream
  ret = avcodec_parameters_from_context(video_stream->codecpar, vcodec_ctx);
  if (ret < 0) {
    av_strerror(ret, err.data(), ERR_LEN);
    qCritical() << "Could not copy video encoder parameters to output stream, code=" << err.data();
    ed->export_error = tr("could not copy video encoder parameters to output stream (%1)").arg(QString::number(ret));
    return false;
  }

  // create AVFrame
  video_frame = av_frame_alloc();
  av_frame_make_writable(video_frame);
  video_frame->format = AV_PIX_FMT_RGBA;
  video_frame->width = global::sequence->width();
  video_frame->height = global::sequence->height();
  av_frame_get_buffer(video_frame, 0);

  av_init_packet(&video_pkt);

  setupScaler();

  return true;
}

bool ExportThread::setupAudio()
{
  // if audio is disabled, no setup necessary
  if (!audio_params_.enabled) {
    return true;
  }

  // find encoder
  acodec = avcodec_find_encoder(static_cast<AVCodecID>(audio_params_.codec));
  if (!acodec) {
    qCritical() << "Could not find audio encoder";
    ed->export_error = tr("could not audio encoder for %1").arg(QString::number(audio_params_.codec));
    return false;
  }

  // allocate audio stream
  audio_stream = avformat_new_stream(fmt_ctx, acodec);
  audio_stream->id = 1;
  if (!audio_stream) {
    qCritical() << "Could not allocate audio stream";
    ed->export_error = tr("could not allocate audio stream");
    return false;
  }

  // allocate context
  acodec_ctx = avcodec_alloc_context3(acodec);
  if (!acodec_ctx) {
    qCritical() << "Could not find allocate audio encoding context";
    ed->export_error = tr("could not allocate audio encoding context");
    return false;
  }

  // setup context
  acodec_ctx->codec_id = static_cast<AVCodecID>(audio_params_.codec);
  acodec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
  acodec_ctx->sample_rate = audio_params_.sampling_rate;
  acodec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;  // change this to support surround/mono sound in the future (this is what the user sets the output audio to)
  acodec_ctx->channels = av_get_channel_layout_nb_channels(acodec_ctx->channel_layout);
  acodec_ctx->sample_fmt = acodec->sample_fmts[0];
  acodec_ctx->bit_rate = audio_params_.bitrate * 1000;

  acodec_ctx->time_base.num = 1;
  acodec_ctx->time_base.den = audio_params_.sampling_rate;
  audio_stream->time_base = acodec_ctx->time_base;

  if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
    acodec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  // open encoder
  auto ret = avcodec_open2(acodec_ctx, acodec, nullptr);

  if (ret < 0) {
    av_strerror(ret, err.data(), ERR_LEN);
    qCritical() << "Could not open output audio encoder, code=" << err.data();
    ed->export_error = tr("could not open output audio encoder (%1)").arg(QString::number(ret));
    return false;
  }

  // copy params to output stream
  ret = avcodec_parameters_from_context(audio_stream->codecpar, acodec_ctx);
  if (ret < 0) {
    av_strerror(ret, err.data(), ERR_LEN);
    qCritical() << "Could not copy audio encoder parameters to output stream, code=" << err.data();
    ed->export_error = tr("could not copy audio encoder parameters to output stream (%1)").arg(QString::number(ret));
    return false;
  }

  // init audio resampler context
  swr_ctx = swr_alloc_set_opts(
              nullptr,
              static_cast<int64_t>(acodec_ctx->channel_layout),
              acodec_ctx->sample_fmt,
              acodec_ctx->sample_rate,
              global::sequence->audioLayout(),
              AV_SAMPLE_FMT_S16,
              global::sequence->audioFrequency(),
              0,
              nullptr
              );

  ret = swr_init(swr_ctx);
  if (ret < 0) {
    av_strerror(ret, err.data(), ERR_LEN);
    qCritical() << "Could not init resample context, code=" << err.data();
    ed->export_error = tr("cCould not init resample context (%1)").arg(QString::number(ret));
    return false;
  }

  // initialize raw audio frame
  audio_frame = av_frame_alloc();
  audio_frame->sample_rate = global::sequence->audioFrequency();
  audio_frame->nb_samples = acodec_ctx->frame_size;
  if (audio_frame->nb_samples == 0) audio_frame->nb_samples = 256; // should possibly be smaller?
  audio_frame->format = AV_SAMPLE_FMT_S16;
  audio_frame->channel_layout = AV_CH_LAYOUT_STEREO; // change this to support surround/mono sound in the future (this is whatever format they're held in the internal buffer)
  audio_frame->channels = av_get_channel_layout_nb_channels(audio_frame->channel_layout);
  av_frame_make_writable(audio_frame);
  ret = av_frame_get_buffer(audio_frame, 0);
  if (ret < 0) {
    av_strerror(ret, err.data(), ERR_LEN);
    qCritical() << "Could not allocate audio buffer, code=" << err.data();
    ed->export_error = tr("could not allocate audio buffer (%1)").arg(QString::number(ret));
    return false;
  }
  aframe_bytes = av_samples_get_buffer_size(nullptr, audio_frame->channels, audio_frame->nb_samples, static_cast<AVSampleFormat>(audio_frame->format), 0);

  // init converted audio frame
  swr_frame = av_frame_alloc();
  swr_frame->channel_layout = acodec_ctx->channel_layout;
  swr_frame->channels = acodec_ctx->channels;
  swr_frame->sample_rate = acodec_ctx->sample_rate;
  swr_frame->format = acodec_ctx->sample_fmt;
  av_frame_make_writable(swr_frame);

  av_init_packet(&audio_pkt);

  return true;
}

bool ExportThread::setupContainer()
{
  auto ret = avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, filename.toUtf8().data());
  if (!fmt_ctx || ret < 0) {
    av_strerror(ret, err.data(), ERR_LEN);
    qCritical() << "Could not create output context, code=" << err.data();
    ed->export_error = tr("could not create output format context");
    return false;
  }

  ret = avio_open(&fmt_ctx->pb,  filename.toUtf8().data(), AVIO_FLAG_WRITE);
  if (ret < 0) {
    av_strerror(ret, err.data(), ERR_LEN);
    qCritical() << "Could not open output file, code=" << err.data();
    ed->export_error = tr("could not open output file (%1)").arg(QString::number(ret));
    return false;
  }

  return true;
}

bool ExportThread::setUpContext(RenderThread& rt, Viewer& vwr)
{
  if ( (vwr.viewer_widget == nullptr) || (vwr.viewer_widget->context() == nullptr) || (ed == nullptr) ) {
    return false;
  }
  auto ctxt = vwr.viewer_widget->context();

  QObject::disconnect(&rt, SIGNAL(ready()), vwr.viewer_widget, SLOT(queue_repaint()));
  QObject::connect(&rt, SIGNAL(ready()), this, SLOT(wake()));

  vwr.pause();
  vwr.seek(start_frame);
  vwr.reset_all_audio();

  ctxt->moveToThread(this);

  if (!ctxt->makeCurrent(&surface)) {
    qCritical() << "Make current failed";
    ed->export_error = tr("could not make OpenGL context current");
    return false;
  }
  return true;
}


void ExportThread::setDownContext(RenderThread& rt, Viewer& vwr) const
{
  if ( (vwr.viewer_widget == nullptr) || (vwr.viewer_widget->context() == nullptr)) {
    return;
  }

  auto ctxt = vwr.viewer_widget->context();
  ctxt->doneCurrent();
  ctxt->moveToThread(QCoreApplication::instance()->thread());
  QObject::connect(&rt, SIGNAL(ready()), vwr.viewer_widget, SLOT(queue_repaint()));
}

void ExportThread::run()
{
  RenderThread* renderer = PanelManager::sequenceViewer().viewer_widget->get_renderer();
  if (renderer == nullptr) {
    qCritical() << "No render thread available";
    return;
  }

  continue_encode_ = setUpContext(*renderer, PanelManager::sequenceViewer());
  continue_encode_ = continue_encode_ && setupContainer();

  if (video_params_.enabled && continue_encode_) {
    continue_encode_ = setupVideo();
  }

  if (audio_params_.enabled && continue_encode_) {
    continue_encode_ = setupAudio();
  }

  if (continue_encode_) {
    auto ret = avformat_write_header(fmt_ctx, nullptr);
    if (ret < 0) {
      av_strerror(ret, err.data(), ERR_LEN);
      qCritical() << "Could not write output file header, code=" << err.data();
      ed->export_error = tr("could not write output file header (%1)").arg(QString::number(ret));
      continue_encode_ = false;
    }
  }


  long file_audio_samples = 0;
  qint64 start_time, frame_time, avg_time, eta, total_time = 0;
  long remaining_frames, frame_count = 1;

  mutex.lock();

  while (global::sequence->playhead_ <= end_frame && continue_encode_) {
    start_time = QDateTime::currentMSecsSinceEpoch();

    if (audio_params_.enabled) {
      compose_audio(nullptr, global::sequence, true);
    }

    if (video_params_.enabled) {
      do {
        // TODO optimize by rendering the next frame while encoding the last
        renderer->start_render(nullptr, global::sequence, false, video_frame->data[0]);
        continue_encode_ = waitCond.wait(&mutex, WAIT_TIMEOUT_MILLIS);
        if (!continue_encode_) {
          qCritical() << "Timeout occured waiting for RenderThread";
          break;
        }
      } while (renderer->did_texture_fail());
      if (!continue_encode_) {
        break;
      }
    }

    // encode last frame while rendering next frame
    double timecode_secs = static_cast<double> (global::sequence->playhead_ - start_frame) / global::sequence->frameRate();
    if (video_params_.enabled) {
      // change pixel format
      sws_scale(sws_ctx, video_frame->data, video_frame->linesize, 0, video_frame->height, sws_frame->data, sws_frame->linesize);
      sws_frame->pts = qRound(timecode_secs/av_q2d(video_stream->time_base));

      // send to encoder
      continue_encode_ = encode(fmt_ctx, vcodec_ctx, sws_frame, &video_pkt, video_stream, false);
    }
    if (audio_params_.enabled) {
      // do we need to encode more audio samples?
      while (continue_encode_ && (file_audio_samples <= (timecode_secs * audio_params_.sampling_rate))) {
        // copy samples from audio buffer to AVFrame
        int adjusted_read = audio_ibuffer_read % AUDIO_IBUFFER_SIZE;
        int copylen = qMin(aframe_bytes, AUDIO_IBUFFER_SIZE - adjusted_read);
        memcpy(audio_frame->data[0], audio_ibuffer + adjusted_read, static_cast<size_t>(copylen));
        memset(audio_ibuffer + adjusted_read, 0, static_cast<size_t>(copylen));
        audio_ibuffer_read += copylen;

        if (copylen < aframe_bytes) {
          // copy remainder
          auto remainder_len = static_cast<size_t>(aframe_bytes - copylen);
          memcpy(audio_frame->data[0] + copylen, audio_ibuffer, remainder_len);
          memset(audio_ibuffer, 0, remainder_len);
          audio_ibuffer_read += remainder_len;
        }

        // convert to export sample format
        swr_convert_frame(swr_ctx, swr_frame, audio_frame);
        swr_frame->pts = file_audio_samples;

        // send to encoder
        continue_encode_ = encode(fmt_ctx, acodec_ctx, swr_frame, &audio_pkt, audio_stream, true);

        file_audio_samples += swr_frame->nb_samples;
      }
    }

    // encoding stats
    frame_time = (QDateTime::currentMSecsSinceEpoch() - start_time);
    total_time += frame_time;
    remaining_frames = (end_frame - global::sequence->playhead_);
    avg_time = (total_time / frame_count);
    eta = (remaining_frames * avg_time);

    emit progress_changed(qRound((static_cast<double>(global::sequence->playhead_ - start_frame)
                                  / static_cast<double>(end_frame - start_frame)) * 100), eta);
    global::sequence->playhead_++;
    frame_count++;
  }


  mutex.unlock();

  if (continue_encode_) {
    if (video_params_.enabled) {
      vpkt_alloc = true;
    }

    if (audio_params_.enabled) {
      apkt_alloc = true;
    }
  }

  MainWindow::instance().set_rendering_state(false);

  if (audio_params_.enabled && continue_encode_) {
    // flush swresample
    do {
      swr_convert_frame(swr_ctx, swr_frame, nullptr);
      if (swr_frame->nb_samples == 0) {
        break;
      }
      swr_frame->pts = file_audio_samples;
      continue_encode_ = encode(fmt_ctx, acodec_ctx, swr_frame, &audio_pkt, audio_stream, true);
      file_audio_samples += swr_frame->nb_samples;
    } while (swr_frame->nb_samples > 0);
  }

  bool continueVideo = true;
  bool continueAudio = true;
  if (continue_encode_) {
    // flush remaining packets
    while (continueVideo && continueAudio) {
      if (continueVideo && video_params_.enabled) {
        continueVideo = encode(fmt_ctx, vcodec_ctx, nullptr, &video_pkt, video_stream, false);
      }
      if (continueAudio && audio_params_.enabled) {
        continueAudio = encode(fmt_ctx, acodec_ctx, nullptr, &audio_pkt, audio_stream, true);
      }
    }

    auto ret = av_write_trailer(fmt_ctx);
    if (ret < 0) {
      av_strerror(ret, err.data(), ERR_LEN);
      qCritical() << "Could not write output file trailer, code=" << err.data();
      ed->export_error = tr("could not write output file trailer (%1)").arg(QString::number(ret));
      continue_encode_ = false;
    }

    emit progress_changed(100, 0);
  }

  avio_closep(&fmt_ctx->pb);

  if (vpkt_alloc) {
    av_packet_unref(&video_pkt);
  }
  if (video_frame != nullptr) {
    av_frame_free(&video_frame);
  }
  if (vcodec_ctx != nullptr) {
    avcodec_close(vcodec_ctx);
    avcodec_free_context(&vcodec_ctx);
  }

  if (apkt_alloc) {
    av_packet_unref(&audio_pkt);
  }
  if (audio_frame != nullptr) {
    av_frame_free(&audio_frame);
  }
  if (acodec_ctx != nullptr) {
    avcodec_close(acodec_ctx);
    avcodec_free_context(&acodec_ctx);
  }

  avformat_free_context(fmt_ctx);

  if (sws_ctx != nullptr) {
    sws_freeContext(sws_ctx);
    av_frame_free(&sws_frame);
  }
  if (swr_ctx != nullptr) {
    swr_free(&swr_ctx);
    av_frame_free(&swr_frame);
  }

  setDownContext(*renderer, PanelManager::sequenceViewer());
}

void ExportThread::wake()
{
  waitCond.wakeAll();
}

void ExportThread::setupH264Encoder(AVCodecContext& ctx, const Params& video_params) const
{
  int ret = 0;
  switch (video_params.compression_type_) {
    case CompressionType::CRF:
      ret = av_opt_set(ctx.priv_data,
                       "crf",
                       QString::number(static_cast<int>(video_params.bitrate_)).toUtf8(),
                       AV_OPT_SEARCH_CHILDREN);
      if (ret < 0) {
        av_strerror(ret, err.data(), ERR_LEN);
        qWarning() << "Failed to set compression mode to CRF, code=" << err.data();
      }
      break;
    default:
      qWarning() << "Unhandled h264 compression type" << static_cast<int>(video_params.compression_type_);
      break;
  }

  ret = 0;
  // FIXME: neither ctx.profile nor av_opt_set does anything
  if (video_params.profile_ == H264_BASELINE_PROFILE) {
    ctx.profile = FF_PROFILE_H264_BASELINE;
  } else if (video_params.profile_ == H264_MAIN_PROFILE) {
    ctx.profile = FF_PROFILE_H264_MAIN;
  } else if (video_params.profile_ == H264_HIGH_PROFILE) {
    ctx.profile = FF_PROFILE_H264_HIGH;
  } else if (video_params.profile_ == H264_HIGH10_PROFILE) {
    ctx.profile = FF_PROFILE_H264_HIGH_10;
  } else if (video_params.profile_ == H264_HIGH422_PROFILE) {
    ctx.profile = FF_PROFILE_H264_HIGH_422;
  } else if (video_params.profile_ == H264_HIGH444_PROFILE) {
    ctx.profile = FF_PROFILE_H264_HIGH_444;
  } else {
    qWarning() << "Unknown H264 profile" << video_params.profile_;
  }

  ret = av_opt_set(ctx.priv_data, "preset", X264_PRESET, 0);
  if (ret < 0) {
    av_strerror(ret, err.data(), ERR_LEN);
    qWarning() << "Failed to set preset, code=" << err.data();
  }

  try {
    bool conv_ok;
    ctx.level = qRound(video_params.level_.toDouble(&conv_ok) * 10);
    Q_ASSERT(conv_ok);
  } catch (const std::invalid_argument& ex) {
    qWarning() << "Failed to convert H264 level to double, ex =" << ex.what();
  }
}


void ExportThread::setupMPEG2Encoder(AVCodecContext& ctx, AVStream& stream, const Params& video_params) const
{
  const auto brate = qRound(video_params.bitrate_ * 1E6);
  // libav complains when using bits as unit. no documentation on what unit actually is
  ctx.rc_buffer_size = brate / 8;
  ctx.rc_max_available_vbv_use = 1.0;
  ctx.rc_min_vbv_overflow_use = 1.0;

  auto props = reinterpret_cast<AVCPBProperties*>(av_stream_new_side_data(&stream, AV_PKT_DATA_CPB_PROPERTIES, NULL));
  props->avg_bitrate = brate;
  props->buffer_size = brate;
  props->max_bitrate = brate;
  props->min_bitrate = brate;

  if (video_params.profile_ == MPEG2_SIMPLE_PROFILE) {
    ctx.profile = FF_PROFILE_MPEG2_SIMPLE;
    ctx.intra_dc_precision = 10;
  } else if (video_params.profile_ == MPEG2_MAIN_PROFILE) {
    ctx.profile = FF_PROFILE_MPEG2_MAIN;
    ctx.intra_dc_precision = 10;
  } else if (video_params.profile_ == MPEG2_HIGH_PROFILE) {
    ctx.profile = FF_PROFILE_MPEG2_HIGH;
    ctx.intra_dc_precision = 11;
  } else if (video_params.profile_ == MPEG2_422_PROFILE) {
    ctx.profile = FF_PROFILE_MPEG2_422;
    ctx.intra_dc_precision = 11;
    // Technically can be 4:2:2 or 4:2:0 but it's the only separable difference between it and "high"
    ctx.pix_fmt = AV_PIX_FMT_YUV422P;
  }

  if (video_params.profile_ == MPEG2_422_PROFILE) {
    if (video_params.level_ == MPEG2_MAIN_LEVEL) {
      ctx.level = 5;
    } else if (video_params.level_ == MPEG2_HIGH_LEVEL) {
      ctx.level = 2;
    } else {
      qWarning() << "Unhandled MPEG2-video level for 422-profile";
    }
  } else {
    if (video_params.level_ == MPEG2_LOW_LEVEL) {
      // Can't find the ffmpeg constant
    } else if (video_params.level_ == MPEG2_MAIN_LEVEL) {
      ctx.level = 8;
    } else if (video_params.level_ == MPEG2_HIGH1440_LEVEL) {
      ctx.level = 6;
    } else if (video_params.level_ == MPEG2_HIGH_LEVEL) {
      ctx.level = 4;
    } else {
      qWarning() << "Unknown MPEG2-video level";
    }
  }
}


void ExportThread::setupMPEG4Encoder(AVCodecContext& ctx, const Params& video_params) const
{
  if (video_params.profile_ == MPEG4_SSTP_PROFILE) {
    ctx.profile = FF_PROFILE_MPEG4_SIMPLE_STUDIO;
  } else {
    qWarning() << "Unknown MPEG4 profile";
  }

  // found in https://svn.code.sf.net/p/gpac/code/trunk/gpac/src/media_tools/av_parsers.c
  // TODO: identify if these are actually being used and are correct
  if (video_params.level_ == MPEG4_SSTP_1_LEVEL) {
    ctx.level = 0xE1;
  } else if (video_params.level_ == MPEG4_SSTP_2_LEVEL) {
    ctx.level = 0xE2;
  } else if (video_params.level_ == MPEG4_SSTP_3_LEVEL) {
    ctx.level = 0xE3;
  } else if (video_params.level_ == MPEG4_SSTP_4_LEVEL) {
    ctx.level = 0xE4;
  } else {
    qWarning() << "Unknown MPEG4 level";
  }
}


void ExportThread::setupDNXHDEncoder(AVCodecContext& ctx, const Params& video_params) const
{
  ctx.profile = FF_PROFILE_DNXHD;
  // FIXME: DNXHDEncContext.pb needs setting up somehow

  if (video_params.profile_.endsWith("x")) {
    // dnxhdenc will deduce this as 10bits
    if (!video_params.pix_fmts_.empty())
    {
      auto fmt = video_params.pix_fmts_.front();
      switch (fmt)
      {
        case PixelFormat::YUV444:
          ctx.pix_fmt = AV_PIX_FMT_YUV444P10;
          break;
        case PixelFormat::YUV422:
          ctx.pix_fmt = AV_PIX_FMT_YUV422P10;
          break;
        case PixelFormat::YUV420:
          // this may not be in any profile
          ctx.pix_fmt = AV_PIX_FMT_YUV420P10;
          break;
      }
    }
  }
}

void ExportThread::setupFrameRate(AVCodecContext& ctx, const double frame_rate) const
{
  Q_ASSERT(frame_rate > 0.0);
  if (qFuzzyCompare(frame_rate, NTSC_24P.first)) {
    ctx.framerate = NTSC_24P.second;
  } else if (qFuzzyCompare(frame_rate, NTSC_30P.first)) {
    ctx.framerate = NTSC_30P.second;
  } else if (qFuzzyCompare(frame_rate, NTSC_60P.first)) {
    ctx.framerate = NTSC_60P.second;
  } else {
    ctx.framerate = av_d2q(frame_rate, INT_MAX);
  }
}


constexpr int ExportThread::convertInterpolationType(const InterpolationType interpoltype) const noexcept
{
  int conv_type = 0;

  switch (interpoltype) {
    case InterpolationType::FAST_BILINEAR:
      conv_type = SWS_FAST_BILINEAR;
      break;
    case InterpolationType::BILINEAR:
      conv_type = SWS_BILINEAR;
      break;
    case InterpolationType::BICUBIC:
      conv_type = SWS_BICUBIC;
      break;
    case InterpolationType::BICUBLIN:
      conv_type = SWS_BICUBLIN;
      break;
    case InterpolationType::LANCZOS:
      break;
      conv_type = SWS_LANCZOS;
      break;
  }

  return conv_type;
}
