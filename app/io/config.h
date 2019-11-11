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
#ifndef CONFIG_H
#define CONFIG_H

#include <QString>

constexpr int SAVE_VERSION = 190104; // YYMMDD
constexpr int MIN_SAVE_VERSION = 190104; // lowest compatible project version

constexpr int TIMECODE_DROP = 0;
constexpr int TIMECODE_NONDROP = 1;
constexpr int TIMECODE_FRAMES = 2;
constexpr int TIMECODE_MILLISECONDS = 3;

constexpr int RECORD_MODE_MONO = 1;
constexpr int RECORD_MODE_STEREO = 2;

constexpr int AUTOSCROLL_NO_SCROLL = 0;
constexpr int AUTOSCROLL_PAGE_SCROLL = 1;
constexpr int AUTOSCROLL_SMOOTH_SCROLL = 2;


constexpr int FRAME_QUEUE_TYPE_FRAMES = 0;
constexpr int FRAME_QUEUE_TYPE_SECONDS = 1;

enum class ProjectView {
  TREE = 0,
  ICON = 1
};

//FIXME; oh,wow. use QSettings?
struct Config {
    bool saved_layout {false};
    bool show_track_lines {true};
    bool scroll_zooms {false};
    bool edit_tool_selects_links {false};
    bool edit_tool_also_seeks {false};
    bool select_also_seeks {false};
    bool paste_seeks {true};
    QString img_seq_formats {"bmp|exr|gif|jp2|jpeg|jpg|png|psd|tga|tiff|tif"};
    bool rectified_waveforms {false};
    int default_transition_length {30};
    int timecode_view {TIMECODE_DROP};
    bool show_title_safe_area {false};
    bool use_custom_title_safe_ratio {false};
    double custom_title_safe_ratio {1};
    bool enable_drag_files_to_timeline {true};
    bool autoscale_by_default {false};
    int recording_mode {2};
    bool enable_seek_to_import {false};
    bool enable_audio_scrubbing {true};
    bool drop_on_media_to_replace {true};
    int autoscroll {AUTOSCROLL_PAGE_SCROLL};
    int audio_rate {48000};
    bool fast_seeking {false};
    bool hover_focus {false};
    ProjectView project_view_type {ProjectView::TREE};
    bool set_name_with_marker {true};
    bool show_project_toolbar {false};
    bool disable_multithreading_for_images {false};
    double previous_queue_size {3};
    int previous_queue_type {FRAME_QUEUE_TYPE_FRAMES};
    double upcoming_queue_size {0.5};
    int upcoming_queue_type {FRAME_QUEUE_TYPE_SECONDS};
    bool loop {false};
    bool pause_at_out_point {true};
    bool seek_also_selects {false};
    QString css_path;
    int effect_textbox_lines {3};

    void load(QString path);
    void save(QString path);
};

namespace global
{
  extern Config config;
}

#endif // CONFIG_H
