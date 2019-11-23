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
#include "keyframe.h"

#include <QVector>

#include "effectfield.h"
#include "undo.h"
#include "panels/panelmanager.h"

constexpr auto ROOT_TAG = "keyframe";
constexpr auto TYPE_TAG = "type";
constexpr auto DATA_TAG = "value";
constexpr auto TIME_TAG = "frame";
constexpr auto PRE_HANDLE_X_TAG = "prex";
constexpr auto PRE_HANDLE_Y_TAG = "prey";
constexpr auto POST_HANDLE_X_TAG = "postx";
constexpr auto POST_HANDLE_Y_TAG = "posty";

void delete_keyframes(QVector<EffectField*>& selected_key_fields, QVector<int> &selected_keys)
{
  QVector<EffectField*> fields;
  QVector<int> key_indices;

  for (int i=0;i<selected_keys.size();i++) {
    bool added = false;
    for (int j=0;j<key_indices.size();j++) {
      if (key_indices.at(j) < selected_keys.at(i)) {
        key_indices.insert(j, selected_keys.at(i));
        fields.insert(j, selected_key_fields.at(i));
        added = true;
        break;
      }
    }
    if (!added) {
      key_indices.append(selected_keys.at(i));
      fields.append(selected_key_fields.at(i));
    }
  }

  if (!fields.empty()) {
    auto ca = new ComboAction();
    for (int i=0;i<key_indices.size();i++) {
      ca->append(new KeyframeDelete(fields.at(i), key_indices.at(i)));
    }
    e_undo_stack.push(ca);
    selected_keys.clear();
    selected_key_fields.clear();
    panels::PanelManager::refreshPanels(false);
  }
}

EffectKeyframe::EffectKeyframe(const EffectField* parent) : parent_(parent)
{
  Q_ASSERT(parent != nullptr);
}


bool EffectKeyframe::load(QXmlStreamReader& stream)
{
  bool type_set = false;
  for (const auto& attr : stream.attributes()) {
    auto name = attr.name().toString().toLower();
    if (name == TYPE_TAG) {
      type = static_cast<KeyframeType>(attr.value().toInt());
      type_set = true;
    } else {
      qWarning() << "Unhandled attribute" << name;
    }
  }

  if (!type_set) {
    qCritical() << "No Keyframe type found in stream";
    return false;
  }

  // NOTE: these are only needed for bezier type
  auto post_x_set = false;
  auto post_y_set = false;
  auto pre_x_set = false;
  auto pre_y_set = false;

  while (stream.readNextStartElement()) {
    auto name = stream.name().toString().toLower();
    if (name == DATA_TAG) {
      data.setValue(stream.readElementText());
    } else if (name == TIME_TAG) {
      time = stream.readElementText().toInt();
    } else if (name == PRE_HANDLE_X_TAG) {
      pre_handle_x = stream.readElementText().toDouble();
      pre_x_set = true;
    } else if (name == PRE_HANDLE_Y_TAG) {
      pre_handle_y = stream.readElementText().toDouble();
      pre_y_set = true;
    } else if (name == POST_HANDLE_X_TAG) {
      post_handle_x = stream.readElementText().toDouble();
      post_x_set = true;
    } else if (name == POST_HANDLE_Y_TAG) {
      post_handle_y = stream.readElementText().toDouble();
      post_y_set = true;
    } else {
      qWarning() << "Unhandled Element" << name;
      stream.skipCurrentElement();
    }
  }

  return (!data.isNull() && time >= 0 && post_x_set && post_y_set && pre_x_set && pre_y_set);
}

bool EffectKeyframe::save(QXmlStreamWriter& stream) const
{
  Q_ASSERT(parent_ != nullptr);
  stream.writeStartElement(ROOT_TAG);

  stream.writeAttribute(TYPE_TAG, QString::number(static_cast<int>(type)));

  stream.writeTextElement(DATA_TAG, fieldTypeValueToString(parent_->type_, data));
  stream.writeTextElement(TIME_TAG, QString::number(time));
  stream.writeTextElement(PRE_HANDLE_X_TAG, QString::number(pre_handle_x));
  stream.writeTextElement(PRE_HANDLE_Y_TAG, QString::number(pre_handle_y));
  stream.writeTextElement(POST_HANDLE_X_TAG, QString::number(post_handle_x));
  stream.writeTextElement(POST_HANDLE_Y_TAG, QString::number(post_handle_y));
  stream.writeEndElement();
  return true;
}
