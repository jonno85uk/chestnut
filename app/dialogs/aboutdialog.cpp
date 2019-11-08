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
#include "aboutdialog.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>

#include "chestnut.h"

AboutDialog::AboutDialog(QWidget *parent) :
  QDialog(parent)
{
  setWindowTitle("About Chestnut");
  setMaximumWidth(360);

  QVBoxLayout* layout = new QVBoxLayout();
  layout->setSpacing(20);
  setLayout(layout);

  const QString fmt(QString(chestnut::version::PREREL).size() > 0 ? "%1.%2.%3-%4" : "%1.%2.%3");

  const QString version(fmt.arg(chestnut::version::MAJOR)
                        .arg(chestnut::version::MINOR)
                        .arg(chestnut::version::PATCH)
                        .arg(chestnut::version::PREREL));

  auto label =
      new QLabel("<html><head/><body>"
                 "<h1>Chestnut " + version + "</h1>"
                 "<p><a href=\"https://github.com/jonno85uk/chestnut\">"
                 "<span style=\" text-decoration: underline; color:#007af4;\">"
                 "https://github.com/jonno85uk/chestnut"
                 "</span></a></p><p>"
                 + tr("Chestnut is a non-linear video editor. This software is free and protected by the GNU GPL.")
                 + "</p><p>"
                 + tr("Chestnut source code is available for download from its website.")
                 + "</p></body></html>");
  label->setAlignment(Qt::AlignCenter);
  label->setWordWrap(true);
  layout->addWidget(label);

  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  buttons->setCenterButtons(true);
  layout->addWidget(buttons);

  connect(buttons, SIGNAL(accepted()), this, SLOT(accept()));
}
