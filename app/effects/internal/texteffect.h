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
#ifndef TEXTEFFECT_H
#define TEXTEFFECT_H

#include "project/effect.h"

#include <QFont>
#include <QImage>
class QOpenGLTexture;

class TextEffect : public Effect {
    Q_OBJECT
  public:
    TextEffect(ClipPtr c, const EffectMeta& em);

    TextEffect(const TextEffect& ) = delete;
    TextEffect& operator=(const TextEffect&) = delete;
    virtual void setupUi() override;

    EffectField* text_val {nullptr};
    EffectField* size_val {nullptr};
    EffectField* set_color_button {nullptr};
    EffectField* set_font_combobox {nullptr};
    EffectField* halign_field {nullptr};
    EffectField* valign_field {nullptr};
    EffectField* word_wrap_field {nullptr};

    EffectField* outline_bool {nullptr};
    EffectField* outline_width {nullptr};
    EffectField* outline_color {nullptr};

    EffectField* shadow_bool {nullptr};
    EffectField* shadow_distance {nullptr};
    EffectField* shadow_color {nullptr};
    EffectField* shadow_softness {nullptr};
    EffectField* shadow_opacity {nullptr};

  protected:
    void redraw(double timecode) override;
  private slots:
    void outline_enable(bool);
    void shadow_enable(bool);
    void text_edit_menu();
    void open_text_edit();
  private:
    QFont font;
};

#endif // TEXTEFFECT_H
