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

#ifndef DATABASETEST_H
#define DATABASETEST_H

#include <QObject>
#include <QtTest>


class DatabaseTest : public QObject
{
    Q_OBJECT
  public:
    DatabaseTest() {}

  private slots:
    void testCaseInstanceEmptyPath();
    void testCaseInstancePopulatedPath();
    void testCaseGetsFromNewInstance();
    void testCaseAddNoParams();
    void testCaseAdd();
    void testCaseAddDuplicate();
    void testCaseAddGet();
    void testCaseAddPresetSameNameDifferentEffect();
    void testCaseDeletePreset();
};

#endif // DATABASETEST_H
