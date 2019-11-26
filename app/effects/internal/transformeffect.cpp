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
#include "transformeffect.h"

#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QOpenGLFunctions>
#include <QComboBox>
#include <QMouseEvent>

#include "ui/collapsiblewidget.h"
#include "project/clip.h"
#include "project/sequence.h"
#include "project/footage.h"
#include "io/math.h"
#include "ui/labelslider.h"
#include "ui/comboboxex.h"
#include "debug.h"


constexpr int BLEND_MODE_NORMAL = 0;
constexpr int BLEND_MODE_SCREEN = 1;
constexpr int BLEND_MODE_MULTIPLY = 2;
constexpr int BLEND_MODE_OVERLAY = 3;
constexpr auto SCALE_DEFAULT_VALUE = 100;
constexpr auto OPACITY_DEFAULT_VALUE = 100;
constexpr auto ANCHOR_DEFAULT_VALUE = 0;

TransformEffect::TransformEffect(ClipPtr c, const EffectMeta& em) : Effect(c, em)
{
  setCapability(Capability::COORDS);
}

void TransformEffect::refresh()
{
  if (ui_setup && parent_clip != nullptr && parent_clip->sequence != nullptr) {
    const double new_default_pos_x = parent_clip->sequence->width() >> 1;
    const double new_default_pos_y = parent_clip->sequence->height() >> 1;

    position_x->set_double_default_value(new_default_pos_x);
    position_y->set_double_default_value(new_default_pos_y);
    scale_x->set_double_default_value(SCALE_DEFAULT_VALUE);
    scale_y->set_double_default_value(SCALE_DEFAULT_VALUE);

    anchor_x_box->set_double_default_value(ANCHOR_DEFAULT_VALUE);
    anchor_y_box->set_double_default_value(ANCHOR_DEFAULT_VALUE);
    opacity->set_double_default_value(OPACITY_DEFAULT_VALUE);

    const double x_percent_multipler = 200.0 / parent_clip->sequence->width();
    const double y_percent_multipler = 200.0 / parent_clip->sequence->height();
    top_left_gizmo->x_field_multi1 = -x_percent_multipler;
    top_left_gizmo->y_field_multi1 = -y_percent_multipler;
    top_center_gizmo->y_field_multi1 = -y_percent_multipler;
    top_right_gizmo->x_field_multi1 = x_percent_multipler;
    top_right_gizmo->y_field_multi1 = -y_percent_multipler;
    bottom_left_gizmo->x_field_multi1 = -x_percent_multipler;
    bottom_left_gizmo->y_field_multi1 = y_percent_multipler;
    bottom_center_gizmo->y_field_multi1 = y_percent_multipler;
    bottom_right_gizmo->x_field_multi1 = x_percent_multipler;
    bottom_right_gizmo->y_field_multi1 = y_percent_multipler;
    left_center_gizmo->x_field_multi1 = -x_percent_multipler;
    right_center_gizmo->x_field_multi1 = x_percent_multipler;
    rotate_gizmo->x_field_multi1 = x_percent_multipler;

    set = true;
  }
}

void TransformEffect::toggle_uniform_scale(bool enabled)
{
  scale_y->set_enabled(!enabled);

  top_center_gizmo->y_field1 = enabled ? scale_x : scale_y;
  bottom_center_gizmo->y_field1 = enabled ? scale_x : scale_y;
  top_left_gizmo->y_field1 = enabled ? nullptr : scale_y;
  top_right_gizmo->y_field1 = enabled ? nullptr : scale_y;
  bottom_left_gizmo->y_field1 = enabled ? nullptr : scale_y;
  bottom_right_gizmo->y_field1 = enabled ? nullptr : scale_y;
}

void TransformEffect::process_coords(double timecode, GLTextureCoords& coords, int)
{
  // position
  glTranslatef(static_cast<GLfloat>(position_x->get_double_value(timecode) - (parent_clip->sequence->width() >> 1)),
               static_cast<GLfloat>(position_y->get_double_value(timecode) - (parent_clip->sequence->height() >> 1)),
               0);

  // anchor point
  const int anchor_x_offset = qRound(anchor_x_box->get_double_value(timecode));
  const int anchor_y_offset = qRound(anchor_y_box->get_double_value(timecode));
  coords.vertices_[0].x_ -= anchor_x_offset;
  coords.vertices_[1].x_ -= anchor_x_offset;
  coords.vertices_[3].x_ -= anchor_x_offset;
  coords.vertices_[2].x_ -= anchor_x_offset;
  coords.vertices_[0].y_ -= anchor_y_offset;
  coords.vertices_[1].y_ -= anchor_y_offset;
  coords.vertices_[3].y_ -= anchor_y_offset;
  coords.vertices_[2].y_ -= anchor_y_offset;

  // rotation
  glRotatef(static_cast<GLfloat>(rotation->get_double_value(timecode)), 0, 0, 1);

  // scale
  const auto sx = static_cast<float>(scale_x->get_double_value(timecode) * 0.01);
  const float sy = (uniform_scale_field->get_bool_value(timecode))
                   ? sx : static_cast<float>(scale_y->get_double_value(timecode) * 0.01);
  glScalef(sx, sy, 1);

  // blend mode
  switch (blend_mode_box->get_combo_data(timecode).toInt()) {
    case BLEND_MODE_NORMAL:
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case BLEND_MODE_OVERLAY:
      glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      break;
    case BLEND_MODE_SCREEN:
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
      break;
    case BLEND_MODE_MULTIPLY:
      glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
      break;
    default:
      qCritical() << "Invalid blend mode. This is a bug - please contact developers";
  }

  // opacity
  float color[4];
  glGetFloatv(GL_CURRENT_COLOR, color);
  glColor4f(1.0, 1.0, 1.0, color[3] * static_cast<float>(opacity->get_double_value(timecode) * 0.01));
}

void TransformEffect::gizmo_draw(double /*timecode*/, GLTextureCoords& coords)
{
  top_left_gizmo->world_pos[0] = QPoint(coords.vertices_[0].x_, coords.vertices_[0].y_);
  top_center_gizmo->world_pos[0] = QPoint(lerp(coords.vertices_[0].x_, coords.vertices_[1].x_, 0.5),
                                          lerp(coords.vertices_[0].y_, coords.vertices_[1].y_, 0.5));
  top_right_gizmo->world_pos[0] = QPoint(coords.vertices_[1].x_, coords.vertices_[1].y_);
  right_center_gizmo->world_pos[0] = QPoint(lerp(coords.vertices_[1].x_, coords.vertices_[2].x_, 0.5),
                                            lerp(coords.vertices_[1].y_, coords.vertices_[2].y_, 0.5));
  bottom_right_gizmo->world_pos[0] = QPoint(coords.vertices_[2].x_, coords.vertices_[2].y_);
  bottom_center_gizmo->world_pos[0] = QPoint(lerp(coords.vertices_[2].x_, coords.vertices_[3].x_, 0.5),
                                             lerp(coords.vertices_[2].y_, coords.vertices_[3].y_, 0.5));
  bottom_left_gizmo->world_pos[0] = QPoint(coords.vertices_[3].x_, coords.vertices_[3].y_);
  left_center_gizmo->world_pos[0] = QPoint(lerp(coords.vertices_[3].x_, coords.vertices_[0].x_, 0.5),
                                           lerp(coords.vertices_[3].y_, coords.vertices_[0].y_, 0.5));

  rotate_gizmo->world_pos[0] = QPoint(lerp(top_center_gizmo->world_pos[0].x(),
                                           bottom_center_gizmo->world_pos[0].x(),
                                           -0.1),
                                      lerp(top_center_gizmo->world_pos[0].y(),
                                           bottom_center_gizmo->world_pos[0].y(),
                                           -0.1));


  rect_gizmo->world_pos[0] = QPoint(coords.vertices_[0].x_, coords.vertices_[0].y_); // TL
  rect_gizmo->world_pos[1] = QPoint(coords.vertices_[1].x_, coords.vertices_[1].y_); // TR
  rect_gizmo->world_pos[2] = QPoint(coords.vertices_[2].x_, coords.vertices_[2].y_); // BR
  rect_gizmo->world_pos[3] = QPoint(coords.vertices_[3].x_, coords.vertices_[3].y_); // BL
}

void TransformEffect::setupUi()
{
  if (ui_setup) {
    return;
  }
  Effect::setupUi();
  EffectRowPtr position_row = add_row(tr("Position"));
  position_x = position_row->add_field(EffectFieldType::DOUBLE, "posx"); // position X
  position_y = position_row->add_field(EffectFieldType::DOUBLE, "posy"); // position Y

  EffectRowPtr scale_row = add_row(tr("Scale"));
  scale_x = scale_row->add_field(EffectFieldType::DOUBLE, "scalex"); // scale X (and Y is uniform scale is selected)
  scale_x->set_double_minimum_value(0);
  scale_x->set_double_maximum_value(3000);
  scale_y = scale_row->add_field(EffectFieldType::DOUBLE, "scaley"); // scale Y (disabled if uniform scale is selected)
  scale_y->set_double_minimum_value(0);
  scale_y->set_double_maximum_value(3000);

  EffectRowPtr uniform_scale_row = add_row(tr("Uniform Scale"));
  uniform_scale_field = uniform_scale_row->add_field(EffectFieldType::BOOL, "uniformscale"); // uniform scale option

  EffectRowPtr rotation_row = add_row(tr("Rotation"));
  rotation = rotation_row->add_field(EffectFieldType::DOUBLE, "rotation");
  rotation->set_double_default_value(0);

  EffectRowPtr anchor_point_row = add_row(tr("Anchor Point"));
  anchor_x_box = anchor_point_row->add_field(EffectFieldType::DOUBLE, "anchorx"); // anchor point X
  anchor_y_box = anchor_point_row->add_field(EffectFieldType::DOUBLE, "anchory"); // anchor point Y

  EffectRowPtr opacity_row = add_row(tr("Opacity"));
  opacity = opacity_row->add_field(EffectFieldType::DOUBLE, "opacity"); // opacity
  opacity->set_double_minimum_value(0);
  opacity->set_double_maximum_value(100);

  EffectRowPtr blend_mode_row = add_row(tr("Blend Mode"));
  blend_mode_box = blend_mode_row->add_field(EffectFieldType::COMBO, "blendmode"); // blend mode
  blend_mode_box->add_combo_item(tr("Normal"), BLEND_MODE_NORMAL);
  blend_mode_box->add_combo_item(tr("Overlay"), BLEND_MODE_OVERLAY);
  blend_mode_box->add_combo_item(tr("Screen"), BLEND_MODE_SCREEN);
  blend_mode_box->add_combo_item(tr("Multiply"), BLEND_MODE_MULTIPLY);

  // set up gizmos
  top_left_gizmo = add_gizmo(GizmoType::DOT);
  top_left_gizmo->set_cursor(Qt::SizeFDiagCursor);
  top_left_gizmo->x_field1 = scale_x;

  top_center_gizmo = add_gizmo(GizmoType::DOT);
  top_center_gizmo->set_cursor(Qt::SizeVerCursor);
  top_center_gizmo->y_field1 = scale_x;

  top_right_gizmo = add_gizmo(GizmoType::DOT);
  top_right_gizmo->set_cursor(Qt::SizeBDiagCursor);
  top_right_gizmo->x_field1 = scale_x;

  bottom_left_gizmo = add_gizmo(GizmoType::DOT);
  bottom_left_gizmo->set_cursor(Qt::SizeBDiagCursor);
  bottom_left_gizmo->x_field1 = scale_x;

  bottom_center_gizmo = add_gizmo(GizmoType::DOT);
  bottom_center_gizmo->set_cursor(Qt::SizeVerCursor);
  bottom_center_gizmo->y_field1 = scale_x;

  bottom_right_gizmo = add_gizmo(GizmoType::DOT);
  bottom_right_gizmo->set_cursor(Qt::SizeFDiagCursor);
  bottom_right_gizmo->x_field1 = scale_x;

  left_center_gizmo = add_gizmo(GizmoType::DOT);
  left_center_gizmo->set_cursor(Qt::SizeHorCursor);
  left_center_gizmo->x_field1 = scale_x;

  right_center_gizmo = add_gizmo(GizmoType::DOT);
  right_center_gizmo->set_cursor(Qt::SizeHorCursor);
  right_center_gizmo->x_field1 = scale_x;

  anchor_gizmo = add_gizmo(GizmoType::TARGET);
  anchor_gizmo->set_cursor(Qt::SizeAllCursor);
  anchor_gizmo->x_field1 = anchor_x_box;
  anchor_gizmo->y_field1 = anchor_y_box;
  anchor_gizmo->x_field2 = position_x;
  anchor_gizmo->y_field2 = position_y;

  rotate_gizmo = add_gizmo(GizmoType::DOT);
  rotate_gizmo->color = Qt::green;
  rotate_gizmo->set_cursor(Qt::SizeAllCursor);
  rotate_gizmo->x_field1 = rotation;

  rect_gizmo = add_gizmo(GizmoType::POLY);
  rect_gizmo->x_field1 = position_x;
  rect_gizmo->y_field1 = position_y;

  QObject::connect(uniform_scale_field, SIGNAL(toggled(bool)), this, SLOT(toggle_uniform_scale(bool)));

  // set defaults
  uniform_scale_field->set_bool_value(true);
  uniform_scale_field->setDefaultValue(true);
  blend_mode_box->set_combo_index(0);
  blend_mode_box->setDefaultValue(0);
  set = false;
  this->refresh();
}
