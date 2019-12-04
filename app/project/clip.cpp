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
#include "clip.h"

#include <QtMath>
#include <filesystem>

#include "project/effect.h"
#include "project/transition.h"
#include "project/footage.h"
#include "io/config.h"
#include "playback/playback.h"
#include "playback/audio.h"
#include "project/sequence.h"
#include "panels/panelmanager.h"
#include "project/media.h"
#include "io/clipboard.h"
#include "io/avtogl.h"
#include "undo.h"
#include "debug.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

using project::FootageStreamPtr;
using panels::PanelManager;
using project::ScanMethod;

constexpr bool WAIT_ON_CLOSE = true;
constexpr AVSampleFormat SAMPLE_FORMAT = AV_SAMPLE_FMT_S16;
constexpr int AUDIO_SAMPLES = 2048;
constexpr int AUDIO_BUFFER_PADDING = 2048;
int32_t Clip::next_id = 0;

namespace  {
  constexpr auto ERR_LEN = 256;
  std::array<char, ERR_LEN> err;
}

double bytes_to_seconds(const int nb_bytes, const int nb_channels, const int sample_rate) {
  return (static_cast<double>(nb_bytes >> 1) / nb_channels / sample_rate);
}

Clip::Clip(SequencePtr s) :
  SequenceItem(project::SequenceItemType::CLIP),
  sequence(std::move(s)),
  replaced(false),
  ignore_reverse(false),
  use_existing_frame(false),
  filter_graph(nullptr),
  fbo(nullptr),
  id_(next_id++)
{
  media_handling_.pkt_ = av_packet_alloc();
  timeline_info.autoscale = global::config.autoscale_by_default;
  reset();
}


Clip::~Clip()
{
  if (is_open) {
    close(WAIT_ON_CLOSE);
  }

  av_packet_free(&media_handling_.pkt_);
}


ClipPtr Clip::copy(SequencePtr s)
{
  auto copy_clip = std::make_shared<Clip>(std::move(s));

  copy_clip->timeline_info = timeline_info;

  for (auto& eff : effects) {
    if (eff == nullptr) {
      qWarning() << "Null Effect instance";
      continue;
    }
    copy_clip->effects.append(eff->copy(copy_clip));
  }

  // leave id_ modification and linked population for callees

  copy_clip->timeline_info.cached_fr = (this->sequence == nullptr) ? timeline_info.cached_fr : this->sequence->frameRate();

  // copy transitions
  if (auto trans = getTransition(ClipTransitionType::OPENING)) {
    copy_clip->setTransition(trans->meta, ClipTransitionType::OPENING, trans->get_length());
  }
  if (auto trans = getTransition(ClipTransitionType::CLOSING)) {
    copy_clip->setTransition(trans->meta, ClipTransitionType::CLOSING, trans->get_length());
  }

  copy_clip->recalculateMaxLength();

  return copy_clip;
}


ClipPtr Clip::copyPreserveLinks(SequencePtr s)
{
  auto clp = copy(std::move(s));
  clp->id_ = id_;
  clp->linked = linked;
  return clp;
}

QString Clip::name() const
{
  if (timeline_info.media == nullptr) {
    return SequenceItem::name();
  } else if (timeline_info.media->type() == MediaType::FOOTAGE) {
    if (const auto ftg = timeline_info.media->object<Footage>()) {
      return std::filesystem::path(ftg->location().toStdString()).filename().c_str();
    }
  }
  return "NoName";
}

bool Clip::isActive(const long playhead)
{
  if (timeline_info.enabled) {
    if (sequence != nullptr) {
      if ( timelineInWithTransition() < (playhead + (ceil(sequence->frameRate()*2)) ) )  {                      //TODO:what are we checking?
        if (timelineOutWithTransition() > playhead) {                                                            //TODO:what are we checking?
          if ( (playhead - timelineInWithTransition() + clipInWithTransition()) < maximumLength() ){ //TODO:what are we checking?
            return true;
          }
        }
      }
    }
  }
  return false;
}

/**
 * @brief Identify if the clip is being cached
 * @return true==caching used
 */
bool Clip::usesCacher() const
{
  return (( (timeline_info.media == nullptr) && (timeline_info.track_ >= 0) )
          || ( (timeline_info.media != nullptr) && (timeline_info.media->type() == MediaType::FOOTAGE)));
}

/**
 * @brief open_worker
 * @return true==success
 */
bool Clip::openWorker()
{
  if (timeline_info.media == nullptr) {
    if (timeline_info.track_ >= 0) {
      media_handling_.frame_ = av_frame_alloc();
      media_handling_.frame_->format = SAMPLE_FORMAT;
      media_handling_.frame_->channel_layout = sequence->audioLayout();
      media_handling_.frame_->channels = av_get_channel_layout_nb_channels(media_handling_.frame_->channel_layout);
      media_handling_.frame_->sample_rate = current_audio_freq();
      media_handling_.frame_->nb_samples = AUDIO_SAMPLES;
      av_frame_make_writable(media_handling_.frame_);
      if (av_frame_get_buffer(media_handling_.frame_, 0)) {
        qCritical() << "Could not allocate buffer for tone clip";
      }
      audio_playback.reset = true;
    }
  } else if (timeline_info.media->type() == MediaType::FOOTAGE) {
    // opens file resource for FFmpeg and prepares Clip struct for playback
    auto ftg = timeline_info.media->object<Footage>();
    auto filename = ftg->location().toUtf8().data();

    FootageStreamPtr ms;
    if (mediaType() == ClipType::VISUAL) {
      ms = ftg->video_stream_from_file_index(timeline_info.media_stream);
    } else {
      ms = ftg->audio_stream_from_file_index(timeline_info.media_stream);
    }

    if (ms == nullptr) {
      qCritical() << "Footage stream is NULL";
      return false;
    }
    int errCode = avformat_open_input(
                    &media_handling_.format_ctx_,
                    filename,
                    nullptr,
                    nullptr
                    );
    if (errCode != 0) {
      char err[1024];
      av_strerror(errCode, err, 1024);
      qCritical() << "Could not open" << filename << "-" << err;
      return false;
    }

    errCode = avformat_find_stream_info(media_handling_.format_ctx_, nullptr);
    if (errCode < 0) {
      char err[1024];
      av_strerror(errCode, err, 1024);
      qCritical() << "Could not open" << filename << "-" << err;
      return false;
    }

    av_dump_format(media_handling_.format_ctx_, 0, filename, 0);

    media_handling_.stream_ = media_handling_.format_ctx_->streams[ms->file_index];
    if ( (media_handling_.stream_ == nullptr) || (media_handling_.stream_->codecpar == nullptr) ) {
      qCritical() << "Stream Info instance(s) are null";
      return false;
    }
    media_handling_.codec_ = avcodec_find_decoder(media_handling_.stream_->codecpar->codec_id);
    media_handling_.codec_ctx_ = avcodec_alloc_context3(media_handling_.codec_);
    avcodec_parameters_to_context(media_handling_.codec_ctx_, media_handling_.stream_->codecpar);

    if (ms->infinite_length) {
      max_queue_size = 1;
    } else {
      max_queue_size = 0;
      if (global::config.upcoming_queue_type == FRAME_QUEUE_TYPE_FRAMES) {
        max_queue_size += qCeil(global::config.upcoming_queue_size);
      } else {
        max_queue_size += qCeil(ms->video_frame_rate * ftg->speed_ * global::config.upcoming_queue_size);
      }
      if (global::config.previous_queue_type == FRAME_QUEUE_TYPE_FRAMES) {
        max_queue_size += qCeil(global::config.previous_queue_size);
      } else {
        max_queue_size += qCeil(ms->video_frame_rate * ftg->speed_ * global::config.previous_queue_size);
      }
    }


    if (ms->fieldOrder() != media_handling::FieldOrder::PROGRESSIVE) {
      max_queue_size *= 2;
    }

    media_handling_.opts_ = nullptr;

    // optimized decoding settings
    if ((media_handling_.stream_->codecpar->codec_id != AV_CODEC_ID_PNG &&
         media_handling_.stream_->codecpar->codec_id != AV_CODEC_ID_APNG &&
         media_handling_.stream_->codecpar->codec_id != AV_CODEC_ID_TIFF
     #ifndef DISABLE_PSD
         && media_handling_.stream_->codecpar->codec_id != AV_CODEC_ID_PSD)
    #else
         )
    #endif
        || !global::config.disable_multithreading_for_images) {
      av_dict_set(&media_handling_.opts_, "threads", "auto", 0);
    }
    if (media_handling_.stream_->codecpar->codec_id == AV_CODEC_ID_H264) {
      av_dict_set(&media_handling_.opts_, "tune", "fastdecode", 0);
      av_dict_set(&media_handling_.opts_, "tune", "zerolatency", 0);
    }

    // Open codec
    if (avcodec_open2(media_handling_.codec_ctx_, media_handling_.codec_, &media_handling_.opts_) < 0) {
      qCritical() << "Could not open codec";
    }

    // allocate filtergraph
    filter_graph = avfilter_graph_alloc();
    if (filter_graph == nullptr) {
      qCritical() << "Could not create filtergraph";
    }
    char filter_args[512];

    if (media_handling_.stream_->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      snprintf(filter_args, sizeof(filter_args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
               media_handling_.stream_->codecpar->width,
               media_handling_.stream_->codecpar->height,
               media_handling_.stream_->codecpar->format,
               media_handling_.stream_->time_base.num,
               media_handling_.stream_->time_base.den,
               media_handling_.stream_->codecpar->sample_aspect_ratio.num,
               media_handling_.stream_->codecpar->sample_aspect_ratio.den
               );

      avfilter_graph_create_filter(&buffersrc_ctx, avfilter_get_by_name("buffer"), "in", filter_args, nullptr, filter_graph);
      avfilter_graph_create_filter(&buffersink_ctx, avfilter_get_by_name("buffersink"), "out", nullptr, nullptr, filter_graph);

      AVFilterContext* last_filter = buffersrc_ctx;

      if (ms->fieldOrder() != media_handling::FieldOrder::PROGRESSIVE) {
        AVFilterContext* yadif_filter;
        char yadif_args[100];
        snprintf(yadif_args,
                 sizeof(yadif_args),
                 "mode=3:parity=%d",
                 ((ms->fieldOrder() == media_handling::FieldOrder::TOP_FIRST) ? 0 : 1));
        //TODO: check return values
        constexpr auto deinterlacer = "yadif";
        avfilter_graph_create_filter(&yadif_filter,
                                     avfilter_get_by_name(deinterlacer),
                                     deinterlacer,
                                     yadif_args,
                                     nullptr,
                                     filter_graph);
        avfilter_link(last_filter, 0, yadif_filter, 0);
        last_filter = yadif_filter;
      }

      /* stabilization code */
      bool stabilize = false;
      if (stabilize) {
        AVFilterContext* stab_filter;
        int stab_ret = avfilter_graph_create_filter(&stab_filter,
                                                    avfilter_get_by_name("vidstabtransform"),
                                                    "vidstab",
                                                    "input=/media/matt/Home/samples/transforms.trf", //FIXME: hardcoded location
                                                    nullptr,
                                                    filter_graph);
        if (stab_ret < 0) {
          char err[100];
          av_strerror(stab_ret, err, sizeof(err));
        } else {
          avfilter_link(last_filter, 0, stab_filter, 0);
          last_filter = stab_filter;
        }
      }

      enum AVPixelFormat valid_pix_fmts[] = {
        AV_PIX_FMT_RGB24,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_NONE
      };

      pix_fmt = avcodec_find_best_pix_fmt_of_list(valid_pix_fmts, static_cast<enum AVPixelFormat>(media_handling_.stream_->codecpar->format), 1, nullptr);
      const char* chosen_format = av_get_pix_fmt_name(static_cast<enum AVPixelFormat>(pix_fmt));
      char format_args[100];
      snprintf(format_args, sizeof(format_args), "pix_fmts=%s", chosen_format);

      AVFilterContext* format_conv;
      avfilter_graph_create_filter(&format_conv, avfilter_get_by_name("format"), "fmt", format_args, nullptr, filter_graph);
      avfilter_link(last_filter, 0, format_conv, 0);

      avfilter_link(format_conv, 0, buffersink_ctx, 0);

      avfilter_graph_config(filter_graph, nullptr);
    } else if (media_handling_.stream_->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      if (media_handling_.codec_ctx_->channel_layout == 0) {
        media_handling_.codec_ctx_->channel_layout = av_get_default_channel_layout(media_handling_.stream_->codecpar->channels);
      }

      // set up cache
      queue.append(av_frame_alloc());
      if (timeline_info.reverse) {
        AVFrame* reverse_frame = av_frame_alloc();

        reverse_frame->format = SAMPLE_FORMAT;
        reverse_frame->nb_samples = current_audio_freq()*2;
        reverse_frame->channel_layout = sequence->audioLayout();
        reverse_frame->channels = av_get_channel_layout_nb_channels(sequence->audioLayout());
        av_frame_get_buffer(reverse_frame, 0);

        queue.append(reverse_frame);
      }

      snprintf(filter_args, sizeof(filter_args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64,
               media_handling_.stream_->time_base.num,
               media_handling_.stream_->time_base.den,
               media_handling_.stream_->codecpar->sample_rate,
               av_get_sample_fmt_name(media_handling_.codec_ctx_->sample_fmt),
               media_handling_.codec_ctx_->channel_layout
               );

      avfilter_graph_create_filter(&buffersrc_ctx, avfilter_get_by_name("abuffer"), "in", filter_args, nullptr, filter_graph);
      avfilter_graph_create_filter(&buffersink_ctx, avfilter_get_by_name("abuffersink"), "out", nullptr, nullptr, filter_graph);

      enum AVSampleFormat sample_fmts[] = { SAMPLE_FORMAT,  static_cast<AVSampleFormat>(-1) };
      if (av_opt_set_int_list(buffersink_ctx, "sample_fmts", sample_fmts, -1, AV_OPT_SEARCH_CHILDREN) < 0) {
        qCritical() << "Could not set output sample format";
      }

      int64_t channel_layouts[] = { AV_CH_LAYOUT_STEREO, static_cast<AVSampleFormat>(-1) };
      if (av_opt_set_int_list(buffersink_ctx, "channel_layouts", channel_layouts, -1, AV_OPT_SEARCH_CHILDREN) < 0) {
        qCritical() << "Could not set output sample format";
      }

      int target_sample_rate = current_audio_freq();

      double playback_speed = timeline_info.speed * ftg->speed_;

      if (qFuzzyCompare(playback_speed, 1.0)) {
        avfilter_link(buffersrc_ctx, 0, buffersink_ctx, 0);
      } else if (timeline_info.maintain_audio_pitch) {
        AVFilterContext* previous_filter = buffersrc_ctx;
        AVFilterContext* last_filter = buffersrc_ctx;

        char speed_param[10];

        double base = (playback_speed > 1.0) ? 2.0 : 0.5;

        double speedlog = log(playback_speed) / log(base);
        int whole2 = qFloor(speedlog);
        speedlog -= whole2;

        if (whole2 > 0) {
          snprintf(speed_param, sizeof(speed_param), "%f", base);
          for (int i=0;i<whole2;i++) {
            AVFilterContext* tempo_filter = nullptr;
            avfilter_graph_create_filter(&tempo_filter, avfilter_get_by_name("atempo"), "atempo", speed_param, nullptr, filter_graph);
            avfilter_link(previous_filter, 0, tempo_filter, 0);
            previous_filter = tempo_filter;
          }
        }

        snprintf(speed_param, sizeof(speed_param), "%f", qPow(base, speedlog));
        last_filter = nullptr;
        avfilter_graph_create_filter(&last_filter, avfilter_get_by_name("atempo"), "atempo", speed_param, nullptr, filter_graph);
        avfilter_link(previous_filter, 0, last_filter, 0);
        //				}

        avfilter_link(last_filter, 0, buffersink_ctx, 0);
      } else {
        target_sample_rate = qRound64(target_sample_rate / playback_speed);
        avfilter_link(buffersrc_ctx, 0, buffersink_ctx, 0);
      }

      int sample_rates[] = { target_sample_rate, 0 };
      if (av_opt_set_int_list(buffersink_ctx, "sample_rates", sample_rates, 0, AV_OPT_SEARCH_CHILDREN) < 0) {
        qCritical() << "Could not set output sample rates";
      }

      avfilter_graph_config(filter_graph, nullptr);

      audio_playback.reset = true;
    }

    media_handling_.frame_ = av_frame_alloc();
  }

  for (const auto& eff : effects) {
    eff->open();
  }

  finished_opening = true;

  qInfo() << "Clip opened on track" << timeline_info.track_.load();
  return true;
}

/**
 * @brief Free resources made via libav
 */
void Clip::closeWorker()
{
  finished_opening = false;

  if ( (timeline_info.media != nullptr) && (timeline_info.media->type() == MediaType::FOOTAGE) ) {
    clearQueue();
    // clear resources allocated via libav
    avfilter_graph_free(&filter_graph);
    avcodec_close(media_handling_.codec_ctx_);
    avcodec_free_context(&media_handling_.codec_ctx_);
    av_dict_free(&media_handling_.opts_);
    avformat_close_input(&media_handling_.format_ctx_);
  }

  av_frame_free(&media_handling_.frame_);

  reset();

  qInfo() << "Clip closed on track" << timeline_info.track_.load() << "name: " << name();
}



/**
 * @brief Open clip and allocate necessary resources
 * @param open_multithreaded
 * @return true==success
 */
bool Clip::open(const bool open_multithreaded) {
  if (is_open) {
    return false;
  }
  if (usesCacher()) {
    multithreaded = open_multithreaded;
    if (multithreaded) {
      if (open_lock.tryLock()) {
        this->start((mediaType() == ClipType::VISUAL) ? QThread::HighPriority : QThread::TimeCriticalPriority);
      }
    } else {
      finished_opening = false;
      is_open = true;
      openWorker();
    }
  } else {
    is_open = true;
  }
  return true;
}

/**
 * @brief mediaOpen
 * @return  true==clip's media has been opened
 */
bool Clip::mediaOpen() const
{
  return (timeline_info.media != nullptr) && (timeline_info.media->type() == MediaType::FOOTAGE) && (finished_opening);
}

/**
 * @brief Close this clip and free up resources
 * @param wait  Wait on cache?
 */
void Clip::close(const bool wait) {
  if (is_open) {
    // destroy opengl texture in main thread
    if (texture != nullptr) {
      texture = nullptr;
    }

    for (const auto& eff : effects) {
      if ( (eff != nullptr) && eff->is_open()) {
        eff->close();
      }
    }

    if (fbo != nullptr) {
      delete fbo[0];
      delete fbo[1];
      delete [] fbo;
      fbo = nullptr;
    }

    if (usesCacher()) {
      if (multithreaded) {
        cache_info.caching = false;
        can_cache.wakeAll();
        if (wait) {
          open_lock.lock();
          open_lock.unlock();
        }
      } else {
        closeWorker();
      }
    } else {
      if (timeline_info.media && (timeline_info.media->type() == MediaType::SEQUENCE)) {
        if (auto seq = timeline_info.media->object<Sequence>()) {
          seq->closeActiveClips();
        }
      }

      is_open = false;
    }
  }
}


/**
 * @brief Close this clip and free up resources whilst waiting
 */
void Clip::closeWithWait() {
  close(true);
}


/**
 * @brief Cache the clip at a certian point
 * @param playhead
 * @param reset
 * @param scrubbing
 * @param nests
 * @return  true==cached
 */
bool Clip::cache(const long playhead, const bool do_reset, const bool scrubbing, QVector<ClipPtr>& nests)
{
  if (!usesCacher()) {
    qWarning() << "Not using caching";
    return false;
  }

  if (multithreaded) {
    cache_info.playhead = playhead;
    cache_info.reset = do_reset;
    cache_info.nests = nests;
    cache_info.scrubbing = scrubbing;
    if (cache_info.reset && !queue.empty()) {
      cache_info.interrupt = true;
    }

    can_cache.wakeAll();
  } else {
    cache_worker(playhead, do_reset, scrubbing, nests);
  }
  return true;
}

/**
 * @brief Nudge the clip
 * @param pos The amount + direction to nudge the clip
 * @return true==clips position nudged
 */
bool Clip::nudge(const int pos)
{
  // TODO: check limits
  timeline_info.in += pos;
  timeline_info.out += pos;
  return true;
}


bool Clip::setTransition(const EffectMeta& meta,
                         const ClipTransitionType type,
                         const long length,
                         const ClipPtr& secondary)
{
  if (this->length() < 1) {
    qWarning() << "Cannot set Transition for Clip of length 0";
    return false;
  }

  switch (type) {
    case ClipTransitionType::BOTH:
      transition_.opening_ = get_transition_from_meta(shared_from_this(), nullptr, meta, true);
      transition_.closing_ = get_transition_from_meta(shared_from_this(), nullptr, meta, true);
      if ( (transition_.opening_ != nullptr) && (transition_.closing_ != nullptr) ) {
        if ( (length * 2) > this->length()) {
          // Fit transitions into clip length
          transition_.opening_->setLength(length/2);
          transition_.closing_->setLength(length/2);
        } else {
          transition_.opening_->setLength(length);
          transition_.closing_->setLength(length);
        }
        return true;
      }
      break;
    case ClipTransitionType::CLOSING:
      transition_.closing_ = get_transition_from_meta(shared_from_this(), secondary, meta, true);
      if (transition_.closing_ != nullptr) {
        transition_.closing_->setLength(qMin(length, this->length()));
        return true;
      }
      break;
    case ClipTransitionType::OPENING:
      transition_.opening_ = get_transition_from_meta(shared_from_this(), secondary, meta, true);
      if (transition_.opening_ != nullptr) {
        transition_.opening_->setLength(qMin(length, this->length()));
        return true;
      }
      break;
    default:
      qWarning() << "Unhandled Transition type";
      break;
  }
  return false;
}


void Clip::deleteTransition(const ClipTransitionType type)
{
  switch (type) {
    case ClipTransitionType::BOTH:
      transition_.opening_ = nullptr;
      transition_.closing_ = nullptr;
      break;
    case ClipTransitionType::CLOSING:
      transition_.closing_ = nullptr;
      break;
    case ClipTransitionType::OPENING:
      transition_.opening_ = nullptr;
      break;
  }
}


TransitionPtr Clip::getTransition(const ClipTransitionType type) const
{
  switch (type) {
    case ClipTransitionType::OPENING:
      return transition_.opening_;
    case ClipTransitionType::CLOSING:
      return transition_.closing_;
    case ClipTransitionType::BOTH:
      [[fallthrough]];
    default:
      break;
  }
  return nullptr;
}


ClipPtr Clip::split(const long frame)
{
  // check limits
  if (frame <= timeline_info.in || frame >= timeline_info.out) {
    qWarning() << "Unable to split. Out-of-range";
    return nullptr;
  }
  // Create copy
  auto post = copy(this->sequence);
  // Adjust the in/out points
  timeline_info.out = frame;
  post->timeline_info.in = frame;
  post->timeline_info.clip_in = timeline_info.clip_in + post->timeline_info.in - timeline_info.in;
  // Adjust transitions
  post->transition_.closing_ = transition_.closing_;
  post->transition_.opening_ = nullptr;
  transition_.closing_ = nullptr;

  // ensure transition lengths within limits
  if (transition_.opening_ != nullptr) {
    transition_.opening_->setLength(qMin(transition_.opening_->get_length(), length()));
  }
  if (post->transition_.closing_ != nullptr) {
    post->transition_.closing_->setLength(qMin(post->transition_.closing_->get_length(), post->length()));
  }
  return post;
}


bool Clip::merge(const Clip& split_clip)
{
  // NOTE: only tested for "unsplit" use-case
  if (split_clip.timeline_info.media != timeline_info.media) {
    qWarning() << "Not able to merge clip with differing source material";
    return false;
  }

  if (split_clip.timeline_info.clip_in > timeline_info.clip_in) {
    // appending
    timeline_info.out = split_clip.timeline_info.out.load();
    transition_.closing_ = split_clip.transition_.closing_;
  } else if (split_clip.timeline_info.clip_in < timeline_info.clip_in) {
    // prepending
    timeline_info.clip_in = split_clip.timeline_info.clip_in.load();
    transition_.opening_ = split_clip.transition_.opening_;
  } else {
    // trying to merge clip starting
    return false;
  }

  return true;
}


QVector<ClipPtr> Clip::splitAll(const long frame)
{
  QVector<ClipPtr> split_clips;

  // check limits
  if (frame <= timeline_info.in || frame >= timeline_info.out) {
    qWarning() << "Unable to split. Out-of-range";
    return split_clips;
  }

  auto split_clip = split(frame);
  if (split_clip == nullptr) {
    qWarning() << "Clip not splitted";
    return split_clips;
  }

  split_clips.append(split_clip);
  for (auto l : linked) {
    if (auto l_clip = sequence->clip(l)) {
      if (l_clip == nullptr) {
        qWarning() << "Linked Clip instance is null";
        continue;
      }
      split_clip = l_clip->split(frame);
      if (split_clip == nullptr) {
        qWarning() << "Failed to split linked Clip";
        continue;
      }
      split_clips.append(split_clip);
    }
  }

  linkClips(split_clips);

  return split_clips;
}

/**
 * @brief The length in frames of the clip
 * @return
 */
int64_t Clip::length() const noexcept
{
  return timeline_info.out - timeline_info.in + timeline_info.clip_in;
}

void Clip::reset()
{
  audio_playback.just_reset = false;
  is_open = false;
  finished_opening = false;
  pkt_written = false;
  audio_playback.reset = false;
  audio_playback.frame_sample_index = -1;
  audio_playback.buffer_write = false;
  texture_frame = -1;
  last_invalid_ts = -1;
  //TODO: check memory has been freed correctly or needs to be done here
  media_handling_.format_ctx_ = nullptr;
  media_handling_.stream_ = nullptr;
  media_handling_.codec_ = nullptr;
  media_handling_.codec_ctx_ = nullptr;
  texture = nullptr;
}

void Clip::resetAudio()
{
  if (timeline_info.media == nullptr || timeline_info.media->type() == MediaType::FOOTAGE) {
    audio_playback.reset = true;
    audio_playback.frame_sample_index = -1;
    audio_playback.buffer_write = 0;
  } else if (timeline_info.media->type() == MediaType::SEQUENCE) {
    SequencePtr nested_sequence = timeline_info.media->object<Sequence>();
    for (const auto& clp : nested_sequence->clips()) {
      if (clp != nullptr) {
        clp->resetAudio();
      }
    }
  }
}

void Clip::refresh()
{
  // validates media if it was replaced
  if (replaced && timeline_info.media != nullptr && timeline_info.media->type() == MediaType::FOOTAGE) {
    auto ftg = timeline_info.media->object<Footage>();

    if ((mediaType() == ClipType::VISUAL) && !ftg->videoTracks().empty())  {
      Q_ASSERT(ftg->videoTracks().front());
      timeline_info.media_stream = ftg->videoTracks().front()->file_index;
    } else if ((mediaType() == ClipType::AUDIO) && !ftg->audioTracks().empty()) {
      Q_ASSERT(ftg->audioTracks().front());
      timeline_info.media_stream = ftg->audioTracks().front()->file_index;
    }
  }
  replaced = false;

  // reinitializes all effects... just in case
  for (const auto& eff : effects) {
    if (eff == nullptr) {
      qCritical() << "Null effect ptr";
      continue;
    }
    eff->refresh();
  }

  recalculateMaxLength();
}

void Clip::clearQueue() {
  while (!queue.empty()) {
    av_frame_free(&queue.first());
    queue.removeFirst();
  }
}

void Clip::removeEarliestFromQueue() {
  int earliest_frame = 0;
  for (int i=1;i<queue.size();i++) {
    // TODO/NOTE: this will not work on sped up AND reversed video
    if (queue.at(i)->pts < queue.at(earliest_frame)->pts) {
      earliest_frame = i;
    }
  }
  av_frame_free(&queue[earliest_frame]);
  queue.removeAt(earliest_frame);
}

TransitionPtr Clip::openingTransition() const
{
  return getTransition(ClipTransitionType::OPENING);
}

TransitionPtr Clip::closingTransition() const
{
  return getTransition(ClipTransitionType::CLOSING);
}

/**
 * @brief set frame cache to a position
 * @param playhead
 */
void Clip::frame(const long playhead, bool& texture_failed)
{
  qDebug() << "playhead:" << playhead << "in:" << timeline_info.in << "name:" << name();
  if (finished_opening && (media_handling_.stream_ != nullptr) ) {
    const auto ftg = timeline_info.media->object<Footage>();
    if (!ftg) {
      qDebug() << "Null footage, playhead:" << playhead << "name:" << name();
      return;
    }
    FootageStreamPtr ms;
    if (mediaType() == ClipType::VISUAL) {
      ms = ftg->video_stream_from_file_index(timeline_info.media_stream);
    } else {
      ms = ftg->audio_stream_from_file_index(timeline_info.media_stream);
    }
    if (ms == nullptr) {
      qDebug() << "Null Footage stream, playhead:" <<playhead << "name:" << name();
      return;
    }


    // I'm pretty sure the root cause is the retrieval of the wrong playhead from timestamp
    // TODO unit-test Clip::playhead_to_timestamp
    const auto [target_pts, second_pts] = [&] {
      int64_t target = qMax(static_cast<int64_t>(0), playhead_to_timestamp(playhead));
      int64_t second = qRound64(av_q2d(av_inv_q(media_handling_.stream_->time_base)));
      if (ms->fieldOrder() != media_handling::FieldOrder::PROGRESSIVE) {
        target *= 2;
        second *= 2;
      }
      return std::make_tuple(target, second);
    }();

    AVFrame* target_frame = nullptr;

    bool reset = false;
    bool use_cache = true;

    QMutexLocker locker(&queue_lock);
    if (!queue.empty()) {
      if (ms->infinite_length) {
        target_frame = queue.at(0);
      } else {
        // correct frame may be somewhere else in the queue
        int closest_frame = 0;

        for (int i = 1; i < queue.size(); i++) {
          Q_ASSERT(queue.at(i));
          if (queue.at(i)->pts == target_pts) {
            closest_frame = i;
            break;
          }
          if ( (queue.at(i)->pts > queue.at(closest_frame)->pts) && (queue.at(i)->pts < target_pts) ) {
            closest_frame = i;
          }
        }

        // remove all frames earlier than this one from the queue
        target_frame = queue.at(closest_frame);
        int64_t next_pts = INT64_MAX;
        int64_t minimum_ts = target_frame->pts;

        if (global::config.previous_queue_type == FRAME_QUEUE_TYPE_SECONDS) {
          minimum_ts -= qFloor(second_pts * global::config.previous_queue_size);
        }

        int previous_frame_count = 0;
        for (int i = 0; i < queue.size(); i++) {
          Q_ASSERT(queue.at(i));
          if ( (queue.at(i)->pts > target_frame->pts) && (queue.at(i)->pts < next_pts) ) {
            next_pts = queue.at(i)->pts;
          }
          if ( (queue.at(i) != target_frame) && ((queue.at(i)->pts > minimum_ts) == timeline_info.reverse)) {
            if (global::config.previous_queue_type == FRAME_QUEUE_TYPE_SECONDS) {
              av_frame_free(&queue[i]); // may be a little heavy for the main thread?
              queue.removeAt(i);
              i--;
            } else {
              // TODO sort from largest to smallest
              previous_frame_count++;
            }
          }
        }//for

        if (global::config.previous_queue_type == FRAME_QUEUE_TYPE_FRAMES) {
          while (previous_frame_count > qCeil(global::config.previous_queue_size)) {
            int smallest = 0;
            for (int i=1 ; i < queue.size(); i++) {
              Q_ASSERT(queue.at(i));
              if (queue.at(i)->pts < queue.at(smallest)->pts) {
                smallest = i;
              }
            }
            av_frame_free(&queue[smallest]);
            queue.removeAt(smallest);
            previous_frame_count--;
          }//while
        }

        if (next_pts == INT64_MAX) {
          next_pts = target_frame->pts + target_frame->pkt_duration;
        }

        // we didn't get the exact timestamp
        if ( (target_frame->pts != target_pts) && ( (target_pts < target_frame->pts) || (target_pts > next_pts) )) {
          const int64_t pts_diff = qAbs(target_pts - target_frame->pts);
          if (reached_end && target_pts > target_frame->pts) {
            reached_end = false;
            use_cache = false;
          } else if (target_pts != last_invalid_ts && (target_pts < target_frame->pts || pts_diff > second_pts)) {
            if (!global::config.fast_seeking) {
              target_frame = nullptr;
            }
            reset = true;
            qInfo() << "Resetting";
            last_invalid_ts = target_pts;
          } else {
            if (queue.size() >= max_queue_size) {
              removeEarliestFromQueue();
            }
            ignore_reverse = true;
            target_frame = nullptr;
          }
        } else {
          qDebug() << "target:" << target_pts << "frame:" << target_frame->pts << "name:" << name();
        }
      }
    } else {
      qInfo() << "Resetting";
      reset = true;
    }

    if ( (target_frame == nullptr) || reset) {
      // reset cache
      texture_failed = true;
    }

    if (target_frame != nullptr) {
      const int nb_components = av_pix_fmt_desc_get(static_cast<enum AVPixelFormat>(pix_fmt))->nb_components;
      glPixelStorei(GL_UNPACK_ROW_LENGTH, target_frame->linesize[0] / nb_components);

      bool copied = false;
      uint8_t* data = target_frame->data[0];
      size_t frame_size;

      for (const auto& e : effects) {
        Q_ASSERT(e);
        if (e->hasCapability(Capability::IMAGE)) {
          if (!copied) {
            frame_size = static_cast<size_t>(target_frame->linesize[0] * target_frame->height);
            data = new uint8_t[frame_size];
            memcpy(data, target_frame->data[0], frame_size);
            copied = true;
          }
          auto img = gsl::span<uint8_t>(data, frame_size);
          e->process_image(timecode(playhead), img);
        }
      }

      qDebug() << "Setting data, playhead:" << playhead << "texture:" << texture->textureId() << "target:" << target_pts << "frame:" << target_frame->pts << "name:" << name();
      texture->setData(0, get_gl_pix_fmt_from_av(pix_fmt), QOpenGLTexture::UInt8, static_cast<const void*>(data));

      if (copied) {
        delete [] data;
      }

      glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else {
      qDebug() << "Target frame null, playhead:" << playhead << "name:" << name();
    }
    locker.unlock();

    // get more frames
    QVector<ClipPtr> empty;
    if (use_cache) {
      cache(playhead, reset, false, empty);
    } else {
      qDebug() << "Not using cache, playhead:" << playhead << "name:" << name();
    }
  } else {
    qWarning() << "Not finished opening";
  }
}


double Clip::timecode(const long playhead) {
  return (static_cast<double>(playhead - timelineInWithTransition() + clipInWithTransition())
          / static_cast<double>(sequence->frameRate()) );
}

/**
 * @brief Identify if this clip is selected in the project's current sequence
 * @param containing
 * @return true==is selected
 */
bool Clip::isSelected(const bool containing)
{
  if (sequence == nullptr) {
    qWarning() << "Clip linked to invalid sequence";
    return false;
  }

  for (const auto& sel : sequence->selections_) {

    const auto same_track = timeline_info.track_ == sel.track;
    const auto in_range = (timeline_info.in >= sel.in) && (timeline_info.out <= sel.out);
    if ( (same_track && in_range && !sel.transition_) == containing) {
      return true;
    }
    // keep looking
  }
  return false;
}


bool Clip::isSelected(const Selection& sel) const
{

  return ( !sel.transition_ && (sel.track == timeline_info.track_) &&
           !( ( (timeline_info.in <= sel.in) && (timeline_info.out <= sel.in) )
              || ( (timeline_info.in >= sel.out) && (timeline_info.out >= sel.out) ) ));
}

bool Clip::inRange(const long frame) const noexcept
{
  return ( (timeline_info.in < frame) && (timeline_info.out > frame) );
}

ClipType Clip::mediaType() const noexcept
{
  if (timeline_info.track_ >= 0) {
    return ClipType::AUDIO;
  }
  return ClipType::VISUAL;
}


MediaPtr Clip::parentMedia()
{
  return timeline_info.media;
}

void Clip::addLinkedClip(const Clip& clp)
{
  if (clp.id_ != id_) {
    linked.append(clp.id_);
  }// else trying to link itself
}

void Clip::setLinkedClips(const QVector<int32_t>& links)
{
  linked = links;
}

const QVector<int32_t>& Clip::linkedClipIds() const
{
  return linked;
}


QVector<ClipPtr> Clip::linkedClips() const
{
  Q_ASSERT(sequence != nullptr);
  QVector<ClipPtr> link_clips;
  for (auto id : linked) {
    if (auto l_clip = sequence->clip(id)) {
      link_clips.append(l_clip);
    }
  }
  return link_clips;
}

void Clip::clearLinks()
{
  linked.clear();
}

QSet<int> Clip::getLinkedTracks() const
{
  QSet<int> tracks;

  for (auto link : linked) {
    if (auto clp = global::sequence->clip(link)) { //FIXME: don't like that Sequence is required. use ClipPtr in linked
      tracks.insert(clp->timeline_info.track_);
    }
  }

  return tracks;
}

/**
 * @brief           Update the linked clips using a mapping of old_id : new_clip
 * @param mapping   Mapped ids and clips
 */
void Clip::relink(const QMap<int, int>& mapping)
{
  QVector<int> new_links;
  for (auto link : linked) {
    if (mapping.contains(link)) {
      new_links.append(mapping.value(link));
    } else {
      qDebug() << "No new link for id:" << link;
      new_links.append(link);
    }
  }
  setLinkedClips(new_links);
}

bool Clip::load(QXmlStreamReader& stream)
{
  for (const auto& attr : stream.attributes()) {
    const auto name = attr.name().toString().toLower();
    if (name == "source") {
      timeline_info.media = Project::model().findItemById(attr.value().toInt());
    } else if (name == "id") {
      setId(attr.value().toInt());
    } else {
      qWarning() << "Unhandled clip attribute" << name;
    }
  }

  while (stream.readNextStartElement()) {
    const auto name = stream.name().toString().toLower();
    if (name == "opening_transition") {
      transition_.opening_ = loadTransition(stream);
      if (transition_.opening_) {
        transition_.opening_->setupUi();
      }
    } else if (name == "closing_transition") {
      transition_.closing_ = loadTransition(stream);
      if (transition_.closing_) {
        transition_.closing_->setupUi();
      }
    } else if (name == "timelineinfo") {
      if (!timeline_info.load(stream)) {
        qCritical() << "Failed to load TimelineInfo";
        return false;
      }
    } else if (name == "links") {
      while (stream.readNextStartElement()) {
        if (stream.name().toString().toLower() == "link") {
          linked.append(stream.readElementText().toInt());
        } else {
          stream.skipCurrentElement();
          qWarning() << "Unhandled element" << stream.name();
        }
      }
    } else if (name == "effect") {
      if (!loadInEffect(stream)){
        qCritical() << "Failed to load Effect";
        return false;
      }
    } else if (name == "name") {
      name_ = stream.readElementText();
    } else if (name == "created_object") {
      created_object_ = stream.readElementText().toLower() == "true";
    } else {
      stream.skipCurrentElement();
      qWarning() << "Unhandled element" << name;
    }
  }

  return true;
}

bool Clip::save(QXmlStreamWriter& stream) const
{

  stream.writeStartElement("clip");
  // "Create" clips (e.g. titles) are defined as -1 source
  const QString source_id((timeline_info.media != nullptr) ? QString::number(timeline_info.media->id()) : "-1");
  stream.writeAttribute("source", source_id);
  stream.writeAttribute("id", QString::number(id_));

  stream.writeTextElement("name", name_);
  stream.writeTextElement("created_object", this->isCreatedObject() ? "true" : "false");

  if (!timeline_info.save(stream)) {
    chestnut::throwAndLog("Failed to save timeline info");
  }

  stream.writeStartElement("opening_transition");
  auto length = -1;
  QString trans_name;
  int secondary_id = -1;
  if (auto trans = getTransition(ClipTransitionType::OPENING)) {
    length = trans->get_true_length();
    trans_name = trans->meta.name;
    if (auto scnd = trans->secondaryClip()) {
      secondary_id = scnd->id();
    }
  }
  stream.writeTextElement("name", trans_name);
  stream.writeTextElement("length", QString::number(length));
  stream.writeTextElement("secondary_clip", QString::number(secondary_id));
  stream.writeEndElement();

  stream.writeStartElement("closing_transition");
  length = -1;
  trans_name.clear();
  secondary_id = -1;
  if (auto trans = getTransition(ClipTransitionType::CLOSING)) {
    length = trans->get_true_length();
    trans_name = trans->meta.name;
    if (auto scnd = trans->secondaryClip()) {
      secondary_id = scnd->id();
    }
  }
  stream.writeTextElement("name", trans_name);
  stream.writeTextElement("length", QString::number(length));
  stream.writeTextElement("secondary_clip", QString::number(secondary_id));
  stream.writeEndElement();

  stream.writeStartElement("links");
  for (const auto& link : linked) {
    stream.writeTextElement("link", QString::number(link));
  }
  stream.writeEndElement(); // links

  for (const auto& eff : effects) {
    if (!eff->save(stream)) {
      chestnut::throwAndLog("Failed to save effect");
    }
  }

  stream.writeEndElement();
  return true;
}


void Clip::verifyTransitions(ComboAction &ca)
{
  verifyTransitions(ca, timeline_info.in, timeline_info.out, timeline_info.track_);
}


bool Clip::locked() const
{
  if (sequence == nullptr) {
    return false;
  }

  return sequence->trackLocked(timeline_info.track_);
}

bool Clip::isCreatedObject() const
{
  return created_object_;
}


long Clip::clipInWithTransition() const noexcept
{
  if ( (transition_.opening_ != nullptr) && (transition_.opening_->secondaryClip() != nullptr) ) {
    // we must be the secondary clip, so return (timeline in - length)
    return timeline_info.clip_in - transition_.opening_->get_true_length();
  }
  return timeline_info.clip_in;
}

long Clip::timelineInWithTransition() const noexcept
{
  if ( (transition_.opening_ != nullptr) && (transition_.opening_->secondaryClip() != nullptr) ) {
    // we must be the secondary clip, so return (timeline in - length)
    return timeline_info.in - transition_.opening_->get_true_length();
  }
  return timeline_info.in;
}

long Clip::timelineOutWithTransition() const noexcept
{
  if ( (transition_.closing_ != nullptr) && (transition_.closing_->secondaryClip() != nullptr) ) {
    // we must be the primary clip, so return (timeline out + length2)
    return timeline_info.out + transition_.closing_->get_true_length();
  }

  return timeline_info.out;
}

// timeline functions
long Clip::length() noexcept
{
  return timeline_info.out - timeline_info.in;
}

double Clip::mediaFrameRate()
{
  if (timeline_info.media != nullptr) {
    const double rate = timeline_info.media->frameRate(timeline_info.media_stream);
    if (!qIsNaN(rate)) {
      return rate;
    }
  }
  if (sequence != nullptr) {
    return sequence->frameRate();
  }
  return qSNaN();
}

void Clip::recalculateMaxLength()
{
  // TODO: calculated_length on failures
  if (sequence != nullptr) {
    const double fr = sequence->frameRate() / timeline_info.speed;
    media_handling_.calculated_length_ = LONG_MAX;

    if (timeline_info.media != nullptr) {
      switch (timeline_info.media->type()) {
        case MediaType::FOOTAGE:
        {
          auto ftg = timeline_info.media->object<Footage>();
          if (ftg == nullptr) {
            break;
          }
          const auto ms = timeline_info.isVideo() ? ftg->video_stream_from_file_index(timeline_info.media_stream)
                                                  : ftg->audio_stream_from_file_index(timeline_info.media_stream);
          if ( (ms != nullptr) && ms->infinite_length) {
            media_handling_.calculated_length_ = LONG_MAX;
          } else {
            media_handling_.calculated_length_ = ftg->totalLengthInFrames(fr);
          }
        }
          break;
        case MediaType::SEQUENCE:
        {
          if (SequencePtr s = timeline_info.media->object<Sequence>()) {
            media_handling_.calculated_length_ = refactor_frame_number(s->endFrame(), s->frameRate(), fr);
          }
        }
          break;
        default:
          //TODO: log/something
          break;
      }
    }
  }
}

long Clip::maximumLength() const noexcept
{
  return media_handling_.calculated_length_;
}

int Clip::width()
{
  if ( (timeline_info.media == nullptr) && (sequence != nullptr) ) {
    return sequence->width();
  }

  switch (timeline_info.media->type()) {
    case MediaType::FOOTAGE:
    {
      auto ftg = timeline_info.media->object<Footage>();
      if (!ftg) {
        return 0;
      }
      const auto ms = mediaType() == ClipType::VISUAL ? ftg->video_stream_from_file_index(timeline_info.media_stream) :
                                                        ftg->audio_stream_from_file_index(timeline_info.media_stream);

      if (ms != nullptr) {
        return ms->video_width;
      }
      if (sequence != nullptr) {
        return sequence->width();
      }
      break;
    }
    case MediaType::SEQUENCE:
    {
      if (auto sequenceNow = timeline_info.media->object<Sequence>()){
        return sequenceNow->width();
      }
      break;
    }
    default:
      //TODO: log/something
      break;
  }
  return 0;
}

int Clip::height() {
  if ( (timeline_info.media == nullptr) && (sequence != nullptr) ) {
    return sequence->height();
  }

  switch (timeline_info.media->type()) {
    case MediaType::FOOTAGE:
    {
      auto ftg = timeline_info.media->object<Footage>();
      if (!ftg) {
        return 0;
      }
      const auto ms(mediaType() == ClipType::VISUAL ? ftg->video_stream_from_file_index(timeline_info.media_stream)
                                                    : ftg->audio_stream_from_file_index(timeline_info.media_stream));

      if (ms != nullptr) {
        return ms->video_height;
      }
      if (sequence != nullptr) {
        return sequence->height();
      }
      break;
    }
    case MediaType::SEQUENCE:
    {
      if (auto s = timeline_info.media->object<Sequence>() ) {
        return s->height();
      }
      break;
    }
    default:
      qWarning() << "Unknown Media type" << static_cast<int>(timeline_info.media->type());
      break;
  }//switch
  return 0;
}


int32_t Clip::id() const
{
  return id_;
}

void Clip::refactorFrameRate(ComboAction* ca, double multiplier, bool change_timeline_points) {
  if (change_timeline_points) {
    if (ca != nullptr) {
      move(*ca,
           qRound(static_cast<double>(timeline_info.in) * multiplier),
           qRound(static_cast<double>(timeline_info.out) * multiplier),
           qRound(static_cast<double>(timeline_info.clip_in) * multiplier),
           timeline_info.track_);
    }
  }

  // rescale the length of transitions otherwise the could overlap each other or be longer than the clip
  if (transition_.opening_ != nullptr) {
    transition_.opening_->setLength(qRound(transition_.opening_->get_length() * multiplier));
  }
  if (transition_.closing_ != nullptr) {
    transition_.closing_->setLength(qRound(transition_.closing_->get_length() * multiplier));
  }

  // move keyframes
  for (const auto& effectNow : effects) {
    if (!effectNow) {
      continue;
    }
    for (const auto& effectRowNow : effectNow->getRows()) {
      if (!effectRowNow) {
        continue;
      }
      for (auto f : effectRowNow->getFields()) {
        if (f == nullptr) {
          continue;
        }
        for (auto key : f->keyframes) {
          ca->append(new SetValCommand<long>(key.time, key.time, lround(key.time * multiplier)));
        }//for
      }//for
    }//for
  }//for
}

void Clip::run() {
  // open_lock is used to prevent the clip from being destroyed before the cacher has closed it properly
  lock.lock();
  finished_opening = false;
  is_open = true;
  cache_info.caching = true;
  cache_info.interrupt = false;

  if (openWorker()) {
    qInfo() << "Cache thread starting";
    while (cache_info.caching) {
      can_cache.wait(&lock);
      if (!cache_info.caching) {
        break;
      }
      while (true) {
        cache_worker(cache_info.playhead, cache_info.reset, cache_info.scrubbing, cache_info.nests);
        if (multithreaded && cache_info.interrupt && (timeline_info.isVideo()) ) {
          cache_info.interrupt = false;
        } else {
          break;
        }
      }//while
    }//while

    qWarning() << "caching thread stopped";

    closeWorker();

    lock.unlock();
    open_lock.unlock();
  } else {
    qCritical() << "Failed to open worker";
  }
}


void Clip::apply_audio_effects(const double timecode_start, AVFrame* frame, const int nb_bytes, QVector<ClipPtr>& nests)
{
  // perform all audio effects
  const double timecode_end = timecode_start + bytes_to_seconds(nb_bytes, frame->channels, frame->sample_rate);

  for (const auto& e : effects) {
    if (e != nullptr && e->is_enabled()) {
      e->process_audio(timecode_start, timecode_end, frame->data[0], nb_bytes, 2);
    }
  }
  if (transition_.opening_ != nullptr) {
    if (timeline_info.media != nullptr && timeline_info.media->type() == MediaType::FOOTAGE) {
      const double transition_start = (clipInWithTransition() / sequence->frameRate());
      const double transition_end = (clipInWithTransition() + transition_.opening_->get_length()) / sequence->frameRate();
      if (timecode_end < transition_end) {
        const double adjustment = transition_end - transition_start;
        const double adjusted_range_start = (timecode_start - transition_start) / adjustment;
        const double adjusted_range_end = (timecode_end - transition_start) / adjustment;
        transition_.opening_->process_audio(adjusted_range_start, adjusted_range_end, frame->data[0], nb_bytes, TA_OPENING_TRANSITION);
      }
    }
  }
  if (transition_.closing_ != nullptr) {
    if (timeline_info.media != nullptr && timeline_info.media->type() == MediaType::FOOTAGE) {
      const long length_with_transitions = timelineOutWithTransition() - timelineInWithTransition();
      const double transition_start = (clipInWithTransition() + length_with_transitions - transition_.closing_->get_length()) / sequence->frameRate();
      const double transition_end = (clipInWithTransition() + length_with_transitions) / sequence->frameRate();
      if (timecode_start > transition_start) {
        const double adjustment = transition_end - transition_start;
        const double adjusted_range_start = (timecode_start - transition_start) / adjustment;
        const double adjusted_range_end = (timecode_end - transition_start) / adjustment;
        transition_.closing_->process_audio(adjusted_range_start, adjusted_range_end, frame->data[0], nb_bytes, TA_CLOSING_TRANSITION);
      }
    }
  }

  if (!nests.isEmpty()) {
    auto next_nest = nests.last();
    nests.removeLast();
    if (next_nest != nullptr) {
      next_nest->apply_audio_effects(
            timecode_start + ( (static_cast<double>(timelineInWithTransition()) - clipInWithTransition()) /sequence->frameRate() ),
            frame,
            nb_bytes,
            nests);
    }
  }
}



long Clip::playhead_to_frame(const long playhead) const noexcept
{
  return (qMax(0L, playhead - timelineInWithTransition()) + clipInWithTransition());
}


int64_t Clip::playhead_to_timestamp(const long playhead) const noexcept
{
  return seconds_to_timestamp(playhead_to_seconds(playhead));
}

bool Clip::retrieve_next_frame(AVFrame& frame)
{
  int result = 0;
  int receive_ret;

  // do we need to retrieve a new packet for a new frame?
  av_frame_unref(&frame);
  while ((receive_ret = avcodec_receive_frame(media_handling_.codec_ctx_, &frame)) == AVERROR(EAGAIN)) {
    int read_ret = 0;
    do {
      if (pkt_written) {
        av_packet_unref(media_handling_.pkt_);
        pkt_written = false;
      }
      read_ret = av_read_frame(media_handling_.format_ctx_, media_handling_.pkt_);
      if (read_ret >= 0) {
        pkt_written = true;
      }
    } while (read_ret >= 0 && media_handling_.pkt_->stream_index != timeline_info.media_stream);

    if (read_ret >= 0) {
      int send_ret = avcodec_send_packet(media_handling_.codec_ctx_, media_handling_.pkt_);
      if (send_ret < 0) {
        av_strerror(send_ret, err.data(), ERR_LEN);
        qCritical() << "Failed to send packet to decoder, msg =" << err.data();
        return send_ret;
      }
    } else {
      if (read_ret == AVERROR_EOF) {
        int send_ret = avcodec_send_packet(media_handling_.codec_ctx_, nullptr);
        if (send_ret < 0) {
          av_strerror(send_ret, err.data(), ERR_LEN);
          qCritical() << "Failed to send packet to decoder, msg =" << err.data();
          return send_ret;
        }
      } else {
        av_strerror(read_ret, err.data(), ERR_LEN);
        qCritical() << "Could not read frame, msg =" << err.data();
        return read_ret; // skips trying to find a frame at all
      }
    }
  }
  if (receive_ret < 0) {
    if (receive_ret != AVERROR_EOF) {
      qCritical() << "Failed to receive packet from decoder." << receive_ret;
    }
    result = receive_ret;
  }

  return result == 0;
}

/**
 * @brief returns time in seconds
 * @param playhead
 * @return seconds
 */
double Clip::playhead_to_seconds(const long playhead) const noexcept
{
  Q_ASSERT(timeline_info.media);
  Q_ASSERT(sequence);

  long clip_frame = playhead_to_frame(playhead);
  if (timeline_info.reverse) {
    clip_frame = maximumLength() - clip_frame - 1;
  }
  double secs = (static_cast<double>(clip_frame) / sequence->frameRate()) * timeline_info.speed;
  if (timeline_info.media->type() == MediaType::FOOTAGE) {
    secs *= timeline_info.media->object<Footage>()->speed_;
  }
  return secs;
}

int64_t Clip::seconds_to_timestamp(const double seconds) const noexcept
{
  if (media_handling_.stream_ == nullptr) {
    return -1;
  }
  return qRound64(seconds * av_q2d(av_inv_q(media_handling_.stream_->time_base)))
      + qMax(static_cast<int64_t>(0), media_handling_.stream_->start_time);
}

//TODO: hmmm
void Clip::cache_audio_worker(const bool scrubbing, QVector<ClipPtr> &nests)
{
  long timeline_in = timelineInWithTransition();
  long timeline_out = timelineOutWithTransition();
  long target_frame = audio_playback.target_frame;

  long frame_skip = 0;
  double last_fr = sequence->frameRate();
  if (!nests.isEmpty()) {
    for (const auto& nestedClip : nests) {
      const auto offset = nestedClip->timelineInWithTransition() - nestedClip->clipInWithTransition();
      timeline_in = refactor_frame_number(timeline_in, last_fr,
                                          nestedClip->sequence->frameRate()) + offset;
      timeline_out = refactor_frame_number(timeline_out, last_fr,
                                           nestedClip->sequence->frameRate()) + offset;
      target_frame = refactor_frame_number(target_frame, last_fr,
                                           nestedClip->sequence->frameRate()) + offset;

      timeline_out = qMin(timeline_out, nestedClip->timelineOutWithTransition());

      frame_skip = refactor_frame_number(frame_skip, last_fr, nestedClip->sequence->frameRate());

      const long validator = nestedClip->timelineInWithTransition() - timeline_in;
      if (validator > 0) {
        frame_skip += validator;
        //timeline_in = nests.at(i)->timelineInWithTransition();
      }

      last_fr = nestedClip->sequence->frameRate();
    }//for
  }

  while (true) {
    AVFrame* av_frame;
    int nb_bytes = INT_MAX;

    if (timeline_info.media == nullptr) {
      av_frame = media_handling_.frame_;
      nb_bytes = av_frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(av_frame->format)) * av_frame->channels;
      while (( (audio_playback.frame_sample_index == -1) || (audio_playback.frame_sample_index >= nb_bytes)) && (nb_bytes > 0) ) {
        // create "new frame"
        memset(media_handling_.frame_->data[0], 0, nb_bytes);
        apply_audio_effects(bytes_to_seconds(av_frame->pts, av_frame->channels, av_frame->sample_rate), av_frame, nb_bytes, nests);
        media_handling_.frame_->pts += nb_bytes;
        audio_playback.frame_sample_index = 0;
        if (audio_playback.buffer_write == 0) {
          audio_playback.buffer_write = get_buffer_offset_from_frame(last_fr, qMax(timeline_in, target_frame));
        }
        const int offset = audio_ibuffer_read - audio_playback.buffer_write;
        if (offset > 0) {
          audio_playback.buffer_write += offset;
          audio_playback.frame_sample_index += offset;
        }
      }//while
    } else if (timeline_info.media->type() == MediaType::FOOTAGE) {
      const double timebase = av_q2d(media_handling_.stream_->time_base);

      av_frame = queue.at(0);

      // retrieve frame
      bool new_frame = false;
      while (( (audio_playback.frame_sample_index == -1) || (audio_playback.frame_sample_index >= nb_bytes) ) && (nb_bytes > 0) ) {
        // no more audio left in frame, get a new one
        if (!reached_end) {
          int loop = 0;

          if (timeline_info.reverse && !audio_playback.just_reset) {
            avcodec_flush_buffers(media_handling_.codec_ctx_);
            reached_end = false;
            int64_t backtrack_seek = qMax(audio_playback.reverse_target
                                          - static_cast<int64_t>(av_q2d(av_inv_q(media_handling_.stream_->time_base))), static_cast<int64_t>(0));
            av_seek_frame(media_handling_.format_ctx_, media_handling_.stream_->index, backtrack_seek, AVSEEK_FLAG_BACKWARD);
          }

          do {
            av_frame_unref(av_frame);

            int ret;

            while ((ret = av_buffersink_get_frame(buffersink_ctx, av_frame)) == AVERROR(EAGAIN)) {
              Q_ASSERT(media_handling_.frame_);
              ret = retrieve_next_frame(*media_handling_.frame_);
              if (ret >= 0) {
                if ((ret = av_buffersrc_add_frame_flags(buffersrc_ctx,
                                                        media_handling_.frame_,
                                                        AV_BUFFERSRC_FLAG_KEEP_REF)) < 0) {
                  av_strerror(ret, err.data(), ERR_LEN);
                  qCritical() << "Could not feed filtergraph, msg =" << err.data();
                  break;
                }
              } else {
                if (ret == AVERROR_EOF) {
                  // TODO: revise usage of reached_end in audio
                  if (!timeline_info.reverse) {
                    reached_end = true;
                  }
                } else {
                  av_strerror(ret, err.data(), ERR_LEN);
                  qCritical() << "Raw audio frame data could not be retrieved, msg =" << err.data();
                  reached_end = true;
                }
                break;
              }
            }

            if (ret < 0) {
              if (ret != AVERROR_EOF) {
                av_strerror(ret, err.data(), ERR_LEN);
                qCritical() << "Could not pull from filtergraph, msg =" << err.data();
                reached_end = true;
                break;
              }
              if (!timeline_info.reverse) {
                break;
              }
            }

            if (timeline_info.reverse) {
              if (loop > 1) {
                AVFrame* rev_frame = queue.at(1);
                if (ret != AVERROR_EOF) {
                  if (loop == 2) {
                    rev_frame->nb_samples = 0;
                    rev_frame->pts = media_handling_.frame_->pts;
                  }
                  int offset = rev_frame->nb_samples
                               * av_get_bytes_per_sample(static_cast<AVSampleFormat>(rev_frame->format))
                               * rev_frame->channels;
                  memcpy(
                        rev_frame->data[0]+offset,
                      av_frame->data[0],
                      (av_frame->nb_samples
                       * av_get_bytes_per_sample(static_cast<AVSampleFormat>(av_frame->format))
                       * av_frame->channels)
                      );
                }

                rev_frame->nb_samples += av_frame->nb_samples;

                if ((media_handling_.frame_->pts >= audio_playback.reverse_target) || (ret == AVERROR_EOF)) {
                  double playback_speed = timeline_info.speed * timeline_info.media->object<Footage>()->speed_;
                  rev_frame->nb_samples = qRound64(static_cast<double>(audio_playback.reverse_target - rev_frame->pts)
                                                   / media_handling_.stream_->codecpar->sample_rate
                                                   * (current_audio_freq() / playback_speed));

                  int frame_size = rev_frame->nb_samples
                                   * rev_frame->channels
                                   * av_get_bytes_per_sample(static_cast<AVSampleFormat>(rev_frame->format));
                  int half_frame_size = frame_size >> 1;

                  int sample_size = rev_frame->channels*av_get_bytes_per_sample(static_cast<AVSampleFormat>(rev_frame->format));
                  char* temp_chars = new char[sample_size];
                  for (int i=0;i<half_frame_size;i+=sample_size) {
                    for (int j=0;j<sample_size;j++) {
                      temp_chars[j] = rev_frame->data[0][i+j];
                    }
                    for (int j=0;j<sample_size;j++) {
                      rev_frame->data[0][i+j] = rev_frame->data[0][frame_size-i-sample_size+j];
                    }
                    for (int j=0;j<sample_size;j++) {
                      rev_frame->data[0][frame_size-i-sample_size+j] = temp_chars[j];
                    }
                  }
                  delete [] temp_chars;

                  audio_playback.reverse_target = rev_frame->pts;
                  av_frame = rev_frame;
                  break;
                }
              }

              loop++;

            } else {
              av_frame->pts = media_handling_.frame_->pts;
              break;
            }
          } while (true);
        } else {
          // if there is no more data in the file, we flush the remainder out of swresample
          break;
        }

        new_frame = true;

        if (audio_playback.frame_sample_index < 0) {
          audio_playback.frame_sample_index = 0;
        } else {
          audio_playback.frame_sample_index -= nb_bytes;
        }

        nb_bytes = av_frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(av_frame->format)) * av_frame->channels;

        if (audio_playback.just_reset) {
          // get precise sample offset for the elected clip_in from this audio frame
          double target_sts = playhead_to_seconds(audio_playback.target_frame);
          double frame_sts = ((av_frame->pts - media_handling_.stream_->start_time) * timebase);
          int nb_samples = qRound64((target_sts - frame_sts)*current_audio_freq());
          audio_playback.frame_sample_index = nb_samples * 4;
          if (timeline_info.reverse) {
            audio_playback.frame_sample_index = nb_bytes - audio_playback.frame_sample_index;
          }
          audio_playback.just_reset = false;
        }
        if (audio_playback.buffer_write == 0) {
          audio_playback.buffer_write = get_buffer_offset_from_frame(last_fr, qMax(timeline_in, target_frame));

          if (frame_skip > 0) {
            const int target = get_buffer_offset_from_frame(last_fr, qMax(timeline_in + frame_skip, target_frame));
            audio_playback.frame_sample_index += (target - audio_playback.buffer_write);
            audio_playback.buffer_write = target;
          }
        }

        const int offset = audio_ibuffer_read - audio_playback.buffer_write;
        if (offset > 0) {
          audio_playback.buffer_write += offset;
          audio_playback.frame_sample_index += offset;
        }

        // try to correct negative fsi
        if (audio_playback.frame_sample_index < 0) {
          audio_playback.buffer_write -= audio_playback.frame_sample_index;
          audio_playback.frame_sample_index = 0;
        }
      } //while

      if (timeline_info.reverse) av_frame = queue.at(1);

      // apply any audio effects to the data
      if (nb_bytes == INT_MAX) {
        nb_bytes = av_frame->nb_samples
                   * av_get_bytes_per_sample(static_cast<AVSampleFormat>(av_frame->format))
                   * av_frame->channels;
      }
      if (new_frame) {
        const auto sample_rate = bytes_to_seconds(audio_playback.buffer_write, 2,
                                                  current_audio_freq())
                                 + audio_ibuffer_timecode
                                 + (static_cast<double>(clipInWithTransition())/sequence->frameRate())
                                 - (static_cast<double>(timeline_in)/last_fr);
        apply_audio_effects(sample_rate, av_frame, nb_bytes, nests);
      }
    } else {
      // shouldn't ever get here
      qCritical() <<  "Tried to cache a non-footage/tone clip";
      return;
    }

    // mix audio into internal buffer
    if (av_frame->nb_samples == 0) {
      break;
    }

    // have audio data so write to audio_data_buffer
    long buffer_timeline_out = get_buffer_offset_from_frame(sequence->frameRate(), timeline_out);
    audio_write_lock.lock();

    while (audio_playback.frame_sample_index < nb_bytes
           && audio_playback.buffer_write < audio_ibuffer_read+(AUDIO_IBUFFER_SIZE>>1)
           && audio_playback.buffer_write < buffer_timeline_out) {
      int upper_byte_index = (audio_playback.buffer_write + 1) % AUDIO_IBUFFER_SIZE;
      int lower_byte_index = (audio_playback.buffer_write) % AUDIO_IBUFFER_SIZE;
      const auto old_sample = static_cast<qint16>( ((audio_ibuffer[upper_byte_index] & 0xFF) << 8)
                                                   | (audio_ibuffer[lower_byte_index] & 0xFF));
      const auto new_sample = static_cast<qint16>(((av_frame->data[0][audio_playback.frame_sample_index + 1] & 0xFF) << 8)
          | (av_frame->data[0][audio_playback.frame_sample_index] & 0xFF));
      const qint16 mixed_sample = mix_audio_sample(old_sample, new_sample);

      audio_ibuffer[upper_byte_index] = static_cast<quint8>((mixed_sample >> 8) & 0xFF);
      audio_ibuffer[lower_byte_index] = static_cast<quint8>(mixed_sample & 0xFF);

      audio_playback.buffer_write += 2;
      audio_playback.frame_sample_index += 2;
    }//while
    audio_write_lock.unlock();

    if (scrubbing && (audio_thread != nullptr) ) {
      audio_thread->notifyReceiver();
    }

    if (audio_playback.frame_sample_index == nb_bytes) {
      audio_playback.frame_sample_index = -1;
    } else {
      // assume we have no more data to send
      break;
    }

    if (reached_end) {
      av_frame->nb_samples = 0;
    }
    if (scrubbing) {
      break;
    }
  } //while

  // frame processed, trigger timeline movement
  QMetaObject::invokeMethod(&PanelManager::footageViewer(), "play_wake", Qt::QueuedConnection);
  QMetaObject::invokeMethod(&PanelManager::sequenceViewer(), "play_wake", Qt::QueuedConnection);
}

void Clip::cache_video_worker(const long playhead)
{
  int read_ret, send_ret, retr_ret;

  const int64_t target_pts = seconds_to_timestamp(playhead_to_seconds(playhead));

  const int limit = [this]{
    int lim = max_queue_size;
    if (ignore_reverse) {
      // waiting for one frame
      lim = queue.size() + 1;
    } else if (timeline_info.reverse) {
      lim *= 2;
    }
    return lim;
  }();

  if (queue.size() >= limit) {
    return;
  }
  const bool reverse = (timeline_info.reverse && !ignore_reverse);
  ignore_reverse = false;

  int64_t smallest_pts = INT64_MAX;
  if (reverse && !queue.empty()) {
    const int64_t quarter_sec = qRound64(av_q2d(av_inv_q(media_handling_.stream_->time_base))) >> 2;
    for (const auto& q : queue) {
      Q_ASSERT(q);
      smallest_pts = qMin(smallest_pts, q->pts);
    }
    avcodec_flush_buffers(media_handling_.codec_ctx_);
    reached_end = false;
    const int64_t seek_ts = qMax(static_cast<int64_t>(0), smallest_pts - quarter_sec);
    av_seek_frame(media_handling_.format_ctx_, media_handling_.stream_->index, seek_ts, AVSEEK_FLAG_BACKWARD);
  } else {
    smallest_pts = target_pts;
  }

  if (multithreaded && cache_info.interrupt) { // ignore interrupts for now
    cache_info.interrupt = false;
  }

  while (true) {
    AVFrame* frame = av_frame_alloc();
    Q_ASSERT(frame);

    while ((retr_ret = av_buffersink_get_frame(buffersink_ctx, frame)) == AVERROR(EAGAIN)) {
      if (multithreaded && cache_info.interrupt) return; // abort

      AVFrame* send_frame = media_handling_.frame_;
      Q_ASSERT(send_frame);
      read_ret = (use_existing_frame) ? 0 : retrieve_next_frame(*send_frame);
      use_existing_frame = false;
      if (read_ret >= 0) {
        bool send_it = true;
        if (send_it) {
          if ((send_ret = av_buffersrc_add_frame_flags(buffersrc_ctx, send_frame, AV_BUFFERSRC_FLAG_KEEP_REF)) < 0) {
            av_strerror(send_ret, err.data(), ERR_LEN);
            qCritical() << "Failed to add frame to buffer source, msg =" << err.data();
            av_frame_unref(media_handling_.frame_);
            break;
          }
        }
        av_frame_unref(media_handling_.frame_);
      } else {
        if (read_ret == AVERROR_EOF) {
          reached_end = true;
        } else {
          av_strerror(read_ret, err.data(), ERR_LEN);
          qCritical() << "Failed to read frame, msg =" << err.data();
        }
        break;
      }
    }//while

    if (retr_ret < 0) {
      if (retr_ret == AVERROR_EOF) {
        reached_end = true;
      } else {
        av_strerror(retr_ret, err.data(), ERR_LEN);
        qCritical() << "Failed to retrieve frame from buffersink, msg =" << err.data();
      }
      av_frame_free(&frame);
      break;
    }

    if (reverse && (( (smallest_pts == target_pts) && (frame->pts >= smallest_pts) )
                    || ((smallest_pts != target_pts) && (frame->pts > smallest_pts)))) {
      av_frame_free(&frame);
      break;
    }

    QMutexLocker locker(&queue_lock);
    qDebug() << "Cached! playhead:" << playhead << "ts:" << frame->pts << "name:" << name();
    queue.append(frame);

    const auto ftg = timeline_info.media->object<Footage>();
    if (const auto ms = ftg->video_stream_from_file_index(timeline_info.media_stream);
        (!ms->infinite_length && (!reverse && queue.size() == limit))) {
      // see if we got the frame we needed (used for speed ups primarily)
      bool found = false;
      for (const auto& q : queue) {
        Q_ASSERT(q);
        if (q->pts >= target_pts) {
          found = true;
          break;
        }
      }
      if (found) {
        locker.unlock();
        break;
      }

      // remove earliest frame and loop to store another
      removeEarliestFromQueue();
    }
    locker.unlock();
    if (multithreaded && cache_info.interrupt) { // abort
      return;
    }
  } //while
}

/**
 * @brief To set up the caching thread?
 * @param playhead
 * @param reset
 * @param scrubbing
 * @param nests
 */
void Clip::cache_worker(const long playhead, const bool reset, const bool scrubbing, QVector<ClipPtr>& nests) {
  if (reset) {
    // note: for video, playhead is in "internal clip" frames - for audio, it's the timeline playhead
    reset_cache(playhead);
    audio_playback.reset = false;
  }

  if (timeline_info.media == nullptr) {
    if (timeline_info.track_ >= 0) {
      cache_audio_worker(scrubbing, nests);
    }
  } else if (timeline_info.media->type() == MediaType::FOOTAGE) {
    if ( (media_handling_.stream_ == nullptr) || (media_handling_.stream_->codecpar == nullptr) ) {
      return;
    }
    if (media_handling_.stream_->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      cache_video_worker(playhead);
    } else if (media_handling_.stream_->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      cache_audio_worker(scrubbing, nests);
    }
  }
}

/**
 * @brief reset_cache
 * @param target_frame
 */
void Clip::reset_cache(const long target_frame) {
  // if we seek to a whole other place in the timeline, we'll need to reset the cache with new values
  if (timeline_info.media == nullptr) {
    if (timeline_info.track_ >= 0) {
      // tone clip
      reached_end = false;
      audio_playback.target_frame = target_frame;
      audio_playback.frame_sample_index = -1;
      media_handling_.frame_->pts = 0;
    }
  } else {
    auto ftg = timeline_info.media->object<Footage>();
    if (!ftg) return;

    FootageStreamPtr ms;
    if (mediaType() == ClipType::VISUAL) {
      ms = ftg->video_stream_from_file_index(timeline_info.media_stream);
    } else {
      ms = ftg->audio_stream_from_file_index(timeline_info.media_stream);
    }

    if (ms == nullptr) {
      return;
    }
    if (ms->infinite_length) {
      /*avcodec_flush_buffers(media_handling.codec_ctx_);
            av_seek_frame(media_handling.formatCtx, ms.file_index, 0, AVSEEK_FLAG_BACKWARD);*/
      use_existing_frame = false;
    } else {
      if (media_handling_.stream_->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        // clear current queue
        clearQueue();

        // seeks to nearest keyframe (target_frame represents internal clip frame)
        const int64_t target_ts = seconds_to_timestamp(playhead_to_seconds(target_frame));
        int64_t seek_ts = target_ts;
        const int64_t timebase_half_second = qRound64(av_q2d(av_inv_q(media_handling_.stream_->time_base)));
        if (timeline_info.reverse) {
          seek_ts -= timebase_half_second;
        }

        while (true) {
          // flush ffmpeg codecs
          avcodec_flush_buffers(media_handling_.codec_ctx_);
          reached_end = false;

          if (seek_ts > 0) {
            av_seek_frame(media_handling_.format_ctx_, ms->file_index, seek_ts, AVSEEK_FLAG_BACKWARD);

            av_frame_unref(media_handling_.frame_);
            Q_ASSERT(media_handling_.frame_);
            int ret = retrieve_next_frame(*media_handling_.frame_);
            if (ret < 0) {
              av_strerror(ret, err.data(), ERR_LEN);
              qCritical() << "Seeking terminated prematurely, msg =" << err.data();
              break;
            }
            if (media_handling_.frame_->pts <= target_ts) {
              use_existing_frame = true;
              break;
            }
            seek_ts -= timebase_half_second;
          } else {
            av_frame_unref(media_handling_.frame_);
            av_seek_frame(media_handling_.format_ctx_, ms->file_index, 0, AVSEEK_FLAG_BACKWARD);
            use_existing_frame = false;
            break;
          }
        }
      } else if (media_handling_.stream_->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        // flush ffmpeg codecs
        avcodec_flush_buffers(media_handling_.codec_ctx_);
        reached_end = false;

        // seek (target_frame represents timeline timecode in frames, not clip timecode)

        int64_t timestamp = seconds_to_timestamp(playhead_to_seconds(target_frame));

        if (timeline_info.reverse) {
          audio_playback.reverse_target = timestamp;
          timestamp -= av_q2d(av_inv_q(media_handling_.stream_->time_base));
        }
        av_seek_frame(media_handling_.format_ctx_, ms->file_index, timestamp, AVSEEK_FLAG_BACKWARD);
        audio_playback.target_frame = target_frame;
        audio_playback.frame_sample_index = -1;
        audio_playback.just_reset = true;
      }
    }
  }
}


void Clip::move(ComboAction &ca,
                const long iin, const long iout, const long iclip_in, const int itrack,
                const bool verify_transitions, const bool relative)
{
  // TODO: move the undo action of this class
  ca.append(new MoveClipAction(shared_from_this(), iin, iout, iclip_in, itrack, relative));

  if (!verify_transitions) {
    return;
  }

  const int64_t new_in = relative ? iin + timeline_info.in : iin;
  const int32_t new_track = relative ? itrack + timeline_info.track_ : itrack;
  const int64_t new_out = relative ? iout + timeline_info.out : iout;

  verifyTransitions(ca, new_in, new_out, new_track);
}


bool Clip::loadInEffect(QXmlStreamReader& stream)
{
  QString eff_name;
  bool eff_enabled = false;
  for (const auto& attr : stream.attributes()) {
    const auto name = attr.name().toString().toLower();
    if (name == "name") {
      eff_name = attr.value().toString().toLower();
    } else if (name == "enabled") {
      eff_enabled = attr.value() == "true";
    } else {
      qWarning() << "Unhandled attribute";
    }
  }
  const EffectMeta meta = Effect::getRegisteredMeta(eff_name);
  if (auto eff = create_effect(shared_from_this(), meta, true)) {
    eff->set_enabled(eff_enabled);
    if (!eff->load(stream)) {
      qCritical() << "Failed to load clip effect";
      return false;
    }
    eff->setupUi();
    effects.append(eff);
    return true;
  }
  return false;
}


TransitionPtr Clip::loadTransition(QXmlStreamReader& stream)
{
  QString tran_name;
  int tran_length = 0;
  int secondary_id = -1;
  while (stream.readNextStartElement()) {
    const auto name = stream.name().toString().toLower();
    if (name == "name") {
      tran_name = stream.readElementText();
    } else if (name == "length") {
      tran_length = stream.readElementText().toInt();
    } else if (name == "secondary_clip") {
      secondary_id = stream.readElementText().toInt();
    } else {
      qWarning() << "Unknown element" << name;
      stream.skipCurrentElement();
    }
  }

  if ( (tran_name.size() > 0) && (tran_length > 0) ) {
    // both seemingly valid values loaded from file
    auto meta = Effect::getRegisteredMeta(tran_name);
    if (meta.type > -1) {
      if (auto tran = get_transition_from_meta(shared_from_this(), nullptr, meta, true)) {
        tran->setLength(tran_length);
        tran->setSecondaryLoadId(secondary_id);
        return tran;
      } else {
        qWarning() << "Unabled to get transition from meta";
      }
    }
    qWarning() << "Invalid Effect meta for Transition" << tran_name;
  }
  return nullptr;
}


void Clip::linkClips(const QVector<ClipPtr>& linked_clips) const
{
  for (const auto& link : linked_clips) {
    if (link == nullptr) {
      qWarning() << "Clip instance is null";
      continue;
    }
    for (const auto& other : linked_clips) {
      if (other == nullptr) {
        continue;
      }
      link->addLinkedClip(*other);
    }
  }
}


void Clip::setId(const int32_t id)
{
  id_ = id;
  if (next_id <= id) {
    next_id = id + 1;
  }
}


void Clip::verifyTransitions(ComboAction &ca, const int64_t new_in, const int64_t new_out, const int32_t new_track)
{
  // if there is an opening transition with a secondary, and this clip is separating from the secondary clip,
  // delete both transitions

  if ( transition_.opening_ != nullptr) {
    if (auto secondary = transition_.opening_->secondaryClip()) {
      if ( (secondary->timeline_info.out != new_in) || (secondary->timeline_info.track_ != new_track) ) {
        // delete transitions
        ca.append(new DeleteTransitionCommand(shared_from_this(), ClipTransitionType::OPENING));
        ca.append(new DeleteTransitionCommand(secondary, ClipTransitionType::CLOSING));
      }
    }
  }

  if (transition_.closing_ != nullptr)  {
    if (auto secondary = transition_.closing_->secondaryClip()) {
      if ( (secondary->timeline_info.in != new_out) || (secondary->timeline_info.track_ != new_track) )  {
        // delete transitions
        ca.append(new DeleteTransitionCommand(shared_from_this(), ClipTransitionType::CLOSING));
        ca.append(new DeleteTransitionCommand(secondary, ClipTransitionType::OPENING));
      }
    }
  }
}

