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
#ifndef VIEWER_H
#define VIEWER_H

#include <QDockWidget>
#include <QTimer>
#include <QIcon>
#include <QPushButton>
#include <QLabel>


#include "project/sequence.h"
#include "project/media.h"
#include "ui/markerdockwidget.h"

class Timeline;
class ViewerWidget;

class TimelineHeader;
class ResizableScrollBar;
class ViewerContainer;
class LabelSlider;

bool frame_rate_is_droppable(const double rate);
long timecode_to_frame(const QString& s, int view, double frame_rate);
QString frame_to_timecode(int64_t frame, const int view, const double frame_rate);

class Viewer : public QDockWidget, public ui::MarkerDockWidget
{
    Q_OBJECT

  public:
    bool playing {false};
    long playhead_start {-1};
    qint64 start_msecs {-1};
    QTimer playback_updater;
    bool just_played {false};
    long recording_start {-1};
    long recording_end {-1};
    int recording_track {-1};
    ViewerWidget* viewer_widget {nullptr};


    explicit Viewer(QWidget *parent = nullptr);

    bool is_focused() const;
    bool is_main_sequence() const noexcept;
    void set_main_sequence();
    void set_media(const MediaPtr& m);
    void reset();
    void compose();
    void set_playpause_icon(bool play);
    void update_playhead_timecode(long p);
    void update_end_timecode();
    void update_header_zoom();
    void update_viewer();
    void clear_in();
    void clear_out();
    void clear_inout_point();
    void toggle_enable_inout();
    void set_in_point();
    void set_out_point();
    void set_zoom(bool in);
    void set_panel_name(const QString& name);

    /**
     * @brief Force a render of the current frame displayed
     */
    void reRender();

    // playback functions
    void seek(const int64_t p);
    void play();
    void pause();

    void cue_recording(long start, long end, int track);
    void uncue_recording();
    bool is_recording_cued();

    void reset_all_audio();
    void update_parents(bool reload_fx = false);



    MediaPtr getMedia();
    SequencePtr getSequence();
    /**
     * Create a new marker of an object in the widget
     */
    virtual void setMarker() const override;

    /**
     * @brief         Enable/disable the use of viewed sequence
     * @param value   true==enable
     */
    void enableFXMute(const bool value);

    bool usingEffects() const;

  protected:
    void resizeEvent(QResizeEvent *event) override;

  public slots:
    void play_wake();
    void go_to_start();
    void go_to_in();
    void previous_frame();
    void previousFrameFast();
    void toggle_play();
    void next_frame();
    void nextFrameFast();
    void go_to_out();
    void go_to_end();
    void close_media();

  signals:
    void muteEffects(const bool value);

  private slots:
    void update_playhead();
    void timer_update();
    void recording_flasher_update();
    void resize_move(double d);
    void fxMute(const bool value);

  private:
    friend class ViewerTest;

    SequencePtr sequence_ {nullptr};
    MediaPtr media_ {nullptr};

    bool main_sequence {false};
    bool created_sequence {false};
    long cached_end_frame {0};
    QString panel_name;
    double minimum_zoom {1.0};

    QIcon playIcon;

    TimelineHeader* headers {nullptr};
    ResizableScrollBar* horizontal_bar {nullptr};
    ViewerContainer* viewer_container {nullptr};
    LabelSlider* currentTimecode {nullptr};
    QLabel* endTimecode {nullptr};

    QPushButton* btnSkipToStart {nullptr};
    QPushButton* btnRewind {nullptr};
    QPushButton* btnPlay {nullptr};
    QPushButton* btnFastForward {nullptr};
    QPushButton* btnSkipToEnd {nullptr};
    QPushButton* fx_mute_button_ {nullptr};

    bool cue_recording_internal {false};
    QTimer recording_flasher;

    int64_t previous_playhead {-1};

    std::atomic_bool fx_mute_ {false};

    void update_window_title();
    void clean_created_seq();
    void setSequence(const bool main, SequencePtr sequence_);
    void set_zoom_value(double d);
    void set_sb_max();

    int64_t get_seq_in() const;
    int64_t get_seq_out() const;

    void setup_ui();

    SequencePtr createFootageSequence(const MediaPtr& mda) const;
};

#endif // VIEWER_H
