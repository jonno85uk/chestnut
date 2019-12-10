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

#ifndef DATABASE_H
#define DATABASE_H

#include <memory>
#include <QSqlDatabase>
#include <QVariant>
#include <QSqlResult>

namespace chestnut
{
  //    field->name, field->value
  using ParamType = QPair<QString, QVariant>;
  //    effectrow->fields
  using ParamsType = QVector<ParamType>;
  //    effectrow->name, effectrow->fields
  using EffectParametersType = QMap<QString, ParamsType>;

  struct EffectPreset
  {
    QString effect_name_;
    QString preset_name_;
    EffectParametersType parameters_;
  };

  class Database
  {
    public:
      explicit Database(QString file_path);

      static std::shared_ptr<Database> instance(const QString& file_path="");
      /**
       * @brief         Add a new Effect preset into the database
       * @param value   Structure containing all preset data
       * @return        true==preset added successfully
       */
      bool addNewPreset(const EffectPreset& value);
      /**
       * @brief             Obtain all the preset names of an Effect
       * @param effect_name
       * @return            List of all the names or empty
       */
      QStringList getPresets(const QString& effect_name);
      /**
       * @brief               Obtain all the Effect parameters of a preset
       * @param effect_name
       * @param preset_name
       * @return              Structure in which to setup an Effect
       */
      EffectParametersType getPresetParameters(QString effect_name, QString preset_name);
      /**
       * @brief               Remove Effect preset entry if it exists
       * @param effect_name
       * @param preset_name
       */
      void deletePreset(const QString& effect_name, const QString& preset_name);

    private:
      QSqlDatabase db_;
      inline static std::shared_ptr<Database> instance_{nullptr};

      void setupEffectsTable();
      const QSqlResult* query(const QString& statement);
      int effectId(const QString& name, const bool recurse=true);
      int effectRowId(const QString& name, const int effect_id, const bool recurse=true);
      int presetId(QString effect_name, QString preset_name);
      bool addNewParameterPreset(const int preset_id, const int row_id, const QString& name, const QVariant& value);
      void deletePresetParameters(const int preset_id);
  };
}

#endif // DATABASE_H
