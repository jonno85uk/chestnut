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

#include "io/path.h"
#include "debug.h"


using project::FootageStream;
using media_handling::FieldOrder;
using media_handling::MediaStreamPtr;
using media_handling::MediaProperty;
using media_handling::StreamType;


constexpr auto IMAGE_FRAMERATE = 0;
constexpr auto PREVIEW_HEIGHT = 480;
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
      qInfo() << "Generating preview from video, file:" << source_path_;
      stream_info_->setOutputFormat(media_handling::PixelFormat::RGBA);
      if (auto frame = stream_info_->frame(0)) { //TODO: use the wadsworth constant?
        auto f_d = frame->data();
        video_preview = QImage(*f_d.data_, video_width, video_height, f_d.line_size_, QImage::Format_RGBA8888);
        if (!video_preview.isNull()) {
          video_preview.scaledToHeight(PREVIEW_HEIGHT, Qt::SmoothTransformation);
          if (video_preview.save(preview_path, THUMB_PREVIEW_FORMAT, THUMB_PREVIEW_QUALITY)) {
            // f_d.data_ exists for the lifetime of the frame unless memcpy
            // just reload from FS instead
            video_preview.load(preview_path);
          } else {
            qWarning() << "Video preview did not save, path:" << source_path_;
          }
        } else {
          qCritical() << "Failed to load image data, path:" << source_path_;
        }
      } else {
        qCritical() << "Failed to retrieve a video frame from backend, path:" << source_path_;
      }
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
