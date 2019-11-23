#ifndef FILLLEFTRIGHTEFFECT_H
#define FILLLEFTRIGHTEFFECT_H

#include "project/effect.h"

class FillLeftRightEffect : public Effect {
  public:
    FillLeftRightEffect(ClipPtr c, const EffectMeta& em);
    ~FillLeftRightEffect() override;
    FillLeftRightEffect(const FillLeftRightEffect&) = delete;
    FillLeftRightEffect operator=(const FillLeftRightEffect&) = delete;

    void process_audio(const double timecode_start,
                       const double timecode_end,
                       quint8* samples,
                       const int nb_bytes,
                       const int channel_count) override;

    virtual void setupUi() override;
  private:
    EffectField* fill_type {nullptr};
};

#endif // FILLLEFTRIGHTEFFECT_H
