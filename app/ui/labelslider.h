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
#ifndef LABELSLIDER_H
#define LABELSLIDER_H

#include <QLabel>
#include <QUndoCommand>
#include <optional>
#include "ui/IEffectFieldWidget.h"

enum class SliderType {
    NORMAL = 0,
    FRAMENUMBER = 1,
    PERCENT = 2
};

class LabelSlider : public QLabel, chestnut::ui::IEffectFieldWidget
{
    Q_OBJECT
  public:
    int decimal_places {1};

    explicit LabelSlider(QWidget* parent = nullptr);
    void set_frame_rate(const double d);
    void set_display_type(const SliderType type);
    void set_value(const double val, const bool userSet);
    void set_default_value(const double v);
    void set_minimum_value(const double v);
    void set_maximum_value(const double v);
    void set_step_value(const double v);
    double value();
    bool is_set();
    bool is_dragging();
    QString valueToString(const double v);
    double getPreviousValue();
    void set_previous_value();
    void set_color(QString c="");
    void setPrefix(QString value);
    void setSuffix(QString value);

    QVariant getValue() const override;
    void setValue(QVariant val) override;
  protected:
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
  private:
    std::optional<double> min_value;
    std::optional<double> max_value;
    std::optional<double> step_value_;

    QString suffix_;
    QString prefix_;

    double default_value;
    double internal_value {-1.0};
    double drag_start_value;
    double previous_value;
    double frame_rate {30.0};

    int drag_start_x;
    int drag_start_y;

    SliderType display_type {SliderType::NORMAL};

    bool drag_start {false};
    bool drag_proc {false};
    bool set {false};


  signals:
    void valueChanged();
    void clicked();
};

#endif // LABELSLIDER_H
