#include "fillleftrighteffect.h"

constexpr int FILL_TYPE_LEFT = 0;
constexpr int FILL_TYPE_RIGHT = 1;

FillLeftRightEffect::FillLeftRightEffect(ClipPtr c, const EffectMeta& em)
  : Effect(c, em)
{

}

FillLeftRightEffect::~FillLeftRightEffect()
{
  fill_type = nullptr;
}

void FillLeftRightEffect::process_audio(const double timecode_start,
                                        const double timecode_end,
                                        quint8* samples,
                                        const int nb_bytes,
                                        const int)
{
  double interval = (timecode_end-timecode_start)/nb_bytes;
  for (int i=0;i<nb_bytes;i+=4) {
    if (fill_type->get_combo_data(timecode_start+(interval*i)) == FILL_TYPE_LEFT) {
      samples[i+1] = samples[i+3];
      samples[i] = samples[i+2];
    } else {
      samples[i+3] = samples[i+1];
      samples[i+2] = samples[i];
    }
  }
}

void FillLeftRightEffect::setupUi()
{
  if (ui_setup) {
    return;
  }
  Effect::setupUi();

  EffectRowPtr type_row = add_row(tr("Type"));
  fill_type = type_row->add_field(EffectFieldType::COMBO, "type");
  Q_ASSERT(fill_type);
  fill_type->add_combo_item(tr("Fill Left with Right"), FILL_TYPE_LEFT);
  fill_type->add_combo_item(tr("Fill Right with Left"), FILL_TYPE_RIGHT);
}
