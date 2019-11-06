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

#include "temporalsmootheffect.h"
#include <omp.h>
#include <cmath>
#include <mediahandling/gsl-lite.hpp>

constexpr size_t MEDIAN_LENGTH_LIMIT = 10;

TemporalSmoothEffect::TemporalSmoothEffect(ClipPtr c, const EffectMeta& em) : Effect(std::move(c), em)
{  
  setCapability(Capability::IMAGE);
#ifdef _OPENMP
  thread_count_ = static_cast<int>(round(omp_get_max_threads() * 0.9));
#endif

}

TemporalSmoothEffect::~TemporalSmoothEffect()
{
  while (!frames_.empty()) {
    delete[] frames_.front().data();
    frames_.pop_front();
  }
}

uint8_t average(const int ix, const VectorSpanBytes& data)
{
  uint64_t val = 0;
  for (const auto& d: data) {
    if (val < UINT64_MAX) {
      val += d[ix];
    }
  }
  // average pixel in all frames
  return static_cast<uint8_t>(val / data.size());
}

uint8_t median(const int ix, const VectorSpanBytes& data)
{
  const size_t sz = static_cast<size_t>(data.size());
  size_t lim;
  if (sz < MEDIAN_LENGTH_LIMIT) {
    lim = sz;
  } else {
    lim = MEDIAN_LENGTH_LIMIT;
  }

  std::vector<uint8_t> vals(lim);

  for (size_t i = 0; i < lim; ++i) {
    vals[i] = data[static_cast<int>(i)][ix];
  }

  std::sort(vals.begin(), vals.end());
  uint8_t val;
  if ( (sz % 2) == 0) {
    // sum pixels "around" the middle and avg of the 2
    uint16_t tmp = vals[sz >> 1];
    tmp += vals[static_cast<size_t>(sz + 0.5)];
    val = static_cast<uint8_t>(tmp >> 1);
  } else {
    // use middle val
    val = vals[sz >> 1];
  }
  return val;
}

uint8_t maxVal(const int ix, const VectorSpanBytes& data)
{
  uint8_t val = 0;
  for (const auto& d : data) {
    if (d[ix] > val) {
      val = d[ix];
    }
  }

  return val;
}

uint8_t minVal(const int ix, const VectorSpanBytes& data)
{
  uint8_t val = 255;
  for (const auto& d : data) {
    if (d[ix] < val) {
      val = d[ix];
    }
  }

  return val;
}

void TemporalSmoothEffect::process_image(double timecode, gsl::span<uint8_t>& data)
{
  if (frame_length_ == nullptr || blend_mode_ == nullptr) {
    qCritical() << "Ui Elements null";
    return;
  }
  const auto length = lround(frame_length_->get_double_value(timecode));

  while (frames_.size() > length - 1) { // -1 as new frame is about to be added
    delete[] frames_.front().data();
    frames_.pop_front();
  }
  // data needs to be copied as is deleted outside of class
  const auto tmp = new uint8_t[static_cast<uint32_t>(data.size())];
  memcpy(tmp, data.data(), static_cast<size_t>(data.size()));
  frames_.push_back(gsl::span<uint8_t>(tmp, data.size()));

  if (!is_enabled()) {
    return;
  }


  const auto mode_index = blend_mode_->get_combo_index(timecode);

  uint8_t (*func) (const int ix, const VectorSpanBytes& data) = nullptr;

  switch (mode_index) {
    case 0:
      func = average;
      break;
    case 1:
      func = median;
      break;
    case 2:
      func = maxVal;
      break;
    case 3:
      func = minVal;
      break;
    default:
      break;
  }

#ifdef _OPENMP
  omp_set_num_threads(thread_count_);
#endif
  if (func != nullptr) {
    #pragma omp parallel for
    for (auto i=0L; i < data.size(); ++i) {
      data[i] = func(i, frames_);
    }
  }
}

void TemporalSmoothEffect::setupUi()
{
  if (ui_setup) {
    return;
  }
  Effect::setupUi();

  frame_length_ = add_row(tr("Frame Length"))->add_field(EffectFieldType::DOUBLE, "length");
  frame_length_->set_double_minimum_value(1.0);
  frame_length_->set_double_maximum_value(10.0);
  frame_length_->set_double_default_value(3.0);

  blend_mode_ = add_row(tr("Blend Mode"))->add_field(EffectFieldType::COMBO, "mode");
  blend_mode_->add_combo_item("Average", 0);
  blend_mode_->add_combo_item("Median", 0);
  blend_mode_->add_combo_item("Max", 0);
  blend_mode_->add_combo_item("Min", 0);
  blend_mode_->setDefaultValue(0);
}

