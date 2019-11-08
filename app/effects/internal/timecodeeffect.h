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
#ifndef TIMECODEEFFECT_H
#define TIMECODEEFFECT_H

#include "project/effect.h"

#include <QFont>
#include <QImage>
class QOpenGLTexture;

class TimecodeEffect : public Effect {
	Q_OBJECT
public:
    TimecodeEffect(ClipPtr c, const EffectMeta& em);

    TimecodeEffect(const TimecodeEffect& ) = delete;
    TimecodeEffect& operator=(const TimecodeEffect&) = delete;
    virtual void setupUi() override;

    EffectField* scale_val {nullptr};
    EffectField* color_val {nullptr};
    EffectField* color_bg_val {nullptr};
    EffectField* bg_alpha {nullptr};
    EffectField* offset_x_val {nullptr};
    EffectField* offset_y_val {nullptr};
    EffectField* prepend_text {nullptr};
    EffectField* tc_select {nullptr};
  protected:
    void redraw(double timecode);

private:
    QFont font;
    QString display_timecode;
};

#endif // TIMECODEEFFECT_H
