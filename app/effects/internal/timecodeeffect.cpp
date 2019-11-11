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
#include "timecodeeffect.h"

#include <QGridLayout>
#include <QLabel>
#include <QOpenGLTexture>
#include <QTextEdit>
#include <QPainter>
#include <QPushButton>
#include <QColorDialog>
#include <QFontDatabase>
#include <QComboBox>
#include <QWidget>
#include <QtMath>

#include "ui/labelslider.h"
#include "ui/collapsiblewidget.h"
#include "project/clip.h"
#include "project/sequence.h"
#include "panels/viewer.h"
#include "ui/comboboxex.h"
#include "ui/colorbutton.h"
#include "ui/fontcombobox.h"
#include "io/config.h"
#include "playback/playback.h"

constexpr const char* const FONT_FAMILY = "Helvetica";

TimecodeEffect::TimecodeEffect(ClipPtr c, const EffectMeta& em) :
  Effect(c, em)
{
  setCapability(Capability::SUPERIMPOSE);
  setCapability(Capability::ALWAYS_UPDATE);
}

void TimecodeEffect::setupUi()
{
  if (ui_setup) {
    return;
  }
  Effect::setupUi();

  EffectRowPtr tc_row = add_row(tr("Timecode"));
  tc_select = tc_row->add_field(EffectFieldType::COMBO, "tc_selector");
  tc_select->add_combo_item(tr("Sequence"), true);
  tc_select->add_combo_item(tr("Media"), false);
  tc_select->set_combo_index(0);

  scale_val = add_row(tr("Scale"))->add_field(EffectFieldType::DOUBLE, "scale", 2);
  scale_val->set_double_minimum_value(1);
  scale_val->set_double_default_value(100);
  scale_val->set_double_maximum_value(1000);

  color_val = add_row(tr("Color"))->add_field(EffectFieldType::COLOR, "color", 2);
  color_val->set_color_value(Qt::white);
  color_val->setDefaultValue(Qt::white);

  color_bg_val = add_row(tr("Background Color"))->add_field(EffectFieldType::COLOR, "bgcolor", 2);
  color_bg_val->set_color_value(Qt::black);
  color_bg_val->setDefaultValue(Qt::black);

  bg_alpha = add_row(tr("Background Opacity"))->add_field(EffectFieldType::DOUBLE, "bgalpha", 2);
  bg_alpha->set_double_minimum_value(0);
  bg_alpha->set_double_maximum_value(100);
  bg_alpha->set_double_default_value(50);

  EffectRowPtr offset = add_row(tr("Offset"));
  offset_x_val = offset->add_field(EffectFieldType::DOUBLE, "offsetx");
  offset_y_val = offset->add_field(EffectFieldType::DOUBLE, "offsety");

  prepend_text = add_row(tr("Prepend"))->add_field(EffectFieldType::STRING, "prepend", 2);
}


void TimecodeEffect::redraw(double timecode) {
  if (tc_select->get_combo_data(timecode).toBool()){
    display_timecode = prepend_text->get_string_value(timecode) + frame_to_timecode(global::sequence->playhead_,
                                                                                    global::config.timecode_view,
                                                                                    global::sequence->frameRate());
  }
  else {
    double media_rate = parent_clip->mediaFrameRate();
    display_timecode = prepend_text->get_string_value(timecode) + frame_to_timecode(timecode * media_rate,
                                                                                    global::config.timecode_view,
                                                                                    media_rate);}
  superimpose_.img_.fill(Qt::transparent);

  QPainter p(&superimpose_.img_);
  p.setRenderHint(QPainter::Antialiasing);
  int width = superimpose_.img_.width();
  int height = superimpose_.img_.height();

  // set font
  font.setStyleHint(QFont::Helvetica, QFont::PreferAntialias);
  font.setFamily(FONT_FAMILY);
  font.setPixelSize(qCeil(scale_val->get_double_value(timecode) * 0.001 * height));
  p.setFont(font);
  QFontMetrics fm(font);

  QPainterPath path;

  int text_x, text_y, rect_y, offset_x, offset_y;
  int text_height = fm.height();
  int text_width = fm.width(display_timecode);
  QColor background_color = color_bg_val->get_color_value(timecode);
  int alpha_val = bg_alpha->get_double_value(timecode)*2.55;
  background_color.setAlpha(alpha_val);

  offset_x = int(offset_x_val->get_double_value(timecode));
  offset_y = int(offset_y_val->get_double_value(timecode));

  text_x = offset_x + (width/2) - (text_width/2);
  text_y = offset_y + height - height/10;
  rect_y = text_y + fm.descent()/2 - text_height;

  path.addText(text_x, text_y, font, display_timecode);

  p.setPen(Qt::NoPen);
  p.setBrush(background_color);
  p.drawRect(QRect(text_x-fm.descent()/2, rect_y, text_width+fm.descent(), text_height));
  p.setBrush(color_val->get_color_value(timecode));
  p.drawPath(path);
}
