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
#include "sourcescommon.h"

#include "panels/panels.h"
#include "project/media.h"
#include "project/undo.h"
#include "panels/timeline.h"
#include "panels/project.h"
#include "project/footage.h"
#include "panels/viewer.h"
#include "project/projectfilter.h"
#include "io/config.h"
#include "mainwindow.h"

#include <QProcess>
#include <QMenu>
#include <QAbstractItemView>
#include <QMimeData>
#include <QMessageBox>
#include <QDesktopServices>

SourcesCommon::SourcesCommon(Project* parent) :
    project_parent(parent),
    editing_item(nullptr)
{
    rename_timer.setInterval(1000);
    connect(&rename_timer, SIGNAL(timeout()), this, SLOT(rename_interval()));
}

void SourcesCommon::create_seq_from_selected() {
    if (!selected_items.isEmpty()) {
        QVector<MediaPtr> media_list;
        for (int i=0;i<selected_items.size();i++) {
            media_list.append(project_parent->item_to_media(selected_items.at(i)));
        }

        ComboAction* ca = new ComboAction();
        SequencePtr  s = create_sequence_from_media(media_list);

        // add clips to it
        e_panel_timeline->create_ghosts_from_media(s, 0, media_list);
        e_panel_timeline->add_clips_from_ghosts(ca, s);

        project_parent->new_sequence(ca, s, true, nullptr);
        e_undo_stack.push(ca);
    }
}

void SourcesCommon::show_context_menu(QWidget* parent, const QModelIndexList& items) {
    QMenu menu(parent);

    selected_items = items;

    QAction* import_action = menu.addAction(tr("Import..."));
    QObject::connect(import_action, SIGNAL(triggered(bool)), project_parent, SLOT(import_dialog()));

    QMenu* new_menu = menu.addMenu(tr("New"));
    global::mainWindow->make_new_menu(new_menu);

    if (items.size() > 0) {
        MediaPtr mda = project_parent->item_to_media(items.at(0));

        if (mda != nullptr) {
            if (items.size() == 1) {
                // replace footage
                int type = mda->get_type();
                if (type == MEDIA_TYPE_FOOTAGE) {
                    QAction* replace_action = menu.addAction(tr("Replace/Relink Media"));
                    QObject::connect(replace_action, SIGNAL(triggered(bool)), project_parent, SLOT(replace_selected_file()));

#if defined(Q_OS_WIN)
                    QAction* reveal_in_explorer = menu.addAction(tr("Reveal in Explorer"));
#elif defined(Q_OS_MAC)
                    QAction* reveal_in_explorer = menu.addAction(tr("Reveal in Finder"));
#else
                    QAction* reveal_in_explorer = menu.addAction(tr("Reveal in File Manager"));
#endif
                    QObject::connect(reveal_in_explorer, SIGNAL(triggered(bool)), this, SLOT(reveal_in_browser()));
                }
                if (type != MEDIA_TYPE_FOLDER) {
                    QAction* replace_clip_media = menu.addAction(tr("Replace Clips Using This Media"));
                    QObject::connect(replace_clip_media, SIGNAL(triggered(bool)), project_parent, SLOT(replace_clip_media()));
                }
            }

            // duplicate item
            bool all_sequences = true;
            bool all_footage = true;
            for (int i=0;i<items.size();i++) {
                if (mda->get_type() != MEDIA_TYPE_SEQUENCE) {
                    all_sequences = false;
                }
                if (mda->get_type() != MEDIA_TYPE_FOOTAGE) {
                    all_footage = false;
                }
            }

            // create sequence from
            QAction* create_seq_from = menu.addAction(tr("Create Sequence With This Media"));
            QObject::connect(create_seq_from, SIGNAL(triggered(bool)), this, SLOT(create_seq_from_selected()));

            // ONLY sequences are selected
            if (all_sequences) {
                // ONLY sequences are selected
                QAction* duplicate_action = menu.addAction(tr("Duplicate"));
                QObject::connect(duplicate_action, SIGNAL(triggered(bool)), project_parent, SLOT(duplicate_selected()));
            }

            // ONLY footage is selected
            if (all_footage) {
                QAction* delete_footage_from_sequences = menu.addAction(tr("Delete All Clips Using This Media"));
                QObject::connect(delete_footage_from_sequences, SIGNAL(triggered(bool)), project_parent, SLOT(delete_clips_using_selected_media()));
            }

            // delete media
            QAction* delete_action = menu.addAction(tr("Delete"));
            QObject::connect(delete_action, SIGNAL(triggered(bool)), project_parent, SLOT(delete_selected_media()));

            if (items.size() == 1) {
                QAction* properties_action = menu.addAction(tr("Properties..."));
                QObject::connect(properties_action, SIGNAL(triggered(bool)), project_parent, SLOT(open_properties()));
            }
        } else {
            qCritical() << "Null Media Ptr";
        }
    }

    menu.addSeparator();

    QAction* tree_view_action = menu.addAction(tr("Tree View"));
    connect(tree_view_action, SIGNAL(triggered(bool)), project_parent, SLOT(set_tree_view()));

    QAction* icon_view_action = menu.addAction(tr("Icon View"));
    connect(icon_view_action, SIGNAL(triggered(bool)), project_parent, SLOT(set_icon_view()));

    QAction* toolbar_action = menu.addAction(tr("Show Toolbar"));
    toolbar_action->setCheckable(true);
    toolbar_action->setChecked(project_parent->toolbar_widget->isVisible());
    connect(toolbar_action, SIGNAL(triggered(bool)), project_parent->toolbar_widget, SLOT(setVisible(bool)));

    QAction* show_sequences = menu.addAction(tr("Show Sequences"));
    show_sequences->setCheckable(true);
    show_sequences->setChecked(e_panel_project->sorter->get_show_sequences());
    connect(show_sequences, SIGNAL(triggered(bool)), e_panel_project->sorter, SLOT(set_show_sequences(bool)));

    menu.exec(QCursor::pos());
}

void SourcesCommon::mousePressEvent(QMouseEvent *) {
    stop_rename_timer();
}

void SourcesCommon::item_click(MediaPtr m, const QModelIndex& index) {
    if (editing_item == m) {
        rename_timer.start();
    } else {
        editing_item = m;
        editing_index = index;
    }
}

void SourcesCommon::mouseDoubleClickEvent(QMouseEvent *, const QModelIndexList& selected_items) {
    stop_rename_timer();
    if (project_parent != nullptr) {
        if (selected_items.size() == 0) {
            project_parent->import_dialog();
        } else if (selected_items.size() == 1) {
            MediaPtr item = project_parent->item_to_media(selected_items.at(0));
            if (item != nullptr) {
                switch (item->get_type()) {
                case MEDIA_TYPE_FOOTAGE:
                    e_panel_footage_viewer->set_media(item);
                    e_panel_footage_viewer->setFocus();
                    break;
                case MEDIA_TYPE_SEQUENCE:
                    e_undo_stack.push(new ChangeSequenceAction(item->get_object<Sequence>()));
                    break;
                default:
                    qWarning() << "Unknown media type" << item->get_type();
                    break;
                }//switch
            }
        } else {
            // TODO: ????
        }
    }
}

void SourcesCommon::dropEvent(QWidget* parent, QDropEvent *event, const QModelIndex& drop_item, const QModelIndexList& items) {
    const QMimeData* mimeData = event->mimeData();
    auto m = project_parent->item_to_media(drop_item);
    if (!m) {
        qCritical() << "Null Media ptr";
        return;
    }
    if (mimeData->hasUrls()) {
        // drag files in from outside
        QList<QUrl> urls = mimeData->urls();
        if (!urls.isEmpty()) {
            QStringList paths;
            for (int i=0;i<urls.size();i++) {
                paths.append(urls.at(i).toLocalFile());
            }
            bool replace = false;
            if (urls.size() == 1
                    && drop_item.isValid()
                    && m->get_type() == MEDIA_TYPE_FOOTAGE
                    && !QFileInfo(paths.at(0)).isDir()
                    && e_config.drop_on_media_to_replace
                    && QMessageBox::question(
                        parent,
                        tr("Replace Media"),
                        tr("You dropped a file onto '%1'. Would you like to replace it with the dropped file?").arg(m->get_name()),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
                replace = true;
                project_parent->replace_media(m, paths.at(0));
            }
            if (!replace) {
                QModelIndex parent;
                if (drop_item.isValid()) {
                    if (m->get_type() == MEDIA_TYPE_FOLDER) {
                        parent = drop_item;
                    } else {
                        parent = drop_item.parent();
                    }
                }
                project_parent->process_file_list(paths, false, nullptr, e_panel_project->item_to_media(parent));
            }
        }
        event->acceptProposedAction();
    } else {
        event->ignore();

        // dragging files within project
        // if we dragged to the root OR dragged to a folder
        if (!drop_item.isValid() ||  m->get_type() == MEDIA_TYPE_FOLDER) {
            QVector<MediaPtr> move_items;
            for (int i=0;i<items.size();i++) {
                const QModelIndex& item = items.at(i);
                const QModelIndex& parent = item.parent();
                MediaPtr s = project_parent->item_to_media(item);
                if (parent != drop_item && item != drop_item) {
                    bool ignore = false;
                    if (parent.isValid()) {
                        // if child belongs to a selected parent, assume the user is just moving the parent and ignore the child
                        QModelIndex par = parent;
                        while (par.isValid() && !ignore) {
                            for (int j=0;j<items.size();j++) {
                                if (par == items.at(j)) {
                                    ignore = true;
                                    break;
                                }
                            }
                            par = par.parent();
                        }
                    }
                    if (!ignore) {
                        move_items.append(s);
                    }
                }
            }
            if (move_items.size() > 0) {
                MediaMove* mm = new MediaMove();
                mm->to = m;
                mm->items = move_items;
                e_undo_stack.push(mm);
            }
        }
    }
}

void SourcesCommon::reveal_in_browser() {
    MediaPtr media = project_parent->item_to_media(selected_items.at(0));
    FootagePtr m = media->get_object<Footage>();

#if defined(Q_OS_WIN)
    QStringList args;
    args << "/select," << QDir::toNativeSeparators(m->url);
    QProcess::startDetached("explorer", args);
#elif defined(Q_OS_MAC)
    QStringList args;
    args << "-e";
    args << "tell application \"Finder\"";
    args << "-e";
    args << "activate";
    args << "-e";
    args << "select POSIX file \""+m->url+"\"";
    args << "-e";
    args << "end tell";
    QProcess::startDetached("osascript", args);
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(m->url.left(m->url.lastIndexOf('/'))));
#endif
}

void SourcesCommon::stop_rename_timer() {
    rename_timer.stop();
}

void SourcesCommon::rename_interval() {
    stop_rename_timer();
    if (view->hasFocus() && editing_item != nullptr) {
        view->edit(editing_index);
    }
}

void SourcesCommon::item_renamed(MediaPtr item) {
    if (editing_item == item) {
        MediaRename* mr = new MediaRename(item, "idk");
        e_undo_stack.push(mr);
        editing_item = nullptr;
    }
}
