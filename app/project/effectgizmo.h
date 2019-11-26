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
#ifndef EFFECTGIZMO_H
#define EFFECTGIZMO_H


#include <QString>
#include <QRect>
#include <QPoint>
#include <QVector>
#include <QColor>

#include <memory>

#include "project/effectfield.h"

constexpr float GIZMO_DOT_SIZE = 2.5;
constexpr float GIZMO_TARGET_SIZE = 5.0;

enum class GizmoType {
    DOT = 0,
    POLY = 1,
    TARGET = 2
};

class EffectGizmo
{
public:
    QVector<QPoint> world_pos;
    QVector<QPoint> screen_pos;

    EffectField* x_field1 {nullptr};
    double x_field_multi1 {1.0};
    EffectField* y_field1 {nullptr};
    double y_field_multi1 {1.0};
    EffectField* x_field2 {nullptr};
    double x_field_multi2 {1.0};
    EffectField* y_field2 {nullptr};
    double y_field_multi2 {1.0};
    QColor color;

    explicit EffectGizmo(const GizmoType type);

    void set_previous_value();

    int get_point_count();

    GizmoType get_type() const;

    int get_cursor() const;
    void set_cursor(const int value);
private:
    GizmoType type_;
    int cursor;
};

typedef std::shared_ptr<EffectGizmo> EffectGizmoPtr;

#endif // EFFECTGIZMO_H
