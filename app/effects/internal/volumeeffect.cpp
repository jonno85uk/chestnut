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
#include "volumeeffect.h"

#include <QGridLayout>
#include <QLabel>
#include <QtMath>
#include <stdint.h>

#include "ui/labelslider.h"
#include "ui/collapsiblewidget.h"

constexpr auto VOLUME_MIN = 0;
constexpr auto VOLUME_MAX = 1000;
constexpr auto VOLUME_DEFAULT = 100;

VolumeEffect::VolumeEffect(ClipPtr c, const EffectMeta& em) : Effect(c, em)
{

}

VolumeEffect::~VolumeEffect()
{
  volume_val = nullptr;
}

void VolumeEffect::process_audio(const double timecode_start,
                                 const double timecode_end,
                                 quint8* samples,
                                 const int nb_bytes,
                                 const int channel_count)
{
  double interval = (timecode_end-timecode_start)/nb_bytes;
  for (int i = 0; i < nb_bytes; i += 4) {
    double vol_val = log_volume(volume_val->get_double_value(timecode_start+(interval*i), true)*0.01);

    qint32 right_samp = static_cast<qint16> (((samples[i+3] & 0xFF) << 8) | (samples[i+2] & 0xFF));
    qint32 left_samp = static_cast<qint16> (((samples[i+1] & 0xFF) << 8) | (samples[i] & 0xFF));

    left_samp *= vol_val;
    right_samp *= vol_val;

    if (left_samp > INT16_MAX) {
      left_samp = INT16_MAX;
    } else if (left_samp < INT16_MIN) {
      left_samp = INT16_MIN;
    }

    if (right_samp > INT16_MAX) {
      right_samp = INT16_MAX;
    } else if (right_samp < INT16_MIN) {
      right_samp = INT16_MIN;
    }

    samples[i+3] = static_cast<quint8> (right_samp >> 8);
    samples[i+2] = static_cast<quint8> (right_samp);
    samples[i+1] = static_cast<quint8> (left_samp >> 8);
    samples[i] = static_cast<quint8> (left_samp);
  }
}

void VolumeEffect::setupUi()
{
  if (ui_setup) {
    return;
  }
  Effect::setupUi();
  EffectRowPtr volume_row = add_row(tr("Volume"));
  Q_ASSERT(volume_row);
  volume_val = volume_row->add_field(EffectFieldType::DOUBLE, "volume");
  Q_ASSERT(volume_val);
  volume_val->set_double_minimum_value(VOLUME_MIN);
  volume_val->set_double_maximum_value(VOLUME_MAX);

  // set defaults
  volume_val->set_double_default_value(VOLUME_DEFAULT);
}
