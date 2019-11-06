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

#include <QProcess>
#include <QMenu>
#include <QAbstractItemView>
#include <QMimeData>
#include <QMessageBox>
#include <QDesktopServices>
#include <QDropEvent>
#include <QUrl>

#include "project/media.h"
#include "project/undo.h"
#include "panels/panelmanager.h"
#include "project/projectfilter.h"
#include "io/config.h"
#include "ui/mainwindow.h"
#include "ui/viewerwidget.h"

constexpr int RENAME_INTERVAL = 1000;

using panels::PanelManager;

SourcesCommon::SourcesCommon(Project* parent) :
  QObject(parent),
  project_parent(parent)
{
  rename_timer.setInterval(RENAME_INTERVAL);
  connect(&rename_timer, SIGNAL(timeout()), this, SLOT(rename_interval()));
}

void SourcesCommon::create_seq_from_selected()
{
  if (!selected_items.isEmpty()) {
    QVector<MediaPtr> media_list;
    for (const auto& item : selected_items) {
      media_list.append(project_parent->item_to_media(item));
    }

    auto ca = new ComboAction();
    auto seq = std::make_shared<Sequence>(media_list, PanelManager::projectViewer().get_next_sequence_name());

    // add clips to it
    panels::PanelManager::timeLine().createGhostsFromMedia(seq, 0, media_list);
    panels::PanelManager::timeLine().addClipsFromGhosts(ca, seq);

    project_parent->newSequence(ca, seq, true, nullptr);
    e_undo_stack.push(ca);
  }
}

void SourcesCommon::show_context_menu(QWidget* parent, const QModelIndexList& items)
{
  QMenu menu(parent);

  selected_items = items;

  QAction* import_action = menu.addAction(tr("Import..."));
  QObject::connect(import_action, SIGNAL(triggered(bool)), project_parent, SLOT(import_dialog()));

  QMenu* new_menu = menu.addMenu(tr("New"));
  MainWindow::instance().make_new_menu(*new_menu);

  if (!items.empty()) {
    if (auto mda = project_parent->item_to_media(items.front())) {
      if (items.size() == 1) {
        // replace footage
        const auto type = mda->type();
        if (type == MediaType::FOOTAGE) {
          QAction* replace_action = menu.addAction(tr("Replace/Relink Media"));
          QObject::connect(replace_action, SIGNAL(triggered(bool)), project_parent, SLOT(replace_selected_file()));
          QAction* reveal_in_explorer = menu.addAction(tr("Reveal in File Manager"));
          QObject::connect(reveal_in_explorer, SIGNAL(triggered(bool)), this, SLOT(reveal_in_browser()));
        } else if (type != MediaType::FOLDER) {
          QAction* replace_clip_media = menu.addAction(tr("Replace Clips Using This Media"));
          QObject::connect(replace_clip_media, SIGNAL(triggered(bool)), project_parent, SLOT(replace_clip_media()));
        } else {
          //TODO: who knows?
        }
      }

      // duplicate item
      bool all_sequences = true;
      bool all_footage = true;
      for (int i=0;i<items.size();i++) {
        if (mda->type() != MediaType::SEQUENCE) {
          all_sequences = false;
        }
        if (mda->type() != MediaType::FOOTAGE) {
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
  toolbar_action->setChecked(project_parent->toolbar_widget_->isVisible());
  connect(toolbar_action, SIGNAL(triggered(bool)), project_parent->toolbar_widget_, SLOT(setVisible(bool)));

  QAction* show_sequences = menu.addAction(tr("Show Sequences"));
  show_sequences->setCheckable(true);
  show_sequences->setChecked(PanelManager::projectViewer().sorter_->get_show_sequences());
  connect(show_sequences, SIGNAL(triggered(bool)), PanelManager::projectViewer().sorter_, SLOT(set_show_sequences(bool)));

  menu.exec(QCursor::pos());
}

void SourcesCommon::mousePressEvent(QMouseEvent *)
{
  stop_rename_timer();
}

void SourcesCommon::item_click(const MediaPtr& m, const QModelIndex& index)
{
  if (editing_item == m) {
    rename_timer.start();
  } else {
    editing_item = m;
    editing_index = index;
  }
  if (PanelManager::markersViewer().setMedia(m)) {
    PanelManager::markersViewer().refresh();
  }
}


void SourcesCommon::setCurrentView(QAbstractItemView* currentView)
{
  view = currentView;
}

void SourcesCommon::mouseDoubleClickEvent(QMouseEvent *, const QModelIndexList& items)
{
  stop_rename_timer();
  if (project_parent == nullptr) {
    return;
  }
  if (items.empty()) {
    project_parent->import_dialog();
  } else if (items.size() == 1) {
    auto item = project_parent->item_to_media(items.front());
    if (item != nullptr) {
      switch (item->type()) {
        case MediaType::FOOTAGE:
          PanelManager::footageViewer().set_media(item);
          PanelManager::footageViewer().show();
          PanelManager::footageViewer().raise();
          PanelManager::footageViewer().setFocus(); // this action actually stops rename
          break;
        case MediaType::SEQUENCE:
          e_undo_stack.push(new ChangeSequenceAction(item->object<Sequence>()));
          break;
        default:
          qWarning() << "Unknown media type" << static_cast<int>(item->type());
          break;
      }//switch
    }
  } else {
    // TODO: ????
  }
}

void SourcesCommon::dropEvent(QWidget* parent,
                              QDropEvent *event,
                              const QModelIndex& drop_item,
                              const QModelIndexList& items)
{
  Q_ASSERT(parent);
  Q_ASSERT(event);

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
      for (const auto& url : urls ) {
        paths.append(url.toLocalFile());
      }

      bool replace = false;
      if ( (urls.size() == 1)
           && drop_item.isValid()
           && (m->type() == MediaType::FOOTAGE)
           && !QFileInfo(paths.front()).isDir()
           && e_config.drop_on_media_to_replace
           && QMessageBox::question(
             parent,
             tr("Replace Media"),
             tr("You dropped a file onto '%1'. Would you like to replace it with the dropped file?").arg(m->name()),
             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        replace = true;
        project_parent->replace_media(m, paths.front());
      }
      if (!replace) {
        QModelIndex parentIndex;
        if (drop_item.isValid()) {
          if (m->type() == MediaType::FOLDER) {
            parentIndex = drop_item;
          } else {
            parentIndex = drop_item.parent();
          }
        }
        project_parent->process_file_list(paths, false, nullptr, PanelManager::projectViewer().item_to_media(parentIndex));
      }
    }
    event->acceptProposedAction();
  } else {
    event->ignore();

    // dragging files within project
    // if we dragged to the root OR dragged to a folder
    if (!drop_item.isValid() ||  m->type() == MediaType::FOLDER) {
      QVector<MediaPtr> move_items;
      for (const auto& item : items) {
        const QModelIndex& itemParent = item.parent();
        auto seq = project_parent->item_to_media(item);
        if ( (itemParent != drop_item) && (item != drop_item) ) {
          bool ignore = false;
          if (itemParent.isValid()) {
            // if child belongs to a selected parent, assume the user is just moving the parent and ignore the child
            QModelIndex par = itemParent;
            while (par.isValid() && !ignore) {
              for (auto subitem : items) {
                if (par == subitem) {
                  ignore = true;
                  break;
                }
              }//for
              par = par.parent();
            }//while
          }
          if (!ignore) {
            move_items.append(seq);
          }
        }
      }
      if (!move_items.empty()) {
        auto mm = new MediaMove();
        mm->to = m;
        mm->items = move_items;
        e_undo_stack.push(mm);
      }
    }
  }
}

void SourcesCommon::reveal_in_browser()
{
  if (selected_items.empty()) {
    return;
  }
  auto media = project_parent->item_to_media(selected_items.front());
  if (!media) {
    return;
  }
  auto ftg = media->object<Footage>();
  if (!ftg) {
    return;
  }

  auto dir = QFileInfo(ftg->location()).absoluteDir().path();
  QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void SourcesCommon::stop_rename_timer()
{
  rename_timer.stop();
}

void SourcesCommon::rename_interval() {
  stop_rename_timer();
  if (view->hasFocus() && editing_item != nullptr) {
    view->edit(editing_index);
  }
}

void SourcesCommon::item_renamed(const MediaPtr& item)
{
  if (editing_item == item) {
    auto mr = new MediaRename(item, "idk");
    e_undo_stack.push(mr);
    editing_item = nullptr;
  }
}
