#ifndef FILLLEFTRIGHTEFFECT_H
#define FILLLEFTRIGHTEFFECT_H

#include "project/effect.h"

class FillLeftRightEffect : public Effect {
public:
    FillLeftRightEffect(ClipPtr c, const EffectMeta& em);

    FillLeftRightEffect(const FillLeftRightEffect&) = delete;
    FillLeftRightEffect operator=(const FillLeftRightEffect&) = delete;

  void process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes, int channel_count) override;

  virtual void setupUi() override;
private:
    EffectField* fill_type {nullptr};
};

#endif // FILLLEFTRIGHTEFFECT_H
