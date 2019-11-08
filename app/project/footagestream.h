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

#ifndef FOOTAGESTREAM_H
#define FOOTAGESTREAM_H

#include <memory>
#include <QImage>
#include <QIcon>
#include <mediahandling/imediastream.h>

#include "project/ixmlstreamer.h"

namespace project {

  enum class ScanMethod {
    PROGRESSIVE = 0,
    TOP_FIRST = 1,
    BOTTOM_FIRST = 2,
    UNKNOWN
  };

  class FootageStream : public project::IXMLStreamer {
    public:
      FootageStream();
      explicit FootageStream(media_handling::MediaStreamPtr stream_info);

      FootageStream(const FootageStream& cpy) = delete;
      FootageStream(const FootageStream&& cpy) = delete;
      FootageStream& operator=(const FootageStream& rhs) = delete;
      FootageStream& operator=(const FootageStream&& rhs) = delete;

      void make_square_thumb();
      void setStreamInfo(media_handling::MediaStreamPtr stream_info);
      std::optional<media_handling::FieldOrder> fieldOrder() const;

      virtual bool load(QXmlStreamReader& stream) override;
      virtual bool save(QXmlStreamWriter& stream) const override;

      int file_index {-1};
      int video_width {-1};
      int video_height {-1};
      bool infinite_length {false};
      double video_frame_rate {0.0};
      int audio_channels {-1};
      int audio_layout {-1};
      int audio_frequency {-1};
      bool enabled {true};

      // preview thumbnail/waveform
      bool preview_done {false};
      QImage video_preview;
      QIcon video_preview_square;
      QVector<char> audio_preview;
      media_handling::StreamType type_ {media_handling::StreamType::UNKNOWN};
    private:
      media_handling::MediaStreamPtr stream_info_{nullptr};

      void initialise(const media_handling::IMediaStream& stream);
  };
  using FootageStreamPtr = std::shared_ptr<FootageStream>;
  using FootageStreamWPtr = std::weak_ptr<FootageStream>;
}

#endif // FOOTAGESTREAM_H
