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
#include "exponentialfadetransition.h"

#include <QtMath>

ExponentialFadeTransition::ExponentialFadeTransition(ClipPtr c, ClipPtr s, const EffectMeta& em)
  : Transition(c, s, em)
{

}

void ExponentialFadeTransition::process_audio(const double timecode_start,
                                              const double timecode_end,
                                              quint8* samples,
                                              const int nb_bytes,
                                              const int type)
{
  const auto interval = (timecode_end - timecode_start) / nb_bytes;

  for (int i=0; i<nb_bytes; i+=2) {
    qint16 samp = static_cast<qint16>(((samples[i+1] & 0xFF) << 8) | (samples[i] & 0xFF));
    switch (type) {
      case TA_OPENING_TRANSITION:
        samp *= qPow(timecode_start + (interval * i), 2);
        break;
      case TA_CLOSING_TRANSITION:
        samp *= qPow(1 - (timecode_start + (interval * i)), 2);
        break;
      default:
        qWarning() << "Unhandled transition type" << type;
        break;
    }

    samples[i+1] = static_cast<quint8>(samp >> 8);
    samples[i] = static_cast<quint8>(samp);
  }
}
