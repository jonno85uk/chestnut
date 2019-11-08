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
#ifndef EXPORTTHREAD_H
#define EXPORTTHREAD_H

#include <QThread>
#include <QOffscreenSurface>
#include <QMutex>
#include <QWaitCondition>


#include "ui/renderthread.h"
#include "panels/viewer.h"
#include "coderconstants.h"

class ExportDialog;
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct AVStream;
struct AVCodec;
struct SwsContext;
struct SwrContext;

extern "C" {
#include <libavcodec/avcodec.h>
}

enum class CompressionType {
  CBR = 0,
  CRF,
  TARGETSIZE,
  TARGETBITRATE,
  UNKNOWN
};

enum class InterpolationType
{
  FAST_BILINEAR,
  BILINEAR,
  BICUBIC,
  BICUBLIN,
  LANCZOS
};

class ExportThread : public QThread {
    Q_OBJECT
  public:
    ExportThread();

    ExportThread(const ExportThread& ) = delete;
    ExportThread& operator=(const ExportThread&) = delete;

    // export parameters
    QString filename;
    struct Params {
        double frame_rate_ {0.0};
        double bitrate_ {0.0};
        int codec_ {-1};
        int width_ {-1};
        int height_ {-1};
        CompressionType compression_type_ {CompressionType::UNKNOWN};
        int gop_length_{};
        int b_frames_{};
        QString profile_;
        QString level_;
        PixFmtList pix_fmts_;
        InterpolationType interpol_{InterpolationType::FAST_BILINEAR};
        bool enabled = false;
        bool closed_gop_ {false};
    } video_params_;

    struct {
      int codec = -1;
      int sampling_rate = -1;
      int bitrate = -1;
      bool enabled = false;
    } audio_params_;

    int64_t start_frame;
    int64_t end_frame;

    QOffscreenSurface surface;

    ExportDialog* ed {nullptr};

    std::atomic_bool continue_encode_{true};
  protected:
    void run() override;
  signals:
    void progress_changed(int value, qint64 remaining_ms);
  public slots:
    void wake();
  private:
    bool encode(AVFormatContext* ofmt_ctx, AVCodecContext* codec_ctx, AVFrame* frame,
                AVPacket* packet, AVStream* stream, bool rescale);
    bool setupVideo();
    bool setupAudio();
    bool setupContainer();

    AVFormatContext* fmt_ctx = nullptr;
    AVStream* video_stream = nullptr;
    AVCodec* vcodec = nullptr;
    AVCodecContext* vcodec_ctx = nullptr;
    AVFrame* video_frame= nullptr;
    AVFrame* sws_frame = nullptr;
    SwsContext* sws_ctx = nullptr;
    AVStream* audio_stream = nullptr;
    AVCodec* acodec = nullptr;
    AVFrame* audio_frame = nullptr;
    AVFrame* swr_frame = nullptr;
    AVCodecContext* acodec_ctx = nullptr;
    AVPacket video_pkt;
    AVPacket audio_pkt;
    SwrContext* swr_ctx = nullptr;

    int aframe_bytes {0};

    QMutex mutex;
    QWaitCondition waitCond;

    bool vpkt_alloc = false;
    bool apkt_alloc = false;

    bool setUpContext(RenderThread& rt, Viewer& vwr);
    void setDownContext(RenderThread& rt, Viewer& vwr) const;
    void setupH264Encoder(AVCodecContext& ctx, const Params& video_params_) const;
    void setupMPEG2Encoder(AVCodecContext& ctx, AVStream& stream, const Params& video_params_) const;
    void setupMPEG4Encoder(AVCodecContext& ctx, const Params& video_params_) const;
    void setupDNXHDEncoder(AVCodecContext& ctx, const Params& video_params_) const;

    /**
     * @brief Configure codec frame rate parameter
     * @note av_d2q() is not able to reliably go from a double (of drop-type TCs) to an AVRational which will
     *                work with avcodec_open2()
     * @param ctx         Encoder's context
     * @param frame_rate  value > 0
     */
    void setupFrameRate(AVCodecContext& ctx, const double frame_rate) const;

    constexpr int convertInterpolationType(const InterpolationType interpoltype) const noexcept;


    void setupScaler();
};

#endif // EXPORTTHREAD_H
