/*
 * Chestnut. Chestnut is a free non-linear video editor for Linux.
 * Copyright (C) 2019
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "footagestream.h"

#include <QPainter>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QDateTime>
#include <mediahandling/imediastream.h>
#include <mediahandling/gsl-lite.hpp>
#include <fmt/core.h>
#include <stdlib.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}


#include "io/path.h"
#include "debug.h"

using project::FootageStream;
using media_handling::FieldOrder;
using media_handling::MediaStreamPtr;
using media_handling::MediaProperty;
using media_handling::StreamType;


constexpr auto IMAGE_FRAMERATE = 0;
constexpr auto PREVIEW_HEIGHT = 480;
constexpr auto PREVIEW_CHANNELS = 4; // RGBA
constexpr auto PREVIEW_DIR = "/previews";
constexpr auto THUMB_PREVIEW_FORMAT = "jpg";
constexpr auto THUMB_PREVIEW_QUALITY = 80;
constexpr auto EXTENSION = "dat";
constexpr auto PIXELS_PER_SECOND = 100;
#ifdef __linux__
constexpr auto CMD_FORMAT = "ffmpeg -i \"{0}\" -map 0:{1} -f wav - | audiowaveform --input-format wav --output-format {2} -b 8 "
                            "--split-channels --pixels-per-second {3} -o \"{4}\" &>/dev/null";
#elif _WIN64
//TODO:
constexpr auto CMD_FORMAT = "";
#endif

FootageStream::FootageStream(MediaStreamPtr stream_info, QString source_path, const bool is_audio)
  : stream_info_(std::move(stream_info)),
    source_path_(std::move(source_path)),
    audio_(is_audio)
{
  Q_ASSERT(stream_info_);
  initialise(*stream_info_);
  data_path = chestnut::paths::dataPath() + PREVIEW_DIR;
  const QDir data_dir(data_path);
  if (!data_dir.exists()) {
    data_dir.mkpath(".");
  }
}


bool FootageStream::generatePreview()
{
  bool success = false;
  qDebug() << "Generating preview, index=" << file_index << ", path:" << source_path_;
  if (audio_) {
    success = generateAudioPreview();
  } else {
    success = generateVisualPreview();
    if (success) {
      makeSquareThumb();
    }
  }
  qDebug() << "success:" << success << ", index:" << file_index << ", path:" << source_path_;
  preview_done_ = success;
  return success;
}

void FootageStream::makeSquareThumb()
{
  qDebug() << "Making square thumbnail";
  // generate square version for QListView?
  const auto max_dimension = qMax(video_preview.width(), video_preview.height());
  QPixmap pixmap(max_dimension, max_dimension);
  pixmap.fill(Qt::transparent);
  QPainter p(&pixmap);
  const auto diff = (video_preview.width() - video_preview.height()) / 2;
  const auto sqx = (diff < 0) ? -diff : 0;
  const auto sqy = (diff > 0) ? diff : 0;
  p.drawImage(sqx, sqy, video_preview);
  video_preview_square.addPixmap(pixmap);
}


void FootageStream::setStreamInfo(MediaStreamPtr stream_info)
{
  stream_info_ = std::move(stream_info);
  qDebug() << "Stream Info set, file_index ="  << file_index << this;
  initialise(*stream_info_);
}

std::optional<media_handling::FieldOrder> FootageStream::fieldOrder() const
{
  if (!stream_info_) {
    qDebug() << "Stream Info not available, file_index =" << file_index << this;
    return {};
  }
  if (stream_info_->type() == StreamType::IMAGE) {
    return media_handling::FieldOrder::PROGRESSIVE;
  }
  bool isokay = false;
  const auto f_order(stream_info_->property<FieldOrder>(MediaProperty::FIELD_ORDER, isokay));
  if (!isokay) {
    return {};
  }
  return f_order;
}

bool FootageStream::load(QXmlStreamReader& stream)
{
  auto name = stream.name().toString().toLower();
  if (name == "video") {
    type_ = StreamType::VIDEO;
  } else if (name == "audio") {
    type_ = StreamType::AUDIO;
  } else if (name == "image") {
    type_ = StreamType::IMAGE;
  } else {
    qCritical() << "Unknown stream type" << name;
    return false;
  }

  // attributes
  for (const auto& attr : stream.attributes()) {
    const auto name = attr.name().toString().toLower();
    if (name == "id") {
      file_index = attr.value().toInt();
    } else if ( (name == "infinite")  && (type_ == StreamType::VIDEO) ) {
      infinite_length = attr.value().toString().toLower() == "true";
    } else {
      qWarning() << "Unknown attribute" << name;
    }
  }

  // elements
  while (stream.readNextStartElement()) {
    const auto name = stream.name().toString().toLower();
    if ( (name == "width") && (type_ == StreamType::VIDEO) ) {
      video_width = stream.readElementText().toInt();
    } else if ( (name == "height") && (type_ == StreamType::VIDEO) ) {
      video_height = stream.readElementText().toInt();
    } else if ( (name == "framerate") && (type_ == StreamType::VIDEO) ) {
      video_frame_rate = stream.readElementText().toDouble();
    } else if ( (name == "channels") && (type_ == StreamType::AUDIO) ) {
      audio_channels = stream.readElementText().toInt();
    } else if ( (name == "frequency") && (type_ == StreamType::AUDIO) ) {
      audio_frequency = stream.readElementText().toInt();
    } else if ( (name == "layout") && (type_ == StreamType::AUDIO) ) {
      audio_layout = stream.readElementText().toInt();
    } else {
      qWarning() << "Unhandled element" << name;
      stream.skipCurrentElement();
    }
  }

  return true;
}

bool FootageStream::save(QXmlStreamWriter& stream) const
{
  switch (type_) {
    case StreamType::VIDEO:
      [[fallthrough]];
    case StreamType::IMAGE:
    {
      const auto type_str = type_ == StreamType::VIDEO ? "video" : "image";
      stream.writeStartElement(type_str);
      stream.writeAttribute("id", QString::number(file_index));
      stream.writeAttribute("infinite", infinite_length ? "true" : "false");

      stream.writeTextElement("width", QString::number(video_width));
      stream.writeTextElement("height", QString::number(video_height));
      stream.writeTextElement("framerate", QString::number(video_frame_rate, 'g', 10));
      stream.writeEndElement();
    }
      return true;
    case StreamType::AUDIO:
      stream.writeStartElement("audio");
      stream.writeAttribute("id", QString::number(file_index));

      stream.writeTextElement("channels", QString::number(audio_channels));
      stream.writeTextElement("layout", QString::number(audio_layout));
      stream.writeTextElement("frequency", QString::number(audio_frequency));
      stream.writeEndElement();
      return true;
    default:
      chestnut::throwAndLog("Unknown stream type. Not saving");
  }
  return false;
}


auto convert(char* data, int size)
{
  uint32_t val = 0;
  for (auto i = 0; i < size; ++i){
    val |= static_cast<uint8_t>(data[i]) << (i * 8);
  }
  return val;
}


bool FootageStream::loadWaveformFile(const QString& data_path)
{
  qInfo() << "Reading waveform data, path:" << data_path;
  QFile file(data_path);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Failed to open wavform file, path:" << data_path;
    return false;
  }

  char data[4];
  auto readUint = [&]() -> uint32_t {
                  if (file.read(data, 4) != 4) {
                  throw std::runtime_error("Unable to read from file");
}
                  return convert(data, 4);
};

  waveform_info_.version_ = readUint();
  if (waveform_info_.version_ < 2){
    qCritical() << "audiowaveform version is not supported";
    return false;
  }
  waveform_info_.flags_ = readUint();
  waveform_info_.rate_ = readUint();
  waveform_info_.samples_per_pixel_= readUint();
  waveform_info_.length_ = readUint();
  waveform_info_.channels_ = readUint();

  // TODO: identify what to do about 16bit sampling
  const size_t datasize = [&] {
    size_t sz = waveform_info_.length_ * waveform_info_.channels_ * 2;
    if ((waveform_info_.flags_ & 0x1) == 0) {
      // 16bit values
      sz *= 2;
    }
    return sz;
  }();

  audio_preview.clear();
  QByteArray samples = file.read(datasize);
  for (const auto& samp: samples) {
    audio_preview.push_back(samp);
  }
  return true;
}


void FootageStream::initialise(const media_handling::IMediaStream& stream)
{
  file_index = stream.sourceIndex();
  type_ = stream.type();

  bool is_okay = false;
  if (type_ == StreamType::VIDEO) {
    const auto frate = stream.property<media_handling::Rational>(MediaProperty::FRAME_RATE, is_okay);
    if (!is_okay) {
      constexpr auto msg = "Unable to identify video frame rate";
      qWarning() << msg;
      throw std::runtime_error(msg);
    }
    video_frame_rate = boost::rational_cast<double>(frate);
    const auto dimensions = stream.property<media_handling::Dimensions>(MediaProperty::DIMENSIONS, is_okay);
    if (!is_okay) {
      constexpr auto msg = "Unable to identify video dimension";
      qWarning() << msg;
      throw std::runtime_error(msg);
    }
    video_width = dimensions.width;
    video_height = dimensions.height;
  } else if (type_ == StreamType::IMAGE) {
    infinite_length = true;
    video_frame_rate = 0.0;
    const auto dimensions = stream.property<media_handling::Dimensions>(MediaProperty::DIMENSIONS, is_okay);
    if (!is_okay) {
      constexpr auto msg = "Unable to identify image dimension";
      qWarning() << msg;
      throw std::runtime_error(msg);
    }
    video_width = dimensions.width;
    video_height = dimensions.height;
  } else if (type_ == StreamType::AUDIO) {
    audio_channels = stream.property<int32_t>(MediaProperty::AUDIO_CHANNELS, is_okay);
    if (!is_okay) {
      constexpr auto msg = "Unable to identify audio channel count";
      qWarning() << msg;
      throw std::runtime_error(msg);
    }
    audio_frequency = stream.property<int32_t>(MediaProperty::AUDIO_SAMPLING_RATE, is_okay);
    if (!is_okay) {
      constexpr auto msg = "Unable to identify audio sampling rate";
      qWarning() << msg;
      throw std::runtime_error(msg);
    }
  } else {
    qWarning() << "Unhandled Stream type";
  }
}

void thumb_data_cleanup(void *info)
{
  delete [] static_cast<uint8_t*>(info);
}

bool FootageStream::generateVisualPreview()
{
  if (type_ == media_handling::StreamType::IMAGE) {
    const QImage img(source_path_);
    if (!img.isNull()) {
      video_preview     = img.scaledToHeight(PREVIEW_HEIGHT, Qt::SmoothTransformation);
      video_height      = img.height();
      video_width       = img.width();
      infinite_length   = true;
      video_frame_rate  = IMAGE_FRAMERATE;
      return true;
    } else {

      qCritical() << "Failed to open image, path:" << source_path_;
    }
  } else {
    const auto preview_path = thumbnailPath();
    if (QFileInfo(preview_path).exists()) {
      video_preview = QImage(preview_path);
    } else {
      // TODO: use source_info_
      qInfo() << "Generating preview from video, file:" << source_path_;
      const auto filename(source_path_.toUtf8().data());
      QString errorStr;
      bool error = false;
      AVFormatContext* fmt_ctx = nullptr;
      int err_code = avformat_open_input(&fmt_ctx, filename, nullptr, nullptr);
      if (err_code != 0) {
        return false;
        //          char err[1024];
        //          av_strerror(errCode, err, 1024);
        //          errorStr = QObject::tr("Could not open file - %1").arg(err);
        //          error = true;
      }
      err_code = avformat_find_stream_info(fmt_ctx, nullptr);
      if (err_code < 0) {
        return false;
        //          char err[1024];
        //          av_strerror(err_code, err, 1024);
        //          errorStr = tr("Could not find stream information - %1").arg(err);
        //          error = true;
      }

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
      bool end_of_file = false;

      // get the ball rolling
      do {
        av_read_frame(fmt_ctx, packet);
      } while (codec_ctx[packet->stream_index] == nullptr);
      avcodec_send_packet(codec_ctx[packet->stream_index], packet);

      SwsContext* sws_ctx = nullptr;
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
        if (end_of_file) {
          break;
        }
        const auto isVideo = streams[packet->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
        if (isVideo) {
          if (!preview_done_) {
            const int dstW = lround(PREVIEW_HEIGHT * (static_cast<double>(temp_frame->width) / temp_frame->height));
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

            video_preview = QImage(imgData, dstW, PREVIEW_HEIGHT, linesize[0], QImage::Format_RGBA8888, thumb_data_cleanup);
            if (!video_preview.save(preview_path, THUMB_PREVIEW_FORMAT, THUMB_PREVIEW_QUALITY)) {
              qWarning() << "Video preview did not save, path:" << source_path_;
            }

            sws_freeContext(sws_ctx);

            avcodec_close(codec_ctx[packet->stream_index]);
            codec_ctx[packet->stream_index] = nullptr;
          }
          media_lengths.at(packet->stream_index)++;
        }

        av_packet_unref(packet);
      }//while

      av_frame_free(&temp_frame);
      av_packet_free(&packet);
      for (auto codec : codec_ctx){
        if (codec != nullptr) {
          avcodec_close(codec);
          avcodec_free_context(&codec);
        }
      }
      delete [] media_lengths.data();

      delete [] codec_ctx.data();

      av_dump_format(fmt_ctx, 0, filename, 0);
      avformat_close_input(&fmt_ctx);
    }
  }
  return true;
}

bool FootageStream::generateAudioPreview()
{
  bool success = false;
  const auto preview_path = waveformPath();
  QFileInfo file(preview_path);
  if (file.exists() && file.size() > 0) {
    success = loadWaveformFile(preview_path);
    qDebug() << "Opened existing preview, index:" << file_index;
    success = true;
  } else {
    const auto cmd = fmt::format(CMD_FORMAT, source_path_.toStdString(), file_index, EXTENSION, PIXELS_PER_SECOND,
                                 preview_path.toStdString());
    if (system(cmd.c_str()) == 0) {
      success = loadWaveformFile(preview_path);
      success = true;
    } else {
      qWarning() << "Failed to generate waveform, index:" << file_index;
    }
  }
  return success;
}

QString FootageStream::previewHash() const
{
  const QFileInfo file_info(source_path_);
  const QString cache_file(file_info.fileName()
                           + QString::number(file_info.size())
                           + QString::number(file_info.lastModified().toMSecsSinceEpoch()));
  const QString hash(QCryptographicHash::hash(cache_file.toUtf8(), QCryptographicHash::Md5).toHex());
  return hash;
}

QString FootageStream::thumbnailPath() const
{
  return QDir(data_path).filePath(previewHash() + "t" + QString::number(file_index));
}


QString FootageStream::waveformPath() const
{
  return QDir(data_path).filePath(previewHash() + "w" + QString::number(file_index));
}
