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
#ifndef TONEEFFECT_H
#define TONEEFFECT_H

#include "project/effect.h"

class ToneEffect : public Effect {
  public:
    ToneEffect(ClipPtr c, const EffectMeta& em);

    ToneEffect(const ToneEffect& ) = delete;
    ToneEffect& operator=(const ToneEffect&) = delete;

    void process_audio(const double timecode_start,
                       const double timecode_end,
                       quint8* samples,
                       const int nb_bytes,
                       const int channel_count) override;

    virtual void setupUi() override;

    EffectField* type_val {nullptr};
    EffectField* freq_val {nullptr};
    EffectField* amount_val {nullptr};
    EffectField* mix_val {nullptr};
  private:
    int sinX;
};

#endif // TONEEFFECT_H
