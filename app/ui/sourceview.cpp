/*
 * Chestnut. Chestnut is a free non-linear video editor for Linux.
 * Copyright (C) 2019
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

#include "sourceview.h"

#include <QMimeData>
#include <QMessageBox>

#include "panels/panelmanager.h"
#include "ui/viewerwidget.h"
#include "project/sourcescommon.h"
#include "project/projectmodel.h"
#include "io/config.h"
#include "Forms/subclipcreatedialog.h"

using panels::PanelManager;
using chestnut::ui::SourceView;

SourceView::SourceView(Project& project_parent) : project_parent_(project_parent)
{

}

bool SourceView::onDragEnterEvent(QDragEnterEvent& event) const
{
  bool accepted = false;
  if (event.mimeData()->hasUrls()
      || (event.source() == dynamic_cast<QObject*>(PanelManager::footageViewer().viewer_widget)) ) {
    event.acceptProposedAction();
    accepted = true;
  }
  return accepted;
}

bool SourceView::onDragMoveEvent(QDragMoveEvent& event) const
{
  bool accepted = false;

  if (event.mimeData()->hasUrls()
      || (event.source() == dynamic_cast<QObject*>(panels::PanelManager::footageViewer().viewer_widget)) ) {
    event.acceptProposedAction();
    accepted = true;
  }

  return accepted;
}

bool SourceView::onDropEvent(QDropEvent& event) const
{
  bool accepted;

  if (event.source() == dynamic_cast<QObject*>(panels::PanelManager::footageViewer().viewer_widget))  {
    event.acceptProposedAction();
    addSubclipToProject(event);
    accepted = true;
  } else if (event.mimeData()->hasUrls()) {
    event.acceptProposedAction();
    addMediaToProject(event);
    accepted = true;
  } else {
    moveMedia(event);
    accepted = false;
  }
  return accepted;
}


void SourceView::addSubclipToProject(const QDropEvent& event) const
{
  auto& viewer(panels::PanelManager::footageViewer());
  const auto orig_media(viewer.getMedia());

  const auto drop_item(this->viewIndex(event.pos()));
  const auto parent_mda(std::invoke([&]{
    const auto mda(project_parent_.item_to_media(drop_item));
    if (drop_item.isValid() && (mda->type() != MediaType::FOLDER) ) {
      return orig_media->parentItem();
    }
    return mda;
  }));

  auto mda(std::make_shared<Media>(*orig_media));
  QString sub_name;
  if (event.keyboardModifiers() & Qt::ShiftModifier) {
    auto dial = SubClipCreateDialog(orig_media->name());
    dial.exec();
    sub_name = dial.subClipName();
    if (sub_name.length() == 0) {
      qInfo() << "Cancelled creating sub-clip";
      return;
    }
  } else {
    const QDateTime now(QDateTime::currentDateTime());
    const QString fmt("%1-subclip-%2");
    sub_name = fmt.arg(mda->name()).arg(now.toString(Qt::ISODate));
  }

  mda->setName(sub_name);
  if (auto ftg = mda->object<Footage>()) {
    // FIXME: don't like this one bit. It's linked to how Footage/Sequence/Folder/Media relationships are poorly designed
    ftg->setParent(mda);
  }

  Project::model().appendChild(parent_mda, mda);
}


void SourceView::addMediaToProject(const QDropEvent& event) const
{
  auto urls = event.mimeData()->urls();
  if (urls.isEmpty()) {
    return;
  }

  const auto drop_item = this->viewIndex(event.pos());
  const auto mda = project_parent_.item_to_media(drop_item);
  if (!mda) {
    qCritical() << "Null Media ptr";
    return;
  }

  qDebug() << "Media dragged into Project from OS," << urls;
  QStringList paths;
  for (const auto& url : urls) {
    paths.append(url.toLocalFile());
  }

  bool replace = false;
  if ( (urls.size() == 1)
       && drop_item.isValid()
       && (mda->type() == MediaType::FOOTAGE)
       && !QFileInfo(paths.front()).isDir()
       && global::config.drop_on_media_to_replace
       && QMessageBox::question(
         nullptr,
         QObject::tr("Replace Media"),
         QObject::tr("You dropped a file onto '%1'. Would you like to replace it with the dropped file?").arg(mda->name()),
         QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
    replace = true;
    project_parent_.replace_media(mda, paths.front());
  }

  if (!replace) {
    QModelIndex parentIndex;
    if (drop_item.isValid()) {
      if (mda->type() == MediaType::FOLDER) {
        parentIndex = drop_item;
      } else {
        parentIndex = drop_item.parent();
      }
    }
    project_parent_.process_file_list(paths, false, nullptr, PanelManager::projectViewer().item_to_media(parentIndex));
  }
}


void SourceView::moveMedia(const QDropEvent& event) const
{
  qDebug() << "Dragging files within the Project window";
  const auto drop_item = this->viewIndex(event.pos());
  const auto mda = project_parent_.item_to_media(drop_item);

  if (drop_item.isValid() && (mda->type() != MediaType::FOLDER) ) {
    return;
  }

  // if we dragged to the root OR dragged to a folder
  auto items = this->selectedItems();
  QVector<MediaPtr> move_items;
  for (const auto& item : items) {
    const QModelIndex& itemParent = item.parent();
    auto seq = project_parent_.item_to_media(item);
    if ( (itemParent != drop_item) && (item != drop_item) ) {
      bool ignore = false;
      if (itemParent.isValid()) {
        // if child belongs to a selected parent, assume the user is just moving the parent and ignore the child
        QModelIndex par(itemParent);
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
  }//for

  if (!move_items.empty()) {
    auto mm = new MediaMove();
    mm->to = mda;
    mm->items = move_items;
    e_undo_stack.push(mm);
  }
}
