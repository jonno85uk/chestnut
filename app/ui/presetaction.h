/*
 * Chestnut. Chestnut is a free non-linear video editor.
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

#ifndef PRESETACTION_H
#define PRESETACTION_H

#include <QWidgetAction>
#include <QPushButton>

namespace chestnut::ui
{
  class PresetAction : public QWidgetAction
  {
      Q_OBJECT
    public:
      /**
       * @brief PresetAction
       * @param name    The preset name to show in menu
       * @param parent
       */
      PresetAction(QString name, QObject* parent);
      ~PresetAction() override;
    signals:
      void usePreset(QString);
      void deletePreset(QString);
    private:
      QWidget* main_widget_ {nullptr};
      QPushButton* name_button_ {nullptr};
      QPushButton* delete_button_ {nullptr};
    private slots:
      void onDeleteButtonPress();
      void onPresetButtonPress();
  };
}

#endif // PRESETACTION_H
