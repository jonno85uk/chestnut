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

#include "database.h"

#include <QSql>
#include <QSqlQuery>
#include <QSqlError>

#include "debug.h"

using chestnut::Database;

Database::Database(QString file_path)
{
  db_ = QSqlDatabase::addDatabase("QSQLITE");
  db_.setDatabaseName(std::move(file_path));
  Q_ASSERT(db_.open());
  qInfo() << "Opened database, path =" << db_.databaseName();
  setupEffectsTable();
}

std::shared_ptr<Database> Database::instance(const QString& file_path)
{
  if (instance_) {
    return instance_;
  } else if (file_path.length() > 0) {
    instance_ = std::make_shared<Database>(file_path);
    return instance_;
  }
  throw std::runtime_error("No DB instance and no db file-path specified");
}


bool Database::addNewPreset(const EffectPreset& value)
{
  if ( (value.effect_name_.length() <= 0) || (value.preset_name_.length() <= 0) || value.parameters_.empty()) {
    qWarning() << "Not adding a new preset with empty values";
    return false;
  }

  if (presetId(value.effect_name_, value.preset_name_) >= 0) {
    qWarning() << "Preset" << value.preset_name_ << "for Effect" << value.effect_name_ << "already exists.";
    return false;
  }

  QSqlQuery q(db_);
  const auto e_id = effectId(value.effect_name_);
  Q_ASSERT(e_id >= 0);

  q.prepare("INSERT INTO presets (name, e_id) "
            "VALUES (?, ?)");
  q.addBindValue(value.preset_name_);
  q.addBindValue(e_id);
  if (!q.exec()) {
    qWarning() << q.lastError().text();
    qDebug() << q.executedQuery();
    return false;
  }

  const auto p_id = presetId(value.effect_name_, value.preset_name_);
  Q_ASSERT(p_id >= 0);

  for (const auto& [row, params] : value.parameters_.toStdMap()) {
    const auto row_id = effectRowId(row, e_id);
    Q_ASSERT(row_id >= 0);
    for (const auto& param : params) {
      Q_ASSERT(addNewParameterPreset(p_id, row_id, param.first, param.second));
    }
  }

  return true;
}


QStringList Database::getPresets(const QString& effect_name)
{
  QSqlQuery q(db_);
  q.prepare("SELECT presets.name FROM presets "
            "JOIN effects ON presets.e_id = effects.id "
            "WHERE effects.name=?");
  q.addBindValue(effect_name);
  if (!q.exec()) {
    qWarning() << q.lastError().text();
    qDebug() << q.executedQuery();
    return {};
  }
  QStringList names;
  while (q.next()) {
    names << q.value(0).toString();
  }

  return names;
}


chestnut::EffectParametersType Database::getPresetParameters(QString effect_name, QString preset_name)
{
  QSqlQuery q(db_);
  q.prepare("SELECT preset_parameter.name, preset_parameter.value, preset_parameter.value_type, effect_rows.name "
            "FROM preset_parameter "
            "JOIN presets ON preset_parameter.p_id = presets.id "
            "JOIN effects ON presets.e_id = effects.id "
            "JOIN effect_rows ON preset_parameter.er_id = effect_rows.id "
            "WHERE presets.name=? AND effects.name = ? "
            "ORDER BY effect_rows.id");
  q.addBindValue(std::move(preset_name));
  q.addBindValue(std::move(effect_name));

  if (!q.exec()) {
    qWarning() << q.lastError().text();
    qDebug() << q.executedQuery();
    return {};
  }

  ParamsType params;
  EffectParametersType preset_params;
  QString row_name;

  while (q.next()) {
    const auto new_row(q.value(3).toString() != row_name);
    const auto param_name(q.value(0).toString());
    const auto param_value(q.value(1).toString());
    const auto param_type(static_cast<QVariant::Type>(q.value(2).toInt()));
    QVariant value(param_type);
    value.setValue(param_value);
    if (new_row) {
      params.clear();
      row_name = q.value(3).toString();
    }
    preset_params[row_name].append(QPair<QString, QVariant>(param_name, value));
  }

  return preset_params;
}




void Database::deletePreset(const QString& effect_name, const QString& preset_name)
{
  qInfo() << "Deleting preset, effect:" << effect_name << ", preset:" << preset_name;
  const auto preset_id = presetId(effect_name, preset_name);
  deletePresetParameters(preset_id);
  const auto effect_id = effectId(effect_name);
  QSqlQuery q(db_);
  q.prepare("DELETE FROM presets "
            "WHERE e_id=? AND name=?");
  q.addBindValue(effect_id);
  q.addBindValue(preset_name);
  if (!q.exec()) {
    qWarning() << q.lastError().text();
    qDebug() << q.executedQuery();
  }
}

const QSqlResult* Database::query(const QString& statement)
{
  QSqlQuery q(db_);
  if (!q.exec(statement)) {
    qWarning() << q.lastError().text();
    qDebug() << q.executedQuery();
    return {};
  }
  return q.result();
}


int Database::effectId(const QString& name, const bool recurse)
{
  QSqlQuery q(db_);
  q.prepare("SELECT id FROM effects WHERE name=?");
  q.addBindValue(name);
  q.exec();
  if (q.first()) {
    return q.value(0).toInt();
  } else if (recurse) {
    q.prepare("INSERT INTO effects (name) VALUES (?)");
    q.addBindValue(name);
    q.exec();
    return effectId(name, false);
  }
  return -1;
}

int Database::effectRowId(const QString& name, const int effect_id, const bool recurse)
{
  QSqlQuery q(db_);
  q.prepare("SELECT id FROM effect_rows WHERE name=? AND e_id=?");
  q.addBindValue(name);
  q.addBindValue(effect_id);
  q.exec();
  if (q.first()) {
    return q.value(0).toInt();
  } else if (recurse) {
    q.prepare("INSERT INTO effect_rows (name, e_id) VALUES (?, ?)");
    q.addBindValue(name);
    q.addBindValue(effect_id);
    q.exec();
    return effectRowId(name, effect_id, false);
  }
  return -1;
}

int Database::presetId(QString effect_name, QString preset_name)
{
  QSqlQuery q(db_);
  q.prepare("SELECT presets.id FROM presets "
            "JOIN effects ON presets.e_id = effects.id "
            "WHERE presets.name=? AND effects.name=?");
  q.addBindValue(std::move(preset_name));
  q.addBindValue(std::move(effect_name));
  if (!q.exec()) {
    qWarning() << q.lastError().text();
    qDebug() << q.executedQuery();
    return -1;
  }
  if (q.first()) {
    return q.value(0).toInt();
  }
  return -1;
}


bool Database::addNewParameterPreset(const int preset_id, const int row_id, const QString& name, const QVariant& value)
{
  QSqlQuery q(db_);
  q.prepare("INSERT INTO preset_parameter"
            "(name, value, value_type, p_id, er_id)"
            "VALUES (?, ?, ?, ?, ?)");
  q.addBindValue(name);
  q.addBindValue(value.toString());
  q.addBindValue(static_cast<int>(value.type()));
  q.addBindValue(preset_id);
  q.addBindValue(row_id);
  if (!q.exec()) {
    qWarning() << q.lastError().text();
    qDebug() << q.executedQuery();
    return false;
  }
  return true;
}


void Database::deletePresetParameters(const int preset_id)
{
  QSqlQuery q(db_);
  q.prepare("DELETE FROM preset_parameter "
            "WHERE p_id=?");
  q.addBindValue(preset_id);
  if (!q.exec()) {
    qWarning() << q.lastError().text();
    qDebug() << q.executedQuery();
  }
}

void Database::setupEffectsTable()
{
  query("CREATE TABLE IF NOT EXISTS effects ("
        "id INTEGER,"
        "name VARCHAR(256),"
        "PRIMARY KEY (id) )");

  query("CREATE TABLE IF NOT EXISTS presets ("
        "id INTEGER,"
        "name VARCHAR(256),"
        "e_id INTEGER,"
        "PRIMARY KEY (id),"
        "FOREIGN KEY (e_id) REFERENCES effects(id) )");

  query("CREATE TABLE IF NOT EXISTS effect_rows ("
        "id INTEGER,"
        "name VARCHAR(256),"
        "e_id INTEGER,"
        "PRIMARY KEY (id),"
        "FOREIGN KEY (e_id) REFERENCES effects(id))");

  query("CREATE TABLE IF NOT EXISTS preset_parameter ("
        "id INTEGER,"
        "name VARCHAR(256),"
        "value VARCHAR(256),"
        "value_type INTEGER,"
        "p_id INTEGER,"
        "er_id INTEGER,"
        "PRIMARY KEY (id),"
        "FOREIGN KEY (p_id) REFERENCES presets(id),"
        "FOREIGN KEY (er_id) REFERENCES effect_rows(id))");
}
