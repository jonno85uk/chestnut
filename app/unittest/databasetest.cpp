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

#include "databasetest.h"
#include "database.h"

using namespace chestnut;

void DatabaseTest::testCaseInstanceEmptyPath()
{
  QVERIFY_EXCEPTION_THROWN(Database::instance(), std::runtime_error);
}


void DatabaseTest::testCaseInstancePopulatedPath()
{
  auto db = Database::instance(":memory:");
  QVERIFY(db != nullptr);
}


void DatabaseTest::testCaseGetsFromNewInstance()
{
  auto db = Database::instance(":memory:");
  auto presets = db->getPresets("spam");
  QVERIFY(presets.empty());
  auto params = db->getPresetParameters("foo", "bar");
  QVERIFY(params.empty());
}


void DatabaseTest::testCaseAddNoParams()
{
  auto db = Database::instance(":memory:");
  EffectPreset preset {"foo", "bar", {}};
  QVERIFY(db->addNewPreset(preset) == false);
}

void DatabaseTest::testCaseAdd()
{
  auto db = Database::instance(":memory:");
  EffectPreset preset {"foo", "bar", {}};
  preset.parameters_["row1"].append(QPair<QString, QVariant>("spam", "eggs"));
  preset.parameters_["row2"].append(QPair<QString, QVariant>("egg", "bacon and spam"));
  QVERIFY(db->addNewPreset(preset) == true);
}

void DatabaseTest::testCaseAddDuplicate()
{
  auto db = Database::instance(":memory:");
#if false
  // although creating a new instance, previous records are held in :memory:
  QVERIFY(db->getPresets("foo").empty());
#endif
  EffectPreset preset {"foo", "bar2", {}};
  preset.parameters_["row1"].append(QPair<QString, QVariant>("spam", "eggs"));
  preset.parameters_["row2"].append(QPair<QString, QVariant>("egg", "bacon and spam"));
  QVERIFY(db->addNewPreset(preset) == true);
  QVERIFY(db->addNewPreset(preset) == false);
}


void DatabaseTest::testCaseAddGet()
{
  auto db = Database::instance(":memory:");
  EffectPreset preset {"foo", "bar3", {}};
  preset.parameters_["row1"].append(QPair<QString, QVariant>("spam", "eggs"));
  preset.parameters_["row2"].append(QPair<QString, QVariant>("egg", "bacon and spam"));
  preset.parameters_["row3"].append(QPair<QString, QVariant>("bacon", 100.001));
  preset.parameters_["row3"].append(QPair<QString, QVariant>("123", 456));
  db->addNewPreset(preset);
  EffectParametersType preset_vals = db->getPresetParameters("foo", "bar3");
  QCOMPARE(preset.parameters_.size(), preset_vals.size());

  for (const auto& [name, params] : preset.parameters_.toStdMap()) {
    QVERIFY(preset_vals.contains(name));
    QVERIFY(preset_vals[name].size() == params.size());
    for (const auto& p : params) {
      bool param_found = false;
      for (const auto& pv_p : preset_vals[name]) {
        if (p.first == pv_p.first) {
          QVERIFY(p.second == pv_p.second);
          param_found = true;
          break;
        }
      }
      QVERIFY(param_found);
    }
  }
}


void DatabaseTest::testCaseAddPresetSameNameDifferentEffect()
{
  auto db = Database::instance(":memory:");
  EffectParametersType params;
  params["spam"].append(QPair<QString, QVariant>("spam", "eggs"));
  EffectPreset preset {"foo", "bar4", params};
  db->addNewPreset(preset);
  EffectPreset preset2 {"spam", "bar4", params};
  QVERIFY(db->addNewPreset(preset2));
}


void DatabaseTest::testCaseDeletePreset()
{
  auto db = Database::instance(":memory:");
  EffectPreset preset {"foodel", "bardel", {}};
  preset.parameters_["row1del"].append(QPair<QString, QVariant>("spam", "eggs"));
  preset.parameters_["row2del"].append(QPair<QString, QVariant>("egg", "bacon and spam"));
  db->addNewPreset(preset);
  auto presets = db->getPresets("foodel");
  QVERIFY(presets.size() == 1);
  db->deletePreset(preset.effect_name_, preset.preset_name_);
  presets = db->getPresets("foodel");
  QVERIFY(presets.empty());
}
