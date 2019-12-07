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
#ifndef PROJECT_H
#define PROJECT_H

#include <QDockWidget>
#include <QVector>
#include <QTimer>
#include <QDir>
#include <QXmlStreamWriter>
#include <QXmlStreamReader>
#include <QFile>
#include <QSortFilterProxyModel>
#include <QAbstractItemView>

#include "project/projectmodel.h"
#include "project/sequence.h"
#include "project/media.h"
#include "project/previewgeneratorthread.h"

class Footage;

class Clip;
class Timeline;
class Viewer;
class SourceTable;
class ProjectFilter;
class ComboAction;
class SourceIconView;
class QPushButton;
class SourcesCommon;

constexpr int LOAD_TYPE_VERSION = 69;
constexpr int LOAD_TYPE_URL = 70;

extern QString autorecovery_filename;
extern QString project_url;
extern QStringList recent_projects;
extern QString recent_proj_file;

QString get_channel_layout_name(int channels, uint64_t layout);
QString get_interlacing_name(const media_handling::FieldOrder interlacing);

class Project : public QDockWidget {
    Q_OBJECT
public:
    SourceTable* tree_view_{nullptr};
    SourceIconView* icon_view_ {nullptr};
    SourcesCommon* sources_common_ {nullptr};
    ProjectFilter* sorter_ {nullptr};
    QWidget* toolbar_widget_ {nullptr};


    explicit Project(QWidget *parent = nullptr);
    virtual ~Project();

    Project(const Project& ) = delete;
    Project& operator=(const Project&) = delete;

    static ProjectModel& model();

    bool is_focused() const;
    void clear();
    /**
     * @brief               Create a new sequence
     * @param ca            undo history manager object
     * @param seq           sequence to copy
     * @param open          open sequence on creation
     * @param parentItem    the parent for the created item
     * @return              A media item for the model
     */
    MediaPtr newSequence(ComboAction *ca, SequencePtr seq, const bool open, MediaPtr parentItem) const;
    QString get_next_sequence_name(QString start="");
    void process_file_list(QStringList& files, bool recursive = false, MediaPtr replace = nullptr, MediaPtr parent = nullptr);
    void replace_media(MediaPtr item, QString filename);
    MediaPtr get_selected_folder();
    bool reveal_media(MediaPtr media, QModelIndex parent = QModelIndex());
    void add_recent_project(QString url);
    /**
     * @brief           Get a Media item that was in the last import
     * @param index     Location in store
     * @return          ptr or null
     */
    MediaPtr getImportedMedia(const int index);
    /**
     * @brief Get the size of the last Import media list
     * @return  integer
     */
    int getMediaSize() const;


    QAbstractItemView* getCurrentView();

    void new_project();
    void load_project(const bool autorecovery);
    void save_project(const bool autorecovery);

    MediaPtr newFolder(const QString& name="");
    MediaPtr item_to_media(const QModelIndex& index);

    void save_recent_projects();

    QVector<MediaPtr> list_all_project_sequences();
    QModelIndexList get_current_selected();

    void get_all_media_from_table(QVector<MediaPtr> &items, QVector<MediaPtr> &list, const MediaType type = MediaType::NONE);

    /**
     * Redraw/setup viewer using the models items
     */
    void refresh();

    /**
     * @brief Force an widget redraw of the Project panel
     */
    void updatePanel();

public slots:
    void import_dialog();
    void delete_selected_media();
    void duplicate_selected();
    void delete_clips_using_selected_media();
    void replace_selected_file();
    void replace_clip_media();
    void open_properties();
private:
    int folder_id;
    int media_id;
    int sequence_id;
    QDir proj_dir;
    QWidget* icon_view_container;
    QPushButton* directory_up;
    QVector<MediaPtr> last_imported_media;
    inline static std::unique_ptr<ProjectModel> model_ {nullptr};
    chestnut::project::PreviewGeneratorThread* preview_gen_ {nullptr};
    QMap<MediaPtr, MediaThrobber*> media_throbbers_;

    void list_all_sequences_worker(QVector<MediaPtr> &list, MediaPtr parent); //TODO: recursive depth limit
    QString get_file_name_from_path(const QString &path) const;
    QString get_file_ext_from_path(const QString &path) const;

    void getPreview(MediaPtr mda);

    void setModelItemIcon(FootagePtr ftg, QIcon icon) const;

    void stopThrobber(FootagePtr ftg);

private slots:
    void update_view_type();
    void set_icon_view();
    void set_tree_view();
    void clear_recent_projects();
    void set_icon_view_size(int);
    void set_up_dir_enabled();
    void go_up_dir();
    void make_new_menu();
    void setItemIcon(FootageWPtr item);
    void setItemMissing(FootageWPtr item);
};

class MediaThrobber : public QObject {
    Q_OBJECT
public:
    MediaThrobber(MediaPtr item, QObject* parent=nullptr);
public slots:
    void start();
    void stop();
private slots:
    void animation_update();
private:
    QPixmap pixmap;
    int animation;
    MediaPtr item;
    QTimer* animator;
};

#endif // PROJECT_H
