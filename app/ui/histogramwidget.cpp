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

#include "histogramwidget.h"

#include <QtGlobal>

using ui::HistogramWidget;

constexpr int MAX_PIXELS = 256;

HistogramWidget::HistogramWidget(QWidget *parent)
  : QWidget(parent)
{
  values_.fill(0);
}



void HistogramWidget::paintEvent(QPaintEvent */*event*/)
{
  if (!isVisible()) {
    return;
  }
  auto max_val = 0; // for scaling results
  for (auto i = 0UL; i < COLORS_PER_CHANNEL; ++i) {
    if (values_[i] > max_val) {
      max_val = values_[i];
    }
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::HighQualityAntialiasing);
  QPen pen;
  const auto h = height();

  if (max_val > 0) {
    // paint histogram
    pen.setColor(color_);
    painter.setPen(pen);
    double scaled = 0.0;
    int32_t j;
    for (int i = 0; i < MAX_PIXELS; ++i) {
      scaled = static_cast<double>(values_[static_cast<size_t>(i)]) / max_val;
      scaled = qMin(scaled, static_cast<double>(h));
      j = static_cast<int32_t>(h - qRound(scaled * h));
      painter.drawLine(i+1, h, i+1, j);
    }
  } else {
    painter.eraseRect(1, 1, width() - 1, height() - 1);
  }

  // paint surrounding box
  pen.setColor(Qt::black);
  painter.setPen(pen);
  painter.drawRect(0, 0, MAX_PIXELS, h-1);

  // paint grid
  QVector<qreal> dashes;
  dashes << 3 << 3;
  pen.setDashPattern(dashes);
  QColor clr = pen.color();
  clr.setAlpha(128);
  pen.setColor(clr);
  painter.setPen(pen);
  painter.drawLine(0, h/2, MAX_PIXELS, h/2);
  painter.drawLine(MAX_PIXELS/2, 0, MAX_PIXELS/2, h);
}
