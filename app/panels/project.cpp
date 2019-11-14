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
#include "project.h"

#include <QApplication>
#include <QFileDialog>
#include <QString>
#include <QVariant>
#include <QCharRef>
#include <QMessageBox>
#include <QDropEvent>
#include <QMimeData>
#include <QPushButton>
#include <QInputDialog>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QMenu>
#include <memory>
#include <QStandardPaths>

#include "project/footage.h"
#include "panels/panelmanager.h"
#include "panels/viewer.h"
#include "playback/playback.h"
#include "project/effect.h"
#include "project/transition.h"
#include "project/sequence.h"
#include "project/clip.h"
#include "io/previewgenerator.h"
#include "project/undo.h"
#include "ui/mainwindow.h"
#include "io/config.h"
#include "dialogs/replaceclipmediadialog.h"
#include "panels/effectcontrols.h"
#include "dialogs/newsequencedialog.h"
#include "dialogs/mediapropertiesdialog.h"
#include "io/clipboard.h"
#include "project/media.h"
#include "ui/sourcetable.h"
#include "ui/sourceiconview.h"
#include "project/sourcescommon.h"
#include "project/projectfilter.h"
#include "debug.h"


extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

using panels::PanelManager;

QString autorecovery_filename;
QString project_url = "";
QStringList recent_projects;
QString recent_proj_file;


constexpr int MAXIMUM_RECENT_PROJECTS = 10;
constexpr int THROBBER_INTERVAL       = 20; //ms
constexpr int THROBBER_LIMIT          = 20;
constexpr int THROBBER_SIZE           = 50;
constexpr int MIN_WIDTH = 320;

constexpr auto VIDEO_FMT_FILTER = "*.avi *.m4v *.mkv *.mov *.mp4 *.mts *.mxf *.ogv *.webm *.wmv";
constexpr auto AUDIO_FMT_FILTER = "*.aac *.aif *.alac *.flac *.m4a *.mp3 *.ogg *.wav *.wma";
constexpr auto IMAGE_FMT_FILTER = "*.bmp *.dpx *.exr *.jp2 *.jpeg *.jpg *.png *.tga *.tif *.tiff *.webp";
constexpr auto TMP_SAVE_FILENAME = "tmpsave.nut";

#ifdef QT_NO_DEBUG
constexpr bool XML_SAVE_FORMATTING = false;
#else
constexpr bool XML_SAVE_FORMATTING = true; // creates bigger files
#endif

Project::Project(QWidget *parent) :
  QDockWidget(parent)
{
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  auto dockWidgetContents = new QWidget(this);
  auto verticalLayout = new QVBoxLayout(dockWidgetContents);
  verticalLayout->setContentsMargins(0, 0, 0, 0);
  verticalLayout->setSpacing(0);
  setMinimumWidth(MIN_WIDTH);

  setWidget(dockWidgetContents);

  sources_common_ = new SourcesCommon(this);

  sorter_ = new ProjectFilter(this);
  sorter_->setSourceModel(&Project::model());

  // optional toolbar
  toolbar_widget_ = new QWidget();
  toolbar_widget_->setVisible(global::config.show_project_toolbar);
  QHBoxLayout* toolbar = new QHBoxLayout();
  toolbar->setMargin(0);
  toolbar->setSpacing(0);
  toolbar_widget_->setLayout(toolbar);

  auto toolbar_new = new QPushButton("New");
  toolbar_new->setIcon(QIcon(":/icons/tri-down.png"));
  toolbar_new->setIconSize(QSize(8, 8));
  toolbar_new->setToolTip("New");
  connect(toolbar_new, SIGNAL(clicked(bool)), this, SLOT(make_new_menu()));
  toolbar->addWidget(toolbar_new);

  auto toolbar_open = new QPushButton("Open");
  toolbar_open->setToolTip("Open Project");
  connect(toolbar_open, SIGNAL(clicked(bool)), &MainWindow::instance(), SLOT(open_project()));
  toolbar->addWidget(toolbar_open);

  auto toolbar_save = new QPushButton("Save");
  toolbar_save->setToolTip("Save Project");
  connect(toolbar_save, SIGNAL(clicked(bool)), &MainWindow::instance(), SLOT(save_project()));
  toolbar->addWidget(toolbar_save);

  auto toolbar_undo = new QPushButton("Undo");
  toolbar_undo->setToolTip("Undo");
  connect(toolbar_undo, SIGNAL(clicked(bool)), &MainWindow::instance(), SLOT(undo()));
  toolbar->addWidget(toolbar_undo);

  auto toolbar_redo = new QPushButton("Redo");
  toolbar_redo->setToolTip("Redo");
  connect(toolbar_redo, SIGNAL(clicked(bool)), &MainWindow::instance(), SLOT(redo()));
  toolbar->addWidget(toolbar_redo);

  toolbar->addStretch();

  auto toolbar_tree_view = new QPushButton("Tree View");
  toolbar_tree_view->setToolTip("Tree View");
  connect(toolbar_tree_view, SIGNAL(clicked(bool)), this, SLOT(set_tree_view()));
  toolbar->addWidget(toolbar_tree_view);

  auto toolbar_icon_view = new QPushButton("Icon View");
  toolbar_icon_view->setToolTip("Icon View");
  connect(toolbar_icon_view, SIGNAL(clicked(bool)), this, SLOT(set_icon_view()));
  toolbar->addWidget(toolbar_icon_view);

  verticalLayout->addWidget(toolbar_widget_);

  // tree view
  tree_view_ = new SourceTable(sorter_, *this, dockWidgetContents);
  verticalLayout->addWidget(tree_view_);

  // icon view
  icon_view_container = new QWidget();

  auto icon_view_container_layout = new QVBoxLayout();
  icon_view_container_layout->setMargin(0);
  icon_view_container_layout->setSpacing(0);
  icon_view_container->setLayout(icon_view_container_layout);

  auto icon_view_controls = new QHBoxLayout();
  icon_view_controls->setMargin(0);
  icon_view_controls->setSpacing(0);

  QIcon directory_up_button;
  directory_up_button.addFile(":/icons/dirup.png", QSize(), QIcon::Normal);
  directory_up_button.addFile(":/icons/dirup-disabled.png", QSize(), QIcon::Disabled);

  directory_up = new QPushButton();
  directory_up->setIcon(directory_up_button);
  directory_up->setEnabled(false);
  icon_view_controls->addWidget(directory_up);

  icon_view_controls->addStretch();

  auto icon_size_slider = new QSlider(Qt::Horizontal);
  icon_size_slider->setMinimum(16);
  icon_size_slider->setMaximum(120);
  icon_view_controls->addWidget(icon_size_slider);
  connect(icon_size_slider, SIGNAL(valueChanged(int)), this, SLOT(set_icon_view_size(int)));

  icon_view_container_layout->addLayout(icon_view_controls);

  icon_view_ = new SourceIconView(*this, dockWidgetContents);
  icon_view_->setModel(sorter_);
  icon_view_->setIconSize(QSize(100, 100));
  icon_view_->setViewMode(QListView::IconMode);
  icon_view_->setUniformItemSizes(true);
  icon_view_container_layout->addWidget(icon_view_);

  icon_size_slider->setValue(icon_view_->iconSize().height());

  verticalLayout->addWidget(icon_view_container);

  connect(directory_up, SIGNAL(clicked(bool)), this, SLOT(go_up_dir()));
  connect(icon_view_, SIGNAL(changed_root()), this, SLOT(set_up_dir_enabled()));

  //retranslateUi(Project);
  setWindowTitle(tr("Project"));

  update_view_type();
}

Project::~Project()
{
  delete sorter_;
  delete sources_common_;
}


ProjectModel& Project::model()
{
  if (model_ == nullptr){
    model_ = std::make_unique<ProjectModel>();
  }
  return *model_;
}

QString Project::get_next_sequence_name(QString start)
{
  if (start.isEmpty()) {
    start = tr("Sequence");
  }

  int n = 1;
  bool found = true;
  QString name;
  while (found) {
    found = false;
    name = start + " ";
    if (n < 10) {
      name += "0";
    }
    name += QString::number(n);
    for (int i=0;i<Project::model().childCount();i++) {
      if (QString::compare(Project::model().child(i)->name(), name, Qt::CaseInsensitive) == 0) {
        found = true;
        n++;
        break;
      }
    }
  }
  return name;
}


void Project::duplicate_selected() {
  QModelIndexList items = get_current_selected();
  bool duped = false;
  ComboAction* ca = new ComboAction();
  for (int j=0; j<items.size(); ++j) {
    MediaPtr i = item_to_media(items.at(j));
    if (i->type() == MediaType::SEQUENCE) {
      newSequence(ca, SequencePtr(i->object<Sequence>()->copy()), false, item_to_media(items.at(j).parent()));
      duped = true;
    }
  }
  if (duped) {
    e_undo_stack.push(ca);
  } else {
    delete ca;
  }
}

void Project::replace_selected_file()
{
  QModelIndexList selected_items = get_current_selected();
  if (selected_items.size() == 1) {
    MediaPtr item = item_to_media(selected_items.front());
    if (item->type() == MediaType::FOOTAGE) {
      replace_media(item, nullptr);
    }
  } else {
    qWarning() << "Not able to replace multiple files at one time";
  }
}

void Project::replace_media(MediaPtr item, QString filename) {
  if (filename.isEmpty()) {
    filename = QFileDialog::getOpenFileName(
                 this,
                 tr("Replace '%1'").arg(item->name()),
                 "",
                 tr("All Files") + " (*)");
  }
  if (!filename.isEmpty()) {
    ReplaceMediaCommand* rmc = new ReplaceMediaCommand(item, filename);
    e_undo_stack.push(rmc);
  }
}

void Project::replace_clip_media() {
  if (global::sequence == nullptr) {
    QMessageBox::critical(this,
                          tr("No active sequence"),
                          tr("No sequence is active, please open the sequence you want to replace clips from."),
                          QMessageBox::Ok);
  } else {
    QModelIndexList selected_items = get_current_selected();
    if (selected_items.size() == 1) {
      MediaPtr item = item_to_media(selected_items.at(0));
      if (item->type() == MediaType::SEQUENCE && global::sequence == item->object<Sequence>()) {
        QMessageBox::critical(this,
                              tr("Active sequence selected"),
                              tr("You cannot insert a sequence into itself, so no clips of this media would be in this sequence."),
                              QMessageBox::Ok);
      } else {
        ReplaceClipMediaDialog dialog(this, item);
        dialog.exec();
      }
    }
  }
}

void Project::open_properties() {
  QModelIndexList selected_items = get_current_selected();
  if (selected_items.size() == 1) {
    MediaPtr item = item_to_media(selected_items.at(0));
    switch (item->type()) {
      case MediaType::FOOTAGE:
      {
        MediaPropertiesDialog mpd(this, item);
        mpd.exec();
      }
        break;
      case MediaType::SEQUENCE:
      {
        NewSequenceDialog nsd(this, item);
        nsd.exec();
      }
        break;
      default:
      {
        // fall back to renaming
        QString new_name = QInputDialog::getText(this,
                                                 tr("Rename '%1'").arg(item->name()),
                                                 tr("Enter new name:"),
                                                 QLineEdit::Normal,
                                                 item->name());
        if (!new_name.isEmpty()) {
          auto mr = new MediaRename(item, new_name);
          e_undo_stack.push(mr);
        }
      }
    }
  }
}


MediaPtr Project::newSequence(ComboAction *ca, SequencePtr seq, const bool open, MediaPtr parentItem) const
{
  if (parentItem == nullptr) {
    parentItem = Project::model().root();
  }
  auto item = std::make_shared<Media>(parentItem);
  item->setSequence(seq);

  if (ca != nullptr) {
    auto cmd = new NewSequenceCommand(item, parentItem, MainWindow::instance().isWindowModified());
    ca->append(cmd);
    if (open) {
      ca->append(new ChangeSequenceAction(seq));
    }
  } else {
    if (parentItem == Project::model().root()) {
      Project::model().appendChild(parentItem, item);
    } else {
      parentItem->appendChild(item);
    }
    if (open) {
      set_sequence(seq);
    }
  }
  return item;
}

QString Project::get_file_name_from_path(const QString& path) const
{
  return QFileInfo(path).fileName();
}


QString Project::get_file_ext_from_path(const QString &path) const
{
  return QFileInfo(path).suffix();
}


bool Project::is_focused() const
{
  return tree_view_->hasFocus() || icon_view_->hasFocus();
}

MediaPtr Project::newFolder(const QString &name)
{
  MediaPtr item = std::make_shared<Media>();
  item->setFolder();
  item->setName(name);
  return item;
}

MediaPtr Project::item_to_media(const QModelIndex &index) {
  if (sorter_ != nullptr) {
    const auto src = sorter_->mapToSource(index);
    return Project::model().get(src);
  }

  return {};
}

void Project::get_all_media_from_table(QVector<MediaPtr>& items, QVector<MediaPtr>& list, const MediaType search_type) {
  for (int i=0;i<items.size();i++) {
    MediaPtr item = items.at(i);
    if (item->type() == MediaType::FOLDER) {
      QVector<MediaPtr> children;
      for (int j=0;j<item->childCount();j++) {
        children.append(item->child(j));
      }
      get_all_media_from_table(children, list, search_type);
    } else if (search_type == item->type() || search_type == MediaType::NONE) {
      list.append(item);
    }
  }
}


void Project::refresh()
{
  for (const auto& item : model_->items()) {
    start_preview_generator(item, true);
  }
}

void Project::updatePanel()
{
  tree_view_->viewport()->update();
  icon_view_->viewport()->update();
}

bool delete_clips_in_clipboard_with_media(ComboAction* ca, MediaPtr m) {
  int delete_count = 0;
  if (e_clipboard_type == CLIPBOARD_TYPE_CLIP) {
    for (int i=0;i<e_clipboard.size();i++) {
      auto c = std::dynamic_pointer_cast<Clip>(e_clipboard.at(i));
      if (c->timeline_info.media == m) {
        ca->append(new RemoveClipsFromClipboard(i-delete_count));
        delete_count++;
      }
    }
  }
  return (delete_count > 0);
}

void Project::delete_selected_media()
{
  auto ca = new ComboAction();
  auto selected_items = get_current_selected();
  QVector<MediaPtr> items;

  for (const auto& idx : selected_items) {
    auto mda = item_to_media(idx);
    if (mda == nullptr) {
      qCritical() << "Null Media Ptr";
      continue;
    }
    items.append(mda);
  }

  auto remove = true;
  auto redraw = false;

  // check if media is in use
  QVector<MediaPtr> parents;
  QVector<MediaPtr> sequence_items;
  QVector<MediaPtr> all_top_level_items;

  for (auto i=0;i<Project::model().childCount();i++) {
    all_top_level_items.append(Project::model().child(i));
  }

  get_all_media_from_table(all_top_level_items, sequence_items, MediaType::SEQUENCE); // find all sequences in project
  if (!sequence_items.empty()) {
    QVector<MediaPtr> media_items;
    get_all_media_from_table(items, media_items, MediaType::FOOTAGE);
    auto abort = false;

    for (const auto& item: media_items) {
      bool confirm_delete = false;
      auto skip = false;
      for (const auto& seq_item : sequence_items) {
        const auto seq = seq_item->object<Sequence>();
        for (const auto& c : seq->clips()) {
          if (c == nullptr || c->timeline_info.media != item) {
            continue;
          }
          if (!confirm_delete) {
            auto ftg = item->object<Footage>();
            // we found a reference, so we know we'll need to ask if the user wants to delete it
            QMessageBox confirm(this);
            confirm.setWindowTitle(tr("Delete media in use?"));
            confirm.setText(tr("The media '%1' is currently used in '%2'. Deleting it will remove all instances in the sequence."
                               "Are you sure you want to do this?").arg(ftg ->name(), seq->name()));
            const auto yes_button = confirm.addButton(QMessageBox::Yes);
            QAbstractButton* skip_button = nullptr;
            if (!items.empty()) {
              skip_button = confirm.addButton("Skip", QMessageBox::NoRole);
            }
            auto abort_button = confirm.addButton(QMessageBox::Cancel);
            confirm.exec();

            if (confirm.clickedButton() == yes_button) {
              // remove all clips referencing this media
              confirm_delete = true;
              redraw = true;
            } else if (confirm.clickedButton() == skip_button) {
              // remove media item and any folders containing it from the remove list
              auto parent = item;
              while (parent) {
                parents.append(parent);
                // re-add item's siblings
                for (int m = 0; m < parent->childCount(); m++) {
                  auto child = parent->child(m);
                  bool found = false;
                  for (const auto& n : items) {
                    if (n == child) {
                      found = true;
                      break;
                    }
                  }
                  if (!found) {
                    items.append(child);
                  }
                }//for
                parent = parent->parentItem();
              }//while
              skip = true;
            } else if (confirm.clickedButton() == abort_button) {
              // break out of loop
              abort = true;
              remove = false;
            } else {
              // TODO: anything expected to be done here?
            }
          }
          if (confirm_delete) {
            ca->append(new DeleteClipAction(c));
          }
        }//for
      }//for
      if (confirm_delete) {
        delete_clips_in_clipboard_with_media(ca, item);
      }
    }//for
  }

  // remove
  if (remove) {
    PanelManager::fxControls().clear_effects(true);
    if (global::sequence != nullptr) {
      global::sequence->selections_.clear();
    }

    // remove media and parents
    for (int m=0; m < parents.size(); m++) {
      for (int l=0; l < items.size(); l++) {
        if (auto parPtr = parents.at(m)) {
          if (items.at(l) == parPtr) {
            items.removeAt(l);
            l--;
          }
        }
      }//for
    }//for

    for (auto& item : items) {
      if (!item) {
        continue;
      }
      ca->append(new DeleteMediaCommand(item));

      if (item->type() == MediaType::SEQUENCE) {
        redraw = true;
        auto s = item->object<Sequence>();

        if (s == global::sequence) {
          ca->append(new ChangeSequenceAction(nullptr));
        }

        if (s == PanelManager::footageViewer().getSequence()) {
          PanelManager::footageViewer().set_media(nullptr);
        }
      } else if (item->type() == MediaType::FOOTAGE) {
        if (PanelManager::footageViewer().getSequence()) {
          for (const auto& clp : PanelManager::footageViewer().getSequence()->clips()) {
            if (!clp) {
              continue;
            }
            if (clp->timeline_info.media == item) {
              // Media viewer is displaying the clip for deletion, so clear it
              PanelManager::footageViewer().set_media(nullptr); //TODO: create a clear()
              break;
            }
          }//for
        }
      }
    } //for
    e_undo_stack.push(ca);

    // redraw clips
    if (redraw) {
      PanelManager::refreshPanels(true);
    }
  } else {
    delete ca;
  }
}

void Project::start_preview_generator(MediaPtr item, const bool replacing)
{
  if (item->object<Footage>() == nullptr) {
    // No preview to generate
    return;
  }
  // set up throbber animation
  const auto throbber = new MediaThrobber(item, this);
  throbber->moveToThread(QApplication::instance()->thread());
  QMetaObject::invokeMethod(throbber, "start", Qt::QueuedConnection);

  const auto pg = new PreviewGenerator(item, item->object<Footage>(), replacing, this);
  connect(pg, SIGNAL(set_icon(int, bool)), throbber, SLOT(stop(int, bool)));
  pg->start(QThread::LowPriority);
  item->object<Footage>()->preview_gen = pg;
}

void Project::process_file_list(QStringList& files, bool recursive, MediaPtr replace, MediaPtr parent)
{
  bool imported = false;

  QVector<QString> image_sequence_urls;
  QVector<bool> image_sequence_importassequence;
  QStringList image_sequence_formats = global::config.img_seq_formats.split("|");

  if (!recursive) {
    last_imported_media.clear();
  }

  bool create_undo_action = (!recursive && replace == nullptr);
  ComboAction* ca = nullptr;
  if (create_undo_action) {
    ca = new ComboAction();
  }

  for (QString fileName: files) {
    if (QFileInfo(fileName).isDir()) {
      QString folder_name = get_file_name_from_path(fileName);
      MediaPtr folder = newFolder(folder_name);

      QDir directory(fileName);
      directory.setFilter(QDir::NoDotAndDotDot | QDir::AllEntries);

      QFileInfoList subdir_files = directory.entryInfoList();
      QStringList subdir_filenames;

      for (int j=0; j<subdir_files.size(); j++) {
        subdir_filenames.append(subdir_files.at(j).filePath());
      }

      process_file_list(subdir_filenames, true, nullptr, folder);

      if (create_undo_action) {
        ca->append(new AddMediaCommand(folder, parent));
      } else {
        Project::model().appendChild(parent, folder);
      }

      imported = true;
    } else if (!fileName.isEmpty()) {
      bool skip = false;
      /* Heuristic to determine whether file is part of an image sequence */
      // check file extension (assume it's not a
      int lastcharindex = fileName.lastIndexOf(".");
      bool found = true;
      if ( (lastcharindex != -1) && (lastcharindex > fileName.lastIndexOf('/')) ) {
        // img sequence check
        const QString ext(get_file_ext_from_path(fileName));
        found = image_sequence_formats.contains(ext);
      } else {
        lastcharindex = fileName.length();
      }

      if (lastcharindex == 0) {
        lastcharindex++;
      }

      if (found && fileName[lastcharindex-1].isDigit()) {
        bool is_img_sequence = false;

        // how many digits are in the filename?
        int digit_count = 0;
        int digit_test = lastcharindex-1;
        while (fileName[digit_test].isDigit()) {
          digit_count++;
          digit_test--;
        }

        // retrieve number from filename
        digit_test++;
        int file_number = fileName.mid(digit_test, digit_count).toInt();

        // Check if there are files with the same filename but just different numbers
        if (QFileInfo::exists(QString(fileName.left(digit_test) + QString("%1").arg(file_number-1, digit_count, 10, QChar('0')) + fileName.mid(lastcharindex)))
            || QFileInfo::exists(QString(fileName.left(digit_test) + QString("%1").arg(file_number+1, digit_count, 10, QChar('0')) + fileName.mid(lastcharindex)))) {
          is_img_sequence = true;
        }

        if (is_img_sequence) {
          // get the URL that we would pass to FFmpeg to force it to read the image as a sequence
          QString new_filename = fileName.left(digit_test) + "%" + QString("%1").arg(digit_count, 2, 10, QChar('0')) + "d" + fileName.mid(lastcharindex);

          // add image sequence url to a vector in case the user imported several files that
          // we're interpreting as a possible sequence
          found = false;
          for (int i=0;i<image_sequence_urls.size();i++) {
            if (image_sequence_urls.at(i) == new_filename) {
              // either SKIP if we're importing as a sequence, or leave it if we aren't
              if (image_sequence_importassequence.at(i)) {
                skip = true;
              }
              found = true;
              break;
            }
          }
          if (!found) {
            image_sequence_urls.append(new_filename);
            if (QMessageBox::question(this,
                                      tr("Image sequence detected"),
                                      tr("The file '%1' appears to be part of an image sequence. Would you like to import it as such?").arg(fileName),
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::Yes) == QMessageBox::Yes) {
              fileName = new_filename;
              image_sequence_importassequence.append(true);
            } else {
              image_sequence_importassequence.append(false);
            }
          }
        }
      }

      if (!skip) {
        MediaPtr item;
        FootagePtr ftg;

        if (replace != nullptr) {
          item = replace;
          ftg = replace->object<Footage>();
          ftg->reset();
        } else {
          item = std::make_shared<Media>(parent);
          ftg = std::make_shared<Footage>(fileName, item);
        }

        ftg->using_inout = false;
        ftg->setName(get_file_name_from_path(fileName));

        item->setFootage(ftg);

        last_imported_media.append(item);

        if (replace == nullptr) {
          if (create_undo_action) {
            ca->append(new AddMediaCommand(item, parent));
          } else {
            parent->appendChild(item);
          }
        }

        imported = true;
      }
    }
  }
  if (create_undo_action) {
    if (imported) {
      e_undo_stack.push(ca);

      for (MediaPtr& mda : last_imported_media){
        // generate waveform/thumbnail in another thread
        start_preview_generator(mda, replace != nullptr);
      }
    } else {
      delete ca;
    }
  }
}

MediaPtr Project::get_selected_folder()
{
  // if one item is selected and it's a folder, return it
  QModelIndexList selected_items = get_current_selected();
  if (selected_items.size() == 1) {
    MediaPtr m = item_to_media(selected_items.front());
    if (m != nullptr) {
      if (m->type() == MediaType::FOLDER) {
        return m;
      }
    } else {
      qCritical() << "Null Media Ptr";
    }
  }
  return nullptr;
}

bool Project::reveal_media(MediaPtr media, QModelIndex parent)
{
  for (int i=0;i<Project::model().rowCount(parent);i++) {
    const QModelIndex& item = Project::model().index(i, 0, parent);
    MediaPtr m = Project::model().getItem(item);

    if (m->type() == MediaType::FOLDER) {
      if (reveal_media(media, item)) return true;
    } else if (m == media) {
      // expand all folders leading to this media
      QModelIndex sorted_index = sorter_->mapFromSource(item);

      QModelIndex hierarchy = sorted_index.parent();

      if (global::config.project_view_type == ProjectView::TREE) {
        while (hierarchy.isValid()) {
          tree_view_->setExpanded(hierarchy, true);
          hierarchy = hierarchy.parent();
        }

        // select item
        tree_view_->selectionModel()->select(sorted_index, QItemSelectionModel::Select);
      } else if (global::config.project_view_type == ProjectView::ICON) {
        icon_view_->setRootIndex(hierarchy);
        icon_view_->selectionModel()->select(sorted_index, QItemSelectionModel::Select);
        set_up_dir_enabled();
      }

      return true;
    }
  }

  return false;
}

void Project::import_dialog()
{
  QString formats = tr("Video") + "(" + VIDEO_FMT_FILTER + QString(VIDEO_FMT_FILTER).toUpper() + ")";
  formats += ";;" + tr("Audio") + "(" + AUDIO_FMT_FILTER + QString(AUDIO_FMT_FILTER).toUpper() + ")";
  formats += ";;" + tr("Image") + "(" + IMAGE_FMT_FILTER + QString(IMAGE_FMT_FILTER).toUpper() + ")";
  formats += ";;" + tr("All Files") + "(*)";
  QFileDialog fd(this,
                 tr("Import media..."),
                 "",
                 formats);
  fd.setFileMode(QFileDialog::ExistingFiles);

  if (fd.exec()) {
    QStringList files = fd.selectedFiles();
    process_file_list(files, false, nullptr, get_selected_folder());
  }
}

void Project::delete_clips_using_selected_media()
{
  if (global::sequence == nullptr) {
    QMessageBox::critical(this,
                          tr("No active sequence"),
                          tr("No sequence is active, please open the sequence you want to delete clips from."),
                          QMessageBox::Ok);
  } else {
    auto ca = new ComboAction();
    bool deleted = false;
    QModelIndexList items = get_current_selected();
    for (const auto& del_clip : global::sequence->clips()) {
      if (del_clip == nullptr) {
        continue;
      }
      for (auto item : items) {
        MediaPtr m = item_to_media(item);
        if (del_clip->parentMedia() == m) {
          ca->append(new DeleteClipAction(del_clip));
          deleted = true;
        }
      }
    }

    for (auto item : items) {
      MediaPtr m = item_to_media(item);
      if (delete_clips_in_clipboard_with_media(ca, m)) {
        deleted = true;
      }
    }

    if (deleted) {
      e_undo_stack.push(ca);
      PanelManager::refreshPanels(true);
    } else {
      delete ca;
    }
  }
}

void Project::clear()
{
  // clear effects cache
  PanelManager::fxControls().clear_effects(true);

  // delete sequences first because it's important to close all the clips before deleting the media
  QVector<MediaPtr> sequences = list_all_project_sequences();
  //TODO: are we clearing the right things?
  for (auto mda : sequences) {
    if (mda != nullptr) {
      mda->clearObject();
    }
  }

  // delete everything else
  model().clear();
}

void Project::new_project()
{
  // clear existing project
  set_sequence(nullptr);
  Media::resetNextId();
  PanelManager::footageViewer().set_media(nullptr);
  clear();
  MainWindow::instance().setWindowModified(false);
}

void Project::load_project(const bool autorecovery)
{
  new_project();

  QFile file(project_url);
  if (!file.open(QIODevice::ReadOnly)) {
    qCritical() << "Could not open file";
    return;
  }

  bool success = false;
  QXmlStreamReader stream(&file);
  if (stream.readNextStartElement() && stream.name() == "project") {
    if (PanelManager::projectViewer().model().load(stream)) {
      qInfo() << "Successful load of project file" <<  file.fileName();
      success = true;

    } else {
      qCritical() << "Failed to read project file" << file.fileName();
    }
  } else {
    qWarning() << "Read failure" << file.fileName();
  }

  file.close();

  if (success) {
    if (autorecovery) {
      MainWindow::instance().updateTitle("untitled");
    } else {
      PanelManager::projectViewer().add_recent_project(project_url);
    }

    MainWindow::instance().setWindowModified(autorecovery);
    if (global::sequence != nullptr) {
      // global::sequence will be set in file load as required
      //TODO: think of a better way than this and playback.h - set_sequence()
      PanelManager::fxControls().clear_effects(true);
      PanelManager::sequenceViewer().set_main_sequence();
      PanelManager::timeLine().setSequence(global::sequence);
      PanelManager::timeLine().update_sequence();
      PanelManager::timeLine().setFocus();
    }
    PanelManager::refreshPanels(false);
    refresh();
  } else {
    QMessageBox::critical(&MainWindow::instance(),
                          tr("Project Load Error"),
                          tr("Error loading project: %1").arg(project_url),
                          QMessageBox::Ok);
  }
}

void Project::save_project(const bool autorecovery)
{
  folder_id = 1;
  media_id = 1;
  sequence_id = 1;

  // save to a temporary file first
  const QString tmp_path(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
  if (tmp_path.size() < 1) {
    qCritical() << "Unable to write temporary files";
    return;
  }

  QFile o_file(TMP_SAVE_FILENAME);
  QDir::setCurrent(tmp_path);
  if (!o_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qCritical() << "Could not open file";
    return;
  }

  bool save_success = true;

  QXmlStreamWriter stream(&o_file);
  stream.setAutoFormatting(XML_SAVE_FORMATTING);
  stream.writeStartDocument(); // doc

  save_success = false;
  const QString fmt("Failed to save project file : %1\nReason : %2");
  try {
    if (PanelManager::projectViewer().model().save(stream)) {
      save_success = true;
    } else {
      // by saving to a temporary, the original project file is untouched
      const QString msg(fmt.arg(o_file.fileName()).arg(tr("Unknown")));
      qCritical() << msg;
      QMessageBox::critical(this,
                            tr("Save failure"),
                            tr(msg.toStdString().c_str()));
    }
  } catch (const std::exception& ex) {
    const QString msg(fmt.arg(o_file.fileName()).arg(ex.what()));
    qCritical() << msg;
    QMessageBox::critical(this,
                          tr("Save failure"),
                          tr(msg.toStdString().c_str()));
  } catch (...) {
    const QString msg(fmt.arg(o_file.fileName().arg(tr("Unknown"))));
    qCritical() << msg;
    QMessageBox::critical(this,
                          tr("Save failure"),
                          tr(msg.toStdString().c_str()));
  }


  stream.writeEndDocument(); // doc

  o_file.close();

  if (save_success) {
    // move temp to desired location
    const QString final_path(autorecovery ? autorecovery_filename : project_url);
    QDir().remove(final_path);
    if (QDir().rename(o_file.fileName(), final_path)) {
      qInfo() << "Successfully saved project, fileName =" << final_path;
    } else {
      qCritical() << "Failed to save project";
    }
  }

  if (!autorecovery) {
    add_recent_project(project_url);
    MainWindow::instance().setWindowModified(false);
  }
}

void Project::update_view_type()
{
  tree_view_->setVisible(global::config.project_view_type == ProjectView::TREE);
  icon_view_container->setVisible(global::config.project_view_type == ProjectView::ICON);

  switch (global::config.project_view_type) {
    case ProjectView::TREE:
      sources_common_->setCurrentView(tree_view_);
      break;
    case ProjectView::ICON:
      sources_common_->setCurrentView(icon_view_);
      break;
    default:
      qWarning() << "Unhandled Project View type" << static_cast<int>(global::config.project_view_type);
      break;
  }//switch
}

void Project::set_icon_view() {
  global::config.project_view_type = ProjectView::ICON;
  update_view_type();
}

void Project::set_tree_view() {
  global::config.project_view_type = ProjectView::TREE;
  update_view_type();
}

void Project::save_recent_projects() {
  // save to file
  QFile f(recent_proj_file);
  if (f.open(QFile::WriteOnly | QFile::Truncate | QFile::Text)) {
    QTextStream out(&f);
    for (int i=0;i<recent_projects.size();i++) {
      if (i > 0) {
        out << "\n";
      }
      out << recent_projects.at(i);
    }
    f.close();
  } else {
    qWarning() << "Could not save recent projects";
  }
}

void Project::clear_recent_projects() {
  recent_projects.clear();
  save_recent_projects();
}

void Project::set_icon_view_size(int s) {
  icon_view_->setIconSize(QSize(s, s));
}

void Project::set_up_dir_enabled() {
  directory_up->setEnabled(icon_view_->rootIndex().isValid());
}

void Project::go_up_dir() {
  icon_view_->setRootIndex(icon_view_->rootIndex().parent());
  set_up_dir_enabled();
}

void Project::make_new_menu()
{
  QMenu new_menu(this);
  MainWindow::instance().make_new_menu(new_menu);
  new_menu.exec(QCursor::pos());
}

void Project::add_recent_project(QString url) {
  bool found = false;
  for (int i=0;i<recent_projects.size();i++) {
    if (url == recent_projects.at(i)) {
      found = true;
      recent_projects.move(i, 0);
      break;
    }
  }
  if (!found) {
    recent_projects.insert(0, url);
    if (recent_projects.size() > MAXIMUM_RECENT_PROJECTS) {
      recent_projects.removeLast();
    }
  }
  save_recent_projects();
}

/**
 * @brief           Get a Media item that was imported
 * @param index     Location in store
 * @return          ptr or null
 */
MediaPtr Project::getImportedMedia(const int index)
{
  MediaPtr item;

  if (last_imported_media.size() >= index){
    item = last_imported_media.at(index);
  } else {
    qWarning() << "No Media item at location" << index;
  }

  return item;
}

/**
 * @brief Get the size of the Import media list
 * @return  integer
 */
int Project::getMediaSize() const {
  return last_imported_media.size();
}

void Project::list_all_sequences_worker(QVector<MediaPtr>& list, MediaPtr parent) {
  for (int i=0; i<Project::model().childCount(parent); ++i) {
    if (auto item = Project::model().child(i, parent)) {
      switch (item->type()) {
        case MediaType::SEQUENCE:
          list.append(item);
          break;
        case MediaType::FOLDER:
          list_all_sequences_worker(list, item);
          break;
        case MediaType::FOOTAGE:
          // Ignore
          break;
        default:
          qWarning() << "Unknown media type" << static_cast<int>(item->type());
          break;
      }
    } else {
      qWarning() << "Null Media ptr";
    }
  }//for
}


QVector<MediaPtr> Project::list_all_project_sequences() {
  QVector<MediaPtr> list;
  list_all_sequences_worker(list, nullptr);
  return list;
}

QModelIndexList Project::get_current_selected()
{
  if (global::config.project_view_type == ProjectView::TREE) {
    return PanelManager::projectViewer().tree_view_->selectionModel()->selectedRows();
  }
  return PanelManager::projectViewer().icon_view_->selectionModel()->selectedIndexes();
}


MediaThrobber::MediaThrobber(MediaPtr i, QObject* parent) :
  pixmap(":/icons/throbber.png"),
  animation(0),
  item(i),
  animator(nullptr)
{
  animator = std::make_unique<QTimer>(this);
  setParent(parent);
}

void MediaThrobber::start() {
  // set up throbber
  animation_update();
  animator->setInterval(THROBBER_INTERVAL);
  connect(animator.get(), SIGNAL(timeout()), this, SLOT(animation_update()));
  animator->start();
}

void MediaThrobber::animation_update() {
  if (animation == THROBBER_LIMIT) {
    animation = 0;
  }
  Project::model().set_icon(item, QIcon(pixmap.copy(THROBBER_SIZE*animation, 0, THROBBER_SIZE, THROBBER_SIZE)));
  animation++;
}

void MediaThrobber::stop(const int icon_type, const bool replace) {
  if (animator.get() != nullptr) {
    animator->stop();
  }

  QIcon icon;
  switch (icon_type) {
    case ICON_TYPE_VIDEO:
      icon.addFile(":/icons/videosource.png");
      break;
    case ICON_TYPE_AUDIO:
      icon.addFile(":/icons/audiosource.png");
      break;
    case ICON_TYPE_IMAGE:
      icon.addFile(":/icons/imagesource.png");
      break;
    case ICON_TYPE_ERROR:
      icon.addFile(":/icons/error.png");
      break;
    default:
      qWarning() << "Unknown icon type" << static_cast<int>(icon_type);
      break;
  }//switch
  Project::model().set_icon(item,icon);

  // refresh all clips
  auto sequences = PanelManager::projectViewer().list_all_project_sequences();
  for (auto sqn : sequences) {
    if (auto s = sqn->object<Sequence>()) {
      for (auto clp: s->clips()) {
        if (clp != nullptr) {
          clp->refresh();
        }
      }
    }
  }//for

  // redraw clips
  PanelManager::refreshPanels(replace);

  PanelManager::projectViewer().tree_view_->viewport()->update();
  deleteLater();
}
