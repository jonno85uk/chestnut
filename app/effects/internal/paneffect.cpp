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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "paneffect.h"

#include <QGridLayout>
#include <QLabel>
#include <QtMath>
#include <cmath>

#include "ui/labelslider.h"
#include "ui/collapsiblewidget.h"

PanEffect::PanEffect(ClipPtr c, const EffectMeta& em)
  : Effect(c, em)
{

}

void PanEffect::process_audio(const double timecode_start,
                              const double timecode_end,
                              quint8* samples,
                              const int nb_bytes,
                              const int)
{
  double interval = (timecode_end - timecode_start)/nb_bytes;
  for (int i=0;i<nb_bytes;i+=4) {
    double pval = log_volume(pan_val->get_double_value(timecode_start+(interval*i), true)*0.01);
    qint16 left_sample = (qint16) (((samples[i+1] & 0xFF) << 8) | (samples[i] & 0xFF));
    qint16 right_sample = (qint16) (((samples[i+3] & 0xFF) << 8) | (samples[i+2] & 0xFF));

    if (pval < 0) {
      // affect right channel
      right_sample *= (1-std::abs(pval));
    } else {
      // affect left channel
      left_sample *= (1-pval);
    }

    samples[i+3] = (quint8) (right_sample >> 8);
    samples[i+2] = (quint8) right_sample;
    samples[i+1] = (quint8) (left_sample >> 8);
    samples[i] = (quint8) left_sample;
  }
}

void PanEffect::setupUi()
{
  if (ui_setup) {
    return;
  }
  Effect::setupUi();
  EffectRowPtr pan_row = add_row(tr("Pan"));
  Q_ASSERT(pan_row);
  pan_val = pan_row->add_field(EffectFieldType::DOUBLE, "pan");
  Q_ASSERT(pan_val);
  pan_val->set_double_minimum_value(-100);
  pan_val->set_double_maximum_value(100);

  // set defaults
  pan_val->set_double_default_value(0);
}
