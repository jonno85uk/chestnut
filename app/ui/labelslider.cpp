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
#include "labelslider.h"

#include "project/undo.h"
#include "panels/viewer.h"
#include "io/config.h"
#include "debug.h"

#include <QMouseEvent>
#include <QInputDialog>
#include <QApplication>
#include <QRegularExpression>

constexpr auto TIMECODE_REGEX = "^[0-9]+:[0-9]+:[0-9]+[:;][0-9]+";
constexpr auto NUMBER_REGEX = "^[0-9]+";
constexpr auto DEFAULT_LABEL_COLOUR = "#ffc000";
constexpr auto NAN_REPR = "---";

LabelSlider::LabelSlider(QWidget* parent)
  : QLabel(parent)
{
  set_color();
  setCursor(Qt::SizeHorCursor);
  set_default_value(0);
}

void LabelSlider::set_frame_rate(const double d)
{
  frame_rate = d;
}

void LabelSlider::set_display_type(const SliderType type)
{
  display_type = type;
  setText(valueToString(internal_value));
}

void LabelSlider::set_value(const double val, const bool userSet)
{
  set = true;
  if (min_value && (val < min_value)) {
    internal_value = min_value.value();
  } else if (max_value && (val > max_value)) {
    internal_value = max_value.value();
  } else {
    internal_value = val;
  }

  setText(prefix_ + valueToString(internal_value) + suffix_);
  if (userSet) {
    emit valueChanged();
  }
}

bool LabelSlider::is_set()
{
  return set;
}

bool LabelSlider::is_dragging()
{
  return drag_proc;
}

QString LabelSlider::valueToString(const double v)
{
  if (qIsNaN(v)) {
    return NAN_REPR;
  } else {
    switch (display_type) {
      case SliderType::FRAMENUMBER:
        return frame_to_timecode(qRound(v), global::config.timecode_view, frame_rate);
      case SliderType::PERCENT:
        return QString::number((v * 100), 'f', decimal_places) + "%";
      default:
        // unhandled label slider type
        break;
    }
    return QString::number(v, 'f', decimal_places);
  }
}

double LabelSlider::getPreviousValue()
{
  return previous_value;
}

void LabelSlider::set_previous_value()
{
  previous_value = internal_value;
}

void LabelSlider::set_color(QString c)
{
  if (c.isEmpty()) {
    c = DEFAULT_LABEL_COLOUR;
  }
  setStyleSheet("QLabel{color:" + c + ";text-decoration:underline;}QLabel:disabled{color:#808080;}");
}

void LabelSlider::setPrefix(QString value)
{
  prefix_ = std::move(value);
}

void LabelSlider::setSuffix(QString value)
{
  suffix_ = std::move(value);
}

QVariant LabelSlider::getValue() const
{
  return internal_value;
}

void LabelSlider::setValue(QVariant val)
{
  internal_value = val.toDouble();
}

double LabelSlider::value()
{
  return internal_value;
}

void LabelSlider::set_default_value(const double v)
{
  default_value = v;
  if (!set) {
    set_value(v, false);
    // set_value "sets" set ...
    set = false;
  }
}

void LabelSlider::set_minimum_value(const double v)
{
  min_value = v;
}

void LabelSlider::set_maximum_value(const double v)
{
  max_value = v;
}


void LabelSlider::set_step_value(const double v)
{
  step_value_ = v;
}

void LabelSlider::mousePressEvent(QMouseEvent *ev)
{
  drag_start_value = internal_value;
  if (ev->modifiers() & Qt::AltModifier) {
    if (!qFuzzyCompare(internal_value, default_value) && !qIsNaN(default_value)) {
      set_previous_value();
      set_value(default_value, true);
    }
  } else {
    if (qIsNaN(internal_value)) {
      internal_value = 0;
    }

    qApp->setOverrideCursor(Qt::BlankCursor);
    drag_start = true;
    drag_start_x = cursor().pos().x();
    drag_start_y = cursor().pos().y();
  }
  emit clicked();
}

void LabelSlider::mouseMoveEvent(QMouseEvent* event)
{
  if (!drag_start) {
    return;
  }

  drag_proc = true;
  double diff = (cursor().pos().x() - drag_start_x) + (drag_start_y - cursor().pos().y());
  if (event->modifiers() & Qt::ControlModifier) {
    diff *= 0.01;
  }
  if (display_type == SliderType::PERCENT) {
    diff *= 0.01;
  }
  if (step_value_){
    diff *= step_value_.value();
  }
  set_value(internal_value + diff, true);
  cursor().setPos(drag_start_x, drag_start_y);
}

void LabelSlider::mouseReleaseEvent(QMouseEvent*)
{
  if (!drag_start) {
    return;
  }
  qApp->restoreOverrideCursor();
  drag_start = false;
  if (drag_proc) {
    drag_proc = false;
    previous_value = drag_start_value;
    emit valueChanged();
  } else {
    double d = internal_value;
    if (display_type == SliderType::FRAMENUMBER) {
      QString set_value(QInputDialog::getText(
                          this,
                          tr("Set Value"),
                          tr("New value:"),
                          QLineEdit::Normal,
                          valueToString(internal_value)
                          ));

      const QRegularExpression tc_rgx(TIMECODE_REGEX);
      const QRegularExpression num_rgx(NUMBER_REGEX);
      auto tc_matched = tc_rgx.match(set_value);
      auto num_matched = num_rgx.match(set_value);
      if (set_value.isEmpty() || (tc_matched.capturedRef().isEmpty() && num_matched.capturedRef().isEmpty())) {
        // ignore invalid entry
        return;
      }
      d = timecode_to_frame(set_value, global::config.timecode_view, frame_rate); // string to frame number
    } else {
      bool ok;
      d = QInputDialog::getDouble(
            this,
            tr("Set Value"),
            tr("New value:"),
            (display_type == SliderType::PERCENT) ? internal_value * 100 : internal_value,
            (min_value) ? min_value.value() : INT_MIN,
            (max_value) ? max_value.value() : INT_MAX,
            decimal_places,
            &ok
            );
      if (!ok) {
        return;
      }
      if (display_type == SliderType::PERCENT) {
        d *= 0.01;
      }
    }
    if (!qFuzzyCompare(d, internal_value)) {
      set_previous_value();
      set_value(d, true);
    }
  }
}
