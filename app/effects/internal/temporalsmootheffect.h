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

#ifndef TEMPORALSMOOTHEFFECT_H
#define TEMPORALSMOOTHEFFECT_H

#include <mediahandling/gsl-lite.hpp>
#include "project/effect.h"

using VectorSpanBytes = QVector<gsl::span<uint8_t>>;

class TemporalSmoothEffect : public Effect
{
  public:
    TemporalSmoothEffect(ClipPtr c, const EffectMeta& em);
    ~TemporalSmoothEffect() override;

    TemporalSmoothEffect(const TemporalSmoothEffect&) = delete;
    TemporalSmoothEffect(const TemporalSmoothEffect&&) = delete;
    TemporalSmoothEffect& operator=(const TemporalSmoothEffect&) = delete;
    TemporalSmoothEffect& operator=(const TemporalSmoothEffect&&) = delete;

    virtual void process_image(double timecode, gsl::span<uint8_t>& data) override;
    virtual void setupUi() override;
  private:
    EffectField* frame_length_ {nullptr};
    EffectField* blend_mode_ {nullptr};
    VectorSpanBytes frames_;
    int thread_count_{1};
};

#endif // TEMPORALSMOOTHEFFECT_H
