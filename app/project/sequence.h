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
#ifndef SEQUENCE_H
#define SEQUENCE_H

#include <QVector>
#include <QMap>
#include <memory>

#include "project/marker.h"
#include "project/selection.h"
#include "project/projectitem.h"
#include "project/track.h"

//FIXME: this is used EVERYWHERE. This has to be water-tight and heavily tested.

class Clip;
class Media;
class Transition;

using ClipPtr = std::shared_ptr<Clip>;
using TransitionPtr = std::shared_ptr<Transition>;
using TransitionWPtr = std::weak_ptr<Transition>;

using SequencePtr = std::shared_ptr<Sequence>;
using SequenceUPtr = std::unique_ptr<Sequence>;
using SequenceWPtr = std::weak_ptr<Sequence>;


Q_DECLARE_METATYPE(SequencePtr)


class Sequence : public std::enable_shared_from_this<Sequence>, public project::ProjectItem {
public:

    QVector<Selection> selections_;
    int32_t save_id_ = 0; //FIXME: fudge
    struct {
        bool using_ {false};
        bool enabled_ {true};
        int64_t in_ {0};
        int64_t out_ {0};
    } workarea_;

    QVector<TransitionPtr> transitions_;
    int64_t playhead_ = 0;
    bool wrapper_sequence_ = false;
    std::weak_ptr<Media> parent_mda{};

    Sequence() = default;
    explicit Sequence(std::shared_ptr<Media> parent);
    Sequence(QVector<std::shared_ptr<Media>>& media_list, const QString& sequenceName);

    std::shared_ptr<Sequence> copy();
    /**
     * @brief Obtain the track extents of a sequence,
     * i.e. last populated video-track on track1, last populated audio-track on track4
     * @return video-limit, audio-limit
     */
    std::pair<int64_t,int64_t> trackLimits() const;
    /**
     * @brief   Obtain the frame at where the last clip ends in the timeline
     * @return  frame number
     */
    int64_t endFrame() const noexcept;

    void hardDeleteTransition(const ClipPtr& c, const int32_t type);

    bool setWidth(const int32_t val);
    int32_t width() const;
    bool setHeight(const int32_t val);
    int32_t height() const;
    double frameRate() const noexcept;
    bool setFrameRate(const double frameRate);
    int32_t audioFrequency() const;
    bool setAudioFrequency(const int32_t frequency);

    /**
     * @brief audioLayout from ffmpeg libresample
     * @return AV_CH_LAYOUT_*
     */
    int32_t audioLayout() const noexcept;
    /**
     * @brief setAudioLayout using ffmpeg libresample
     * @param layout AV_CH_LAYOUT_* value from libavutil/channel_layout.h
     */
    void setAudioLayout(const int32_t layout) noexcept;

    /**
     * @brief         Obtain the track count in the sequence. This includes empty tracks e.g. between populated tracks
     * @param video   true==video tracks, false==audio tracks
     * @return        track count
     */
    int trackCount(const bool video) const;
    /**
     * @brief               Identify if the track is enabled (for viewing and rendering)
     * @param track_number
     * @return              true==track is enabled
     */
    bool trackEnabled(const int track_number) const;
    /**
     * @brief               Identify if the track is locked editing in the timeline
     * @param track_number
     * @return              true==track is locked
     */
    bool trackLocked(const int track_number) const;

    /**
     * @brief         Obtain all clips at a position in the sequence
     * @param frame   Position
     * @return        list of clips
     */
    QVector<ClipPtr> clips(const long frame) const;

    /**
     * @brief           Add a clip to the sequence if not already present
     * @param new_clip  Clip instance
     * @return          true==clip added successfully
     */
    bool addClip(const ClipPtr& new_clip);

    /**
     * @brief   Obtain a list of all the clips in the sequence
     * @return  A list
     */
    QVector<ClipPtr> clips();

    void closeActiveClips(const int32_t depth=0);
    /**
     * @brief     Obtain a clip from this sequence
     * @param id  Clip id
     * @return    Clip instance or null
     */
    ClipPtr clip(const int32_t id);
    /**
     * @brief     Remove a clip from this sequence
     * @param id  Clip id
     */
    void deleteClip(const int32_t id);

    virtual bool load(QXmlStreamReader& stream) override;
    virtual bool save(QXmlStreamWriter& stream) const override;

    /**
     * @brief         Lock a track to prevent editing (via the timeline) of contained clips until unlocked
     * @param number  The track number
     * @param lock    true==lock
     */
    void lockTrack(const int number, const bool lock);
    /**
     * @brief         Enable a track (for the timeline and sequence viewer)
     * @param number  The track number
     */
    void enableTrack(const int number);
    /**
     * @brief         Disable a track (for the timeline and sequence viewer)
     * @param number  The track number
     */
    void disableTrack(const int number);
    /**
     * @brief         Add a track to the sequence
     * @param number  Track index
     * @param video   true==video track false==audio track
     */
    void addTrack(const int number, const bool video);
    /**
     * @brief As clips are moved amongst tracks, ensure correct Track instances are available
     */
    void verifyTrackCount();
    /**
     * @brief       Add a selection to the sequence taking into account track locks
     * @param sel   The selection
     */
    void addSelection(const Selection& sel);

    /**
     * @brief   Obtain the active length of the sequence, i.e. between in and out points
     * @return  length in frames
     */
    int64_t activeLength() const noexcept;

    QVector<project::Track> audioTracks();
    QVector<project::Track> videoTracks();

private:
    friend class SequenceTest;
    int32_t width_ = -1;
    int32_t height_ = -1;
    double frame_rate_ = -0.0;
    int32_t audio_frequency_ = -1;
    int32_t audio_layout_ = -1;
    QMap<int, project::Track> tracks_;
    QVector<ClipPtr> clips_;

    bool loadWorkArea(QXmlStreamReader& stream);

    QVector<project::Track> tracks(const bool video);
};

namespace global {
    // static variable for the currently active sequence
    extern SequencePtr sequence;
}

#endif // SEQUENCE_H
