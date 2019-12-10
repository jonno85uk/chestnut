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

#include "presetaction.h"

#include <QHBoxLayout>

using chestnut::ui::PresetAction;

PresetAction::PresetAction(QString name, QObject* parent)  : QWidgetAction(parent)
{
  auto layout = new QHBoxLayout();
  layout->setSpacing(0);
  layout->setMargin(1);
  QAction();
  name_button_ = new QPushButton(std::move(name));
  name_button_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
  name_button_->setFlat(true);
  name_button_->setToolTip(tr("Apply effect preset"));
  name_button_->setStyleSheet("QPushButton {"
                              "text-align: left;"
                              "padding-left: 1px;"
                              "}");
  connect(name_button_, &QPushButton::pressed, this, &PresetAction::onPresetButtonPress);
  layout->addWidget(name_button_);
  delete_button_ = new QPushButton();
  delete_button_->setText("-");
  delete_button_->setMaximumSize(QSize(16, 16));
  delete_button_->setToolTip(tr("Delete effect preset"));
  connect(delete_button_, &QPushButton::pressed, this, &PresetAction::onDeleteButtonPress);
  layout->addWidget(delete_button_);
  main_widget_ = new QWidget();
  main_widget_->setLayout(layout);
  this->setDefaultWidget(main_widget_);
}


PresetAction::~PresetAction()
{
  this->releaseWidget(main_widget_);
  delete delete_button_;
  delete name_button_;
  delete main_widget_;
}

void PresetAction::onDeleteButtonPress()
{
  emit deletePreset(name_button_->text());
}
void PresetAction::onPresetButtonPress()
{
  emit usePreset(name_button_->text());
}
