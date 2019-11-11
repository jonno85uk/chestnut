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
#include "scrollarea.h"

#include <QWheelEvent>
#include <QDebug>

#include "io/config.h"
#include "panels/panelmanager.h"

ScrollArea::ScrollArea(QWidget* parent) : QScrollArea(parent)
{

}

void ScrollArea::wheelEvent(QWheelEvent *event)
{
  Q_ASSERT(event);
  if (global::config.scroll_zooms) {
    event->ignore();
    if (event->angleDelta().y() != 0) {
      panels::PanelManager::timeLine().set_zoom(event->angleDelta().y() > 0);
    }
  } else {
    QScrollArea::wheelEvent(event);
  }
}
