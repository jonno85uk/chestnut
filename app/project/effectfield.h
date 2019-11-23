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
#ifndef EFFECTFIELD_H
#define EFFECTFIELD_H


#include <QObject>
#include <QVariant>
#include <QVector>
#include <memory>

#include "ui/labelslider.h"
#include "project/keyframe.h"
#include "project/ixmlstreamer.h"

class EffectRow;
class ComboAction;

enum class EffectFieldType {
  DOUBLE = 0,
  COLOR = 1,
  STRING = 2,
  BOOL = 3,
  COMBO = 4,
  FONT = 5,
  FILE_T = 6,
  UNKNOWN
};


QString fieldTypeValueToString(const EffectFieldType type, const QVariant& value);

class EffectField : public QObject, public project::IXMLStreamer  {
  Q_OBJECT
public:
  explicit EffectField(EffectRow* parent);
  EffectField(EffectRow* parent, const EffectFieldType t, const QString& i);

  EffectField() = delete;

  template<typename T>
  void setDefaultValue(const T val) {
    default_data_.setValue(val);
  }

  QVariant get_previous_data();
  QVariant get_current_data() const;
  double frameToTimecode(const long frame);
  long timecodeToFrame(double timecode);
  void set_current_data(const QVariant&);
  void get_keyframe_data(double timecode, int& before, int& after, double& d);
  QVariant validate_keyframe_data(double timecode, bool async = false);

  double get_double_value(double timecode, bool async = false);
  void set_double_value(double v);
  void set_double_default_value(double v);
  void set_double_minimum_value(double v);
  void set_double_maximum_value(double v);
  void set_double_step_value(const double v);

  QString get_string_value(double timecode, bool async = false);
  void set_string_value(const QString &s);

  void add_combo_item(const QString& name, const QVariant &data);
  int get_combo_index(double timecode, bool async = false);
  QVariant get_combo_data(double timecode);
  QString get_combo_string(double timecode);
  void set_combo_index(int index);
  void set_combo_string(const QString& s);

  bool get_bool_value(double timecode, bool async = false);
  void set_bool_value(bool b);

  QString get_font_name(double timecode, bool async = false);
  void set_font_name(const QString& s);

  QColor get_color_value(double timecode, bool async = false);
  void set_color_value(const QColor& color);

  QString get_filename(double timecode, bool async = false);
  void set_filename(const QString& s);

  QWidget* get_ui_element();
  void set_enabled(bool e);

  void make_key_from_change(ComboAction* ca);
  const QVariant& getDefaultData() const;

  bool setValue(const QVariant& value);

  QVariant value() const;

  void setPrefix(QString value);
  void setSuffix(QString value);


  virtual bool load(QXmlStreamReader& stream) override;
  virtual bool save(QXmlStreamWriter& stream) const override;

  QString name() const {return id_;}

  /**
   * @brief           Obtain a keyframe before a position
   * @param position  Position in a clip
   * @return          keyframe, check type for validity
   */
  EffectKeyframe previousKey(const long position) const;
  /**
   * @brief           Obtain a keyframe after a position
   * @param position  Position in a clip
   * @return          keyframe, check type for validity
   */
  EffectKeyframe nextKey(const long position) const;


  EffectRow* parent_row;
  EffectFieldType type_{EffectFieldType::UNKNOWN};
  QVector<EffectKeyframe> keyframes;
  QWidget* ui_element = nullptr;
public slots:
  void ui_element_change();
private:
  QString id_;
  QVariant default_data_;

  bool hasKeyframes();
signals:
  void changed();
  void toggled(bool);
  void clicked();

};

#endif // EFFECTFIELD_H
