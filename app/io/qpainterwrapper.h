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
#ifndef QPAINTERWRAPPER_H
#define QPAINTERWRAPPER_H

#include <QObject>

class QPainter;

class QPainterWrapper : public QObject {
    Q_OBJECT
  public:
    QPainterWrapper();

    QPainterWrapper(const QPainterWrapper&) = delete;
    QPainterWrapper operator=(const QPainterWrapper&) = delete;

    QImage* img {nullptr};
    QPainter* painter {nullptr};
  public slots:
    void fill(const QString& color);
    void fillRect(int x, int y, int width, int height, const QString& brush);
    void drawRect(int x, int y, int width, int height);
    void setPen(const QString& pen);
    void setBrush(const QString& brush);
};

extern QPainterWrapper painter_wrapper;

#endif // QPAINTERWRAPPER_H
