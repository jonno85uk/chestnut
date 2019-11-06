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
    video_tracks(cpy.video_tracks),
    audio_tracks(cpy.audio_tracks),
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
    parent_mda_(/*cpy.parent_mda_*/) // this parent is of the wrong media object
{

}

Footage::Footage(const std::shared_ptr<Media>& parent) : parent_mda_(parent)
{
}

Footage::Footage(QString url, const std::shared_ptr<Media>& parent)
  : url_(std::move(url)),
    parent_mda_(parent)
{
  media_source_ = media_handling::createSource(url_.toStdString());
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
  return url_;
}


void Footage::parseStreams()
{
  if (!media_source_) {
    qWarning() << "No media to parse";
  }

  video_tracks.clear();
  for (auto[key, stream] : media_source_->visualStreams()) {
    auto ftg_stream = std::make_shared<project::FootageStream>(stream);
    video_tracks.insert(key, ftg_stream);
  }

  audio_tracks.clear();

  for (auto[key, stream] : media_source_->audioStreams()) {
    auto ftg_stream = std::make_shared<project::FootageStream>(stream);
    audio_tracks.insert(key, ftg_stream);
  }

  bool is_okay = false;
  length_ = media_source_->property<int64_t>(MediaProperty::DURATION, is_okay);
  if (!is_okay) {
    qWarning() << "Failed to retrieve footage duration";
  }


  //    gsl::span<AVStream*> streams(fmt_ctx->streams, fmt_ctx->nb_streams);
  //    for (int i=0; i < streams.size(); ++i) {
  //      AVStream* const stream = streams.at(i);
  //      if ( (stream == nullptr) || (stream->codecpar == nullptr) ) {
  //        qCritical() << "AV Stream instance(s) are null";
  //        continue;
  //      }
  //      // Find the decoder for the video stream
  //      if (avcodec_find_decoder(stream->codecpar->codec_id) == nullptr) {
  //        qCritical() << "Unsupported codec in stream" << i << "of file" << ftg->name();
  //      } else {
  //        // TODO: use mediastream
  //        auto ms = std::make_shared<FootageStream>();
  //        ms->preview_done = false;
  //        ms->file_index = i;
  //        ms->enabled = true;

  //        bool append = false;

  //        if ( (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
  //            && (stream->codecpar->width > 0)
  //            && (stream->codecpar->height > 0) ) {
  //          ms->type_ = StreamType::VIDEO;
  //          // heuristic to determine if video is a still image
  //          if ( (stream->avg_frame_rate.den == 0)
  //              && (stream->codecpar->codec_id != AV_CODEC_ID_DNXHD) ) { // silly hack but this is the only scenario i've seen this
  //            if (ftg->location().contains('%')) {
  //              // must be an image sequence
  //              ms->infinite_length = false;
  //              ms->video_frame_rate = 25;
  //            } else {
  //              ms->infinite_length = true;
  //              contains_still_image = true;
  //              ms->video_frame_rate = 0;
  //            }
  //          } else {
  //            ms->infinite_length = false;
  //            // using ffmpeg's built-in heuristic
  //            ms->video_frame_rate = av_q2d(av_guess_frame_rate(fmt_ctx, stream, nullptr));
  //          }

  //          ms->video_width = stream->codecpar->width;
  //          ms->video_height = stream->codecpar->height;
  //          append = true;
  //        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
  //          ms->type_ = StreamType::AUDIO;
  //          ms->audio_channels = stream->codecpar->channels;
  //          ms->audio_layout = stream->codecpar->channel_layout;
  //          ms->audio_frequency = stream->codecpar->sample_rate;

  //          append = true;
  //        }

  //        if (append) {
  //          QVector<FootageStreamPtr>& stream_list = (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
  //                                                   ? ftg->audio_tracks : ftg->video_tracks;
  //          for (int j=0;j<stream_list.size();j++) {
  //            if (stream_list.at(j)->file_index == i) {
  //              stream_list[j] = ms;
  //              append = false;
  //            }
  //          }
  //          if (append) {
  //            stream_list.append(ms);
  //          }
  //        }
  //      }
  //    }
  //    ftg->length_ = fmt_ctx->duration;
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
    if ( (elem_name == "video") || (elem_name == "audio") ) {
      auto ms = std::make_shared<project::FootageStream>();
      ms->load(stream);
      if (elem_name == "video") {
        video_tracks.append(ms);
      } else {
        audio_tracks.append(ms);
      }
    } else if (elem_name == "name") {
      setName(stream.readElementText());
    } else if (elem_name == "url") {
      url_ = stream.readElementText();
      media_source_ = media_handling::createSource(url_.toStdString());
    } else if (elem_name == "duration") {
      length_ = stream.readElementText().toLong();
    } else if (elem_name == "marker") {
      auto mrkr = std::make_shared<Marker>();
      mrkr->load(stream);
      markers_.append(mrkr);
    } else if (elem_name == "speed") {
      if (speed_ = stream.readElementText().toDouble(&okay); !okay) {

      }
    } else {
      qWarning() << "Unhandled element" << elem_name;
      stream.skipCurrentElement();
    }
  }

  for (int i = 0; i < audio_tracks.size(); ++i) {
    auto strm(media_source_->audioStream(i));
    Q_ASSERT(strm);
    audio_tracks.at(i)->setStreamInfo(strm);
  }

  for (int i = 0; i < video_tracks.size(); ++i) {
    auto strm(media_source_->visualStream(i));
    Q_ASSERT(strm);
    video_tracks.at(i)->setStreamInfo(strm);
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
    for (auto track : tracks) {
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
