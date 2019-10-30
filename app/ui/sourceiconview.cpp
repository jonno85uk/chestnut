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
#include "sourceiconview.h"

#include <QMimeData>
#include <QMouseEvent>

#include "panels/project.h"
#include "project/media.h"
#include "project/sourcescommon.h"
#include "debug.h"

SourceIconView::SourceIconView(Project *projParent, QWidget *parent)
  : QListView(parent),
    project_parent(projParent)
{
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setResizeMode(QListView::Adjust);
  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, SIGNAL(clicked(const QModelIndex&)), this, SLOT(item_click(const QModelIndex&)));
  connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(show_context_menu()));
}

SourceIconView::~SourceIconView()
{
  project_parent = nullptr;
}

void SourceIconView::show_context_menu()
{
  project_parent->sources_common->show_context_menu(this, selectedIndexes());
}

void SourceIconView::item_click(const QModelIndex& index)
{
  if ( (selectedIndexes().size() == 1) && (index.column() == 0) ) {
    project_parent->sources_common->item_click(project_parent->item_to_media(index), index);
  }
}

void SourceIconView::mousePressEvent(QMouseEvent* event)
{
  project_parent->sources_common->mousePressEvent(event);
  if (!indexAt(event->pos()).isValid()) {
    selectionModel()->clear();
  }
  QListView::mousePressEvent(event);
}

void SourceIconView::dragEnterEvent(QDragEnterEvent *event)
{
  Q_ASSERT(event);
  if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  } else {
    QListView::dragEnterEvent(event);
  }
}

void SourceIconView::dragMoveEvent(QDragMoveEvent *event)
{
  Q_ASSERT(event);
  if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  } else {
    QListView::dragMoveEvent(event);
  }
}

void SourceIconView::dropEvent(QDropEvent* event)
{
  Q_ASSERT(event);
  QModelIndex drop_item = indexAt(event->pos());
  if (!drop_item.isValid()) {
    drop_item = rootIndex();
  }
  project_parent->sources_common->dropEvent(this, event, drop_item, selectedIndexes());
}

void SourceIconView::mouseDoubleClickEvent(QMouseEvent *event)
{
  bool default_behavior = true;
  if (selectedIndexes().size() == 1) {
    if (auto m = project_parent->item_to_media(selectedIndexes().front())) {
      if (m->type() == MediaType::FOLDER) {
        default_behavior = false;
        setRootIndex(selectedIndexes().at(0));
        emit changed_root();
      }
    } else {
      qCritical() << "Selection failed";
    }
  }
  if (default_behavior) {
    project_parent->sources_common->mouseDoubleClickEvent(event, selectedIndexes());
  }
}
