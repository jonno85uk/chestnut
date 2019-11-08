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
#include "sequence.h"

#include <QCoreApplication>

#include <libavutil/channel_layout.h>

#include "panels/panelmanager.h"
#include "project/objectclip.h"
#include "clip.h"
#include "transition.h"

#include "debug.h"

using project::ScanMethod;
using project::Track;

constexpr int       RECURSION_LIMIT            = 100;
constexpr int       MAXIMUM_WIDTH              = 4096;
constexpr int       MAXIMUM_HEIGHT             = 2160;
constexpr int       MAXIMUM_FREQUENCY          = 192000;
constexpr int       DEFAULT_WIDTH              = 1920;
constexpr int       DEFAULT_HEIGHT             = 1080;
constexpr double    DEFAULT_FRAMERATE          = 29.97;
constexpr int       DEFAULT_AUDIO_FREQUENCY    = 48000;
constexpr int       DEFAULT_LAYOUT             = AV_CH_LAYOUT_STEREO;



// static variable for the currently active sequence
SequencePtr global::sequence;


Sequence::Sequence(std::shared_ptr<Media> parent) : parent_mda(std::move(parent))
{

}


Sequence::Sequence(QVector<std::shared_ptr<Media>>& media_list, const QString& sequenceName)
  : ProjectItem(sequenceName),
    width_(DEFAULT_WIDTH),
    height_(DEFAULT_HEIGHT),
    frame_rate_(DEFAULT_FRAMERATE),
    audio_frequency_(DEFAULT_AUDIO_FREQUENCY),
    audio_layout_(DEFAULT_LAYOUT)
{
  bool got_video_values = false;
  bool got_audio_values = false;
  for (const auto& mda : media_list){
     if (mda == nullptr) {
      qCritical() << "Null MediaPtr";
      continue;
    }
    switch (mda->type()) {
      case MediaType::FOOTAGE:
      {
        auto ftg = mda->object<Footage>();
        if (!ftg->ready_ || got_video_values) {
          break;
        }

        for (const auto& ms : ftg->video_tracks) {
          width_ = ms->video_width;
          height_ = ms->video_height;
          if (!qFuzzyCompare(ms->video_frame_rate, 0.0)) {
            frame_rate_ = ms->video_frame_rate * ftg->speed_;

            if (ms->fieldOrder() != media_handling::FieldOrder::PROGRESSIVE) {
              frame_rate_ *= 2;
            }

            // only break with a decent frame rate, otherwise there may be a better candidate
            got_video_values = true;
            break;
          }
        }//for
        if (!got_audio_values && !ftg->audio_tracks.empty()) {
          const auto ms = ftg->audio_tracks.front();
          Q_ASSERT(ms);
          audio_frequency_ = ms->audio_frequency;
          got_audio_values = true;
        }
      }
        break;
      case MediaType::SEQUENCE:
      {
        if (auto seq = mda->object<Sequence>()) {
          width_ = seq->width();
          height_  = seq->height();
          frame_rate_ = seq->frameRate();
          audio_frequency_ = seq->audioFrequency();
          audio_layout_ = seq->audioLayout();

          got_video_values = true;
          got_audio_values = true;
        }
      }
        break;
      default:
        qWarning() << "Unknown media type" << static_cast<int>(mda->type());
    }//switch

    if (got_video_values && got_audio_values) {
      break;
    }
  } //for

  qDebug() << "Created width=" << width_ << ", height=" << height_ << ", framerate=" << frame_rate_;

}


std::shared_ptr<Sequence> Sequence::copy()
{
  auto sqn = std::make_shared<Sequence>();
  sqn->name_ = QCoreApplication::translate("Sequence", "%1 (copy)").arg(name_);
  sqn->width_ = width_;
  sqn->height_ = height_;
  sqn->frame_rate_ = frame_rate_;
  sqn->audio_frequency_ = audio_frequency_;
  sqn->audio_layout_ = audio_layout_;
  sqn->clips_.resize(clips_.size());

  for (auto i=0;i<clips_.size();i++) {
    auto c = clips_.at(i);
    if (c == nullptr) {
      sqn->clips_[i] = nullptr;
    } else {
      auto copy(c->copyPreserveLinks(sqn));
      sqn->clips_[i] = copy;
    }
  }
  return sqn;
}

int64_t Sequence::endFrame() const noexcept
{
  auto end = 0L;

  try {
    for (const auto& clp : clips_) {
      if (clp && (clp->timeline_info.out > end) ) {
        end = clp->timeline_info.out;
      }
    }
  }
  catch (const std::exception& ex) {
    qWarning() << "Caught an exception, ex =" << ex.what();
  } catch (...) {
    qWarning() << "Caught an unknown exception";
  }

  return end;
}

void Sequence::hardDeleteTransition(const ClipPtr& c, const int32_t type)
{


  if (type == TA_OPENING_TRANSITION) {
    c->transition_.opening_ = nullptr;
  } else {
    c->transition_.closing_ = nullptr;
  }
//TODO: reassess
//  auto tran = (type == TA_OPENING_TRANSITION) ? c->transition_.opening_ : c->transition_.closing_;
//  if (tran == nullptr) {
//    return;
//  }
//  auto del = true;

//  auto transition_for_delete = transitions_.at(transition_index);
//  if (auto secondary = transition_for_delete->secondary_clip.lock()) {
//    for (const auto& comp : clips_) {
//      if ( (!comp) && (c != comp) ) {
//        continue;
//      }
//      if ( (c->opening_transition == transition_index) || (c->closing_transition == transition_index) ) {
//        if (type == TA_OPENING_TRANSITION) {
//          // convert to closing transition
//          transition_for_delete->parent_clip = secondary;
//        }

//        del = false;
//        secondary.reset();
//      }
//    }//for
//  }

//  if (del) {
//    transitions_[transition_index] = nullptr;
//  }

//  if (type == TA_OPENING_TRANSITION) {
//    c->opening_transition = -1;
//  } else {
//    c->closing_transition = -1;
//  }
}


bool Sequence::setWidth(const int32_t val)
{
  if ( (val % 2  == 0) && (val > 0) && (val <= MAXIMUM_WIDTH) ) {
    width_ = val;
    return true;
  }
  return false;
}
int32_t Sequence::width() const
{
  return width_;
}

bool Sequence::setHeight(const int32_t val)
{
  if ( (val % 2  == 0) && (val > 0) && (val <= MAXIMUM_HEIGHT) ) {
    height_ = val;
    return true;
  }
  return false;
}
int32_t Sequence::height() const
{
  return height_;
}

double Sequence::frameRate() const
{
  return frame_rate_;
}

bool Sequence::setFrameRate(const double frameRate)
{
  if (frameRate > 0.0) {
    frame_rate_ = frameRate;
    return true;
  }
  return false;
}


int32_t Sequence::audioFrequency() const
{
  return audio_frequency_;
}
bool Sequence::setAudioFrequency(const int32_t frequency)
{
  if ( (frequency >= 0) && (frequency <= MAXIMUM_FREQUENCY) ) {
    audio_frequency_ = frequency;
    return true;
  }
  return false;
}

/**
 * @brief getAudioLayout from ffmpeg libresample
 * @return AV_CH_LAYOUT_*
 */
int32_t Sequence::audioLayout() const noexcept
{
  return audio_layout_;
}
/**
 * @brief setAudioLayout using ffmpeg libresample
 * @param layout AV_CH_LAYOUT_* value from libavutil/channel_layout.h
 */
void Sequence::setAudioLayout(const int32_t layout) noexcept
{
  audio_layout_ = layout;
}


int Sequence::trackCount(const bool video) const
{
  QSet<int> tracks;

  for (const auto& c : clips_) {
    if (c == nullptr) {
      qWarning() << "Clip instance is null";
      continue;
    }
    if (video == (c->mediaType() == ClipType::VISUAL)) {
      tracks.insert(c->timeline_info.track_);
    }
  }

  if (clips_.empty()) {
    return 0;
  }

  if (tracks_.empty()) {
    return 0;
  }

  if (video) {
    return abs(*std::min_element(tracks.begin(), tracks.end()));
  }

  // fudge because of audio track #0
  auto itor = std::max_element(tracks.begin(), tracks.end());
  if (itor == tracks.end()) {
    return 0;
  }

  if (*itor >= 0) {
    return *itor + 1;
  }
  return 0;
}


bool Sequence::trackEnabled(const int track_number) const
{
  if (tracks_.contains(track_number)) {
    return tracks_[track_number].enabled_;
  }
  return true;
}


bool Sequence::trackLocked(const int track_number) const
{
  if (tracks_.contains(track_number)) {
    return tracks_[track_number].locked_;
  }
  return false;
}

QVector<ClipPtr> Sequence::clips(const long frame) const
{
  QVector<ClipPtr> frame_clips;

  for (const auto& c : clips_) {
    if (c == nullptr) {
      qWarning() << "Clip instance is null";
      continue;
    }
    if (c->inRange(frame)) {
      frame_clips.append(c);
    }
  }
  return frame_clips;
}


bool Sequence::addClip(const ClipPtr& new_clip)
{
  if (new_clip == nullptr) {
    qWarning() << "Clip instance is null";
    return false;
  }

  if (clip(new_clip->id())) {
    qWarning() << "Clip already added" << new_clip->id();
    return false;
  }

  clips_.append(new_clip);
  if (!tracks_.contains(new_clip->timeline_info.track_)) {
    addTrack(new_clip->timeline_info.track_, new_clip->mediaType() == ClipType::VISUAL);
    enableTrack(new_clip->timeline_info.track_);
  }

  return true;
}


QVector<ClipPtr> Sequence::clips()
{
  return clips_;
}


void Sequence::closeActiveClips(const int32_t depth)
{
  if (depth > RECURSION_LIMIT) {
    return;
  }
  for (const auto& c : clips_) {
    if (!c) {
      qWarning() << "Null Clip ptr";
      continue;
    }
    if (c->timeline_info.media && (c->timeline_info.media->type() == MediaType::SEQUENCE) ) {
      if (auto seq = c->timeline_info.media->object<Sequence>()) {
        seq->closeActiveClips(depth + 1);
      }
    }
    c->close(true);
  }//for
}


ClipPtr Sequence::clip(const int32_t id)
{
  for (const auto& clp : clips_) {
    if ( (clp != nullptr) && (clp->id() == id) ) {
      return clp;
    }
  }
  return nullptr;
}


void Sequence::deleteClip(const int32_t id)
{
  auto iter = std::find_if(clips_.begin(), clips_.end(), [id](const ClipPtr& clp) {return clp->id() == id;});
  if (iter != clips_.end()) {
    clips_.erase(iter);
  }
}

bool Sequence::load(QXmlStreamReader& stream)
{
  for (const auto& attr : stream.attributes()) {
    const auto name = attr.name().toString().toLower();
    if (name == "id") {
      if (auto mda  = parent_mda.lock()) {
        save_id_ = attr.value().toInt();
        mda->setId(save_id_);
      }
    } else if (name == "folder") {
      const auto folder_id = attr.value().toInt();
      if (folder_id > 0) {
        auto folder = Project::model().findItemById(folder_id);
        if (auto par = parent_mda.lock()) {
          par->setParent(folder);
        }
      }
    } else if (name == "open") {
      bool is_open = attr.value().toString().toLower() == "true";
      if (is_open) {
        global::sequence = shared_from_this();
      }
    } else {
      qWarning() << "Unhandled attribute" << name;
    }
  }

  while (stream.readNextStartElement()) {
    const auto name = stream.name().toString().toLower();
    if (name == "workarea") {
      if (!loadWorkArea(stream)) {
        qCritical() << "Failed to load workarea";
        return false;
      }
      stream.skipCurrentElement();
    } else if (name == "name") {
      setName(stream.readElementText());
    } else if (name == "width") {
      setWidth(stream.readElementText().toInt());
    } else if (name == "height") {
      setHeight(stream.readElementText().toInt());
    } else if (name == "framerate") {
      setFrameRate(stream.readElementText().toDouble());
    } else if (name == "frequency") {
      setAudioFrequency(stream.readElementText().toInt());
    } else if (name == "layout") {
      setAudioLayout(stream.readElementText().toInt());
    } else if (name == "track") {
      Track trk;
      if (trk.load(stream)) {
        tracks_.insert(trk.index_, trk);
      } else {
        qCritical() << "Failed to load Track";
        return false;
      }
    } else if (name == "clip") {
      auto clp = std::make_shared<Clip>(shared_from_this());
      if (clp->load(stream)) {
        // NOTE: ensure Tracks are loaded first otherwise they won't get used
        if (clp->isCreatedObject()) {
          auto created_obj = std::make_shared<ObjectClip>(*clp);
		  addClip(created_obj);
        } else {
          addClip(clp);
        }
      } else {
        qCritical() << "Failed to load clip";
        return false;
      }
    } else if (name == "marker") {
      auto mark = std::make_shared<Marker>();
      if (mark->load(stream)) {
        markers_.append(mark);
      } else {
        qCritical() << "Failed to read marker element";
        return false;
      }
    } else {
      qWarning() << "Unhandled element" << name;
      stream.skipCurrentElement();
    }
  }//while
  return true;
}
bool Sequence::save(QXmlStreamWriter& stream) const
{
  stream.writeStartElement("sequence");
  if (auto par = parent_mda.lock()) {
    stream.writeAttribute("id", QString::number(par->id()));
    stream.writeAttribute("folder", QString::number(par->parentItem()->id()));
  } else {
    chestnut::throwAndLog("Null parent Media");
  }
  stream.writeAttribute("open", this == global::sequence.get() ? "true" : "false");

  stream.writeStartElement("workarea");
  stream.writeAttribute("using", workarea_.using_ ? "true" : "false");
  stream.writeAttribute("enabled", workarea_.enabled_ ? "true" : "false");
  stream.writeAttribute("in", QString::number(workarea_.in_));
  stream.writeAttribute("out", QString::number(workarea_.out_));
  stream.writeEndElement();

  stream.writeTextElement("name", name_);
  stream.writeTextElement("width", QString::number(width_));
  stream.writeTextElement("height", QString::number(height_));
  stream.writeTextElement("framerate", QString::number(frame_rate_, 'g', 10));
  stream.writeTextElement("frequency", QString::number(audio_frequency_));
  stream.writeTextElement("layout", QString::number(audio_layout_));

  for (auto t : tracks_) {
    t.save(stream);
  }

  for (const auto& clp : clips_) {
    if (clp == nullptr) {
      qDebug() << "Null clip ptr";
      continue;
    }
    if (!clp->save(stream)) {
      chestnut::throwAndLog("Failed to save clip");
    }
  }

  for (const auto& mark : markers_) {
    if (mark == nullptr) {
      qDebug() << "Null marker ptr";
      continue;
    }
    if (!mark->save(stream)) {
      chestnut::throwAndLog("Failed to save marker");
    }
  }

  stream.writeEndElement();
  return true;
}


void Sequence::lockTrack(const int number, const bool lock)
{
  if (!tracks_.contains(number)) {
    tracks_.insert(number, Track(number));
  }
  tracks_[number].locked_ = lock;
}

void Sequence::enableTrack(const int number)
{
  if (!tracks_.contains(number)) {
    qWarning() << "No track to enable" << number;
    return;
  }
  tracks_[number].enabled_ = true;
}

void Sequence::disableTrack(const int number)
{
  if (!tracks_.contains(number)) {
    qWarning() << "No track to disable" << number;
    return;
  }
  tracks_[number].enabled_ = false;
}


void Sequence::addTrack(const int number, const bool video)
{
  Track t(number);
  t.name_ = video ? "Video" : "Audio";
  t.name_ += ":" + QString::number(abs(video ? number : number + 1)); //audio track index starts at 0
  tracks_.insert(number, t);
}


void Sequence::verifyTrackCount()
{
  // TODO: decide whether to remove tracks as they become empty or not

  // add missing video tracks
  auto vid_count = trackCount(true); // video tracks start at -1
  for (auto ix=-vid_count; ix < 0; ++ix) {
    if (!tracks_.contains(ix)) {
      addTrack(ix, true);
    }
  }

  // remove video Tracks no longer required
  if (!tracks_.isEmpty()) {
    auto key = tracks_.firstKey();
    while (key < -vid_count) {
      tracks_.remove(key);
      if (tracks_.empty()) {
        break;
      }
      key = tracks_.firstKey();
    }
  }

  // add missing audio tracks
  auto audio_count = trackCount(false);
  for (auto ix = 0; ix < audio_count; ++ix) {
    if (!tracks_.contains(ix)) {
      addTrack(ix, false);
    }
  }


  // remove audio Tracks no longer required
  if (!tracks_.isEmpty()) {
    auto key = tracks_.lastKey();
    while (key >= audio_count) {
      tracks_.remove(key);
      if (tracks_.empty()) {
        break;
      }
      key = tracks_.lastKey();
    }
  }
}


void Sequence::addSelection(const Selection& sel)
{
  if (trackLocked(sel.track)) {
    qDebug() << "Not able to add selection";
  } else {
    selections_.append(sel);
  }
}

int64_t Sequence::activeLength() const noexcept
{
  if (workarea_.using_) {
    return workarea_.out_ - workarea_.in_;
  }
  return endFrame();
}


QVector<project::Track> Sequence::audioTracks()
{
  return tracks(false);
}

QVector<project::Track> Sequence::videoTracks()
{
  return tracks(true);
}


bool Sequence::loadWorkArea(QXmlStreamReader& stream)
{
  for (const auto& attr : stream.attributes()) {
    const auto name = attr.name().toString().toLower();
    if (name == "using") {
      workarea_.using_ = attr.value().toString() == "true";
    } else if (name == "enabled") {
      workarea_.enabled_ = attr.value().toString() == "true";
    } else if (name == "in") {
      workarea_.in_ = attr.value().toInt();
    } else if (name == "out") {
      workarea_.out_ = attr.value().toInt();
    } else {
      qWarning() << "Unhandled workarea attribute" << name;
    }
  }
  return true;
}


QVector<project::Track> Sequence::tracks(const bool video)
{
  QVector<project::Track> trks;

  for (auto t : tracks_) {
    if ( (t.index_ < 0) == video) {
      trks.append(t);
    }
  }

  return trks;
}

std::pair<int64_t, int64_t> Sequence::trackLimits() const
{
  int64_t video_limit = 0;
  int64_t audio_limit = 0;

  for (const auto& clp : clips_) {
    if (clp == nullptr) {
      continue;
    }
    if ( (clp->mediaType() == ClipType::VISUAL) && (clp->timeline_info.track_ < video_limit) ) { // video clip
      video_limit = clp->timeline_info.track_;
    } else if (clp->timeline_info.track_ > audio_limit) {
      audio_limit = clp->timeline_info.track_;
    }
  }//for
  return {video_limit, audio_limit};
}

