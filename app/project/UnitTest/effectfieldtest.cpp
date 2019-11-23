#include "effectfieldtest.h"

#include <QtTest>

#include "project/effectfield.h"
#include "ui/colorbutton.h"

EffectFieldTest::EffectFieldTest(QObject *parent) : QObject(parent)
{

}


void EffectFieldTest::testCasePrevKey()
{
  EffectField fld(nullptr);
  QVERIFY(fld.previousKey(1).type == KeyframeType::UNKNOWN);
  EffectKeyframe key;
  key.time = 0;
  key.type = KeyframeType::BEZIER;
  fld.keyframes.append(key);
  QVERIFY(fld.previousKey(1).type == KeyframeType::BEZIER);
  QVERIFY(fld.previousKey(0).type == KeyframeType::UNKNOWN);
}

void EffectFieldTest::testCaseNextKey()
{
  EffectField fld(nullptr);
  QVERIFY(fld.nextKey(1).type == KeyframeType::UNKNOWN);
  EffectKeyframe key;
  key.time = 2;
  key.type = KeyframeType::BEZIER;
  fld.keyframes.append(key);
  QVERIFY(fld.nextKey(1).type == KeyframeType::BEZIER);
  QVERIFY(fld.nextKey(2).type == KeyframeType::UNKNOWN);
}


void EffectFieldTest::testCaseSetValueColor()
{
  EffectField fld(nullptr);
  fld.type_ = EffectFieldType::COLOR;
  fld.ui_element = new ColorButton;
  auto val = "#FF000000";
  fld.setValue(val);
  QVERIFY(fld.get_color_value(0).toRgb() == QColor(val));
}
