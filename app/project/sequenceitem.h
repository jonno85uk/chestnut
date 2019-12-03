/*
 * Chestnut. Chestnut is a free non-linear video editor for Linux.
 * Copyright (C) 2019
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SEQUENCEITEM_H
#define SEQUENCEITEM_H

#include <memory>
#include <QString>

class Clip;
class Effect;

namespace project {
enum class SequenceItemType {
  CLIP = 0,
  EFFECT = 1,
  NONE
};

class SequenceItem
{
public:
  SequenceItem();
  virtual ~SequenceItem() {}
  explicit SequenceItem(const SequenceItemType sequenceType);
  void setName(QString name);
  virtual QString name() const;

  SequenceItemType type() const noexcept;

private:
  friend class ::Clip;
  friend class ::Effect;
  QString name_;
  SequenceItemType type_;

};
typedef std::shared_ptr<SequenceItem> SequenceItemPtr;
}

#endif // SEQUENCEITEM_H
