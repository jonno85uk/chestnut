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
#ifndef CORNERPINEFFECT_H
#define CORNERPINEFFECT_H

#include "project/effect.h"

class CornerPinEffect : public Effect {
    Q_OBJECT
  public:
    CornerPinEffect(ClipPtr c, const EffectMeta& em);

    CornerPinEffect(const CornerPinEffect& ) = delete;
    CornerPinEffect& operator=(const CornerPinEffect&) = delete;

    virtual void process_coords(double timecode, GLTextureCoords& coords, int data) override;
    virtual void process_shader(double timecode, GLTextureCoords& coords, const int iteration) override;
    virtual void gizmo_draw(double timecode, GLTextureCoords& coords) override;
    virtual void setupUi() override;
  private:
    EffectField* top_left_x {nullptr};
    EffectField* top_left_y {nullptr};
    EffectField* top_right_x {nullptr};
    EffectField* top_right_y {nullptr};
    EffectField* bottom_left_x {nullptr};
    EffectField* bottom_left_y {nullptr};
    EffectField* bottom_right_x {nullptr};
    EffectField* bottom_right_y {nullptr};
    EffectField* perspective {nullptr};

    EffectGizmoPtr top_left_gizmo;
    EffectGizmoPtr top_right_gizmo;
    EffectGizmoPtr bottom_left_gizmo;
    EffectGizmoPtr bottom_right_gizmo;
};

#endif // CORNERPINEFFECT_H
