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
#include "previewgenerator.h"

#include <QPainter>
#include <QPixmap>
#include <QtMath>
#include <QTreeWidgetItem>
#include <QSemaphore>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QMimeType>
#include <QMimeDatabase>
#include <mediahandling/gsl-lite.hpp>
#include <mediahandling/mediahandling.h>

#include "project/media.h"
#include "project/footage.h"
#include "panels/viewer.h"
#include "panels/project.h"
#include "io/config.h"
#include "io/path.h"
#include "debug.h"


extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

using project::FootageStreamPtr;
using project::FootageStream;
using project::ScanMethod;

constexpr auto IMAGE_FRAMERATE = 0;
constexpr auto PREVIEW_HEIGHT = 480;
constexpr auto PREVIEW_CHANNELS = 4;
constexpr auto WAVEFORM_RESOLUTION = 64.0;
constexpr auto PREVIEW_DIR = "/previews";
constexpr auto THUMB_PREVIEW_FORMAT = "jpg";
constexpr auto THUMB_PREVIEW_QUALITY = 80;

namespace {
  QSemaphore sem(3); // only 5 preview generators can run at one time
}


//FIXME: this whole class needs to be redone. it is trying to haphazardly lazily initialize the Footage and Stream classes

PreviewGenerator::PreviewGenerator(MediaPtr item, const FootagePtr& ftg, const bool replacing, QObject *parent) :
  QThread(parent),
  fmt_ctx(nullptr),
  media(std::move(item)),
  footage(ftg),
  contains_still_image(false),
  replace(replacing),
  cancelled(false)
{
  data_path = chestnut::paths::dataPath() + PREVIEW_DIR;
  QDir data_dir(data_path);
  if (!data_dir.exists()) {
    data_dir.mkpath(".");
  }

  connect(this, SIGNAL(finished()), this, SLOT(deleteLater()));
}

PreviewGenerator::~PreviewGenerator()
{
  //TODO: check
  fmt_ctx = nullptr;
}

void PreviewGenerator::parse_media()
{
  if (auto ftg = footage.lock()) {
    if (!ftg->videoTracks().empty()) {
      if (const auto ms = ftg->videoTracks().front(); ms->type_ == media_handling::StreamType::IMAGE) {
        contains_still_image = true;
      }
    }

    finalize_media();
  } else {
    qCritical() << "Footage object is NULL";
  }
}

bool PreviewGenerator::retrieve_preview(const QString& hash)
{
  qDebug() << "Retrieving preview";
  bool found = false;
  auto ftg = footage.lock();
  if (ftg == nullptr) {
    qCritical() << "Footage instance is null";
    return false;
  }

  for (auto& stream: ftg->videoTracks()) {
    const auto thumb_path(get_thumbnail_path(hash, stream));
    QFile f(thumb_path);
    if (f.exists() && stream->video_preview.load(thumb_path)) {
//      stream->make_square_thumb();
      stream->preview_done = true;
    } else {
      found = false;
      break;
    }
  } //for

  if (!found) {
    for (const auto& stream: ftg->videoTracks()) {
      Q_ASSERT(stream);
      stream->preview_done = false;
    }
  }
  return found;
}

void PreviewGenerator::finalize_media()
{
  if (auto ftg = footage.lock()) {
    ftg->ready_ = true;

    if (!cancelled) {
      if (ftg->videoTracks().empty()) {
        emit set_icon(ICON_TYPE_AUDIO, replace);
      } else if (contains_still_image) {
        emit set_icon(ICON_TYPE_IMAGE, replace);
      } else {
        emit set_icon(ICON_TYPE_VIDEO, replace);
      }
    }
  }
}

void thumb_data_cleanup(void *info)
{
  delete [] static_cast<uint8_t*>(info);
}

void PreviewGenerator::generate_waveform()
{
  qDebug() << "Generating waveform";
  gsl::span<AVCodecContext*> codec_ctx(new AVCodecContext* [fmt_ctx->nb_streams], fmt_ctx->nb_streams);
  gsl::span<AVStream*> streams(fmt_ctx->streams, fmt_ctx->nb_streams);
  for (size_t i = 0; i < streams.size(); ++i) {
    codec_ctx[i] = nullptr;
    if ( (streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
         || (streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ) {
      AVCodec* const codec = avcodec_find_decoder(streams[i]->codecpar->codec_id);
      if (codec == nullptr) {
        continue;
      }
      codec_ctx[i] = avcodec_alloc_context3(codec); //FIXME: memory leak
      avcodec_parameters_to_context(codec_ctx[i], streams[i]->codecpar);
      avcodec_open2(codec_ctx[i], codec, nullptr);
      if ( (streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) && (codec_ctx[i]->channel_layout == 0) ) {
        codec_ctx[i]->channel_layout = av_get_default_channel_layout(streams[i]->codecpar->channels);
      }
    }
  }//for

  AVPacket* packet = av_packet_alloc();
  bool done = true;

  bool end_of_file = false;

  // get the ball rolling
  do {
    av_read_frame(fmt_ctx, packet);
  } while (codec_ctx[packet->stream_index] == nullptr);
  avcodec_send_packet(codec_ctx[packet->stream_index], packet);

  if (auto ftg = footage.lock()) {
    SwsContext* sws_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;
    AVFrame* temp_frame = av_frame_alloc();
    gsl::span<int64_t> media_lengths(new int64_t[fmt_ctx->nb_streams], fmt_ctx->nb_streams);
    while (!end_of_file) {
      while ( (codec_ctx[packet->stream_index] == nullptr)
              || (avcodec_receive_frame(codec_ctx[packet->stream_index], temp_frame) == AVERROR(EAGAIN)) ) {
        av_packet_unref(packet);
        const auto read_ret = av_read_frame(fmt_ctx, packet);
        if (read_ret < 0) {
          end_of_file = true;
          if (read_ret != AVERROR_EOF) {
            qCritical() << "Failed to read packet for preview generation" << read_ret;
          }
          break;
        }
        if (codec_ctx[packet->stream_index] != nullptr) {
          const auto send_ret = avcodec_send_packet(codec_ctx[packet->stream_index], packet);
          if (send_ret < 0 && send_ret != AVERROR(EAGAIN)) {
            qCritical() << "Failed to send packet for preview generation - aborting" << send_ret;
            end_of_file = true;
            break;
          }
        }
      }
      if (!end_of_file) {
        const auto isVideo = streams[packet->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
        FootageStreamPtr ms;
        if (isVideo) {
          ms = ftg->video_stream_from_file_index(packet->stream_index);
        } else {
          ms = ftg->audio_stream_from_file_index(packet->stream_index);
        }

        if (ms != nullptr) {
          if (isVideo) {
            if (!ms->preview_done) {
              const int dstW = lround(PREVIEW_HEIGHT * (static_cast<double>(temp_frame->width)/static_cast<double>(temp_frame->height)));
              auto const imgData = new uint8_t[dstW * PREVIEW_HEIGHT * PREVIEW_CHANNELS]; //FIXME: still showing up as a memory leak

              sws_ctx = sws_getContext(
                          temp_frame->width,
                          temp_frame->height,
                          static_cast<AVPixelFormat>(temp_frame->format),
                          dstW,
                          PREVIEW_HEIGHT,
                          static_cast<AVPixelFormat>(AV_PIX_FMT_RGBA),
                          SWS_FAST_BILINEAR,
                          nullptr,
                          nullptr,
                          nullptr
                          );

              int linesize[AV_NUM_DATA_POINTERS];
              linesize[0] = dstW * 4;
              sws_scale(sws_ctx, temp_frame->data, temp_frame->linesize, 0, temp_frame->height, &imgData, linesize);

              ms->video_preview = QImage(imgData, dstW, PREVIEW_HEIGHT, linesize[0], QImage::Format_RGBA8888, thumb_data_cleanup);
//              ms->make_square_thumb();
              ms->preview_done = true;

              sws_freeContext(sws_ctx);

              avcodec_close(codec_ctx[packet->stream_index]);
              codec_ctx[packet->stream_index] = nullptr;
            }
            media_lengths.at(packet->stream_index)++;
          } else if (streams[packet->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            // Do nothing
          } else {
            qWarning() << "Unhandled stream type" << streams[static_cast<size_t>(packet->stream_index)]->codecpar->codec_type;
          }
        } else {
          qCritical() << "FootageStream is null";
        }

        // check if we've got all our previews
        if (ftg->audioTracks().empty()) {
          done = true;
          for (const auto& stream : ftg->videoTracks()) {
            if (stream && !stream->preview_done) {
              done = false;
              break;
            }
          }
          if (done) {
            end_of_file = true;//FIXME: never used
            break;
          }
        }
        av_packet_unref(packet);
      }
    }//while

    av_frame_free(&temp_frame);
    av_packet_free(&packet);
    for (auto codec : codec_ctx){
      if (codec != nullptr) {
        avcodec_close(codec);
        avcodec_free_context(&codec);
      }
    }
    finalize_media();
    delete [] media_lengths.data();
  }

  delete [] codec_ctx.data();
}

QString PreviewGenerator::get_thumbnail_path(const QString& hash, FootageStreamPtr& ms)
{
  return data_path + "/" + hash + "t" + QString::number(ms->file_index);
}


QString PreviewGenerator::getWaveformPath(QString file_path, const int index) const
{
  return QDir(data_path).filePath(generatePreviewHash(file_path) + "w" + QString::number(index));
}

void PreviewGenerator::generateAudioPreview(FootagePtr ftg, FootageStreamPtr ms)
{

}

void PreviewGenerator::generateVideoPreview()
{
  //TODO:
}

void PreviewGenerator::generateImagePreview(FootagePtr ftg)
{
  Q_ASSERT(ftg);
  qInfo() << "Generating image preview, fileName =" << ftg->location();
  sem.acquire();
  if (generate_image_thumbnail(ftg)) {
    qDebug() << "Preview generated, fileName =" << ftg->location();
    ftg->ready_ = true;
    ftg->has_preview_ = true;
    emit set_icon(ICON_TYPE_IMAGE, replace);
    return;
  }

  qWarning() << "Failed to generate thumbnail for image. path=" << ftg->location();
  sem.release();
}


QString PreviewGenerator::generatePreviewHash(QString file_path) const
{
  const QFileInfo file_info(std::move(file_path));
  const QString cache_file(file_info.fileName()
                           + QString::number(file_info.size())
                           + QString::number(file_info.lastModified().toMSecsSinceEpoch()));
  const QString hash(QCryptographicHash::hash(cache_file.toUtf8(), QCryptographicHash::Md5).toHex());
  return hash;
}


// TODO: make the Footage class responsible for creating its previews
void PreviewGenerator::run()
{
  Q_ASSERT(media != nullptr);

  auto ftg = footage.lock();
  if (ftg == nullptr) {
    qCritical() << "Footage instance is NULL";
    return;
  }

  if (const auto v_t = ftg->visualType()) {
    if (v_t == media_handling::StreamType::IMAGE) {
      generateImagePreview(ftg);
    } else if (v_t == media_handling::StreamType::VIDEO) {
      generateVideoPreview();
      for (auto& ms : ftg->audioTracks()) {
        generateAudioPreview(ftg, ms);
      }
    } else {
      qWarning() << "Unhandled visual type, fileName =" << ftg->location();
    }
  } else if (ftg->hasAudio()) {
    for (auto& ms : ftg->audioTracks()) {
      generateAudioPreview(ftg, ms);
    }
  } else {
    qCritical() << "Source has no supported streams, fileName =" << ftg->location();
  }

  // video/audio waveform generation
  const auto filename(ftg->location().toUtf8().data());

  QString errorStr;
  bool error = false;
  int errCode = avformat_open_input(&fmt_ctx, filename, nullptr, nullptr);
  if(errCode != 0) {
    char err[1024];
    av_strerror(errCode, err, 1024);
    errorStr = tr("Could not open file - %1").arg(err);
    error = true;
  } else {
    errCode = avformat_find_stream_info(fmt_ctx, nullptr);
    if (errCode < 0) {
      char err[1024];
      av_strerror(errCode, err, 1024);
      errorStr = tr("Could not find stream information - %1").arg(err);
      error = true;
    } else {
      parse_media();

      // see if we already have data for this
      const auto hash = generatePreviewHash(ftg->location());
      qDebug() << "Preview hash =" << hash;
      if (!retrieve_preview(hash)) {
        av_dump_format(fmt_ctx, 0, filename, 0);
        qDebug() << "No preview found";
        sem.acquire();

        // FIXME: This does far more than the name suggests i.e. finds interlace method of Ftg
        generate_waveform();

        // save preview to file
        for (auto& ms : ftg->videoTracks()) {
          Q_ASSERT(ms);
          const auto thumbPath = get_thumbnail_path(hash, ms);
          auto tmp = ms->video_preview;
          if (!tmp.save(thumbPath, THUMB_PREVIEW_FORMAT, THUMB_PREVIEW_QUALITY)) {
            qWarning() << "Video Preview did not save." << thumbPath;
          }
        }
        sem.release();
      }
    }
    avformat_close_input(&fmt_ctx);
  }

  if (error) {
    media->updateTooltip(errorStr);
    emit set_icon(ICON_TYPE_ERROR, replace);
    qCritical() << "Failed to generate preview, msg =" << errorStr << ", fileName =" << ftg->location();
  } else {
    qDebug() << "Preview generated, fileName =" << ftg->location();
    ftg->has_preview_ = true;
    media->updateTooltip();
  }
}

bool PreviewGenerator::generate_image_thumbnail(const FootagePtr& ftg) const
{
  const QImage img(ftg->location());
  if (auto ms = ftg->videoTracks().front()) {
    ms->enabled           = true;
    ms->file_index        = 0;
    ms->video_preview     = img.scaledToHeight(PREVIEW_HEIGHT, Qt::SmoothTransformation);
//    ms->make_square_thumb();
    ms->preview_done      = true;
    ms->video_height      = img.height();
    ms->video_width       = img.width();
    ms->infinite_length   = true;
    ms->video_frame_rate  = IMAGE_FRAMERATE;
    return true;
  }
  return false;
}

void PreviewGenerator::cancel()
{
  cancelled = true;
}
