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

#ifndef FOOTAGE_H
#define FOOTAGE_H

#include <QString>
#include <QVector>
#include <QDir>
#include <mediahandling/imediasource.h>

#include "project/projectitem.h"
#include "project/ixmlstreamer.h"
#include "project/footagestream.h"


class Clip;
class PreviewGenerator;
class MediaThrobber;
class Media;


using FootagePtr = std::shared_ptr<Footage>;
using FootageWPtr = std::weak_ptr<Footage>;


class Footage : public std::enable_shared_from_this<Footage>, public project::ProjectItem
{
  public:
    int64_t length_{0};

    QDir proj_dir_{};
    int save_id{-1};
    int folder_{-1};

    double speed_{1.0};

    PreviewGenerator* preview_gen{nullptr};

    long in{0};
    long out{0};
    bool using_inout{false};

    std::atomic_bool ready_;
    std::atomic_bool has_preview_;

    Footage() = delete;
    explicit Footage(const std::shared_ptr<Media>& parent);
    Footage(QString url, const std::shared_ptr<Media>& parent, const bool import_as_sequence=false);
    Footage(const Footage& cpy);

    bool generatePreviews();

    /**
     * @brief Identify if the Footage is missing its source file
     * @return true==file is not present/available
     */
    bool isMissing() const noexcept;

    /**
     * @brief Obtain the length of the footage, ignoring any in/out points
     * @param frame_rate
     * @return
     */
    long totalLengthInFrames(const double frame_rate) const noexcept;

    /**
     * @brief  Obtain the length of the footage acknowledging any in/out points
     * @param frame_rate
     * @return
     */
    long activeLengthInFrames(const double frame_rate) const noexcept;

    project::FootageStreamPtr video_stream_from_file_index(const int index);
    project::FootageStreamPtr audio_stream_from_file_index(const int index);
    bool has_stream_from_file_index(const int index);
    bool has_video_stream_from_file_index(const int index);
    bool has_audio_stream_from_file_index(const int index) const;
    void reset();
    [[deprecated]]
    bool isImage() const;
    std::optional<media_handling::StreamType> visualType() const;
    bool hasAudio() const;

    void setParent(std::shared_ptr<Media> mda);
    /**
     * @brief   Retrieve the location of the footage's source
     * @return  Path
     */
    QString location() const;

    /**
     * @brief Read the footage and extract the streams
     */
    void parseStreams();

    bool addVideoTrack(project::FootageStreamPtr track);
    QVector<project::FootageStreamPtr> videoTracks() const;
    bool addAudioTrack(project::FootageStreamPtr track);
    QVector<project::FootageStreamPtr> audioTracks() const;

    virtual bool load(QXmlStreamReader& stream) override;
    virtual bool save(QXmlStreamWriter& stream) const override;
  private:
    friend class FootageTest;
    QString url_;
    std::weak_ptr<Media> parent_mda_;
    media_handling::MediaSourcePtr media_source_ {nullptr};
    QVector<project::FootageStreamPtr> video_tracks;
    QVector<project::FootageStreamPtr> audio_tracks;
    bool import_as_sequence_ {false};

    project::FootageStreamPtr get_stream_from_file_index(const bool video, const int index);
};

#endif // FOOTAGE_H
