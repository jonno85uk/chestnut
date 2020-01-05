/*
 * Chestnut. Chestnut is a free non-linear video editor for Linux.
 * Copyright (C) 2019 Jonathan Noble
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

#ifndef TIMECODETEST_H
#define TIMECODETEST_H

#include <QObject>

namespace chestnut::project
{

class TimeCodeTest : public QObject
{
    Q_OBJECT
  public:
    TimeCodeTest();

  private slots:
    void testCaseInit();
    void testCaseInitSpecifiedTimestamp();
    void testCaseInitPAL25();
    void testCaseInitNTSC24();
    void testCaseInitNTSC30();
    void testCaseNTSC30Frames();
    void testCaseInitNTSC60();
};
}

#endif // TIMECODETEST_H