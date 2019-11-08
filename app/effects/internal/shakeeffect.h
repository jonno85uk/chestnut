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
#ifndef SHAKEEFFECT_H
#define SHAKEEFFECT_H

#include "project/effect.h"

constexpr int RANDOM_VAL_SIZE = 30;

class ShakeEffect : public Effect {
    Q_OBJECT
  public:
    ShakeEffect(ClipPtr c, const EffectMeta& em);

    ShakeEffect(const ShakeEffect& ) = delete;
    ShakeEffect& operator=(const ShakeEffect&) = delete;

    void process_coords(double timecode, GLTextureCoords& coords, int data) override;
    virtual void setupUi() override;

    EffectField* intensity_val {nullptr};
    EffectField* rotation_val {nullptr};
    EffectField* frequency_val {nullptr};
  private:
    double random_vals[RANDOM_VAL_SIZE];
};

#endif // SHAKEEFFECT_H
