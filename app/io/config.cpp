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
#include "config.h"

#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "panels/panelmanager.h"

#include "debug.h"

Config global::config;

void Config::load(QString path)
{
  QFile f(std::move(path));
  if (!f.exists()) {
    qWarning() << "Config file was not found, fileName =" << f.fileName();
    return;
  }
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning() << "Failed to open config file read only, fileName =" << f.fileName();
    return;
  }
  QXmlStreamReader stream(&f);

  while (!stream.atEnd()) {
    stream.readNext();
    if (!stream.isStartElement()) {
      continue;
    }
    if (stream.name() == "SavedLayout") {
      stream.readNext();
      saved_layout = (stream.text() == "1");
    } else if (stream.name() == "ShowTrackLines") {
      stream.readNext();
      show_track_lines = (stream.text() == "1");
    } else if (stream.name() == "ScrollZooms") {
      stream.readNext();
      scroll_zooms = (stream.text() == "1");
    } else if (stream.name() == "EditToolSelectsLinks") {
      stream.readNext();
      edit_tool_selects_links = (stream.text() == "1");
    } else if (stream.name() == "EditToolAlsoSeeks") {
      stream.readNext();
      edit_tool_also_seeks = (stream.text() == "1");
    } else if (stream.name() == "SelectAlsoSeeks") {
      stream.readNext();
      select_also_seeks = (stream.text() == "1");
    } else if (stream.name() == "PasteSeeks") {
      stream.readNext();
      paste_seeks = (stream.text() == "1");
    } else if (stream.name() == "RectifiedWaveforms") {
      stream.readNext();
      rectified_waveforms = (stream.text() == "1");
    } else if (stream.name() == "DefaultTransitionLength") {
      stream.readNext();
      default_transition_length = stream.text().toInt();
    } else if (stream.name() == "TimecodeView") {
      stream.readNext();
      timecode_view = stream.text().toInt();
    } else if (stream.name() == "ShowTitleSafeArea") {
      stream.readNext();
      show_title_safe_area = (stream.text() == "1");
    } else if (stream.name() == "UseCustomTitleSafeRatio") {
      stream.readNext();
      use_custom_title_safe_ratio = (stream.text() == "1");
    } else if (stream.name() == "CustomTitleSafeRatio") {
      stream.readNext();
      custom_title_safe_ratio = stream.text().toDouble();
    } else if (stream.name() == "EnableDragFilesToTimeline") {
      stream.readNext();
      enable_drag_files_to_timeline = (stream.text() == "1");
    } else if (stream.name() == "AutoscaleByDefault") {
      stream.readNext();
      autoscale_by_default = (stream.text() == "1");
    } else if (stream.name() == "RecordingMode") {
      stream.readNext();
      recording_mode = stream.text().toInt();
    } else if (stream.name() == "EnableSeekToImport") {
      stream.readNext();
      enable_seek_to_import = (stream.text() == "1");
    } else if (stream.name() == "AudioScrubbing") {
      stream.readNext();
      enable_audio_scrubbing = (stream.text() == "1");
    } else if (stream.name() == "DropFileOnMediaToReplace") {
      stream.readNext();
      drop_on_media_to_replace = (stream.text() == "1");
    } else if (stream.name() == "Autoscroll") {
      stream.readNext();
      autoscroll = stream.text().toInt();
    } else if (stream.name() == "AudioRate") {
      stream.readNext();
      audio_rate = stream.text().toInt();
    } else if (stream.name() == "FastSeeking") {
      stream.readNext();
      fast_seeking = (stream.text() == "1");
    } else if (stream.name() == "HoverFocus") {
      stream.readNext();
      hover_focus = (stream.text() == "1");
    } else if (stream.name() == "ProjectViewType") {
      stream.readNext();
      project_view_type = static_cast<ProjectView>(stream.text().toInt());
    } else if (stream.name() == "SetNameWithMarker") {
      stream.readNext();
      set_name_with_marker = (stream.text() == "1");
    } else if (stream.name() == "ShowProjectToolbar") {
      stream.readNext();
      show_project_toolbar = (stream.text() == "1");
    } else if (stream.name() == "DisableMultithreadedImages") {
      stream.readNext();
      disable_multithreading_for_images = (stream.text() == "1");
    } else if (stream.name() == "PreviousFrameQueueSize") {
      stream.readNext();
      previous_queue_size = stream.text().toDouble();
    } else if (stream.name() == "PreviousFrameQueueType") {
      stream.readNext();
      previous_queue_type = stream.text().toInt();
    } else if (stream.name() == "UpcomingFrameQueueSize") {
      stream.readNext();
      upcoming_queue_size = stream.text().toDouble();
    } else if (stream.name() == "UpcomingFrameQueueType") {
      stream.readNext();
      upcoming_queue_type = stream.text().toInt();
    } else if (stream.name() == "Loop") {
      stream.readNext();
      loop = (stream.text() == "1");
    } else if (stream.name() == "PauseAtOutPoint") {
      stream.readNext();
      pause_at_out_point = (stream.text() == "1");
    } else if (stream.name() == "SeekAlsoSelects") {
      stream.readNext();
      seek_also_selects = (stream.text() == "1");
    } else if (stream.name() == "CSSPath") {
      stream.readNext();
      css_path = stream.text().toString();
    } else if (stream.name() == "EffectTextboxLines") {
      stream.readNext();
      effect_textbox_lines = stream.text().toInt();
    }
  }

  if (stream.hasError()) {
    qCritical() << "Error parsing config XML." << stream.errorString();
  }

  f.close();
}

void Config::save(QString path)
{
  QFile f(std::move(path));
  if (!f.open(QIODevice::WriteOnly)) {
    qCritical() << "Could not open file to save configuration, fileName =" << f.fileName();
    return;
  }

  QXmlStreamWriter stream(&f);
  stream.setAutoFormatting(true);
  stream.writeStartDocument(); // doc
  stream.writeStartElement("Configuration"); // configuration

  stream.writeTextElement("Version", QString::number(SAVE_VERSION));
  stream.writeTextElement("SavedLayout", QString::number(saved_layout));
  stream.writeTextElement("ShowTrackLines", QString::number(show_track_lines));
  stream.writeTextElement("ScrollZooms", QString::number(scroll_zooms));
  stream.writeTextElement("EditToolSelectsLinks", QString::number(edit_tool_selects_links));
  stream.writeTextElement("EditToolAlsoSeeks", QString::number(edit_tool_also_seeks));
  stream.writeTextElement("SelectAlsoSeeks", QString::number(select_also_seeks));
  stream.writeTextElement("PasteSeeks", QString::number(paste_seeks));
  stream.writeTextElement("RectifiedWaveforms", QString::number(rectified_waveforms));
  stream.writeTextElement("DefaultTransitionLength", QString::number(default_transition_length));
  stream.writeTextElement("TimecodeView", QString::number(timecode_view));
  stream.writeTextElement("ShowTitleSafeArea", QString::number(show_title_safe_area));
  stream.writeTextElement("UseCustomTitleSafeRatio", QString::number(use_custom_title_safe_ratio));
  stream.writeTextElement("CustomTitleSafeRatio", QString::number(custom_title_safe_ratio));
  stream.writeTextElement("EnableDragFilesToTimeline", QString::number(enable_drag_files_to_timeline));
  stream.writeTextElement("AutoscaleByDefault", QString::number(autoscale_by_default));
  stream.writeTextElement("RecordingMode", QString::number(recording_mode));
  stream.writeTextElement("EnableSeekToImport", QString::number(enable_seek_to_import));
  stream.writeTextElement("AudioScrubbing", QString::number(enable_audio_scrubbing));
  stream.writeTextElement("DropFileOnMediaToReplace", QString::number(drop_on_media_to_replace));
  stream.writeTextElement("Autoscroll", QString::number(autoscroll));
  stream.writeTextElement("AudioRate", QString::number(audio_rate));
  stream.writeTextElement("FastSeeking", QString::number(fast_seeking));
  stream.writeTextElement("HoverFocus", QString::number(hover_focus));
  stream.writeTextElement("ProjectViewType", QString::number(static_cast<int>(project_view_type)));
  stream.writeTextElement("SetNameWithMarker", QString::number(set_name_with_marker));
  stream.writeTextElement("ShowProjectToolbar", QString::number(panels::PanelManager::projectViewer().toolbar_widget_->isVisible()));
  stream.writeTextElement("DisableMultithreadedImages", QString::number(disable_multithreading_for_images));
  stream.writeTextElement("PreviousFrameQueueSize", QString::number(previous_queue_size));
  stream.writeTextElement("PreviousFrameQueueType", QString::number(previous_queue_type));
  stream.writeTextElement("UpcomingFrameQueueSize", QString::number(upcoming_queue_size));
  stream.writeTextElement("UpcomingFrameQueueType", QString::number(upcoming_queue_type));
  stream.writeTextElement("Loop", QString::number(loop));
  stream.writeTextElement("PauseAtOutPoint", QString::number(pause_at_out_point));
  stream.writeTextElement("SeekAlsoSelects", QString::number(seek_also_selects));
  stream.writeTextElement("CSSPath", css_path);
  stream.writeTextElement("EffectTextboxLines", QString::number(effect_textbox_lines));

  stream.writeEndElement(); // configuration
  stream.writeEndDocument(); // doc
  f.close();
}
