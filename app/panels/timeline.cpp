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
#include "timeline.h"

#include <QTime>
#include <QScrollBar>
#include <QtMath>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStatusBar>
#include <QtCore/qglobal.h>

#include "panels/panelmanager.h"
#include "ui/timelinewidget.h"
#include "project/sequence.h"
#include "project/clip.h"
#include "ui/viewerwidget.h"
#include "playback/audio.h"
#include "playback/playback.h"
#include "project/undo.h"
#include "project/media.h"
#include "io/config.h"
#include "project/effect.h"
#include "project/transition.h"
#include "project/footage.h"
#include "io/clipboard.h"
#include "ui/timelineheader.h"
#include "ui/resizablescrollbar.h"
#include "ui/audiomonitor.h"
#include "ui/mainwindow.h"
#include "ui/Forms/timelinetrackarea.h"
#include "debug.h"


constexpr auto STILL_IMAGE_FRAMES = 100;
constexpr int RECORD_MSG_TIMEOUT = 10000;

namespace
{
  const QColor AUDIO_ONLY_COLOR(128, 192, 128);
  const QColor VIDEO_ONLY_COLOR(192, 160, 128);
  const QColor AUDIO_VIDEO_COLOR(128, 128, 192);
  const QColor SEQUENCE_COLOR(192, 128, 128);
}

using panels::PanelManager;


Timeline::Timeline(QWidget *parent) :
  QDockWidget(parent),
  cursor_frame(0),
  cursor_track(0),
  zoom(1.0),
  zoom_just_changed(false),
  showing_all(false),
  snapping(true),
  snapped(false),
  snap_point(0),
  selecting(false),
  rect_select_init(false),
  rect_select_proc(false),
  moving_init(false),
  moving_proc(false),
  move_insert(false),
  trim_in_point(false),
  splitting(false),
  importing(false),
  importing_files(false),
  creating(false),
  transition_tool_init(false),
  transition_tool_proc(false),
  hand_moving(false),
  block_repaints(false),
  last_frame(0),
  scroll(0)
{
  qRegisterMetaType<SequencePtr>();

  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  setup_ui();

  default_track_height = qRound((QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96) * TRACK_DEFAULT_HEIGHT);

  headers->viewer_ = &PanelManager::sequenceViewer();

  video_area->bottom_align = true;
  video_area->scrollBar = videoScrollbar;
  audio_area->scrollBar = audioScrollbar;

  tool_buttons.append(toolArrowButton);
  tool_buttons.append(toolEditButton);
  tool_buttons.append(toolRippleButton);
  tool_buttons.append(toolRazorButton);
  tool_buttons.append(toolSlipButton);
  tool_buttons.append(toolSlideButton);
  tool_buttons.append(toolTransitionButton);
  tool_buttons.append(toolHandButton);

  toolArrowButton->click();

  connect(horizontalScrollBar, SIGNAL(valueChanged(int)), this, SLOT(setScroll(int)));
  connect(videoScrollbar, SIGNAL(valueChanged(int)), video_area, SLOT(setScroll(int)));
  connect(audioScrollbar, SIGNAL(valueChanged(int)), audio_area, SLOT(setScroll(int)));
  connect(horizontalScrollBar, SIGNAL(resize_move(double)), this, SLOT(resize_move(double)));

  update_sequence();
}


bool Timeline::setSequence(const SequencePtr& seq)
{
  if (seq != sequence_) {
    sequence_ = seq;
    emit newSequenceLoaded(sequence_);
    return true;
  }
  return false;
}


void Timeline::previous_cut()
{
  Q_ASSERT(sequence_ != nullptr);

  if (sequence_->playhead_ <= 0) {
    qWarning() << "Playhead position invalid" << sequence_->playhead_;
    return;
  }

  long p_cut = 0;
  for (const auto& seq_clip : sequence_->clips()) {
    if (seq_clip == nullptr) {
      qCritical() << "Clip instance is null";
      continue;
    }
    if ( (seq_clip->timeline_info.out > p_cut) && (seq_clip->timeline_info.out < sequence_->playhead_) ) {
      p_cut = seq_clip->timeline_info.out;
    } else if ( (seq_clip->timeline_info.in > p_cut) && (seq_clip->timeline_info.in < sequence_->playhead_) ) {
      p_cut = seq_clip->timeline_info.in;
    } else {
      // TODO: ?
    }
  }
  PanelManager::sequenceViewer().seek(p_cut);
}

void Timeline::next_cut()
{
  Q_ASSERT(sequence_ != nullptr);

  bool seek_enabled = false;
  long n_cut = LONG_MAX;
  for (const auto& seq_clip : sequence_->clips()) {
    if (seq_clip == nullptr) {
      qCritical() << "Clip instance is null";
      continue;
    }
    if ( (seq_clip->timeline_info.in < n_cut) && (seq_clip->timeline_info.in > sequence_->playhead_) ) {
      n_cut = seq_clip->timeline_info.in;
      seek_enabled = true;
    } else if ( (seq_clip->timeline_info.out < n_cut) && (seq_clip->timeline_info.out > sequence_->playhead_) ) {
      n_cut = seq_clip->timeline_info.out;
      seek_enabled = true;
    }
  }
  if (seek_enabled) {
    PanelManager::sequenceViewer().seek(n_cut);
  }
}

/**
 * @brief Nudge selected clips by a specified amount + direction.
 * @param pos
 */
void Timeline::nudgeClips(const int pos)
{  
  Q_ASSERT(sequence_ != nullptr);

  auto ca = new ComboAction();
  for (const auto& clp : selectedClips()) {
    if (clp != nullptr) {
      ca->append(new NudgeClipCommand(clp, pos));
    }
  }

  for (auto& sel : sequence_->selections_) {
    ca->append(new NudgeSelectionCommand(sel, pos));
  }

  if (ca->size() > 0) {
    e_undo_stack.push(ca);
  } else {
    delete ca;
  }

  repaint_timeline();
  PanelManager::sequenceViewer().update_viewer();
}

void ripple_clips(ComboAction* ca, SequencePtr s, long point, long length, const QVector<int>& ignore)
{
  ca->append(new RippleAction(s, point, length, ignore));
}

void Timeline::toggle_show_all()
{
  if (global::sequence == nullptr) {
    qWarning() << "Sequence instance is null";
    return;
  }

  showing_all = !showing_all;
  if (showing_all) {
    old_zoom = zoom;
    set_zoom_value(double(timeline_area->width() - 200) / double(sequence_->endFrame()));
  } else {
    set_zoom_value(old_zoom);
  }
}

void Timeline::createGhostsFromMedia(SequencePtr &seq, const long entry_point, QVector<MediaPtr>& media_list)
{
  video_ghosts = false;
  audio_ghosts = false;
  auto entry = entry_point;

  for (auto mda : media_list) {
    if (!mda) {
      qWarning() << "Null Media ptr";
      continue;
    }
    bool can_import = true;
    FootagePtr ftg;
    SequencePtr lcl_seq;
    auto sequence_length = 0L;
    auto default_clip_in = 0L;
    auto default_clip_out = 0L;

    switch (mda->type())
    {
      case MediaType::FOOTAGE:
      {
        ftg = mda->object<Footage>();
        if (ftg == nullptr) {
          break;
        }
        can_import = ftg->ready_;
        if (!ftg->using_inout) {
          break;
        }
        auto source_fr = 30.0;
        if ( (!ftg->videoTracks().empty()) && !qIsNull(ftg->videoTracks().front()->video_frame_rate)) {
          source_fr = ftg->videoTracks().front()->video_frame_rate * ftg->speed_;
        }
        default_clip_in = refactor_frame_number(ftg->in, source_fr, seq->frameRate());
        default_clip_out = refactor_frame_number(ftg->out, source_fr, seq->frameRate());
      }
        break;
      case MediaType::SEQUENCE:
        lcl_seq = mda->object<Sequence>();
        sequence_length = lcl_seq->endFrame();
        if (lcl_seq != nullptr) {
          sequence_length = refactor_frame_number(sequence_length, lcl_seq->frameRate(), seq->frameRate());
        }
        can_import = ( (lcl_seq != seq) && (sequence_length != 0) );
        if (!lcl_seq->workarea_.using_) {
          break;
        }
        default_clip_in = refactor_frame_number(lcl_seq->workarea_.in_, lcl_seq->frameRate(), seq->frameRate());
        default_clip_out = refactor_frame_number(lcl_seq->workarea_.out_, lcl_seq->frameRate(), seq->frameRate());
        break;
      default:
        can_import = false;
        break;
    }//switch

    if (!can_import) {
      continue;
    }

    Ghost g;
    g.clip_.reset();
    g.trimming = false;
    g.old_clip_in = g.clip_in = default_clip_in;
    g.media = mda;
    g.in = entry;
    g.transition.reset();

    switch (mda->type()) {
      case MediaType::FOOTAGE:
        if (ftg->isImage()) {
          g.out = g.in + STILL_IMAGE_FRAMES; //TODO: shouldn't this be calculated to a duration?
        } else {
          long length = ftg->totalLengthInFrames(seq->frameRate());
          g.out = entry + length - default_clip_in;
          if (ftg->using_inout) {
            g.out -= (length - default_clip_out);
          }
        }

        for (int j = 0; j < ftg->audioTracks().size(); ++j) {
          Q_ASSERT(ftg->audioTracks().at(j));
          if (!ftg->audioTracks().at(j)->enabled) {
            continue;
          }
          g.track = j;
          g.media_stream = ftg->audioTracks().at(j)->file_index;
          ghosts.append(g);
          audio_ghosts = true;
        }

        for (int j = 0; j < ftg->videoTracks().size(); ++j) {
          Q_ASSERT(ftg->videoTracks().at(j));
          if (!ftg->videoTracks().at(j)->enabled) {
            continue;
          }
          g.track = -1 - j;
          qDebug() << "Video Track" << g.track;
          g.media_stream = ftg->videoTracks().at(j)->file_index;
          ghosts.append(g);
          video_ghosts = true;
        }
        break;
      case MediaType::SEQUENCE:
        g.out = entry + sequence_length - default_clip_in;

        if (lcl_seq->workarea_.using_ && lcl_seq->workarea_.enabled_) {
          g.out -= (sequence_length - default_clip_out);
        }

        g.track = -1;
        ghosts.append(g);
        g.track = 0;
        ghosts.append(g);

        video_ghosts = true;
        audio_ghosts = true;
        break;
      default:
        qWarning() << "Unhandled Media Type" << static_cast<int>(mda->type());
        break;
    }//switch

    entry = g.out;
  } //for

  for (auto& g : ghosts) {
    g.old_in = g.in;
    g.old_out = g.out;
    g.old_track = g.track;
  }
}

void Timeline::addClipsFromGhosts(ComboAction* ca, const SequencePtr& seq)
{
  Q_ASSERT(seq != nullptr);
  // add clips
  long earliest_point = LONG_MAX;
  QVector<ClipPtr> added_clips;
  for (const auto& g : ghosts) {
    if (seq->trackLocked(g.track)) {
      qInfo() << "Not adding clip to locked track, track =" << g.track;
      continue;
    }
    earliest_point = qMin(earliest_point, g.in);

    auto clp = std::make_shared<Clip>(seq);
    clp->timeline_info.media = g.media;
    clp->timeline_info.media_stream = g.media_stream;
    clp->timeline_info.in = g.in;
    clp->timeline_info.out = g.out;
    clp->timeline_info.clip_in = g.clip_in;
    clp->timeline_info.track_ = g.track;
    if (clp->timeline_info.media->type() == MediaType::FOOTAGE) {
      auto ftg = clp->timeline_info.media->object<Footage>();
      if (ftg->videoTracks().empty()) {
        // audio only (greenish)
        clp->timeline_info.color = AUDIO_ONLY_COLOR;
      } else if (ftg->audioTracks().empty()) {
        // video only (orangeish)
        clp->timeline_info.color = VIDEO_ONLY_COLOR;
      } else {
        // video and audio (blueish)
        clp->timeline_info.color = AUDIO_VIDEO_COLOR;
      }
      clp->timeline_info.name_ = ftg->name();
    } else if (clp->timeline_info.media->type() == MediaType::SEQUENCE) {
      // sequence (red?ish?)
      clp->timeline_info.color = SEQUENCE_COLOR;

      SequencePtr media = clp->timeline_info.media->object<Sequence>();
      clp->timeline_info.name_ = media->name();
    }
    clp->recalculateMaxLength();
    added_clips.append(clp);
  }
  ca->append(new AddClipsCommand(seq, added_clips));

  // link clips from the same media
  for (const auto& c : added_clips) {
    if (c == nullptr) {
      qWarning() << "Clip instance is null";
      continue;
    }
    for (const auto& cc : added_clips) {
      if ( (cc != nullptr)
          && (c != cc)
          && (c->timeline_info.media == cc->timeline_info.media) ) {
        c->addLinkedClip(*cc);
      }
    }

    if (c->mediaType() == ClipType::VISUAL) {
      // add default video effects
      c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT)));
    } else {
      // add default audio effects
      c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_VOLUME, EFFECT_TYPE_EFFECT)));
      c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_PAN, EFFECT_TYPE_EFFECT)));
    }
  }
  if (e_config.enable_seek_to_import) {
    PanelManager::sequenceViewer().seek(earliest_point);
  }
  PanelManager::timeLine().ghosts.clear();
  PanelManager::timeLine().importing = false;
  PanelManager::timeLine().snapped = false;
}

int Timeline::get_track_height_size(bool video) {
  if (video) {
    return video_track_heights.size();
  }
  return audio_track_heights.size();
}

void Timeline::add_transition()
{
  Q_ASSERT(sequence_ != nullptr);

  auto ca = new ComboAction();
  bool adding = false;

  const auto audio_meta = get_internal_meta(TRANSITION_INTERNAL_LINEARFADE, EFFECT_TYPE_TRANSITION);
  const auto video_meta = get_internal_meta(TRANSITION_INTERNAL_CROSSDISSOLVE, EFFECT_TYPE_TRANSITION);


  for (const auto& clp : sequence_->clips()) {
    if (clp != nullptr && clp->isSelected(true)){
      auto meta = clp->mediaType() == ClipType::AUDIO ? audio_meta : video_meta;
      ca->append(new AddTransitionCommand(clp, nullptr, meta, ClipTransitionType::BOTH, 30));
      adding = true;
    }
  }

  if (adding) {
    e_undo_stack.push(ca);
  } else {
    delete ca;
  }

  PanelManager::refreshPanels(true);
}

int Timeline::calculate_track_height(const int track, const int value)
{
  if (sequence_ == nullptr) {
    // nothing to do
    return -1;
  }
  //FIXME: every time the mouse moves close to the scroll area limit (horizontally) a new track is added
  //FIXME: this is forever called when the mouse is moved over the timeline area
  int index = (track < 0) ? qAbs(track + 1) : track;
  const bool video_track = track < 0;
  QVector<int>& heights = video_track ? video_track_heights : audio_track_heights;
  TimelineTrackArea* const area = video_track ? video_track_area_ : audio_track_area_;
  Q_ASSERT(area != nullptr);

  while ( heights.size() < index+1 ) {
    heights.append(default_track_height);
  }

  if (value > -1) {
    heights[index] = value;
  }

  area->setHeights(heights);

  return heights.at(index);
}

void Timeline::update_sequence() {
  bool null_sequence = (sequence_ == nullptr);

  for (auto btn : tool_buttons) {
    btn->setEnabled(!null_sequence);
  }
  snappingButton->setEnabled(!null_sequence);
  zoomInButton->setEnabled(!null_sequence);
  zoomOutButton->setEnabled(!null_sequence);
  recordButton->setEnabled(!null_sequence);
  addButton->setEnabled(!null_sequence);
  headers->setEnabled(!null_sequence);
  video_track_area_->reset();
  audio_track_area_->reset();
  video_track_heights.clear();
  audio_track_heights.clear();

  QString title = tr("Timeline: ");
  if (null_sequence) {
    setWindowTitle(title + tr("<none>"));
  } else {
    setWindowTitle(title + sequence_->name());
    PanelManager::refreshPanels(false);
  }
}

int Timeline::get_snap_range()
{
  return getFrameFromScreenPoint(zoom, 10);
}

bool Timeline::focused() {
  return (sequence_ != nullptr && (headers->hasFocus() || video_area->hasFocus() || audio_area->hasFocus()));
}

void Timeline::repaint_timeline()
{
  if (block_repaints) {
    return;
  }
  bool draw = true;

  if (sequence_ != nullptr
      && !horizontalScrollBar->isSliderDown()
      && !horizontalScrollBar->is_resizing()
      && PanelManager::sequenceViewer().playing
      && !zoom_just_changed) {
    // auto scroll
    if (e_config.autoscroll == AUTOSCROLL_PAGE_SCROLL) {
      const int playhead_x = PanelManager::timeLine().getTimelineScreenPointFromFrame(sequence_->playhead_);
      if (playhead_x < 0 || playhead_x > (editAreas->width() - videoScrollbar->width())) {
        horizontalScrollBar->setValue(getScreenPointFromFrame(zoom, sequence_->playhead_));
        draw = false;
      }
    } else if (e_config.autoscroll == AUTOSCROLL_SMOOTH_SCROLL) {
      if (center_scroll_to_playhead(horizontalScrollBar, zoom, sequence_->playhead_)) {
        draw = false;
      }
    }
  }

  zoom_just_changed = false;

  if (draw) {
    headers->update();
    video_area->update();
    audio_area->update();

    if (sequence_ != nullptr) {
      set_sb_max();

      if (last_frame != sequence_->playhead_) {
        audio_monitor->update();
        last_frame = sequence_->playhead_;
      }
    }
  }

  updateTrackAreas();
}

void Timeline::select_all()
{
  Q_ASSERT(sequence_ != nullptr);

  sequence_->selections_.clear();
  for (const auto& seq_clip : sequence_->clips()) {
    if (seq_clip != nullptr) {
      Selection s;
      s.in = seq_clip->timeline_info.in;
      s.out = seq_clip->timeline_info.out;
      s.track = seq_clip->timeline_info.track_;
      sequence_->addSelection(s);
    }
  }
  repaint_timeline();
}

void Timeline::scroll_to_frame(int64_t frame)
{
  scroll_to_frame_internal(horizontalScrollBar, frame, zoom, timeline_area->width());
}

void Timeline::select_from_playhead()
{
  Q_ASSERT(sequence_ != nullptr);

  sequence_->selections_.clear();
  for (int i=0;i<sequence_->clips().size();i++) {
    ClipPtr c = sequence_->clips().at(i);
    if (c != nullptr
        && c->timeline_info.in <= sequence_->playhead_
        && c->timeline_info.out > sequence_->playhead_) {
      Selection s;
      s.in = c->timeline_info.in;
      s.out = c->timeline_info.out;
      s.track = c->timeline_info.track_;
      sequence_->addSelection(s);
    }
  }
}

void Timeline::resizeEvent(QResizeEvent* /*event*/) {
  if (sequence_ != nullptr) {
    set_sb_max();
  }
}

void Timeline::delete_in_out(bool ripple) {
  if (sequence_ != nullptr && sequence_->workarea_.using_) {
    QVector<Selection> areas;
    auto[video_tracks, audio_tracks] = sequence_->trackLimits();
        for (auto i=video_tracks;i<=audio_tracks;i++) {
      Selection s;
      s.in = sequence_->workarea_.in_;
      s.out = sequence_->workarea_.out_;
      s.track = static_cast<int>(i);
      areas.append(s);
    }
    ComboAction* ca = new ComboAction();
    delete_areas(ca, areas);
    if (ripple) ripple_clips(ca, sequence_, sequence_->workarea_.in_, sequence_->workarea_.in_ - sequence_->workarea_.out_);
    ca->append(new SetTimelineInOutCommand(sequence_, false, 0, 0));
    e_undo_stack.push(ca);
    PanelManager::refreshPanels(true);
  }
}

void Timeline::delete_selection(QVector<Selection>& selections, bool ripple_delete)
{
  if (!selections.empty()) {
    PanelManager::fxControls().clear_effects(true);

    auto ca = new ComboAction();

    delete_areas(ca, selections);

    if (ripple_delete) {
      int64_t ripple_point = selections.at(0).in;
      int64_t ripple_length = selections.at(0).out - selections.at(0).in;

      // retrieve ripple_point and ripple_length from current selection
      for (int i=1;i<selections.size();i++) {
        const Selection& s = selections.at(i);
        ripple_point = qMin(ripple_point, s.in);
        ripple_length = qMin(ripple_length, s.out - s.in);
      }

      // it feels a little more intuitive with this here
      ripple_point++;

      bool can_ripple = true;
      for (const auto& seq_clip : sequence_->clips()) {
        if (seq_clip == nullptr) {
          continue;
        }
        if ( (seq_clip->timeline_info.in < ripple_point) && (seq_clip->timeline_info.out > ripple_point) ) {
          // conflict detected, but this clip may be getting deleted so let's check
          bool deleted = false;
          for (int j=0;j<selections.size();j++) {
            const Selection& s = selections.at(j);
            if (s.track == seq_clip->timeline_info.track_
                && !(seq_clip->timeline_info.in < s.in && seq_clip->timeline_info.out < s.in)
                && !(seq_clip->timeline_info.in > s.out && seq_clip->timeline_info.out > s.out)) {
              deleted = true;
              break;
            }
          }
          if (!deleted) {
            for (const auto& cc : sequence_->clips()) {
              if (cc == nullptr) {
                continue;
              }
              if (cc->timeline_info.track_ == seq_clip->timeline_info.track_
                  && cc->timeline_info.in > seq_clip->timeline_info.out
                  && cc->timeline_info.in < seq_clip->timeline_info.out + ripple_length) {
                ripple_length = cc->timeline_info.in - seq_clip->timeline_info.out;
              }
            }
          }
        }
      }//for

      if (can_ripple) {
        ripple_clips(ca, sequence_, ripple_point, -ripple_length);
        PanelManager::sequenceViewer().seek(ripple_point-1);
      }
    }

    selections.clear();

    e_undo_stack.push(ca);

    PanelManager::refreshPanels(true);
  }
}

void Timeline::set_zoom_value(double v) {
  zoom = v;
  zoom_just_changed = true;
  headers->update_zoom(zoom);

  repaint_timeline();

  // TODO find a way to gradually move towards target_scroll instead of just centering it?
  if (!horizontalScrollBar->is_resizing())
    center_scroll_to_playhead(horizontalScrollBar, zoom, sequence_->playhead_);
}

void Timeline::set_zoom(bool in) {
  showing_all = false;
  set_zoom_value(zoom * ((in) ? 2 : 0.5));
}

void Timeline::decheck_tool_buttons(QObject* sender) {
  for (int i=0;i<tool_buttons.count();i++) {
    tool_buttons[i]->setChecked(tool_buttons.at(i) == sender);
  }
}

QVector<int> Timeline::get_tracks_of_linked_clips(int i)
{
  Q_ASSERT(sequence_ != nullptr);

  QVector<int> tracks;
  if (ClipPtr clip = sequence_->clip(i)) {
    for (auto l : clip->linkedClipIds()) {
      if (auto l_clip = sequence_->clip(l)) {
        tracks.append(l_clip->timeline_info.track_);
      }
    }
  }
  return tracks;
}

void Timeline::zoom_in() {
  set_zoom(true);
}

void Timeline::zoom_out() {
  set_zoom(false);
}



void Timeline::snapping_clicked(bool checked) {
  snapping = checked;
}

void Timeline::clean_up_selections(QVector<Selection>& areas)
{
  for (int i=0;i<areas.size();i++) {
    Selection& s = areas[i];
    for (int j=0;j<areas.size();j++) {
      if (i == j) {
        continue;
      }
      Selection& ss = areas[j];
      if (s.track == ss.track) {
        bool remove = false;
        if (s.in < ss.in && s.out > ss.out) {
          // do nothing
        } else if (s.in >= ss.in && s.out <= ss.out) {
          remove = true;
        } else if (s.in <= ss.out && s.out > ss.out) {
          ss.out = s.out;
          remove = true;
        } else if (s.out >= ss.in && s.in < ss.in) {
          ss.in = s.in;
          remove = true;
        }
        if (remove) {
          areas.removeAt(i);
          i--;
          break;
        }
      }
    }
  }
}

bool selection_contains_transition(const Selection& s, ClipPtr c, int type) {

  if (c == nullptr) {
    qWarning() << "Clip instance is null";
    return false;
  }
  //FIXME: christ almighty
  if (type == TA_OPENING_TRANSITION) {
    auto opening = c->getTransition(ClipTransitionType::OPENING);
    return (opening != nullptr)
        && s.out == (c->timeline_info.in + opening->get_true_length())
        && (( (opening->secondaryClip() == nullptr) && (s.in == c->timeline_info.in) )
            || ( (opening->secondaryClip() != nullptr)
                && (s.in == (c->timeline_info.in - opening->get_true_length()))));
  }

  auto closing = c->getTransition(ClipTransitionType::CLOSING);
  return (closing != nullptr)
      && (s.in == (c->timeline_info.out - closing->get_true_length()))
      && ( ((closing->secondaryClip() == nullptr) && (s.out == c->timeline_info.out))
          || ( (closing->secondaryClip() != nullptr)
              && (s.out == (c->timeline_info.out + closing->get_true_length()))));
  }

void Timeline::delete_areas(ComboAction* ca, QVector<Selection>& areas)
{
  Q_ASSERT(sequence_ != nullptr);
  clean_up_selections(areas);

  QVector<ClipPtr> post_clips;

  for (const auto& sel : areas) {
      for (auto& clp : sequence_->clips()) {
      if ( (clp != nullptr) && (clp->timeline_info.track_ == sel.track) && clp->deleteable ) {
        if (selection_contains_transition(sel, clp, TA_OPENING_TRANSITION)) {
          // delete opening transition
          ca->append(new DeleteTransitionCommand(clp, ClipTransitionType::OPENING));
        } else if (selection_contains_transition(sel, clp, TA_CLOSING_TRANSITION)) {
          // delete closing transition
          ca->append(new DeleteTransitionCommand(clp, ClipTransitionType::CLOSING));
        } else if ( (clp->timeline_info.in >= sel.in) && (clp->timeline_info.out <= sel.out) ) {
          // clips falls entirely within deletion area
          ca->append(new DeleteClipAction(clp));
        } else if ( (clp->timeline_info.in < sel.in) && (clp->timeline_info.out > sel.out) ) {
          // middle of clip is within deletion area

          // FIXME: no method to undo this correctly
//          ca->append(new SplitClipCommand(c, sel.in));
//          ca->append(new SplitClipCommand(c, sel.out)); // the 2nd split should be performed on the result of the 1st

          ClipPtr post = split_clip(*ca, clp, sel.in);
          post_clips.append(post);
          post = split_clip(*ca, post, sel.out);
          post_clips.append(post);

        } else if ( (clp->timeline_info.in < sel.in) && (clp->timeline_info.out > sel.in) ) {
          // only out point is in deletion area
          clp->move(*ca, clp->timeline_info.in, sel.in, clp->timeline_info.clip_in, clp->timeline_info.track_);

          if (auto closing = clp->getTransition(ClipTransitionType::CLOSING)) {
            if (sel.in < (clp->timeline_info.out - closing->get_true_length())) {
              ca->append(new DeleteTransitionCommand(clp, ClipTransitionType::CLOSING));
            } else {
              ca->append(new ModifyTransitionCommand(clp,
                                                     ClipTransitionType::CLOSING,
                                                     closing->get_true_length() - (clp->timeline_info.out - sel.in)));
            }
          }
        } else if ( (clp->timeline_info.in < sel.out) && (clp->timeline_info.out > sel.out) ) {
          // only in point is in deletion area
          clp->move(*ca, sel.out, clp->timeline_info.out,
                    clp->timeline_info.clip_in + sel.out - clp->timeline_info.in,
                    clp->timeline_info.track_);

          if (auto opening = clp->getTransition(ClipTransitionType::OPENING)) {
            if (sel.out > (clp->timeline_info.in + opening->get_true_length())) {
              ca->append(new DeleteTransitionCommand(clp, ClipTransitionType::OPENING));
            } else {
              ca->append(new ModifyTransitionCommand(clp, ClipTransitionType::OPENING,
                                                     opening->get_true_length() - (sel.out - clp->timeline_info.in)));
            }
          }
        }
      }
    }//for
  }//for

  ca->append(new AddClipsCommand(sequence_, post_clips));
}

void Timeline::copy(bool del)
{
  Q_ASSERT(sequence_ != nullptr);

  bool cleared = false;
  bool copied = false;

  int64_t min_in = 0;


  for (const auto& c : sequence_->clips()) {
    if (c == nullptr) {
      qWarning() << "Clip instance is null";
      continue;
    }
    for (const Selection& s : sequence_->selections_) {
      if (!c->isSelected(s)) {
        // ignore
        continue;
      }

      if (!cleared) {
        clear_clipboard();
        cleared = true;
        e_clipboard_type = CLIPBOARD_TYPE_CLIP;
      }

      std::shared_ptr<Clip> copied_clip(c->copyPreserveLinks(nullptr));

      if (copied_clip->timeline_info.in < s.in) {
        copied_clip->timeline_info.clip_in += (s.in - copied_clip->timeline_info.in);
        copied_clip->timeline_info.in = s.in;
      }

      if (copied_clip->timeline_info.out > s.out) {
        copied_clip->timeline_info.out = s.out;
      }

      if (copied) {
        min_in = qMin(min_in, s.in);
      } else {
        min_in = s.in;
        copied = true;
      }


      e_clipboard.append(copied_clip);
    }//for
  }//for

  for (const auto& e_c : e_clipboard) {
    // initialize all timeline_ins to 0 or offsets of
    std::dynamic_pointer_cast<Clip>(e_c)->timeline_info.in -= min_in;
    std::dynamic_pointer_cast<Clip>(e_c)->timeline_info.out -= min_in;
  }

  if (del && copied) {
    delete_selection(sequence_->selections_, false);
  }
}

void Timeline::pasteClip(const QVector<project::SequenceItemPtr>& items, const bool insert, const SequencePtr& seq)
{
  auto ca = new ComboAction();

  // create copies and delete areas that we'll be pasting to
  QVector<Selection> del_areas;
  QVector<ClipPtr> pasted_clips;
  int64_t paste_start = LONG_MAX;
  int64_t paste_end = LONG_MIN;
  QMap<int, int> mapped_clips;

  for (int i=0; i<items.size(); i++) {
    ClipPtr c = std::dynamic_pointer_cast<Clip>(items.at(i));

    // create copy of clip and offset by playhead
    ClipPtr cc = c->copy(seq);
    mapped_clips[c->id()] = cc->id();
    cc->setLinkedClips(c->linkedClipIds());

    // convert frame rates
    cc->timeline_info.in = refactor_frame_number(cc->timeline_info.in, c->timeline_info.cached_fr,
                                                 seq->frameRate());
    cc->timeline_info.out = refactor_frame_number(cc->timeline_info.out, c->timeline_info.cached_fr,
                                                  seq->frameRate());
    cc->timeline_info.clip_in = refactor_frame_number(cc->timeline_info.clip_in, c->timeline_info.cached_fr,
                                                      seq->frameRate());

    cc->timeline_info.in += seq->playhead_;
    cc->timeline_info.out += seq->playhead_;
    cc->timeline_info.track_ = c->timeline_info.track_.load();

    paste_start = qMin(paste_start, cc->timeline_info.in.load());
    paste_end = qMax(paste_end, cc->timeline_info.out.load());

    pasted_clips.append(cc);

    if (!insert) {
      Selection s;
      s.in = cc->timeline_info.in;
      s.out = cc->timeline_info.out;
      s.track = c->timeline_info.track_;
      del_areas.append(s);
    }
  }//for

  // update clip linkage for the copies
  for (const auto& p_clip : pasted_clips) {
    if (p_clip == nullptr) {
      qWarning() << "Clip instance is null";
      continue;
    }
    p_clip->relink(mapped_clips);
  }

  if (insert) {
    // TODO: if clip is in the way, resize it ("split") and paste in place
    split_all_clips_at_point(ca, seq->playhead_);
    ripple_clips(ca, seq, paste_start, paste_end - paste_start);
  } else {
    delete_areas(ca, del_areas);
  }

  ca->append(new AddClipsCommand(seq, pasted_clips));

  e_undo_stack.push(ca);

  PanelManager::refreshPanels(true);

  if (e_config.paste_seeks) {
    PanelManager::sequenceViewer().seek(paste_end);
  }
}


ClipPtr Timeline::split_clip(ComboAction& ca, const ClipPtr& pre_clip, const long frame) const
{
  ClipPtr post_clip;

  if (pre_clip != nullptr) {
    post_clip = pre_clip->split(frame);
  }
  return post_clip;
}

void Timeline::paste(bool insert)
{
  if (e_clipboard.empty()) {
    return;
  }
  if (e_clipboard_type == CLIPBOARD_TYPE_CLIP) {
    pasteClip(e_clipboard, insert, sequence_);
  } else if (e_clipboard_type == CLIPBOARD_TYPE_EFFECT) {
    auto ca = new ComboAction();
    bool push = false;

    bool replace = false;
    bool skip = false;
    bool ask_conflict = true;

    for (int i=0;i<sequence_->clips().size();i++) {
      ClipPtr c = sequence_->clips().at(i);
      if (c != nullptr && c->isSelected(true)) {
        for (int j=0;j<e_clipboard.size();j++) {
          EffectPtr e = std::dynamic_pointer_cast<Effect>(e_clipboard.at(j));
          if ((c->mediaType() == ClipType::VISUAL) == (e->meta.subtype == EFFECT_TYPE_VIDEO)) {
            int found = -1;
            if (ask_conflict) {
              replace = false;
              skip = false;
            }
            for (int k=0;k<c->effects.size();k++) {
              if (c->effects.at(k)->meta == e->meta) {
                found = k;
                break;
              }
            }
            if (found >= 0 && ask_conflict) {
              QMessageBox box(this);
              box.setWindowTitle(tr("Effect already exists"));
              box.setText(tr("Clip '%1' already contains a '%2' effect. Would you like to replace it with the pasted one "
                             "or add it as a separate effect?").arg(c->name(), e->meta.name));
              box.setIcon(QMessageBox::Icon::Question);

              box.addButton(tr("Add"), QMessageBox::YesRole);
              QPushButton* replace_button = box.addButton(tr("Replace"), QMessageBox::NoRole);
              QPushButton* skip_button = box.addButton(tr("Skip"), QMessageBox::RejectRole);

              auto future_box = new QCheckBox(tr("Do this for all conflicts found"));
              box.setCheckBox(future_box);

              box.exec();

              if (box.clickedButton() == replace_button) {
                replace = true;
              } else if (box.clickedButton() == skip_button) {
                skip = true;
              }
              ask_conflict = !future_box->isChecked();
            }

            if (found >= 0 && skip) {
              // do nothing
            } else if (found >= 0 && replace) {
              auto delcom = new EffectDeleteCommand();
              delcom->clips.append(c);
              delcom->fx.append(found);
              ca->append(delcom);

              ca->append(new AddEffectCommand(c, e->copy(c), found));
              push = true;
            } else {
              ca->append(new AddEffectCommand(c, e->copy(c)));
              push = true;
            }
          }
        }
      }
    }
    if (push) {
      ca->appendPost(new ReloadEffectsCommand());
      e_undo_stack.push(ca);
    } else {
      delete ca;
    }
    PanelManager::refreshPanels(true);
  }
}

void Timeline::ripple_to_in_point(bool in, bool ripple) {
  if ( (sequence_ != nullptr) && !sequence_->clips().empty()) {
    // get track count
    int track_min = INT_MAX;
    int track_max = INT_MIN;
    int64_t sequence_end = 0;

    bool playhead_falls_on_in = false;
    bool playhead_falls_on_out = false;
    int64_t next_cut = LONG_MAX;
    int64_t prev_cut = 0;

    // find closest in point to playhead
    for (int i=0;i<sequence_->clips().size();i++) {
      ClipPtr c = sequence_->clips().at(i);
      if (c != nullptr) {
        track_min = qMin(track_min, c->timeline_info.track_.load());
        track_max = qMax(track_max, c->timeline_info.track_.load());

        sequence_end = qMax(c->timeline_info.out.load(), sequence_end);
        // TODO: check this. Looks repetitive at first glance
        if (c->timeline_info.in == sequence_->playhead_)
          playhead_falls_on_in = true;
        if (c->timeline_info.out == sequence_->playhead_)
          playhead_falls_on_out = true;
        if (c->timeline_info.in > sequence_->playhead_)
          next_cut = qMin(c->timeline_info.in.load(), next_cut);
        if (c->timeline_info.out > sequence_->playhead_)
          next_cut = qMin(c->timeline_info.out.load(), next_cut);
        if (c->timeline_info.in < sequence_->playhead_)
          prev_cut = qMax(c->timeline_info.in.load(), prev_cut);
        if (c->timeline_info.out < sequence_->playhead_)
          prev_cut = qMax(c->timeline_info.out.load(), prev_cut);
      }
    }

    next_cut = qMin(sequence_end, next_cut);

    QVector<Selection> areas;
    ComboAction* ca = new ComboAction();
    bool push_undo = true;
    long seek = sequence_->playhead_;

    if ((in && (playhead_falls_on_out || (playhead_falls_on_in && sequence_->playhead_ == 0)))
        || (!in && (playhead_falls_on_in || (playhead_falls_on_out && sequence_->playhead_ == sequence_end)))) { // one frame mode
      if (ripple) {
        // set up deletion areas based on track count
        long in_point = sequence_->playhead_;
        if (!in) {
          in_point--;
          seek--;
        }

        if (in_point >= 0) {
          Selection s;
          s.in = in_point;
          s.out = in_point + 1;
          for (int i=track_min;i<=track_max;i++) {
            s.track = i;
            areas.append(s);
          }

          // trim and move clips around the in point
          delete_areas(ca, areas);

          if (ripple) ripple_clips(ca, sequence_, in_point, -1);
        } else {
          push_undo = false;
        }
      } else {
        push_undo = false;
      }
    } else {
      // set up deletion areas based on track count
      Selection s;
      if (in) {
        seek = prev_cut;
      }
      s.in = in ? prev_cut : sequence_->playhead_;
      s.out = in ? sequence_->playhead_ : next_cut;

      if (s.in == s.out) {
        push_undo = false;
      } else {
        for (int i=track_min;i<=track_max;i++) {
          s.track = i;
          areas.append(s);
        }

        // trim and move clips around the in point
        delete_areas(ca, areas);
        if (ripple) ripple_clips(ca, sequence_, s.in, s.in - s.out);
      }
    }

    if (push_undo) {
      e_undo_stack.push(ca);

      PanelManager::refreshPanels(true);

      if ( (seek != sequence_->playhead_) && ripple) {
        PanelManager::sequenceViewer().seek(seek);
      }
    } else {
      delete ca;
    }
  } else {
    PanelManager::sequenceViewer().seek(0);
  }
}

bool Timeline::split_all_clips_at_point(ComboAction* ca, const long point)
{
  Q_ASSERT(sequence_ != nullptr);
  // used on a paste-insert
  QVector<int> split_clips;
  QVector<ClipPtr> posts;
  bool split = false;
  for (const auto& clp : sequence_->clips()) {
    if (clp == nullptr) {
      qWarning() << "Clip instance is null";
      continue;
    }
    if (!split_clips.contains(clp->id()) && clp->inRange(point)) {
      ca->append(new SplitClipCommand(clp, point));
      // prevent agains multiple splits of a clip
      split_clips.append(clp->id());
      split_clips = split_clips + clp->linkedClipIds();
    }
  }

  return split;
}

void Timeline::split_at_playhead()
{
  Q_ASSERT(sequence_ != nullptr);

  // Get tracks at playhead
  QVector<ClipPtr> clips = sequence_->clips(sequence_->playhead_);
  QSet<int> tracks;
  QVector<ClipPtr> posts;

  // Split each clip at each track taking into account linked tracks/clips
  for (const auto& c : clips) {
    if (c == nullptr) {
      qWarning() << "Clip instance is null";
    }
    if (tracks.find(c->timeline_info.track_) == tracks.end()) {
      // ok to split
      // TODO: an undo command for the pre-clip size change
      auto splits = c->splitAll(sequence_->playhead_);
      // make sure clip and its linked aren't split again
      tracks.insert(c->timeline_info.track_);
      for (auto l : splits) {
        if (l == nullptr) {
          continue;
        }
        tracks.insert(l->timeline_info.track_);
      }
      posts = posts + splits;
    }//else ignore this clip. should be split already
  }//for

  if (!posts.empty()) {
    auto ca = new ComboAction();
    ca->append(new AddClipsCommand(sequence_, posts));
    e_undo_stack.push(ca);
    PanelManager::timeLine().repaint_timeline();
  }

}

void Timeline::unlinkClips()
{
  clipLinkage(false);
}
void Timeline::linkClips()
{
  clipLinkage(true);
}

void Timeline::deselect_area(long in, long out, int track)
{
  Q_ASSERT(sequence_ != nullptr);

  int len = sequence_->selections_.size();
  for (int i=0;i<len;i++) {
    Selection& s = sequence_->selections_[i];
    if (s.track == track) {
      if (s.in >= in && s.out <= out) {
        // whole selection is in deselect area
        sequence_->selections_.removeAt(i);
        i--;
        len--;
      } else if (s.in < in && s.out > out) {
        // middle of selection is in deselect area
        Selection new_sel;
        new_sel.in = out;
        new_sel.out = s.out;
        new_sel.track = s.track;
        sequence_->addSelection(new_sel);

        s.out = in;
      } else if (s.in < in && s.out > in) {
        // only out point is in deselect area
        s.out = in;
      } else if (s.in < out && s.out > out) {
        // only in point is in deselect area
        s.in = out;
      }
    }
  }
}

bool Timeline::snap_to_point(long point, long* l) {
  int limit = get_snap_range();
  if (*l > point-limit-1 && *l < point+limit+1) {
    snap_point = point;
    *l = point;
    snapped = true;
    return true;
  }
  return false;
}

bool Timeline::snap_to_timeline(long* l, bool use_playhead, bool use_markers, bool use_workarea)
{
  Q_ASSERT(sequence_ != nullptr);
  snapped = false;
  if (snapping) {
    if (use_playhead && !PanelManager::sequenceViewer().playing) {
      // snap to playhead
      if (snap_to_point(sequence_->playhead_, l)) return true;
    }

    // snap to marker
    if (use_markers) {
      for (const auto& mark : sequence_->markers_) {
        if (mark != nullptr && snap_to_point(mark->frame, l)) {
          return true;
        }
      }
    }

    // snap to in/out
    if (use_workarea && sequence_->workarea_.using_) {
      if (snap_to_point(sequence_->workarea_.in_, l)) {
        return true;
      }
      if (snap_to_point(sequence_->workarea_.out_, l)) {
        return true;
      }
    }

    // snap to clip/transition
    for (const auto& c : sequence_->clips()) {
      if (c == nullptr) {
        continue;
      }
      if (snap_to_point(c->timeline_info.in, l)) {
        return true;
      } else if (snap_to_point(c->timeline_info.out, l)) {
        return true;
      } else if (c->getTransition(ClipTransitionType::OPENING) != nullptr
                 && snap_to_point(c->timeline_info.in
                                  + c->getTransition(ClipTransitionType::OPENING)->get_true_length(), l)) {
        return true;
      } else if (c->getTransition(ClipTransitionType::CLOSING) != nullptr
                 && snap_to_point(c->timeline_info.out
                                  - c->getTransition(ClipTransitionType::CLOSING)->get_true_length(), l)) {
        return true;
      }
    }
  }
  return false;
}


void Timeline::setMarker() const
{
  Q_ASSERT(sequence_ != nullptr);
  bool add_marker = !e_config.set_name_with_marker;
  QString marker_name;

  if (!add_marker) {
    std::tie(marker_name, add_marker) = this->getName();
  }

  if (add_marker) {
    // TODO: create a thumbnail for this
    e_undo_stack.push(new AddMarkerAction(sequence_, sequence_->playhead_, marker_name));
  }
}

void Timeline::clipLinkage(const bool link)
{
  Q_ASSERT(sequence_ != nullptr);

  auto ca = new ComboAction();

  QVector<int32_t> links;
  if (link) {
    for (const auto& sel_clip : selectedClips()) {
      if (sel_clip == nullptr) {
        qWarning() << "Clip instance is null";
        continue;
      }
      links.append(sel_clip->id());
    }

    for (const auto& sel_clip : selectedClips()) {
      if (sel_clip == nullptr) {
        qWarning() << "Clip instance is null";
        continue;
      }
      links.append(sel_clip->id());
    }

  }

  for (const auto& sel_clip : selectedClips()) {
    if (sel_clip == nullptr) {
      qWarning() << "Clip instance is null";
      continue;
    }
    QVector<int32_t> link_ids = link ? links : QVector<int32_t>();
    auto cmd = new ClipLinkCommand(sel_clip, link_ids, link);
    ca->append(cmd);
  }

  if (ca->size() > 0) {
    e_undo_stack.push(ca);
    repaint_timeline();
  } else {
    delete ca;
  }
}


void Timeline::updateTrackAreas()
{
  Q_ASSERT(video_scroll_);
  Q_ASSERT(videoScrollbar);
  Q_ASSERT(audio_scroll_);
  Q_ASSERT(audioScrollbar);


  if (sequence_ == nullptr) {
    return;
  }
  sequence_->verifyTrackCount();
  // on clip movement within timeline area, tracks are not created/deleted
  video_track_area_->setTracks(sequence_->videoTracks());
  video_track_area_->setHeights(video_track_heights);
  audio_track_area_->setTracks(sequence_->audioTracks());
  audio_track_area_->setHeights(audio_track_heights);

  // Update the timelinearea widgets so they align with the tracks
  video_scroll_->setScroll(videoScrollbar->value());
  audio_scroll_->setScroll(audioScrollbar->value());
}


void Timeline::increase_track_height()
{
  changeTrackHeight(video_track_heights, TRACK_HEIGHT_INCREMENT);
  changeTrackHeight(audio_track_heights, TRACK_HEIGHT_INCREMENT);
  repaint_timeline();
}

void Timeline::decrease_track_height()
{
  changeTrackHeight(video_track_heights, -TRACK_HEIGHT_INCREMENT);
  changeTrackHeight(audio_track_heights, -TRACK_HEIGHT_INCREMENT);
  repaint_timeline();
}

void Timeline::deselect()
{
  Q_ASSERT(sequence_ != nullptr);

  sequence_->selections_.clear();
  repaint_timeline();
}

long getFrameFromScreenPoint(double zoom, int x)
{
  long f = qCeil(double(x) / zoom);
  if (f < 0) {
    return 0;
  }
  return f;
}

int getScreenPointFromFrame(double zoom, int64_t frame)
{
  return qFloor(double(frame)*zoom);
}

long Timeline::getTimelineFrameFromScreenPoint(int x)
{
  return getFrameFromScreenPoint(zoom, x + scroll);
}

int Timeline::getTimelineScreenPointFromFrame(int64_t frame)
{
  return getScreenPointFromFrame(zoom, frame) - scroll;
}

void Timeline::add_btn_click()
{
  QMenu add_menu(this);

  auto titleMenuItem = new QAction(&add_menu);
  titleMenuItem->setText(tr("Title..."));
  titleMenuItem->setData(static_cast<int>(AddObjectType::TITLE));
  add_menu.addAction(titleMenuItem);

  auto solidMenuItem = new QAction(&add_menu);
  solidMenuItem->setText(tr("Solid Color..."));
  solidMenuItem->setData(static_cast<int>(AddObjectType::SOLID));
  add_menu.addAction(solidMenuItem);

  auto barsMenuItem = new QAction(&add_menu);
  barsMenuItem->setText(tr("Bars..."));
  barsMenuItem->setData(static_cast<int>(AddObjectType::BARS));
  add_menu.addAction(barsMenuItem);

  add_menu.addSeparator();

  auto toneMenuItem = new QAction(&add_menu);
  toneMenuItem->setText(tr("Tone..."));
  toneMenuItem->setData(static_cast<int>(AddObjectType::TONE));
  add_menu.addAction(toneMenuItem);

  auto noiseMenuItem = new QAction(&add_menu);
  noiseMenuItem->setText(tr("Noise..."));
  noiseMenuItem->setData(static_cast<int>(AddObjectType::NOISE));
  add_menu.addAction(noiseMenuItem);

  connect(&add_menu, SIGNAL(triggered(QAction*)), this, SLOT(add_menu_item(QAction*)));

  add_menu.exec(QCursor::pos());
}

void Timeline::add_menu_item(QAction* action)
{
  creating = true;
  creating_object = static_cast<AddObjectType>(action->data().toInt());
}

void Timeline::setScroll(int s)
{
  scroll = s;
  headers->set_scroll(s);
  repaint_timeline();
}

void Timeline::record_btn_click()
{
  if (project_url.isEmpty()) {
    QMessageBox::critical(this,
                          tr("Unsaved Project"),
                          tr("You must save this project before you can record audio in it."),
                          QMessageBox::Ok);
  } else {
    creating = true;
    creating_object = AddObjectType::AUDIO;
    MainWindow::instance().statusBar()->showMessage(
          tr("Click on the timeline where you want to start recording (drag to limit the recording to a certain timeframe)"),
          RECORD_MSG_TIMEOUT);
  }
}

void Timeline::transition_tool_click()
{
  creating = false;

  QMenu transition_menu(this);

  for (const auto& named_meta : Effect::getRegisteredMetas().toStdMap()) {
    if ( (named_meta.second.type == EFFECT_TYPE_TRANSITION) && (named_meta.second.subtype == EFFECT_TYPE_VIDEO) ) {
      QAction* action = transition_menu.addAction(named_meta.second.name);
      action->setObjectName("v");
      action->setData(named_meta.first);
    }
  }

  transition_menu.addSeparator();

  for (const auto& named_meta : Effect::getRegisteredMetas().toStdMap()) {
    if ( (named_meta.second.type == EFFECT_TYPE_TRANSITION) && (named_meta.second.subtype == EFFECT_TYPE_AUDIO) ) {
      QAction* action = transition_menu.addAction(named_meta.second.name);
      action->setObjectName("a");
      action->setData(named_meta.first);
    }
  }

  connect(&transition_menu, SIGNAL(triggered(QAction*)), this, SLOT(transition_menu_select(QAction*)));

  toolTransitionButton->setChecked(false);

  transition_menu.exec(QCursor::pos());
}

void Timeline::transition_menu_select(QAction* a)
{
  transition_tool_meta = Effect::getRegisteredMeta(a->data().toString());

  if (a->objectName() == "v") {
    transition_tool_side = -1;
  } else {
    transition_tool_side = 1;
  }

  decheck_tool_buttons(sender());
  timeline_area->setCursor(Qt::CrossCursor);
  tool = TimelineToolType::TRANSITION;
  toolTransitionButton->setChecked(true);
}

void Timeline::resize_move(double z) {
  set_zoom_value(zoom * z);
}

void Timeline::set_sb_max() {
  headers->set_scrollbar_max(horizontalScrollBar, sequence_->endFrame(), editAreas->width() - getScreenPointFromFrame(zoom, 200));
}

QWidget* Timeline::createToolButtonsWidget(QWidget* parent)
{
  Q_ASSERT(parent != nullptr);

  auto tool_buttons_widget = new QWidget(parent);
  tool_buttons_widget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);

  auto tool_buttons_layout = new QVBoxLayout(tool_buttons_widget);
  tool_buttons_layout->setSpacing(4);
  tool_buttons_layout->setContentsMargins(0, 0, 0, 0);

  toolArrowButton = new QPushButton(tool_buttons_widget);
  QIcon arrow_icon;
  arrow_icon.addFile(QStringLiteral(":/icons/arrow.png"), QSize(), QIcon::Normal, QIcon::Off);
  arrow_icon.addFile(QStringLiteral(":/icons/arrow-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  toolArrowButton->setIcon(arrow_icon);
  toolArrowButton->setCheckable(true);
  toolArrowButton->setToolTip(tr("Pointer Tool") + " (V)");
  toolArrowButton->setProperty("tool", static_cast<int>(TimelineToolType::POINTER));
  connect(toolArrowButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolArrowButton);

  toolEditButton = new QPushButton(tool_buttons_widget);
  QIcon icon1;
  icon1.addFile(QStringLiteral(":/icons/beam.png"), QSize(), QIcon::Normal, QIcon::Off);
  icon1.addFile(QStringLiteral(":/icons/beam-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  toolEditButton->setIcon(icon1);
  toolEditButton->setCheckable(true);
  toolEditButton->setToolTip(tr("Edit Tool") + " (X)");
  toolEditButton->setProperty("tool", static_cast<int>(TimelineToolType::EDIT));
  connect(toolEditButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolEditButton);

  toolRippleButton = new QPushButton(tool_buttons_widget);
  QIcon icon2;
  icon2.addFile(QStringLiteral(":/icons/ripple.png"), QSize(), QIcon::Normal, QIcon::Off);
  icon2.addFile(QStringLiteral(":/icons/ripple-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  toolRippleButton->setIcon(icon2);
  toolRippleButton->setCheckable(true);
  toolRippleButton->setToolTip(tr("Ripple Tool") + " (B)");
  toolRippleButton->setProperty("tool", static_cast<int>(TimelineToolType::RIPPLE));
  connect(toolRippleButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolRippleButton);

  toolRazorButton = new QPushButton(tool_buttons_widget);
  QIcon icon4;
  icon4.addFile(QStringLiteral(":/icons/razor.png"), QSize(), QIcon::Normal, QIcon::Off);
  icon4.addFile(QStringLiteral(":/icons/razor-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  toolRazorButton->setIcon(icon4);
  toolRazorButton->setCheckable(true);
  toolRazorButton->setToolTip(tr("Razor Tool") + " (C)");
  toolRazorButton->setProperty("tool", static_cast<int>(TimelineToolType::RAZOR));
  connect(toolRazorButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolRazorButton);

  toolSlipButton = new QPushButton(tool_buttons_widget);
  QIcon icon5;
  icon5.addFile(QStringLiteral(":/icons/slip.png"), QSize(), QIcon::Normal, QIcon::On);
  icon5.addFile(QStringLiteral(":/icons/slip-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  toolSlipButton->setIcon(icon5);
  toolSlipButton->setCheckable(true);
  toolSlipButton->setToolTip(tr("Slip Tool") + " (Y)");
  toolSlipButton->setProperty("tool", static_cast<int>(TimelineToolType::SLIP));
  connect(toolSlipButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolSlipButton);

  toolSlideButton = new QPushButton(tool_buttons_widget);
  QIcon icon6;
  icon6.addFile(QStringLiteral(":/icons/slide.png"), QSize(), QIcon::Normal, QIcon::On);
  icon6.addFile(QStringLiteral(":/icons/slide-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  toolSlideButton->setIcon(icon6);
  toolSlideButton->setCheckable(true);
  toolSlideButton->setToolTip(tr("Slide Tool") + " (U)");
  toolSlideButton->setProperty("tool", static_cast<int>(TimelineToolType::SLIDE));
  connect(toolSlideButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolSlideButton);

  toolHandButton = new QPushButton(tool_buttons_widget);
  QIcon icon7;
  icon7.addFile(QStringLiteral(":/icons/hand.png"), QSize(), QIcon::Normal, QIcon::On);
  icon7.addFile(QStringLiteral(":/icons/hand-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  toolHandButton->setIcon(icon7);
  toolHandButton->setCheckable(true);
  toolHandButton->setToolTip(tr("Hand Tool") + " (H)");
  toolHandButton->setProperty("tool", static_cast<int>(TimelineToolType::HAND));
  connect(toolHandButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolHandButton);

  toolTransitionButton = new QPushButton(tool_buttons_widget);
  QIcon icon8;
  icon8.addFile(QStringLiteral(":/icons/transition-tool.png"), QSize(), QIcon::Normal, QIcon::On);
  icon8.addFile(QStringLiteral(":/icons/transition-tool-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  toolTransitionButton->setIcon(icon8);
  toolTransitionButton->setCheckable(true);
  toolTransitionButton->setToolTip(tr("Transition Tool") + " (T)");
  connect(toolTransitionButton, SIGNAL(clicked(bool)), this, SLOT(transition_tool_click()));
  tool_buttons_layout->addWidget(toolTransitionButton);

  snappingButton = new QPushButton(tool_buttons_widget);
  QIcon icon9;
  icon9.addFile(QStringLiteral(":/icons/magnet.png"), QSize(), QIcon::Normal, QIcon::On);
  icon9.addFile(QStringLiteral(":/icons/magnet-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  snappingButton->setIcon(icon9);
  snappingButton->setCheckable(true);
  snappingButton->setChecked(true);
  snappingButton->setToolTip(tr("Snapping") + " (S)");
  connect(snappingButton, SIGNAL(toggled(bool)), this, SLOT(snapping_clicked(bool)));
  tool_buttons_layout->addWidget(snappingButton);

  zoomInButton = new QPushButton(tool_buttons_widget);
  QIcon icon10;
  icon10.addFile(QStringLiteral(":/icons/zoomin.png"), QSize(), QIcon::Normal, QIcon::On);
  icon10.addFile(QStringLiteral(":/icons/zoomin-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  zoomInButton->setIcon(icon10);
  zoomInButton->setToolTip(tr("Zoom In") + " (=)");
  connect(zoomInButton, SIGNAL(clicked(bool)), this, SLOT(zoom_in()));
  tool_buttons_layout->addWidget(zoomInButton);

  zoomOutButton = new QPushButton(tool_buttons_widget);
  QIcon icon11;
  icon11.addFile(QStringLiteral(":/icons/zoomout.png"), QSize(), QIcon::Normal, QIcon::On);
  icon11.addFile(QStringLiteral(":/icons/zoomout-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  zoomOutButton->setIcon(icon11);
  zoomOutButton->setToolTip(tr("Zoom Out") + " (-)");
  connect(zoomOutButton, SIGNAL(clicked(bool)), this, SLOT(zoom_out()));
  tool_buttons_layout->addWidget(zoomOutButton);

  recordButton = new QPushButton(tool_buttons_widget);
  QIcon icon12;
  icon12.addFile(QStringLiteral(":/icons/record.png"), QSize(), QIcon::Normal, QIcon::On);
  icon12.addFile(QStringLiteral(":/icons/record-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  recordButton->setIcon(icon12);
  recordButton->setToolTip(tr("Record audio"));
  connect(recordButton, SIGNAL(clicked(bool)), this, SLOT(record_btn_click()));

  tool_buttons_layout->addWidget(recordButton);

  addButton = new QPushButton(tool_buttons_widget);
  QIcon icon13;
  icon13.addFile(QStringLiteral(":/icons/add-button.png"), QSize(), QIcon::Normal, QIcon::On);
  icon13.addFile(QStringLiteral(":/icons/add-button-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  addButton->setIcon(icon13);
  addButton->setToolTip(tr("Add title, solid, bars, etc."));
  connect(addButton, SIGNAL(clicked()), this, SLOT(add_btn_click()));
  tool_buttons_layout->addWidget(addButton);

  tool_buttons_layout->addStretch();
  return tool_buttons_widget;

}

QWidget* Timeline::createVideoAreaWidget(QWidget* parent)
{
  Q_ASSERT(parent != nullptr);


  auto video_container = new QWidget(parent);
  auto video_container_layout = new QHBoxLayout(video_container);
  video_container_layout->setSpacing(0);
  video_container_layout->setContentsMargins(0, 0, 0, 0);

  video_track_area_ = new TimelineTrackArea(video_container);
  video_track_area_->type_ = TimelineTrackType::VISUAL;
  QObject::connect(video_track_area_, &TimelineTrackArea::trackLocked, this, &Timeline::trackLocked);
  QObject::connect(video_track_area_, &TimelineTrackArea::trackEnabled, this, &Timeline::trackEnabled);

  video_scroll_ = new ui::BlankScrollArea(parent,true);
  video_scroll_->setAlignment(Qt::AlignBottom | Qt::AlignLeft);
  video_scroll_->containWidget(video_track_area_);

  video_container_layout->addWidget(video_scroll_);

  video_area = new TimelineWidget(true, video_container);
  video_area->setFocusPolicy(Qt::ClickFocus);
  video_area->setMinimumWidth(100);
  video_area->setMinimumHeight(32);

  video_container_layout->addWidget(video_area);

  videoScrollbar = new QScrollBar(video_container);
  videoScrollbar->setMaximum(0);
  videoScrollbar->setSingleStep(20);
  videoScrollbar->setPageStep(1826);
  videoScrollbar->setOrientation(Qt::Vertical);

  QObject::connect(videoScrollbar, &QScrollBar::sliderMoved, video_scroll_, &ui::BlankScrollArea::setScroll);
  video_scroll_->vert_bar_ = videoScrollbar;

  video_container_layout->addWidget(videoScrollbar);

  return video_container;
}

QWidget* Timeline::createAudioAreaWidget(QWidget* parent)
{
  Q_ASSERT(parent != nullptr);

  auto audioContainer = new QWidget(parent);
  auto audioContainerLayout = new QHBoxLayout(audioContainer);
  audioContainerLayout->setSpacing(0);
  audioContainerLayout->setContentsMargins(0, 0, 0, 0);


  audio_track_area_ = new TimelineTrackArea(audioContainer);
  audio_track_area_->type_ = TimelineTrackType::AUDIO;
  QObject::connect(audio_track_area_, &TimelineTrackArea::trackLocked, this, &Timeline::trackLocked);
  QObject::connect(audio_track_area_, &TimelineTrackArea::trackEnabled, this, &Timeline::trackEnabled);

  audio_scroll_ = new ui::BlankScrollArea(parent, false);
  audio_scroll_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  audio_scroll_->containWidget(audio_track_area_);

  audioContainerLayout->addWidget(audio_scroll_);


  audio_area = new TimelineWidget(false, audioContainer);
  audio_area->setFocusPolicy(Qt::ClickFocus);

  audioContainerLayout->addWidget(audio_area);

  audioScrollbar = new QScrollBar(audioContainer);
  audioScrollbar->setMaximum(0);
  audioScrollbar->setOrientation(Qt::Vertical);
  QObject::connect(audioScrollbar, &QScrollBar::sliderMoved, audio_scroll_, &ui::BlankScrollArea::setScroll);
  audio_scroll_->vert_bar_ = audioScrollbar;

  audioContainerLayout->addWidget(audioScrollbar);
  return audioContainer;
}

void Timeline::setup_ui()
{
  auto dockWidgetContents = new QWidget();

  auto horizontalLayout = new QHBoxLayout(dockWidgetContents);
  horizontalLayout->setSpacing(0);
  horizontalLayout->setContentsMargins(0, 0, 0, 0);

  auto tool_button_widget = createToolButtonsWidget(dockWidgetContents);
  horizontalLayout->addWidget(tool_button_widget);

  timeline_area = new QWidget(dockWidgetContents);
  QSizePolicy sizePolicy2(QSizePolicy::Minimum, QSizePolicy::Minimum);
  sizePolicy2.setHorizontalStretch(1);
  sizePolicy2.setVerticalStretch(0);
  sizePolicy2.setHeightForWidth(timeline_area->sizePolicy().hasHeightForWidth());
  timeline_area->setSizePolicy(sizePolicy2);
  auto timeline_area_layout = new QVBoxLayout(timeline_area);
  timeline_area_layout->setSpacing(0);
  timeline_area_layout->setContentsMargins(0, 0, 0, 0);

  auto header_layout = new QHBoxLayout();
  header_layout->setSpacing(0);
  header_layout->setContentsMargins(0, 0, 0, 0);
  timeline_area_layout->addLayout(header_layout);

  header_track_area_ = new TimelineTrackArea(timeline_area);
  header_track_area_->type_ = TimelineTrackType::NONE;
  header_layout->addWidget(header_track_area_);
  headers = new TimelineHeader(timeline_area);
  header_layout->addWidget(headers);
  header_track_area_->setMaximumHeight(headers->height());


  editAreas = new QWidget(timeline_area);
  auto editAreaLayout = new QHBoxLayout(editAreas);
  editAreaLayout->setSpacing(0);
  editAreaLayout->setContentsMargins(0, 0, 0, 0);
  auto splitter = new QSplitter(editAreas);
  splitter->setOrientation(Qt::Vertical);

  // video timeline area setup
  auto videoContainer = createVideoAreaWidget(splitter);
  splitter->addWidget(videoContainer);


  // Audio timeline area setup
  auto audioContainer = createAudioAreaWidget(splitter);

  splitter->addWidget(audioContainer);

  editAreaLayout->addWidget(splitter);

  timeline_area_layout->addWidget(editAreas);

  horizontalScrollBar = new ResizableScrollBar(timeline_area);
  horizontalScrollBar->setMaximum(0);
  horizontalScrollBar->setSingleStep(20);
  horizontalScrollBar->setPageStep(1826);
  horizontalScrollBar->setOrientation(Qt::Horizontal);

  timeline_area_layout->addWidget(horizontalScrollBar);

  horizontalLayout->addWidget(timeline_area);

  audio_monitor = new AudioMonitor(dockWidgetContents);
  audio_monitor->setMinimumSize(QSize(50, 0));

  horizontalLayout->addWidget(audio_monitor);

  setWidget(dockWidgetContents);
}


std::vector<ClipPtr> Timeline::selectedClips()
{
  Q_ASSERT(sequence_ != nullptr);

  std::vector<ClipPtr> clips;
  auto seqClips = sequence_->clips();
  for (const auto& clp : seqClips) {
    if ((clp != nullptr) && clp->isSelected(true)) {
      clips.emplace_back(clp);
    }
  }
  return clips;
}


void Timeline::set_tool()
{
  auto button = dynamic_cast<QPushButton*>(sender());
  decheck_tool_buttons(button);
  tool = static_cast<TimelineToolType>(button->property("tool").toInt());
  creating = false;
  switch (tool) {
    case TimelineToolType::EDIT:
      [[fallthrough]];
    case TimelineToolType::RAZOR:
      timeline_area->setCursor(Qt::IBeamCursor);
      break;
    case TimelineToolType::HAND:
      timeline_area->setCursor(Qt::OpenHandCursor);
      break;
    default:
      timeline_area->setCursor(Qt::ArrowCursor);
  }
}

void Timeline::trackEnabled(const bool enabled, const int track_number)
{
  Q_ASSERT(sequence_ != nullptr);
  if (enabled) {
    sequence_->enableTrack(track_number);
  } else {
    sequence_->disableTrack(track_number);
  }
  repaint_timeline();
  PanelManager::sequenceViewer().update_viewer();
}

void Timeline::trackLocked(const bool locked, const int track_number)
{
  Q_ASSERT(sequence_ != nullptr);
  sequence_->lockTrack(track_number, locked);
  repaint_timeline();
}

void Timeline::changeTrackHeight(QVector<int>& tracks, const int value) const
{
  if (value == 0) {
    return;
  }
  for (auto& trk_height : tracks) {
    trk_height += value;
    if ((value < 0) && (trk_height < TRACK_MIN_HEIGHT) ) {
      trk_height = TRACK_MIN_HEIGHT;
    }
  }
}
