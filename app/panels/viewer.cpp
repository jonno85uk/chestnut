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
#include "viewer.h"

#include <QtMath>
#include <QAudioOutput>
#include <QPainter>
#include <QStringList>
#include <QTimer>
#include <QHBoxLayout>
#include <QPushButton>

#include "playback/audio.h"
#include "timeline.h"
#include "panels/panelmanager.h"
#include "io/config.h"
#include "project/sequence.h"
#include "project/clip.h"
#include "project/footage.h"
#include "project/media.h"
#include "project/undo.h"
#include "ui/audiomonitor.h"
#include "playback/playback.h"
#include "ui/viewerwidget.h"
#include "ui/viewercontainer.h"
#include "ui/labelslider.h"
#include "ui/timelineheader.h"
#include "ui/resizablescrollbar.h"
#include "debug.h"


extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

using panels::PanelManager;

constexpr double NTSC_24 = 23.976;
constexpr double NTSC_30 = 29.97;
constexpr double NTSC_60 = 59.94;

constexpr int FRAMES_IN_ONE_MINUTE = 1798; // 1800 - 2
constexpr int FRAMES_IN_TEN_MINUTES = 17978; // (FRAMES_IN_ONE_MINUTE * 10) - 2

constexpr int RECORD_FLASHER_INTERVAL = 500;

constexpr int MEDIA_WIDTH = 1980;
constexpr int MEDIA_HEIGHT = 1080;
constexpr int MEDIA_AUDIO_FREQUENCY = 48000;
constexpr int MEDIA_FRAME_RATE = 30;

constexpr const char* const PANEL_NAME = "Viewer";
constexpr const char* const PANEL_TITLE_FORMAT = "%1: %2";

constexpr int FAST_SEEK_STEP = 5;


namespace {
  const QColor PAUSE_COLOR(128,192,128); // RGB
}

Viewer::Viewer(QWidget *parent) :
  QDockWidget(parent)
{
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  setup_ui();

  headers->viewer_ = this;
  headers->snapping = false;
  headers->show_text(false);
  Q_ASSERT(viewer_container);
  viewer_container->viewer = this;
  viewer_widget = viewer_container->child;
  Q_ASSERT(viewer_widget);
  viewer_widget->viewer = this;
  viewer_widget->enableGizmos(!fx_mute_);
  set_media(nullptr);

  Q_ASSERT(currentTimecode);
  currentTimecode->setEnabled(false);
  currentTimecode->set_minimum_value(0);
  currentTimecode->set_default_value(qSNaN());
  currentTimecode->set_value(0, false);
  currentTimecode->set_display_type(SliderType::FRAMENUMBER);
  connect(currentTimecode, SIGNAL(valueChanged()), this, SLOT(update_playhead()));

  recording_flasher.setInterval(RECORD_FLASHER_INTERVAL);

  connect(&playback_updater, SIGNAL(timeout()), this, SLOT(timer_update()));
  connect(&recording_flasher, SIGNAL(timeout()), this, SLOT(recording_flasher_update()));
  connect(horizontal_bar, SIGNAL(valueChanged(int)), headers, SLOT(set_scroll(int)));
  connect(horizontal_bar, SIGNAL(valueChanged(int)), viewer_widget, SLOT(set_waveform_scroll(int)));
  connect(horizontal_bar, SIGNAL(resize_move(double)), this, SLOT(resize_move(double)));

  update_playhead_timecode(0);
  update_end_timecode();
}


bool Viewer::is_focused() const {
  return headers->hasFocus()
      || viewer_widget->hasFocus()
      || btnSkipToStart->hasFocus()
      || btnRewind->hasFocus()
      || btnPlay->hasFocus()
      || btnFastForward->hasFocus()
      || btnSkipToEnd->hasFocus();
}

bool Viewer::is_main_sequence() const noexcept
{
  return main_sequence;
}

void Viewer::set_main_sequence()
{
  clean_created_seq();
  setSequence(true, global::sequence);
}

void Viewer::reset_all_audio()
{
  // reset all clip audio
  if (sequence_ != nullptr) {
    audio_ibuffer_frame = sequence_->playhead_;
    audio_ibuffer_timecode = static_cast<double> (audio_ibuffer_frame) / sequence_->frameRate();

    for (const auto& c : sequence_->clips()) {
      if (c != nullptr) {
        c->resetAudio();
      }
    }
  }
  clear_audio_ibuffer();
}

long timecode_to_frame(const QString& s, int view, double frame_rate)
{
  QList<QString> list = s.split(QRegExp("[:;]"));

  if (view == TIMECODE_FRAMES || (list.size() == 1 && view != TIMECODE_MILLISECONDS)) {
    return s.toLong();
  }

  int frRound = qRound(frame_rate);
  int hours, minutes, seconds, frames;

  if (view == TIMECODE_MILLISECONDS) {
    long milliseconds = s.toLong();

    hours = milliseconds/3600000;
    milliseconds -= (hours*3600000);
    minutes = milliseconds/60000;
    milliseconds -= (minutes*60000);
    seconds = milliseconds/1000;
    milliseconds -= (seconds*1000);
    frames = qRound64((milliseconds*0.001)*frame_rate);

    seconds = qRound64(seconds * frame_rate);
    minutes = qRound64(minutes * frame_rate * 60);
    hours = qRound64(hours * frame_rate * 3600);
  } else {
    hours = ((list.size() > 0) ? list.at(0).toInt() : 0) * frRound * 3600;
    minutes = ((list.size() > 1) ? list.at(1).toInt() : 0) * frRound * 60;
    seconds = ((list.size() > 2) ? list.at(2).toInt() : 0) * frRound;
    frames = (list.size() > 3) ? list.at(3).toInt() : 0;
  }

  int f = (frames + seconds + minutes + hours);

  if ((view == TIMECODE_DROP || view == TIMECODE_MILLISECONDS) && frame_rate_is_droppable(frame_rate)) {
    // return drop
    int d;
    int m;

    int dropFrames = qRound(frame_rate * .066666); //Number of frames to drop on the minute marks is the nearest integer to 6% of the framerate
    int framesPer10Minutes = qRound(frame_rate * 60 * 10); //Number of frames per ten minutes
    int framesPerMinute = (qRound(frame_rate)*60)-  dropFrames; //Number of frames per minute is the round of the framerate * 60 minus the number of dropped frames

    d = f / framesPer10Minutes;
    f -= dropFrames*9*d;

    m = f % framesPer10Minutes;

    if (m > dropFrames) {
      f -= (dropFrames * ((m - dropFrames) / framesPerMinute));
    }
  }

  // return non-drop
  return f;
}

QString frame_to_timecode(int64_t frame, const int view, const double frame_rate)
{
  if (qFuzzyCompare(frame_rate, 0)) {
    return "NaN";
  }
  if (view == TIMECODE_FRAMES) {
    return QString::number(frame);
  }

  // return timecode
  int hours = 0;
  int mins = 0;
  int secs = 0;
  int frames = 0;
  auto token = ':';

  if (( (view == TIMECODE_DROP) || (view == TIMECODE_MILLISECONDS) ) && frame_rate_is_droppable(frame_rate)) {
    //CONVERT A FRAME NUMBER TO DROP FRAME TIMECODE
    //Code by David Heidelberger, adapted from Andrew Duncan, further adapted for Olive by Olive Team
    //Given an int called framenumber and a double called framerate
    //Framerate should be 29.97, 59.94, or 23.976, otherwise the calculations will be off.

    const int dropFrames = qRound(frame_rate * 0.066666); //Number of frames to drop on the minute marks is the nearest integer to 6% of the framerate
    const int framesPerHour = qRound(frame_rate * 60 * 60); //Number of frqRound64ames in an hour
    const int framesPer24Hours = framesPerHour * 24; //Number of frames in a day - timecode rolls over after 24 hours
    const int framesPer10Minutes = qRound(frame_rate * 60 * 10); //Number of frames per ten minutes
    const int framesPerMinute = (qRound(frame_rate) * 60) -  dropFrames; //Number of frames per minute is the round of the framerate * 60 minus the number of dropped frames

    //If framenumber is greater than 24 hrs, next operation will rollover clock
    frame = frame % framesPer24Hours; //% is the modulus operator, which returns a remainder. a % b = the remainder of a/b

    const auto d = frame / framesPer10Minutes; // \ means integer division, which is a/b without a remainder. Some languages you could use floor(a/b)
    const auto m = frame % framesPer10Minutes;

    //In the original post, the next line read m>1, which only worked for 29.97. Jean-Baptiste Mardelle correctly pointed out that m should be compared to dropFrames.
    if (m > dropFrames) {
      frame = frame + (dropFrames*9*d) + dropFrames * ((m - dropFrames) / framesPerMinute);
    } else {
      frame = frame + dropFrames*9*d;
    }

    const int frRound = qRound(frame_rate);
    frames = frame % frRound;
    secs = (frame / frRound) % 60;
    mins = ((frame / frRound) / 60) % 60;
    hours = (((frame / frRound) / 60) / 60);

    token = ';';
  } else {
    // non-drop timecode

    const int int_fps = qRound(frame_rate);
    hours = frame / (3600 * int_fps);
    mins = frame / (60 * int_fps) % 60;
    secs = frame / int_fps % 60;
    frames = frame % int_fps;
  }
  if (view == TIMECODE_MILLISECONDS) {
    return QString::number((hours * 3600000) + (mins * 60000) + (secs * 1000) + qCeil(frames * 1000 / frame_rate));
  }
  return QString(QString::number(hours).rightJustified(2, '0') +
                 ":" + QString::number(mins).rightJustified(2, '0') +
                 ":" + QString::number(secs).rightJustified(2, '0') +
                 token + QString::number(frames).rightJustified(2, '0')
                 );
}

bool frame_rate_is_droppable(const double rate)
{
  return (qFuzzyCompare(rate, NTSC_24) || qFuzzyCompare(rate, NTSC_30) || qFuzzyCompare(rate, NTSC_60));
}

void Viewer::seek(const int64_t p)
{
  if (sequence_ == nullptr) {
    qDebug() << "No assigned sequence";
    return;
  }
  pause();
  sequence_->playhead_ = p;
  bool update_fx = false;
  if (main_sequence) {
    PanelManager::timeLine().scroll_to_frame(p);
    PanelManager::fxControls().scroll_to_frame(p);
    if (global::config.seek_also_selects) {
      PanelManager::timeLine().select_from_playhead();
      update_fx = true;
    }
  }
  reset_all_audio();
  audio_scrub = true;
  update_parents(update_fx);
}

void Viewer::go_to_start()
{
  if (sequence_ != nullptr) {
    seek(0);
  }
}

void Viewer::go_to_end()
{
  if (sequence_ != nullptr) {
    seek(sequence_->endFrame());
  }
}

void Viewer::close_media()
{
  set_media(nullptr);
}

void Viewer::go_to_in()
{
  if (sequence_ == nullptr) {
    return;
  }
  if (sequence_->workarea_.using_ && sequence_->workarea_.enabled_) {
    seek(sequence_->workarea_.in_);
  } else {
    go_to_start();
  }
}

void Viewer::previous_frame()
{
  if ((sequence_ != nullptr) && (sequence_->playhead_ > 0)) {
    seek(sequence_->playhead_ - 1);
  }
}


void Viewer::previousFrameFast()
{
  if ( (sequence_ != nullptr) && (sequence_->playhead_ > 0) ) {
    seek(sequence_->playhead_ - FAST_SEEK_STEP);
  }
}

void Viewer::next_frame()
{
  if (sequence_ != nullptr) {
    seek(sequence_->playhead_+ 1);
  }
}

void Viewer::nextFrameFast()
{
  if (sequence_ != nullptr) {
    seek(sequence_->playhead_+ FAST_SEEK_STEP);
  }
}

void Viewer::go_to_out()
{
  if (sequence_ != nullptr) {
    if (sequence_->workarea_.using_ && sequence_->workarea_.enabled_) {
      seek(sequence_->workarea_.out_);
    } else {
      go_to_end();
    }
  }
}

void Viewer::cue_recording(long start, long end, int track)
{
  recording_start = start;
  recording_end = end;
  recording_track = track;
  PanelManager::sequenceViewer().seek(recording_start);
  cue_recording_internal = true;
  recording_flasher.start();
}

void Viewer::uncue_recording()
{
  cue_recording_internal = false;
  recording_flasher.stop();
  btnPlay->setStyleSheet(QString());
}

bool Viewer::is_recording_cued()
{
  return cue_recording_internal;
}

void Viewer::toggle_play()
{
  if (playing) {
    pause();
  } else {
    play();
  }
}

void Viewer::play()
{
  if (PanelManager::sequenceViewer().playing) {
    PanelManager::sequenceViewer().pause();
  }
  if (PanelManager::footageViewer().playing) {
    PanelManager::footageViewer().pause();
  }

  if (sequence_ != nullptr) {
    if (!is_recording_cued()
        && sequence_->playhead_ >= get_seq_out()
        && (global::config.loop || !main_sequence)) {
      seek(get_seq_in());
    }

    reset_all_audio();
    if (is_recording_cued() && !start_recording()) {
      qCritical() << "Failed to record audio";
      return;
    }
    playhead_start = sequence_->playhead_;
    playing = true;
    just_played = true;
    set_playpause_icon(false);
    start_msecs = QDateTime::currentMSecsSinceEpoch();
    timer_update();
    emit startedPlaying();
  }
}

void Viewer::play_wake()
{
  if (just_played) {
    start_msecs = QDateTime::currentMSecsSinceEpoch();
    playback_updater.start();
    if (audio_thread != nullptr) audio_thread->notifyReceiver();
    just_played = false;
  }
}

void Viewer::pause()
{
  emit stoppedPlaying();
  playing = false;
  just_played = false;
  set_playpause_icon(true);
  playback_updater.stop();

  if (is_recording_cued()) {
    uncue_recording();

    if (recording) {
      stop_recording();

      // import audio
      QStringList file_list;
      file_list.append(get_recorded_audio_filename());
      PanelManager::projectViewer().process_file_list(file_list);

      // add it to the sequence
      auto clp = std::make_shared<Clip>(sequence_);
      auto mda = PanelManager::projectViewer().getImportedMedia(0);
      auto ftg = mda->object<Footage>();

      clp->timeline_info.media = mda; // latest media
      clp->timeline_info.media_stream = 0;
      clp->timeline_info.in = recording_start;
      clp->timeline_info.out = recording_start + ftg->totalLengthInFrames(sequence_->frameRate());
      clp->timeline_info.clip_in = 0;
      clp->timeline_info.track_ = recording_track;
      clp->timeline_info.color = PAUSE_COLOR;
      clp->timeline_info.name_ = mda->name();

      QVector<ClipPtr> add_clips;
      add_clips.append(clp);
      e_undo_stack.push(new AddClipsCommand(sequence_, add_clips)); // add clip
    }
  }
}

void Viewer::update_playhead_timecode(long p)
{
  Q_ASSERT(currentTimecode);
  currentTimecode->set_value(p, false);
}

void Viewer::update_end_timecode()
{
  Q_ASSERT(endTimecode);
  endTimecode->setText((sequence_ == nullptr) ? frame_to_timecode(0, global::config.timecode_view, 30)
                                        : frame_to_timecode(sequence_->activeLength(), global::config.timecode_view, sequence_->frameRate()));
}

void Viewer::update_header_zoom()
{
  if (sequence_ == nullptr) {
    return;
  }
  const long sequenceEndFrame = sequence_->endFrame();
  if (cached_end_frame != sequenceEndFrame) {
    minimum_zoom = (sequenceEndFrame > 0) ? ( static_cast<double>(headers->width()) / sequenceEndFrame) : 1;
    headers->update_zoom(qMax(headers->get_zoom(), minimum_zoom));
    set_sb_max();
    viewer_widget->waveform_zoom = headers->get_zoom();
  } else {
    headers->update();
  }
}

void Viewer::update_parents(bool reload_fx)
{
  if (main_sequence) {
    PanelManager::refreshPanels(reload_fx);
  } else {
    update_viewer();
  }
}

void Viewer::resizeEvent(QResizeEvent *)
{
  if (sequence_ != nullptr) {
    set_sb_max();
  }
}


MediaWPtr Viewer::getMedia()
{
  return media_;
}

SequencePtr Viewer::getSequence()
{
  return sequence_;
}

void Viewer::setMarker() const
{
  bool add_marker = !global::config.set_name_with_marker;
  QString marker_name;

  if (!add_marker) {
    std::tie(marker_name, add_marker) = this->getName();
  }

  if (add_marker) {
    // TODO: create a thumbnail for this
    if (auto mda = media_.lock()) {
      e_undo_stack.push(new AddMarkerAction(mda->object<Footage>(), sequence_->playhead_, marker_name));
    }
  }
}


void Viewer::enableFXMute(const bool value)
{
  Q_ASSERT(fx_mute_button_);
  fx_mute_button_->setVisible(value);
}


bool Viewer::usingEffects() const
{
  return !fx_mute_;
}

void Viewer::update_viewer()
{
  update_header_zoom();
  viewer_widget->frame_update();
  // FIXME: doing x2 frame updates resolves Issue#32. Need to find out why!
  viewer_widget->frame_update();
  if (sequence_ != nullptr) {
    update_playhead_timecode(sequence_->playhead_);
  }
  update_end_timecode();
}

void Viewer::clear_in()
{
  if (sequence_->workarea_.using_) {
    e_undo_stack.push(new SetTimelineInOutCommand(sequence_, true, 0, sequence_->workarea_.out_));
    update_parents();
  }
}

void Viewer::clear_out()
{
  if (sequence_->workarea_.using_) {
    e_undo_stack.push(new SetTimelineInOutCommand(sequence_, true, sequence_->workarea_.in_, sequence_->endFrame()));
    update_parents();
  }
}

void Viewer::clear_inout_point()
{
  if (sequence_->workarea_.using_) {
    e_undo_stack.push(new SetTimelineInOutCommand(sequence_, false, 0, 0));
    update_parents();
    PanelManager::projectViewer().updatePanel();
  }
}

void Viewer::toggle_enable_inout() {
  if (sequence_ != nullptr && sequence_->workarea_.using_) {
    e_undo_stack.push(new SetBool(&sequence_->workarea_.enabled_, !sequence_->workarea_.enabled_));
    update_parents();
  }
}

void Viewer::set_in_point()
{
  headers->set_in_point(sequence_->playhead_);
  PanelManager::projectViewer().updatePanel();
}

void Viewer::set_out_point()
{
  headers->set_out_point(sequence_->playhead_);
  PanelManager::projectViewer().updatePanel();
}

void Viewer::set_zoom(bool in)
{
  if (sequence_ != nullptr) {
    set_zoom_value(in ? headers->get_zoom() * 2 : qMax(minimum_zoom, headers->get_zoom() * 0.5));
  }
}

void Viewer::set_panel_name(const QString &n)
{
  panel_name = n;
  update_window_title();
}


void Viewer::reRender()
{
  Q_ASSERT(viewer_widget);
  viewer_widget->frame_update();
}


void Viewer::update_window_title()
{
  const QString name(sequence_ == nullptr  ? tr("(none)") : sequence_->name());
  setWindowTitle(QString("%1: %2").arg(panel_name, name));
}

void Viewer::set_zoom_value(double d)
{
  headers->update_zoom(d);
  if (viewer_widget->waveform) {
    viewer_widget->waveform_zoom = d;
    viewer_widget->update();
  }

  if (sequence_ == nullptr) {
    return;
  }
  set_sb_max();
  if (!horizontal_bar->is_resizing()) {
    center_scroll_to_playhead(horizontal_bar, headers->get_zoom(), sequence_->playhead_);
  }
}

void Viewer::set_sb_max()
{
  headers->set_scrollbar_max(horizontal_bar, sequence_->endFrame(), headers->width());
}

int64_t Viewer::get_seq_in() const
{
  return (sequence_->workarea_.using_ && sequence_->workarea_.enabled_)
      ? sequence_->workarea_.in_
      : 0;
}

int64_t Viewer::get_seq_out() const
{
  return (sequence_->workarea_.using_ && sequence_->workarea_.enabled_ && previous_playhead < sequence_->workarea_.out_)
      ? sequence_->workarea_.out_
      : sequence_->endFrame();
}

void Viewer::setup_ui()
{
  auto contents = new QWidget();

  auto layout = new QVBoxLayout(contents);
  layout->setSpacing(0);
  layout->setMargin(0);

  viewer_container = new ViewerContainer(contents);
  viewer_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  layout->addWidget(viewer_container);

  headers = new TimelineHeader(contents);
  layout->addWidget(headers);

  horizontal_bar = new ResizableScrollBar(contents);
  horizontal_bar->setSingleStep(20);
  horizontal_bar->setOrientation(Qt::Horizontal);
  layout->addWidget(horizontal_bar);

  auto lower_controls = new QWidget(contents);

  auto lower_control_layout = new QHBoxLayout(lower_controls);
  lower_control_layout->setMargin(0);

  // current time code
  auto current_timecode_container = new QWidget(lower_controls);
  auto current_timecode_container_layout = new QHBoxLayout(current_timecode_container);
  current_timecode_container_layout->setSpacing(0);
  current_timecode_container_layout->setMargin(0);
  currentTimecode = new LabelSlider(current_timecode_container);
  currentTimecode->setToolTip(tr("Playhead position"));
  current_timecode_container_layout->addWidget(currentTimecode);
  lower_control_layout->addWidget(current_timecode_container);

  auto playback_controls = new QWidget(lower_controls);

  auto playback_control_layout = new QHBoxLayout(playback_controls);
  playback_control_layout->setSpacing(0);
  playback_control_layout->setMargin(0);

  btnSkipToStart = new QPushButton(playback_controls);
  btnSkipToStart->setToolTip(tr("Go to In"));
  QIcon goToStartIcon;
  goToStartIcon.addFile(QStringLiteral(":/icons/prev.png"), QSize(), QIcon::Normal, QIcon::Off);
  goToStartIcon.addFile(QStringLiteral(":/icons/prev-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  btnSkipToStart->setIcon(goToStartIcon);
  connect(btnSkipToStart, SIGNAL(clicked(bool)), this, SLOT(go_to_in()));
  playback_control_layout->addWidget(btnSkipToStart);

  btnRewind = new QPushButton(playback_controls);
  btnRewind->setToolTip(tr("Step back 1 frame"));
  QIcon rewindIcon;
  rewindIcon.addFile(QStringLiteral(":/icons/rew.png"), QSize(), QIcon::Normal, QIcon::Off);
  rewindIcon.addFile(QStringLiteral(":/icons/rew-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  btnRewind->setIcon(rewindIcon);
  connect(btnRewind, SIGNAL(clicked(bool)), this, SLOT(previous_frame()));
  playback_control_layout->addWidget(btnRewind);

  btnPlay = new QPushButton(playback_controls);
  btnPlay->setToolTip(tr("Toggle playback"));
  playIcon.addFile(QStringLiteral(":/icons/play.png"), QSize(), QIcon::Normal, QIcon::On);
  playIcon.addFile(QStringLiteral(":/icons/play-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  btnPlay->setIcon(playIcon);
  connect(btnPlay, SIGNAL(clicked(bool)), this, SLOT(toggle_play()));
  playback_control_layout->addWidget(btnPlay);

  btnFastForward = new QPushButton(playback_controls);
  btnFastForward->setToolTip(tr("Step forward 1 frame"));
  QIcon ffIcon;
  ffIcon.addFile(QStringLiteral(":/icons/ff.png"), QSize(), QIcon::Normal, QIcon::On);
  ffIcon.addFile(QStringLiteral(":/icons/ff-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  btnFastForward->setIcon(ffIcon);
  connect(btnFastForward, SIGNAL(clicked(bool)), this, SLOT(next_frame()));
  playback_control_layout->addWidget(btnFastForward);

  btnSkipToEnd = new QPushButton(playback_controls);
  btnSkipToEnd->setToolTip(tr("Go to Out"));
  QIcon nextIcon;
  nextIcon.addFile(QStringLiteral(":/icons/next.png"), QSize(), QIcon::Normal, QIcon::Off);
  nextIcon.addFile(QStringLiteral(":/icons/next-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  btnSkipToEnd->setIcon(nextIcon);
  connect(btnSkipToEnd, SIGNAL(clicked(bool)), this, SLOT(go_to_out()));
  playback_control_layout->addWidget(btnSkipToEnd);

  fx_mute_button_ = new QPushButton(playback_controls);
  fx_mute_button_->setCheckable(true);
  fx_mute_button_->setToolTip(tr(""));
  QIcon mute_icon;
  mute_icon.addFile(QStringLiteral(":/icons/fxmute.png"), QSize(), QIcon::Normal, QIcon::Off);
  mute_icon.addFile(QStringLiteral(":/icons/fxmute-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  fx_mute_button_->setIcon(mute_icon);
  fx_mute_button_->setText("fx");
  QFont fx_font;
  fx_font.setItalic(true);
  fx_mute_button_->setFont(fx_font);
  fx_mute_button_->setMinimumSize(16, 16);
  fx_mute_button_->setMaximumSize(24, 24);
  connect(fx_mute_button_, &QPushButton::toggled, this, &Viewer::fxMute);
  playback_control_layout->addWidget(fx_mute_button_);
  fx_mute_button_->setVisible(false);
  fx_mute_button_->setEnabled(false);

  lower_control_layout->addWidget(playback_controls);

  auto end_timecode_container = new QWidget(lower_controls);

  auto end_timecode_layout = new QHBoxLayout(end_timecode_container);
  end_timecode_layout->setSpacing(0);
  end_timecode_layout->setMargin(0);

  endTimecode = new QLabel(end_timecode_container);
  endTimecode->setToolTip(tr("In/Out duration"));
  endTimecode->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);

  end_timecode_layout->addWidget(endTimecode);

  lower_control_layout->addWidget(end_timecode_container);

  layout->addWidget(lower_controls);

  setWidget(contents);
}

SequencePtr Viewer::createFootageSequence(const MediaPtr& mda) const
{
  auto ftg = mda->object<Footage>();
  auto sqn = std::make_shared<Sequence>();
  sqn->markers_ = ftg->markers_;
  sqn->wrapper_sequence_ = true;
  sqn->setName(ftg->name());

  sqn->workarea_.using_ = ftg->using_inout;
  if (ftg->using_inout) {
    sqn->workarea_.in_ = ftg->in;
    sqn->workarea_.out_ = ftg->out;
  }

  sqn->setFrameRate(MEDIA_FRAME_RATE);

  if (!ftg->videoTracks().empty()) {
    if (const auto video_stream = ftg->videoTracks().front()) {
      sqn->setWidth(video_stream->video_width);
      sqn->setHeight(video_stream->video_height);
      if ( (video_stream->video_frame_rate > 0) && (!video_stream->infinite_length) ) { // not image?
        sqn->setFrameRate(video_stream->video_frame_rate * ftg->speed_);
      }

      auto clp = std::make_shared<Clip>(sqn);
      clp->timeline_info.media        = mda;
      clp->timeline_info.media_stream = video_stream->file_index;
      clp->timeline_info.in           = 0;
      clp->timeline_info.out          = ftg->totalLengthInFrames(sqn->frameRate());
      if (clp->timeline_info.out <= 0) {
        clp->timeline_info.out = 150;
      }
      clp->timeline_info.track_ = -1;
      clp->timeline_info.clip_in  = 0;
      clp->recalculateMaxLength();
      sqn->addClip(clp);
    }
  } else {
    sqn->setWidth(MEDIA_WIDTH);
    sqn->setHeight(MEDIA_HEIGHT);
  }

  if (!ftg->audioTracks().empty()) {
    if (const auto audio_stream = ftg->audioTracks().front()) {
      sqn->setAudioFrequency(audio_stream->audio_frequency);

      auto clp = std::make_shared<Clip>(sqn);
      clp->timeline_info.media        = mda;
      clp->timeline_info.media_stream = audio_stream->file_index;
      clp->timeline_info.in           = 0;
      clp->timeline_info.out          = ftg->totalLengthInFrames(sqn->frameRate());
      clp->timeline_info.track_       = 0;
      clp->timeline_info.clip_in      = 0;
      clp->recalculateMaxLength();
      sqn->addClip(clp);

      if (ftg->videoTracks().empty()) {
        viewer_widget->waveform         = true;
        viewer_widget->waveform_clip    = clp;
        viewer_widget->waveform_ms      = audio_stream;
        viewer_widget->frame_update();
      }
    }
  } else {
    sqn->setAudioFrequency(MEDIA_AUDIO_FREQUENCY);
  }

  sqn->setAudioLayout(AV_CH_LAYOUT_STEREO);
  return sqn;
}

void Viewer::set_media(const MediaPtr& m)
{
  main_sequence = false;
  if (auto mda = media_.lock()) {
    if (mda == m) {
      // prevent assigning shared_ptr to itself
      return;
    }
  }
  media_ = m;
  clean_created_seq();
  if (auto mda = media_.lock()) {
    switch (mda->type()) {
      case MediaType::FOOTAGE:
      {
        sequence_ = createFootageSequence(mda);
        created_sequence = true;
        auto ftg = mda->object<Footage>();
        Q_ASSERT(ftg);
        seek(ftg->in);
      }
        break;
      case MediaType::SEQUENCE:
        sequence_ = mda->object<Sequence>();
        break;
      default:
        qWarning() << "Unhandled media type" << static_cast<int>(mda->type());
        break;
    }//switch
  }
  setSequence(false, sequence_);
}

void Viewer::reset()
{
  //TODO:
}

void Viewer::update_playhead()
{
  seek(currentTimecode->value());
}

void Viewer::timer_update()
{
  previous_playhead = sequence_->playhead_;

  sequence_->playhead_ = qRound(playhead_start + ((QDateTime::currentMSecsSinceEpoch()-start_msecs) * 0.001 * sequence_->frameRate()));
  if (global::config.seek_also_selects) {
    PanelManager::timeLine().select_from_playhead();
  }
  update_parents(global::config.seek_also_selects);

  const int64_t end_frame = get_seq_out();
  if (!recording
      && playing
      && (sequence_->playhead_ >= end_frame)
      && (previous_playhead < end_frame) ) {
    if (!global::config.pause_at_out_point && global::config.loop) {
      seek(get_seq_in());
      play();
    } else if (global::config.pause_at_out_point || !main_sequence) {
      pause();
    }
  } else if (recording && (recording_start != recording_end) && (sequence_->playhead_ >= recording_end) ) {
    pause();
  }
}

void Viewer::recording_flasher_update()
{
  if (btnPlay->styleSheet().isEmpty()) {
    btnPlay->setStyleSheet("background: red;");
  } else {
    btnPlay->setStyleSheet(QString());
  }
}

void Viewer::resize_move(double d)
{
  set_zoom_value(headers->get_zoom() * d);
}


void Viewer::fxMute(const bool value)
{
  fx_mute_ = value;
  viewer_widget->enableGizmos(!value);
  reRender();
}

void Viewer::clean_created_seq()
{
  viewer_widget->waveform = false;

  if (created_sequence) {
    // TODO delete undo commands referencing this sequence to avoid crashes
    /*for (int i=0;i<undo_stack.count();i++) {
            undo_stack.command(i)
        }*/

    sequence_ = nullptr;
    created_sequence = false;
  }
}

void Viewer::setSequence(const bool main, SequencePtr seq)
{
  pause();

  reset_all_audio();

  if (sequence_ != nullptr) {
    sequence_->closeActiveClips();
  }

  main_sequence = main;
  sequence_ = main ? global::sequence : std::move(seq);

  const bool null_sequence = (sequence_ == nullptr);

  headers->setEnabled(!null_sequence);
  currentTimecode->setEnabled(!null_sequence);
  viewer_widget->setEnabled(!null_sequence);
  viewer_widget->setVisible(!null_sequence);
  btnSkipToStart->setEnabled(!null_sequence);
  btnRewind->setEnabled(!null_sequence);
  btnPlay->setEnabled(!null_sequence);
  btnFastForward->setEnabled(!null_sequence);
  btnSkipToEnd->setEnabled(!null_sequence);
  fx_mute_button_->setEnabled(!null_sequence);

  if (!null_sequence) {
    currentTimecode->set_frame_rate(sequence_->frameRate());

    playback_updater.setInterval(qFloor(1000 / sequence_->frameRate()));

    update_playhead_timecode(sequence_->playhead_);
    update_end_timecode();

    viewer_container->adjust();
  } else {
    update_playhead_timecode(0);
    update_end_timecode();
  }
  update_window_title();

  update_header_zoom();

  viewer_widget->frame_update();

  update();
}

void Viewer::set_playpause_icon(bool play)
{
  btnPlay->setIcon(play ? playIcon : QIcon(":/icons/pause.png"));
}
