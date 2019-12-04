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

#include <mediahandling/imediastream.h>

#include "debug.h"

using project::FootageStream;
using media_handling::FieldOrder;
using media_handling::MediaStreamPtr;
using media_handling::MediaProperty;
using media_handling::StreamType;

FootageStream::FootageStream(MediaStreamPtr stream_info)
  : stream_info_(std::move(stream_info))
{
  Q_ASSERT(stream_info_);
  initialise(*stream_info_);
}

void FootageStream::make_square_thumb()
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
  video_preview_square = QIcon(pixmap);
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

void FootageStream::onWaveformGenerated(std::string data_path)
{
  qInfo() << "Reading waveform data:" << data_path.c_str();
  QFile file(data_path.c_str());
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Failed to open wavform file, path:" << data_path.c_str();
    return;
  }
  char data[4];
  uint32_t version, flags, rate, spp, length, channels;
  if (file.read(data, 4) != 4) {
    return;
  }
  version = convert(data, 4);
  if (file.read(data, 4) != 4) {
    return;
  }
  flags = convert(data, 4);
  if (file.read(data, 4) != 4) {
    return;
  }
  rate = convert(data, 4);
  if (file.read(data, 4) != 4) {
    return;
  }
  spp = convert(data, 4);
  if (file.read(data, 4) != 4) {
    return;
  }
  length = convert(data, 4);
  if (file.read(data, 4) != 4) {
    return;
  }
  channels = convert(data, 4);
  qDebug() << "Read header";
  //TODO:
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
