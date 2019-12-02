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
#ifndef CLIP_H
#define CLIP_H

#include <QWaitCondition>
#include <QMutex>
#include <QVector>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>
#include <memory>
#include <QMetaType>
#include <QThread>

#include "project/effect.h"
#include "project/sequence.h"
#include "project/sequenceitem.h"
#include "project/footage.h"
#include "project/media.h"
#include "project/timelineinfo.h"


class Transition;
class ComboAction;

struct AVFormatContext;
struct AVStream;
struct AVCodec;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;
struct AVFilterGraph;
struct AVFilterContext;
struct AVDictionary;


using ClipPtr = std::shared_ptr<Clip>;
using ClipUPtr = std::unique_ptr<Clip>;
using ClipWPtr = std::weak_ptr<Clip>;

enum class ClipTransitionType {
  OPENING,
  CLOSING,
  BOTH
};

enum class ClipType {
  AUDIO,
  VISUAL,
  UNKNOWN
};

class Clip : public project::SequenceItem,
    public std::enable_shared_from_this<Clip>,
    public project::IXMLStreamer,
    protected QThread
{
public:

  explicit Clip(SequencePtr s);
  virtual ~Clip() override;
  ClipPtr copy(SequencePtr s);
  ClipPtr copyPreserveLinks(SequencePtr s);

  Clip() = delete;
  Clip(const Clip&) = delete;
  Clip(const Clip&&) = delete;
  Clip& operator=(const Clip&) = delete;
  Clip& operator=(const Clip&&) = delete;

  QString name() const override;

  bool isActive(const long playhead);
  /**
     * @brief Identify if the clip is being cached
     * @return true==caching used
     */
  bool usesCacher() const;
  /**
   * @brief open_worker
   * @return true==success
   */
  bool openWorker();
  /**
   * @brief Free resources made via libav
   */
  void closeWorker();
  /**
   * @brief Open clip and allocate necessary resources
   * @param open_multithreaded
   * @return true==success
   */
  bool open(const bool open_multithreaded);
  /**
   * @brief mediaOpen
   * @return  true==clip's media has been opened
   */
  virtual bool mediaOpen() const;
  /**
   * @brief Close this clip and free up resources
   * @param wait  Wait on cache?
   */
  void close(const bool wait);
  /**
   * @brief Close this clip and free up resources whilst waiting
   */
  void closeWithWait();
  /**
   * @brief Cache the clip at a certain point
   * @param playhead
   * @param reset
   * @param scrubbing
   * @param nests
   * @return  true==cached
   */
  bool cache(const long playhead, const bool do_reset, const bool scrubbing, QVector<ClipPtr>& nests);
  /**
   * @brief Nudge the clip
   * @param pos The amount + direction to nudge the clip
   * @return true==clips position nudged
   */
  bool nudge(const int pos);
  /**
   * @brief         Set the transition effect(s) to the clip if none already
   * @param meta    Information about the Transition effect
   * @param type    opening, closing or both transitions
   * @param length  Length of the transition in frames
   * @return  bool==success
   */
  bool setTransition(const EffectMeta& meta,
                     const ClipTransitionType type,
                     const long length,
                     const ClipPtr& secondary=nullptr);
  /**
   * @brief Delete the Clip's transition(s)
   * @param type  opening, closing or both transitions
   */
  void deleteTransition(const ClipTransitionType type);
  /**
   * @brief Obtain this clip transition
   * @param type  Only 1 transition type i.e. opening or closing
   * @return ptr or null
   */
  TransitionPtr getTransition(const ClipTransitionType type) const;
  /**
   * @brief Split this clip at a sequence frame.
   * If possible to split, a new clip starting after the split is returned and the current clip is modified.
   * @param frame   Sequence playhead
   * @return  The Clip after the split or nullptr
   */
  ClipPtr split(const long frame);
  /**
   * @brief             Merge in a clip that was created in a split
   * @param split_clip
   * @return            true==split clip was merged to this clip
   */
  bool merge(const Clip& split_clip);
  /**
   * @brief         Split this clip and it's linked clips, ensuring the splits are linked afterwards
   * @param frame   Sequence playhead
   * @return        A list of linked, split clips
   */
  QVector<ClipPtr> splitAll(const long frame);

  /**
   * @brief The length in frames of the clip
   * @return
   */
  int64_t length() const noexcept;

  void resetAudio();
  void reset();
  void refresh();
  long clipInWithTransition() const;
  long timelineInWithTransition() const;
  long timelineOutWithTransition() const;
  long length();
  double mediaFrameRate();
  long maximumLength() const;
  void recalculateMaxLength();
  int width();
  int height();
  int32_t id() const;
  void refactorFrameRate(ComboAction* ca, double multiplier, bool change_timeline_points);

  // queue functions
  void clearQueue();
  void removeEarliestFromQueue();


  [[deprecated("Use Clip::getTransition")]]
  TransitionPtr openingTransition() const;
  [[deprecated("Use Clip::getTransition")]]
  TransitionPtr closingTransition() const;

  /**
   * @brief set frame cache to a position
   * @param playhead
   */
  virtual void frame(const long playhead, bool& texture_failed);
  /**
     * @brief get_timecode
     * @param playhead
     * @return
     */
  double timecode(const long playhead);
  /**
     * @brief Identify if this clip is selected in the project's current sequence
     * @param containing
     * @return true==is selected
     */
  bool isSelected(const bool containing);
  /**
     * @brief     Identify if this clip is contained by a selection area
     * @param sel The area selected
     * @return    true==clip is selected
     */
  bool isSelected(const Selection& sel) const;
  /**
     * @brief Identify if clip is populated at frame position (irregardless of track)
     * @param frame position
     * @return true if clip in range
     */
  bool inRange(const long frame) const; //TODO: think of a better name
  /**
   * @brief Obtain this clip type
   * @return
   */
  ClipType mediaType() const;
  /**
   * @brief Obtain the Media parent
   * @return MediaPtr or null
   */
  MediaPtr parentMedia();

  void addLinkedClip(const Clip& clp);
  void setLinkedClips(const QVector<int32_t>& links);
  const QVector<int32_t>& linkedClipIds() const;
  QVector<ClipPtr> linkedClips() const;
  void clearLinks();
  /**
   * @brief Get tracks of linked clips
   * @return set of timeline tracks
   */
  QSet<int> getLinkedTracks() const;
  /**
   * @brief           Update the linked clips using a mapping of old_id : new_clip
   * @param mapping   Mapped ids and clips
   */
  void relink(const QMap<int, int>& mapping);
  void setId(const int32_t id);
  void move(ComboAction& ca, const long iin, const long iout,
            const long iclip_in, const int itrack, const bool verify_transitions = true,
            const bool relative = false);

  virtual bool load(QXmlStreamReader& stream) override;
  virtual bool save(QXmlStreamWriter& stream) const override;

  /**
   * @brief     Verify the transitions are valid with its secondary clip
   * @param ca  For undo actions
   */
  void verifyTransitions(ComboAction &ca);
  /**
   * @brief   Is this Clip due to being drawn on the timeline (Title,Noise, Adjustment Layer, etc clip)
   * @return  true==manually created
   */
  virtual bool isCreatedObject() const;

  /**
   * @brief     Identify if this clip is locked (editing prevented) in its containing sequence
   * @return    true==locked
   */
  bool locked() const;

  //FIXME: all the class members
  SequencePtr sequence;

  // timeline variables (should be copied in copy())
  project::TimelineInfo timeline_info;

  // other variables (should be deep copied/duplicated in copy())
  QList<EffectPtr> effects;

  // media handling
  struct {
    AVFormatContext* format_ctx_ {nullptr};
    AVStream* stream_ {nullptr};
    AVCodec* codec_ {nullptr};
    AVCodecContext* codec_ctx_ {nullptr};
    AVPacket* pkt_ {nullptr};
    AVFrame* frame_ {nullptr};
    AVDictionary* opts_ {nullptr};
    long calculated_length_ {-1};
  } media_handling_; //FIXME: the use of this lot should really be its own library/class

  // temporary variables
  bool deleteable{true};
  bool reached_end{};
  bool is_open{};
  bool replaced;
  std::atomic_bool ignore_reverse{false};
  int pix_fmt{};

  // caching functions
  bool use_existing_frame;
  bool multithreaded{};
  QWaitCondition can_cache;
  int max_queue_size{};
  QVector<AVFrame*> queue;
  QMutex queue_lock;
  QMutex lock;
  QMutex open_lock;
  int64_t last_invalid_ts{};

  // converters/filters
  AVFilterGraph* filter_graph;
  AVFilterContext* buffersink_ctx{};
  AVFilterContext* buffersrc_ctx{};

  // video playback variables
  QOpenGLFramebufferObject** fbo;
  std::unique_ptr<QOpenGLTexture> texture = nullptr;
  long texture_frame{};

  struct AudioPlaybackInfo {
    // audio playback variables
    int64_t reverse_target = 0;
    int frame_sample_index = -1;
    int buffer_write = -1;
    bool reset = false;
    bool just_reset = false;
    long target_frame = -1;
  } audio_playback;

  struct {
    TransitionPtr opening_{nullptr};
    TransitionPtr closing_{nullptr};
  } transition_;


protected:
  virtual void run() override;
  static int32_t next_id;
private:
  friend class ClipTest;
  friend class ObjectClip;
  struct {
    bool caching = false;
    // must be set before caching
    long playhead = 0;
    bool reset = false;
    bool scrubbing = false;
    bool interrupt = false;
    QVector<ClipPtr> nests = QVector<ClipPtr>();
  } cache_info;


  std::atomic_bool finished_opening{false};
  bool pkt_written{};
  int32_t id_{-1};
  //TODO: link to ptrs
  QVector<int32_t> linked; //id of clips linked to this i.e. audio<->video streams of same footage
  ClipType media_type_{ClipType::UNKNOWN};
  bool created_object_{false};

  void apply_audio_effects(const double timecode_start, AVFrame* frame, const int nb_bytes, QVector<ClipPtr>& nests);

  long playhead_to_frame(const long playhead);
  int64_t playhead_to_timestamp(const long playhead);
  bool retrieve_next_frame(AVFrame* frame);
  double playhead_to_seconds(const long playhead);
  int64_t seconds_to_timestamp(const double seconds);

  void cache_audio_worker(const bool scrubbing, QVector<ClipPtr>& nests);
  void cache_video_worker(const long playhead);


  /**
   * @brief To set up the caching thread?
   * @param playhead
   * @param reset
   * @param scrubbing
   * @param nests
   */
  void cache_worker(const long playhead, const bool reset, const bool scrubbing, QVector<ClipPtr>& nests);
  /**
   * @brief reset_cache
   * @param target_frame
   */
  void reset_cache(const long target_frame);
  bool loadInEffect(QXmlStreamReader& stream);
  TransitionPtr loadTransition(QXmlStreamReader& stream);
  void linkClips(const QVector<ClipPtr>& linked_clips) const;

  void verifyTransitions(ComboAction &ca, const int64_t new_in, const int64_t new_out, const int32_t new_track);


};

#endif // CLIP_H
