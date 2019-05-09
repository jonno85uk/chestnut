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
#include "crossdissolvetransition.h"

#include <QOpenGLFunctions>

CrossDissolveTransition::CrossDissolveTransition(ClipPtr c, ClipPtr s, const EffectMeta& em) : Transition(c, s, em)
{
  setCapability(Capability::COORDS);
}

void CrossDissolveTransition::process_coords(double progress, GLTextureCoords&, int data)
{
  if (!(data == TA_CLOSING_TRANSITION && !secondary_clip.expired())) {
    float color[4];
    glGetFloatv(GL_CURRENT_COLOR, color);
    if (data == TA_CLOSING_TRANSITION) {
      progress = 1.0 - progress;
    }
    glColor4f(1.0, 1.0, 1.0, color[3]*progress);
  }
}
