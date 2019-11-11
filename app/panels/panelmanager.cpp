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

#include "panelmanager.h"

#include <QCoreApplication>


#include "project/transition.h"
#include "io/config.h"

#include "debug.h"

using panels::PanelManager;
using panels::HistogramViewer;

QWidget* PanelManager::parent_ = nullptr;
HistogramViewer* PanelManager::histogram_viewer_ = nullptr;
panels::ScopeViewer* PanelManager::scope_viewer_ = nullptr;
GraphEditor* PanelManager::graph_editor_ = nullptr;
Timeline* PanelManager::timeline_ = nullptr;
Project* PanelManager::project_ = nullptr;
EffectControls* PanelManager::fx_controls_ = nullptr;
Viewer* PanelManager::sequence_viewer_ = nullptr;
Viewer* PanelManager::footage_viewer_ = nullptr;
MarkersViewer* PanelManager::markers_viewer_ = nullptr;

//FIXME:?
void scroll_to_frame_internal(QScrollBar* bar, const int64_t frame, const double zoom, const int area_width)
{
  const int screen_point = getScreenPointFromFrame(zoom, frame) - bar->value();
  const auto min_x = static_cast<int>(area_width * 0.1);
  const int max_x = area_width - min_x;
  if (screen_point < min_x) {
    bar->setValue(getScreenPointFromFrame(zoom, frame) - min_x);
  } else if (screen_point > max_x) {
    bar->setValue(getScreenPointFromFrame(zoom, frame) - max_x);
  }
}

void updateEffectsControls(EffectControls& controls, const SequencePtr& seq)
{
  // SEND CLIPS TO EFFECT CONTROLS
  // find out how many clips are selected
  // limits to one video clip and one audio clip and only if they're linked
  // one of these days it might be nice to have multiple clips in the effects panel
  bool multiple = false;
  int vclip = -1;
  int aclip = -1;
  QVector<int> selected_clips;
  int mode = TA_NO_TRANSITION;
  if (seq != nullptr) {
    for (const auto& seq_clip : seq->clips()) {
      if (seq_clip == nullptr) {
        continue;
      }
      for (const auto& seq_sel : seq->selections_) {
        bool add = true;
        if (selection_contains_transition(seq_sel, seq_clip, TA_OPENING_TRANSITION)) {
          mode = TA_OPENING_TRANSITION;
        } else if (selection_contains_transition(seq_sel, seq_clip, TA_CLOSING_TRANSITION)) {
          mode = TA_CLOSING_TRANSITION;          
        } else  if (seq_clip->isSelected(seq_sel)) {
          mode = TA_NO_TRANSITION;
        } else {
          add = false;
        }

        if (!add) {
          continue;
        }
        if ( (seq_clip->mediaType() == ClipType::VISUAL) && (vclip == -1)) {
          vclip = seq_clip->id();
        } else if ( (seq_clip->timeline_info.track_ >= 0) && (aclip == -1) ) {
          aclip = seq_clip->id();
        } else {
          // TODO: Significance of -2?
          vclip = -2;
          aclip = -2;
          multiple = true;
          break;
        }
      }//for
    }

    if (!multiple) {
      // check if aclip is linked to vclip
      if (vclip >= 0) {
        selected_clips.append(vclip);
      }
      if (aclip >= 0) {
        selected_clips.append(aclip);
      }

      if (vclip >= 0 && aclip >= 0) {
        bool found = false;
        ClipPtr vclip_ref = seq->clip(vclip);
        for (auto l_clp : vclip_ref->linkedClipIds()) {
          if (l_clp == aclip) {
            found = true;
            break;
          }
        }
        if (!found) {
          // only display multiple clips if they're linked
          selected_clips.clear();
          multiple = true;
        }
      }
    }
  }

  bool same = (selected_clips.size() == controls.selected_clips.size());
  if (same) {
    for (int i=0;i<selected_clips.size();i++) {
      if (selected_clips.at(i) != controls.selected_clips.at(i)) {
        same = false;
        break;
      }
    }
  }

  if (controls.multiple != multiple || !same) {
    controls.multiple = multiple;
  }
  controls.set_clips(selected_clips, mode);
}


void PanelManager::refreshPanels(const bool modified)
{
  if (modified) {
    updateEffectsControls(fxControls(), global::sequence);
  }
  fxControls().update_keyframes();
  timeLine().repaint_timeline();
  sequenceViewer().update_viewer();
  graphEditor().update_panel();
  markersViewer().refresh();
}

QDockWidget* PanelManager::getFocusedPanel()
{
  QDockWidget* w = nullptr;
  if (global::config.hover_focus) {
    if (projectViewer().underMouse()) {
      w = &projectViewer();
    } else if (fxControls().underMouse()) {
      w = &fxControls();
    } else if (sequenceViewer().underMouse()) {
      w = &sequenceViewer();
    } else if (footageViewer().underMouse()) {
      w = &footageViewer();
    } else if (timeLine().underMouse()) {
      w = &timeLine();
    } else if (graphEditor().view_is_under_mouse()) {
      w = &graphEditor();
    } else if (histogram().underMouse()) {
      w = &histogram();
    } else if (colorScope().underMouse()) {
      w = &colorScope();
    }
  }

  if (w == nullptr) {
    if (projectViewer().is_focused()) {
      w = &projectViewer();
    } else if (fxControls().keyframe_focus() || fxControls().is_focused()) {
      w = &fxControls();
    } else if (sequenceViewer().is_focused()) {
      w = &sequenceViewer();
    } else if (footageViewer().is_focused()) {
      w = &footageViewer();
    } else if (timeLine().focused()) {
      w = &timeLine();
    } else if (graphEditor().view_is_focused()) {
      w = &graphEditor();
    }
  }
  return w;
}

bool PanelManager::setParent(QWidget* parent)
{
  if (parent_ == nullptr) {
    parent_ = parent;
    return true;
  }
  qCritical() << "Parent object already set";
  return false;
}

HistogramViewer& PanelManager::histogram()
{
  if (histogram_viewer_ == nullptr) {
    histogram_viewer_ = new HistogramViewer(parent_);
    histogram_viewer_->setObjectName("histogram viewer");
  }
  return *histogram_viewer_;
}


panels::ScopeViewer& PanelManager::colorScope()
{
  if (scope_viewer_ == nullptr) {
    scope_viewer_ = new panels::ScopeViewer(parent_);
    scope_viewer_->setObjectName("scope viewer");
  }
  return *scope_viewer_;
}


GraphEditor& PanelManager::graphEditor()
{
  if (graph_editor_ == nullptr) {
    graph_editor_ = new GraphEditor(parent_);
    graph_editor_->setObjectName("graph editor");
  }
  return *graph_editor_;
}


Timeline& PanelManager::timeLine()
{
  if (timeline_ == nullptr) {
    timeline_ = new Timeline(parent_);
    timeline_->setObjectName("timeline");
  }
  return *timeline_;
}


EffectControls& PanelManager::fxControls()
{
  if (fx_controls_ == nullptr) {
    fx_controls_ = new EffectControls(parent_);
    fx_controls_->setObjectName("fx controls");
    init_effects(); // TODO: remove
  }
  return *fx_controls_;
}


Project& PanelManager::projectViewer()
{
  if (project_ == nullptr) {
    project_ = new Project(parent_);
    project_->setObjectName("proj_root");
  }
  return *project_;
}


Viewer& PanelManager::sequenceViewer()
{
  if (sequence_viewer_ == nullptr) {
    sequence_viewer_ = new Viewer(parent_);
    sequence_viewer_->setObjectName("seq_viewer");
    sequence_viewer_->set_panel_name(QCoreApplication::translate("Viewer", "Sequence Viewer"));
  }
  return *sequence_viewer_;
}

Viewer& PanelManager::footageViewer()
{
  if (footage_viewer_ == nullptr) {
    footage_viewer_ = new Viewer(parent_);
    footage_viewer_->setObjectName("media_viewer");
    footage_viewer_->set_panel_name(QCoreApplication::translate("Viewer", "Media Viewer"));
  }
  return *footage_viewer_;
}


MarkersViewer& PanelManager::markersViewer()
{
  if (markers_viewer_ == nullptr) {
    markers_viewer_ = new MarkersViewer(parent_);
  }
  return *markers_viewer_;
}

void PanelManager::tearDown()
{
  parent_ = nullptr;
  delete histogram_viewer_;
  histogram_viewer_ = nullptr;
  delete scope_viewer_;
  scope_viewer_ = nullptr;
  delete graph_editor_;
  graph_editor_ = nullptr;
  delete timeline_;
  timeline_ = nullptr;
  delete fx_controls_;
  fx_controls_ = nullptr;
  delete fx_controls_;
  fx_controls_ = nullptr;
  delete sequence_viewer_;
  sequence_viewer_ = nullptr;
  delete footage_viewer_;
  footage_viewer_ = nullptr;
}
