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
#ifndef TRANSFORMEFFECT_H
#define TRANSFORMEFFECT_H

#include "project/effect.h"

class TransformEffect : public Effect {
    Q_OBJECT
  public:
    TransformEffect(ClipPtr c, const EffectMeta& em);

    TransformEffect(const TransformEffect& ) = delete;
    TransformEffect& operator=(const TransformEffect&) = delete;

    void refresh() override;
    void process_coords(double timecode, GLTextureCoords& coords, int data) override;
    virtual void gizmo_draw(double timecode, GLTextureCoords& coords) override;

    virtual void setupUi() override;
  public slots:
    void toggle_uniform_scale(bool enabled);
  private:
    EffectField* position_x {nullptr};
    EffectField* position_y {nullptr};
    EffectField* scale_x {nullptr};
    EffectField* scale_y {nullptr};
    EffectField* uniform_scale_field {nullptr};
    EffectField* rotation {nullptr};
    EffectField* anchor_x_box {nullptr};
    EffectField* anchor_y_box {nullptr};
    EffectField* opacity {nullptr};
    EffectField* blend_mode_box {nullptr};

    EffectGizmoPtr top_left_gizmo {nullptr};
    EffectGizmoPtr top_center_gizmo {nullptr};
    EffectGizmoPtr top_right_gizmo {nullptr};
    EffectGizmoPtr bottom_left_gizmo {nullptr};
    EffectGizmoPtr bottom_center_gizmo {nullptr};
    EffectGizmoPtr bottom_right_gizmo {nullptr};
    EffectGizmoPtr left_center_gizmo {nullptr};
    EffectGizmoPtr right_center_gizmo {nullptr};
    EffectGizmoPtr anchor_gizmo {nullptr};
    EffectGizmoPtr rotate_gizmo {nullptr};
    EffectGizmoPtr rect_gizmo {nullptr};

    bool set;
};

#endif // TRANSFORMEFFECT_H
