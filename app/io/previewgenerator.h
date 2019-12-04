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
#ifndef PREVIEWGENERATOR_H
#define PREVIEWGENERATOR_H

#include <QThread>
#include <QSemaphore>
#include <memory>

#include "project/footage.h"
#include "project/media.h"
#include "io/audiowaveformgenerator.h"

constexpr int ICON_TYPE_VIDEO = 0;
constexpr int ICON_TYPE_AUDIO = 1;
constexpr int ICON_TYPE_IMAGE = 2;
constexpr int ICON_TYPE_ERROR = 3;


struct AVFormatContext;

class PreviewGenerator : public QThread
{
    Q_OBJECT
public:
    PreviewGenerator(MediaPtr item, const FootagePtr& ftg, const bool replacing, QObject* parent=nullptr);
    virtual ~PreviewGenerator() override;

    PreviewGenerator() = delete;
    PreviewGenerator(const PreviewGenerator&) = delete;
    PreviewGenerator(const PreviewGenerator&&) = delete;
    PreviewGenerator& operator=(const PreviewGenerator&) = delete;
    PreviewGenerator& operator=(const PreviewGenerator&&) = delete;

    void cancel();
protected:
    virtual void run() override;
signals:
    void set_icon(int, bool);
private:
    AVFormatContext* fmt_ctx;
    MediaPtr media;
    FootageWPtr footage;
    bool contains_still_image;
    bool replace;
    bool cancelled;
    QString data_path;
    std::unique_ptr<chestnut::io::AudioWaveformGenerator> wav_gen_;

    void parse_media();
    /**
     * @brief retrieve pre-existing (file-system) previews
     * @param hash used to generate file_path
     * @return  true==already exists and loaded in FootageStream of Media (groan)
     * @return
     */
    bool retrieve_preview(const QString &hash);
    void generate_waveform();
    void finalize_media();
    QString get_thumbnail_path(const QString &hash,  project::FootageStreamPtr& ms);
    QString get_waveform_path(const QString& hash,  project::FootageStreamPtr& ms);
    QString getWaveformPath(QString file_path, const int index) const;

    bool generate_image_thumbnail(const FootagePtr& ftg) const;
    void generateImagePreview(FootagePtr ftg);
    void generateAudioPreview(FootagePtr ftg, project::FootageStreamPtr ms);
    void generateVideoPreview();

    QString generatePreviewHash(QString file_path) const;
};

using PreviewGeneratorPtr = std::shared_ptr<PreviewGenerator>;
using PreviewGeneratorUPtr = std::unique_ptr<PreviewGenerator>;
using PreviewGeneratorWPtr = std::weak_ptr<PreviewGenerator>;
#endif // PREVIEWGENERATOR_H
