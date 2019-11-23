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
#include "effectfield.h"

#include <QDateTime>

#include "ui/colorbutton.h"
#include "ui/texteditex.h"
#include "ui/checkboxex.h"
#include "ui/comboboxex.h"
#include "ui/fontcombobox.h"
#include "ui/embeddedfilechooser.h"

#include "io/config.h"

#include "project/effectrow.h"
#include "project/effect.h"

#include "project/undo.h"
#include "project/clip.h"
#include "project/sequence.h"

#include "io/math.h"

#include "debug.h"

QString fieldTypeValueToString(const EffectFieldType type, const QVariant& value)
{
  switch (type) {
    case EffectFieldType::DOUBLE: return QString::number(value.toDouble());
    case EffectFieldType::COLOR: return value.value<QColor>().name();
    case EffectFieldType::BOOL: return value.toBool() ? "true" : "false";
    case EffectFieldType::COMBO: return QString::number(value.toInt());
    case EffectFieldType::STRING:
      [[fallthrough]];
    case EffectFieldType::FONT:
      [[fallthrough]];
    case EffectFieldType::FILE_T:
      return value.toString();
    default:
      break;
  }
  return QString();
}

EffectField::EffectField(EffectRow* parent)
  : QObject(parent),
    parent_row(parent)
{

}

EffectField::EffectField(EffectRow *parent, const EffectFieldType t, const QString &i) :
  QObject(parent),
  parent_row(parent),
  type_(t),
  id_(i)
{
  switch (t) {
    case EffectFieldType::DOUBLE:
    {
      auto ls = new LabelSlider();
      ui_element = ls;
      connect(ls, SIGNAL(valueChanged()), this, SLOT(ui_element_change()));
      connect(ls, SIGNAL(clicked()), this, SIGNAL(clicked()));
    }
      break;
    case EffectFieldType::COLOR:
    {
      auto cb = new ColorButton();
      ui_element = cb;
      connect(cb, SIGNAL(color_changed()), this, SLOT(ui_element_change()));
    }
      break;
    case EffectFieldType::STRING:
    {
      auto edit = new TextEditEx();
      edit->setFixedHeight(edit->fontMetrics().lineSpacing()* global::config.effect_textbox_lines);
      edit->setUndoRedoEnabled(true);
      ui_element = edit;
      connect(edit, SIGNAL(textChanged()), this, SLOT(ui_element_change()));
    }
      break;
    case EffectFieldType::BOOL:
    {
      auto cb = new CheckboxEx();
      ui_element = cb;
      connect(cb, SIGNAL(clicked(bool)), this, SLOT(ui_element_change()));
      connect(cb, SIGNAL(toggled(bool)), this, SIGNAL(toggled(bool)));
    }
      break;
    case EffectFieldType::COMBO:
    {
      auto cb = new ComboBoxEx();
      ui_element = cb;
      connect(cb, SIGNAL(activated(int)), this, SLOT(ui_element_change()));
    }
      break;
    case EffectFieldType::FONT:
    {
      auto fcb = new FontCombobox();
      ui_element = fcb;
      connect(fcb, SIGNAL(activated(int)), this, SLOT(ui_element_change()));
    }
      break;
    case EffectFieldType::FILE_T:
    {
      auto efc = new EmbeddedFileChooser();
      ui_element = efc;
      connect(efc, SIGNAL(changed()), this, SLOT(ui_element_change()));
    }
      break;
    default:
      qWarning() << "Unknown Effect Field Type" << static_cast<int>(t);
      break;
  }//switch
}

QVariant EffectField::get_previous_data() {
  switch (type_) {
    case EffectFieldType::DOUBLE: return dynamic_cast<LabelSlider*>(ui_element)->getPreviousValue();
    case EffectFieldType::COLOR: return dynamic_cast<ColorButton*>(ui_element)->getPreviousValue();
    case EffectFieldType::STRING: return dynamic_cast<TextEditEx*>(ui_element)->getPreviousValue();
    case EffectFieldType::BOOL: return !dynamic_cast<QCheckBox*>(ui_element)->isChecked();
    case EffectFieldType::COMBO: return dynamic_cast<ComboBoxEx*>(ui_element)->getPreviousIndex();
    case EffectFieldType::FONT: return dynamic_cast<FontCombobox*>(ui_element)->getPreviousValue();
    case EffectFieldType::FILE_T: return dynamic_cast<EmbeddedFileChooser*>(ui_element)->getPreviousValue();
    default:
      qWarning() << "Unknown Effect Field Type" << static_cast<int>(type_);
      break;
  }
  return QVariant();
}

QVariant EffectField::get_current_data()  const
{
  switch (type_) {
    case EffectFieldType::DOUBLE: return dynamic_cast<LabelSlider*>(ui_element)->value();
    case EffectFieldType::COLOR: return dynamic_cast<ColorButton*>(ui_element)->get_color();
    case EffectFieldType::STRING: return dynamic_cast<TextEditEx*>(ui_element)->getPlainTextEx();
    case EffectFieldType::BOOL: return dynamic_cast<QCheckBox*>(ui_element)->isChecked();
    case EffectFieldType::COMBO: return dynamic_cast<ComboBoxEx*>(ui_element)->currentIndex();
    case EffectFieldType::FONT: return dynamic_cast<FontCombobox*>(ui_element)->currentText();
    case EffectFieldType::FILE_T: return dynamic_cast<EmbeddedFileChooser*>(ui_element)->getFilename();
    default:
      qWarning() << "Unknown Effect Field Type" << static_cast<int>(type_);
      break;
  }
  return QVariant();
}

double EffectField::frameToTimecode(const long frame)
{
  Q_ASSERT(parent_row != nullptr);
  Q_ASSERT(parent_row->parent_effect != nullptr);
  Q_ASSERT(parent_row->parent_effect->parent_clip != nullptr);
  return (static_cast<double>(frame) / parent_row->parent_effect->parent_clip->sequence->frameRate());
}

long EffectField::timecodeToFrame(double timecode)
{
  Q_ASSERT(parent_row != nullptr);
  Q_ASSERT(parent_row->parent_effect != nullptr);
  Q_ASSERT(parent_row->parent_effect->parent_clip != nullptr);
  return qRound(timecode * parent_row->parent_effect->parent_clip->sequence->frameRate());
}

void EffectField::set_current_data(const QVariant& data) {
  switch (type_) {
    case EffectFieldType::DOUBLE: return dynamic_cast<LabelSlider*>(ui_element)->set_value(data.toDouble(), false);
    case EffectFieldType::COLOR: return dynamic_cast<ColorButton*>(ui_element)->set_color(data.value<QColor>());
    case EffectFieldType::STRING: return dynamic_cast<TextEditEx*>(ui_element)->setPlainTextEx(data.toString());
    case EffectFieldType::BOOL: return dynamic_cast<QCheckBox*>(ui_element)->setChecked(data.toBool());
    case EffectFieldType::COMBO: return dynamic_cast<ComboBoxEx*>(ui_element)->setCurrentIndexEx(data.toInt());
    case EffectFieldType::FONT: return dynamic_cast<FontCombobox*>(ui_element)->setCurrentTextEx(data.toString());
    case EffectFieldType::FILE_T: return dynamic_cast<EmbeddedFileChooser*>(ui_element)->setFilename(data.toString());
    default:
      qWarning() << "Unknown Effect Field Type" << static_cast<int>(type_);
      break;
  }
}

void EffectField::get_keyframe_data(double timecode, int &before, int &after, double &progress) {
  int before_keyframe_index = -1;
  int after_keyframe_index = -1;
  long before_keyframe_time = LONG_MIN;
  long after_keyframe_time = LONG_MAX;
  long frame = timecodeToFrame(timecode);

  for (int i=0;i<keyframes.size();i++) {
    long eval_keyframe_time = keyframes.at(i).time;
    if (eval_keyframe_time == frame) {
      before = i;
      after = i;
      return;
    }

    if (eval_keyframe_time < frame && eval_keyframe_time > before_keyframe_time) {
      before_keyframe_index = i;
      before_keyframe_time = eval_keyframe_time;
    } else if (eval_keyframe_time > frame && eval_keyframe_time < after_keyframe_time) {
      after_keyframe_index = i;
      after_keyframe_time = eval_keyframe_time;
    }
  }

  if ((type_ == EffectFieldType::DOUBLE || type_ == EffectFieldType::COLOR) && (before_keyframe_index > -1 && after_keyframe_index > -1)) {
    // interpolate
    before = before_keyframe_index;
    after = after_keyframe_index;
    progress = (timecode-frameToTimecode(before_keyframe_time))/(frameToTimecode(after_keyframe_time)-frameToTimecode(before_keyframe_time));
  } else if (before_keyframe_index > -1) {
    before = before_keyframe_index;
    after = before_keyframe_index;
  } else {
    before = after_keyframe_index;
    after = after_keyframe_index;
  }
}

bool EffectField::hasKeyframes()
{
  if (parent_row == nullptr) {
    return false;
  }
  return (parent_row->isKeyframing() && (!keyframes.empty()));
}

QVariant EffectField::validate_keyframe_data(double timecode, bool async) {
  if (hasKeyframes()) {
    int before_keyframe;
    int after_keyframe;
    double progress;
    get_keyframe_data(timecode, before_keyframe, after_keyframe, progress);

    const QVariant& before_data = keyframes.at(before_keyframe).data;
    switch (type_) {
      case EffectFieldType::DOUBLE:
      {
        double value;
        if (before_keyframe == after_keyframe) {
          value = keyframes.at(before_keyframe).data.toDouble();
        } else {
          const EffectKeyframe& before_key = keyframes.at(before_keyframe);
          const EffectKeyframe& after_key = keyframes.at(after_keyframe);

          double before_dbl = before_key.data.toDouble();
          double after_dbl = after_key.data.toDouble();


          if (before_key.type == KeyframeType::HOLD) {
            // hold
            value = before_dbl;
          } else if ( (parent_row->parent_effect != nullptr)
                      && (before_key.type == KeyframeType::BEZIER || after_key.type == KeyframeType::BEZIER) ) {
            // bezier interpolation
            if (before_key.type == KeyframeType::BEZIER && after_key.type == KeyframeType::BEZIER) {
              // cubic bezier
              double t = cubic_t_from_x(timecode * parent_row->parent_effect->parent_clip->sequence->frameRate(),
                                        before_key.time, before_key.time+before_key.post_handle_x,
                                        after_key.time + after_key.pre_handle_x, after_key.time);
              value = cubic_from_t(before_dbl, before_dbl + before_key.post_handle_y,
                                   after_dbl + after_key.pre_handle_y, after_dbl, t);
            } else if (after_key.type == KeyframeType::LINEAR) { // quadratic bezier
              // last keyframe is the bezier one
              double t = quad_t_from_x(timecode * parent_row->parent_effect->parent_clip->sequence->frameRate(),
                                       before_key.time, before_key.time + before_key.post_handle_x, after_key.time);
              value = quad_from_t(before_dbl, before_dbl + before_key.post_handle_y, after_dbl, t);
            } else {
              // this keyframe is the bezier one
              double t = quad_t_from_x(timecode * parent_row->parent_effect->parent_clip->sequence->frameRate(),
                                       before_key.time, after_key.time + after_key.pre_handle_x, after_key.time);
              value = quad_from_t(before_dbl, after_dbl + after_key.pre_handle_y, after_dbl, t);
            }
          } else {
            // linear
            value = double_lerp(before_dbl, after_dbl, progress);
          }
        }
        if (async) {
          return value;
        }
        dynamic_cast<LabelSlider*>(ui_element)->set_value(value, false);
      }
        break;
      case EffectFieldType::COLOR:
      {
        QColor value;
        if (before_keyframe == after_keyframe) {
          value = keyframes.at(before_keyframe).data.value<QColor>();
        } else {
          const auto before_color = keyframes.at(before_keyframe).data.value<QColor>();
          const auto after_color = keyframes.at(after_keyframe).data.value<QColor>();
          value = QColor(lerp(before_color.red(), after_color.red(), progress),
                         lerp(before_color.green(), after_color.green(), progress),
                         lerp(before_color.blue(), after_color.blue(), progress));
        }
        if (async) {
          return value;
        }
        dynamic_cast<ColorButton*>(ui_element)->set_color(value);
      }
        break;
      case EffectFieldType::STRING:
        if (async) {
          return before_data;
        }
        dynamic_cast<TextEditEx*>(ui_element)->setPlainTextEx(before_data.toString());
        break;
      case EffectFieldType::BOOL:
        if (async) {
          return before_data;
        }
        dynamic_cast<QCheckBox*>(ui_element)->setChecked(before_data.toBool());
        break;
      case EffectFieldType::COMBO:
        if (async) {
          return before_data;
        }
        dynamic_cast<ComboBoxEx*>(ui_element)->setCurrentIndexEx(before_data.toInt());
        break;
      case EffectFieldType::FONT:
        if (async) {
          return before_data;
        }
        dynamic_cast<FontCombobox*>(ui_element)->setCurrentTextEx(before_data.toString());
        break;
      case EffectFieldType::FILE_T:
        if (async) {
          return before_data;
        }
        dynamic_cast<EmbeddedFileChooser*>(ui_element)->setFilename(before_data.toString());
        break;
      default:
        qWarning() << "Unhandled Effect Field" << static_cast<int>(type_);
        break;
    }//switch
  }
  return QVariant();
}

void EffectField::ui_element_change() {
  bool dragging_double = (type_ == EffectFieldType::DOUBLE && dynamic_cast<LabelSlider*>(ui_element)->is_dragging());
  const auto ca = dragging_double ? nullptr : new ComboAction();
  make_key_from_change(ca);
  if (!dragging_double) {
    e_undo_stack.push(ca);
  }
  emit changed();
}

void EffectField::make_key_from_change(ComboAction* ca) {
  if (parent_row->isKeyframing()) {
    parent_row->set_keyframe_now(ca);
  } else if (ca != nullptr) {
    // set undo
    ca->append(new EffectFieldUndo(this));
  }
}


const QVariant& EffectField::getDefaultData() const
{
  return default_data_;
}

bool EffectField::setValue(const QVariant& value)
{
  auto conv_ok = true;
  switch (type_) {
    case EffectFieldType::BOOL:
      set_bool_value(value.toString() == "true");
      break;
    case EffectFieldType::COLOR:
    {
      // as the color would have been saved as #AARRGGBB
      auto clr = QColor(value.toString());
      set_color_value(clr);
    }
      break;
    case EffectFieldType::COMBO:
      set_combo_index(value.toInt(&conv_ok));
      break;
    case EffectFieldType::DOUBLE:
      set_double_value(value.toDouble(&conv_ok));
      break;
    case EffectFieldType::FILE_T:
      set_filename(value.toString());
      break;
    case EffectFieldType::FONT:
      set_font_name(value.toString());
      break;
    case EffectFieldType::STRING:
      set_string_value(value.toString());
      break;
    case EffectFieldType::UNKNOWN:
      [[fallthrough]];
    default:
      conv_ok = false;
      qWarning() << "unknown field type";
      break;
  }

  if (!conv_ok) {
    qWarning() << "Unable to set value, type=" << static_cast<int>(EffectFieldType::UNKNOWN) << "value=" << value;
  }
  return conv_ok;
}


QVariant EffectField::value() const
{
  Q_ASSERT(ui_element);
  if (type_ == EffectFieldType::BOOL) {
    return dynamic_cast<QCheckBox*>(ui_element)->isChecked();
  } else {
    auto elem = reinterpret_cast<chestnut::ui::IEffectFieldWidget*>(ui_element);
    auto val = elem->getValue();
    return val;
  }
}


void EffectField::setPrefix(QString value)
{
  Q_ASSERT(ui_element);
  if (type_ == EffectFieldType::DOUBLE) {
    dynamic_cast<LabelSlider*>(ui_element)->setPrefix(value);
  } else {
    qWarning() << "Prefix not available with field-type";
  }
}
void EffectField::setSuffix(QString value)
{
  Q_ASSERT(ui_element);
  if (type_ == EffectFieldType::DOUBLE) {
    dynamic_cast<LabelSlider*>(ui_element)->setSuffix(value);
  } else {
    qWarning() << "Prefix not available with field-type";
  }
}


bool EffectField::load(QXmlStreamReader& stream)
{
  while (stream.readNextStartElement()) {
    auto name = stream.name().toString().toLower();
    if (name == "value") {
      setValue(stream.readElementText());
    } else if (name == "keyframe") {
      // TODO:
      stream.skipCurrentElement();
    } else {
      qWarning() << "Unhandled element" << name;
      stream.skipCurrentElement();
    }
  }
  return true;
}
bool EffectField::save(QXmlStreamWriter& stream) const
{
  stream.writeStartElement("field");
  stream.writeTextElement("name", id_);
  stream.writeTextElement("value", fieldTypeValueToString(type_, get_current_data()));

  for (const auto& key : keyframes) {
    if (!key.save(stream)) {
      chestnut::throwAndLog("Failed to save EffectKeyFrame");
    }
  }

  stream.writeEndElement();
  return true;
}

EffectKeyframe EffectField::previousKey(const long position) const
{
  EffectKeyframe key;

  for (auto field_key : keyframes) {
    if ( (field_key.time < position) && (field_key.time > key.time) ) {
      key = field_key;
    }
  }

  return key;
}

EffectKeyframe EffectField::nextKey(const long position) const
{
  EffectKeyframe key;
  key.time = LONG_MAX;

  for (auto field_key : keyframes) {
    if ( (field_key.time > position) && (field_key.time < key.time) ) {
      key = field_key;
    }
  }

  return key;
}

QWidget* EffectField::get_ui_element() {
  return ui_element;
}

void EffectField::set_enabled(bool e) {
  ui_element->setEnabled(e);
}

double EffectField::get_double_value(double timecode, bool async) {
  if (async && hasKeyframes()) {
    return validate_keyframe_data(timecode, true).toDouble();
  }
  validate_keyframe_data(timecode);
  return dynamic_cast<LabelSlider*>(ui_element)->value();
}

void EffectField::set_double_value(double v) {
  dynamic_cast<LabelSlider*>(ui_element)->set_value(v, false);
}

void EffectField::set_double_default_value(double v) {
  setDefaultValue(v);
  dynamic_cast<LabelSlider*>(ui_element)->set_default_value(v);
}

void EffectField::set_double_minimum_value(double v) {
  dynamic_cast<LabelSlider*>(ui_element)->set_minimum_value(v);
}

void EffectField::set_double_maximum_value(double v) {
  dynamic_cast<LabelSlider*>(ui_element)->set_maximum_value(v);
}


void EffectField::set_double_step_value(const double v)
{
  dynamic_cast<LabelSlider*>(ui_element)->set_step_value(v);
}

void EffectField::add_combo_item(const QString& name, const QVariant& data) {
  dynamic_cast<ComboBoxEx*>(ui_element)->addItem(name, data);
}

int EffectField::get_combo_index(double timecode, bool async) {
  if (async && hasKeyframes()) {
    return validate_keyframe_data(timecode, true).toInt();
  }
  validate_keyframe_data(timecode);
  return dynamic_cast<ComboBoxEx*>(ui_element)->currentIndex();
}

QVariant EffectField::get_combo_data(double timecode) {
  validate_keyframe_data(timecode);
  return dynamic_cast<ComboBoxEx*>(ui_element)->currentData();
}

QString EffectField::get_combo_string(double timecode) {
  validate_keyframe_data(timecode);
  return dynamic_cast<ComboBoxEx*>(ui_element)->currentText();
}

void EffectField::set_combo_index(int index) {
  dynamic_cast<ComboBoxEx*>(ui_element)->setCurrentIndexEx(index);
}

void EffectField::set_combo_string(const QString& s) {
  dynamic_cast<ComboBoxEx*>(ui_element)->setCurrentTextEx(s);
}

bool EffectField::get_bool_value(double timecode, bool async) {
  if (async && hasKeyframes()) {
    return validate_keyframe_data(timecode, true).toBool();
  }
  validate_keyframe_data(timecode);
  return dynamic_cast<QCheckBox*>(ui_element)->isChecked();
}

void EffectField::set_bool_value(bool b) {
  dynamic_cast<QCheckBox*>(ui_element)->setChecked(b);
}

QString EffectField::get_string_value(double timecode, bool async) {
  if (async && hasKeyframes()) {
    return validate_keyframe_data(timecode, true).toString();
  }
  validate_keyframe_data(timecode);
  return dynamic_cast<TextEditEx*>(ui_element)->getPlainTextEx();
}

void EffectField::set_string_value(const QString& s) {
  dynamic_cast<TextEditEx*>(ui_element)->setPlainTextEx(s);
}

QString EffectField::get_font_name(double timecode, bool async) {
  if (async && hasKeyframes()) {
    return validate_keyframe_data(timecode, true).toString();
  }
  validate_keyframe_data(timecode);
  return dynamic_cast<FontCombobox*>(ui_element)->currentText();
}

void EffectField::set_font_name(const QString& s) {
  dynamic_cast<FontCombobox*>(ui_element)->setCurrentText(s);
}

QColor EffectField::get_color_value(double timecode, bool async) {
  if (async && hasKeyframes()) {
    return validate_keyframe_data(timecode, true).value<QColor>();
  }
  validate_keyframe_data(timecode);
  return dynamic_cast<ColorButton*>(ui_element)->get_color();
}

void EffectField::set_color_value(const QColor& color)
{
  Q_ASSERT(ui_element != nullptr);
  dynamic_cast<ColorButton*>(ui_element)->set_color(color);
}

QString EffectField::get_filename(double timecode, bool async) {
  if (async && hasKeyframes()) {
    return validate_keyframe_data(timecode, true).toString();
  }
  validate_keyframe_data(timecode);
  return dynamic_cast<EmbeddedFileChooser*>(ui_element)->getFilename();
}

void EffectField::set_filename(const QString &s) {
  dynamic_cast<EmbeddedFileChooser*>(ui_element)->setFilename(s);
}
