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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "footage.h"

#include <mediahandling/mediahandling.h>
#include <regex>

extern "C" {
#include <libavformat/avformat.h>
}

#include "io/previewgenerator.h"
#include "project/clip.h"
#include "panels/project.h"


using project::FootageStreamPtr;
using media_handling::MediaProperty;


Footage::Footage(const Footage& cpy)
  : ProjectItem(cpy),
    length_(cpy.length_),
    proj_dir_(cpy.proj_dir_),
    save_id(0),
    folder_(cpy.folder_),
    speed_(cpy.speed_),
    preview_gen(cpy.preview_gen),
    in(cpy.in),
    out(cpy.out),
    using_inout(cpy.using_inout),
    ready_(cpy.ready_.load()),
    has_preview_(cpy.has_preview_.load()),
    url_(cpy.url_),
    parent_mda_(/*cpy.parent_mda_*/), // this parent is of the wrong media object
    video_tracks(cpy.video_tracks),
    audio_tracks(cpy.audio_tracks),
    import_as_sequence_(cpy.import_as_sequence_)
{

}

Footage::Footage(const std::shared_ptr<Media>& parent) : parent_mda_(parent)
{
}

Footage::Footage(QString url, const std::shared_ptr<Media>& parent, const bool import_as_sequence)
  : url_(std::move(url)),
    parent_mda_(parent),
    import_as_sequence_(import_as_sequence)
{
  try {
    // TODO: not keen on this. Revisit.
    const bool prev_scheme = media_handling::autoDetectImageSequences();
    media_handling::autoDetectImageSequences(import_as_sequence);
    media_source_ = media_handling::createSource(url_.toStdString());
    media_handling::autoDetectImageSequences(prev_scheme);
  }  catch (const std::exception& ex) {
    qWarning() << "Unable to create media_handling source, msg=" << ex.what();
    throw;
  }
  parseStreams();
}

void Footage::reset()
{
  if (preview_gen != nullptr) {
    try {
      preview_gen->cancel();
      preview_gen->wait();
    } catch (const std::exception& ex) {
      qCritical() << "Caught an exception, msg =" << ex.what();
    } catch (...) {
      qCritical() << "Caught an unknown exception";
    }
  }
  video_tracks.clear();
  audio_tracks.clear();
  ready_ = false;
}

bool Footage::isImage() const
{
  return (has_preview_ && !video_tracks.empty()) && video_tracks.front()->infinite_length && (audio_tracks.empty());
}


std::optional<media_handling::StreamType> Footage::visualType() const
{
  Q_ASSERT(media_source_);
  if (!media_source_->visualStreams().empty()) {
    if (auto v_s = media_source_->visualStream(0)) {
      return v_s->type();
    }
  }
  return {};
}

bool Footage::hasAudio() const
{
  Q_ASSERT(media_source_);
  return !media_source_->audioStreams().empty();
}

void Footage::setParent(std::shared_ptr<Media> mda)
{
  parent_mda_ = mda;
}

/**
 * @brief   Retrieve the location of the footage's source
 * @return  Path
 */
QString Footage::location() const
{
  if (import_as_sequence_) {
    if (auto sqn_url = media_handling::utils::generateSequencePattern(url_.toStdString())) {
      return QString::fromStdString(sqn_url.value());
    } else {
      qWarning() << "No sequence pattern found, fileName =" << url_;
    }
  }
  return url_;
}


void Footage::parseStreams()
{
  if (!media_source_) {
    qWarning() << "No media to parse";
  }

  video_tracks.clear();
  for (auto[key, stream] : media_source_->visualStreams()) {
    video_tracks.insert(key, std::make_shared<project::FootageStream>(stream));
  }

  audio_tracks.clear();

  for (auto[key, stream] : media_source_->audioStreams()) {
    audio_tracks.insert(key, std::make_shared<project::FootageStream>(stream));
  }

  bool is_okay = false;
  length_ = media_source_->property<int64_t>(MediaProperty::DURATION, is_okay);
  if (!is_okay) {
    constexpr auto msg = "Failed to retrieve footage duration";
    qCritical() << msg;
    throw std::runtime_error(msg);
  }
}

bool Footage::addVideoTrack(project::FootageStreamPtr track)
{
  if (!track) {
    return false;
  }
  video_tracks.append(std::move(track));
  return true;
}

QVector<project::FootageStreamPtr> Footage::videoTracks() const
{
  return video_tracks;
}

bool Footage::addAudioTrack(project::FootageStreamPtr track)
{
  if (!track) {
    return false;
  }
  audio_tracks.append(std::move(track));
  return true;
}

QVector<project::FootageStreamPtr> Footage::audioTracks() const
{
  return audio_tracks;
}

bool Footage::load(QXmlStreamReader& stream)
{
  // attributes
  for (const auto& attr : stream.attributes()) {
    auto name = attr.name().toString().toLower();
    if (name == "folder") {
      folder_ = attr.value().toInt(); // Media::parent.id
      if (auto par = parent_mda_.lock()) {
        auto folder = Project::model().findItemById(folder_);
        par->setParent(folder);
      }
    } else if (name == "id") {
      save_id = attr.value().toInt();
      if (auto par = parent_mda_.lock()) {
        par->setId(save_id);
      }
    } else if (name == "using_inout") {
      using_inout = attr.value().toString() == "true";
    } else if (name == "in") {
      in = attr.value().toLong();
    } else if (name == "out") {
      out = attr.value().toLong();
    } else {
      qWarning() << "Unknown attribute" << name;
    }
  }

  //elements
  bool okay;
  while (stream.readNextStartElement()) {
    auto elem_name = stream.name().toString().toLower();
    if ( (elem_name == "video") || (elem_name == "audio") || (elem_name == "image")) {
      auto ms = std::make_shared<project::FootageStream>();
      ms->load(stream);
      if ( (elem_name == "video") || (elem_name == "image") ) {
        video_tracks.append(ms);
      } else {
        audio_tracks.append(ms);
      }
    } else if (elem_name == "name") {
      setName(stream.readElementText());
    } else if (elem_name == "url") {
      url_ = stream.readElementText();
      try {
        media_source_ = media_handling::createSource(url_.toStdString());
      }  catch (const std::runtime_error& ex) {
        qWarning() << "Source file failed to load:" << ex.what();
      }
    } else if (elem_name == "duration") {
      length_ = stream.readElementText().toLong();
    } else if (elem_name == "marker") {
      auto mrkr = std::make_shared<Marker>();
      mrkr->load(stream);
      markers_.append(mrkr);
    } else if (elem_name == "speed") {
      if (speed_ = stream.readElementText().toDouble(&okay); !okay) {
        throw std::runtime_error("Failed to extract speed element");
      }
    } else {
      qWarning() << "Unhandled element" << elem_name;
      stream.skipCurrentElement();
    }
  }

  if (media_source_) {
    for (int i = 0; i < audio_tracks.size(); ++i) {
      auto strm(media_source_->audioStream(i));
      Q_ASSERT(strm);
      Q_ASSERT(audio_tracks.at(i));
      audio_tracks.at(i)->setStreamInfo(strm);
    }

    for (int i = 0; i < video_tracks.size(); ++i) {
      auto strm(media_source_->visualStream(i));
      Q_ASSERT(strm);
      Q_ASSERT(video_tracks.at(i));
      video_tracks.at(i)->setStreamInfo(strm);
    }
  } else {
    qWarning() << "Unable to set info for footage streams. Missing source file url =" << url_;
  }

  //TODO: check what this does
  //      if (!QFileInfo::exists(url)) { // if path is not absolute
  //        QString proj_dir_test = proj_dir.absoluteFilePath(url);
  //        QString internal_proj_dir_test = internal_proj_dir.absoluteFilePath(url);

  //        if (QFileInfo::exists(proj_dir_test)) { // if path is relative to the project's current dir
  //          url = proj_dir_test;
  //          qInfo() << "Matched" << attr.value().toString() << "relative to project's current directory";
  //        } else if (QFileInfo::exists(internal_proj_dir_test)) { // if path is relative to the last directory the project was saved in
  //          url = internal_proj_dir_test;
  //          qInfo() << "Matched" << attr.value().toString() << "relative to project's internal directory";
  //        } else if (url.contains('%')) {
  //          // hack for image sequences (qt won't be able to find the URL with %, but ffmpeg may)
  //          url = internal_proj_dir_test;
  //          qInfo() << "Guess image sequence" << attr.value().toString() << "path to project's internal directory";
  //        } else {
  //          qInfo() << "Failed to match" << attr.value().toString() << "to file";
  //        }
  //      } else {
  //        qInfo() << "Matched" << attr.value().toString() << "with absolute path";
  //      }
  return true;
}

bool Footage::save(QXmlStreamWriter& stream) const
{
  stream.writeStartElement("footage");
  if (auto par = parent_mda_.lock()) {
    if (par->parentItem() == nullptr) {
      chestnut::throwAndLog("Parent Media is unlinked");
    }
    stream.writeAttribute("id", QString::number(par->id()));
    stream.writeAttribute("folder", QString::number(par->parentItem()->id()));
  } else {
    chestnut::throwAndLog("Null Media parent");
  }
  stream.writeAttribute("using_inout", using_inout ? "true" : "false");
  stream.writeAttribute("in", QString::number(in));
  stream.writeAttribute("out", QString::number(out));

  stream.writeTextElement("name", name_);
  stream.writeTextElement("url", QDir(url_).absolutePath());
  stream.writeTextElement("duration", QString::number(length_));
  stream.writeTextElement("speed", QString::number(speed_));

  for (const auto& ms : video_tracks) {
    if (!ms) {
      continue;
    }
    ms->save(stream);
  }

  for (const auto& ms : audio_tracks) {
    if (!ms) {
      continue;
    }
    ms->save(stream);
  }

  for (auto& mark : markers_) {
    if (!mark) {
      continue;
    }
    mark->save(stream);
  }
  stream.writeEndElement();
  return true;
}

constexpr long lengthToFrames(const int64_t length, const double frame_rate, const double speed)
{
  Q_ASSERT(AV_TIME_BASE != 0);
  if (length >= 0) {
    return static_cast<long>(std::floor( (static_cast<double>(length) / AV_TIME_BASE)
                                         * (frame_rate / speed) ));
  }
  return 0;
}

bool Footage::generatePreviews()
{
  //TODO: iterate through streams generating previews for each
  return false;
}

bool Footage::isMissing() const noexcept
{
  return media_source_ == nullptr;
}

long Footage::totalLengthInFrames(const double frame_rate) const noexcept
{
  Q_ASSERT(!qFuzzyIsNull(speed_));

  if (length_ >= 0) {
    return lengthToFrames(length_, frame_rate, speed_);
  }
  return 0;
}


long Footage::activeLengthInFrames(const double frame_rate) const noexcept
{
  if (using_inout) {
    return qMax(out - in, 0L);
  }
  return totalLengthInFrames(frame_rate);
}

FootageStreamPtr Footage::video_stream_from_file_index(const int index)
{
  return get_stream_from_file_index(true, index);
}

FootageStreamPtr Footage::audio_stream_from_file_index(const int index)
{
  return get_stream_from_file_index(false, index);
}

bool Footage::has_stream_from_file_index(const int index)
{
  bool found = video_stream_from_file_index(index) != nullptr;
  found |= audio_stream_from_file_index(index) != nullptr;
  return found;
}


bool Footage::has_video_stream_from_file_index(const int index)
{
  return video_stream_from_file_index(index)!= nullptr;
}

FootageStreamPtr Footage::get_stream_from_file_index(const bool video, const int index)
{
  FootageStreamPtr stream;
  auto finder = [index] (auto tracks){
    for (const auto& track : tracks) {
      if (track->file_index == index) {
        return track;
      }
    }
    return FootageStreamPtr();
  };
  if (video) {
    stream = finder(video_tracks);
  } else {
    stream = finder(audio_tracks);
  }

  return stream;
}
