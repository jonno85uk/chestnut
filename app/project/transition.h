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
#ifndef TRANSITION_H
#define TRANSITION_H

#include "effect.h"
#include "project/clip.h"

#include <memory>

constexpr int TA_NO_TRANSITION = 0;
constexpr int TA_OPENING_TRANSITION = 1;
constexpr int TA_CLOSING_TRANSITION = 2;

constexpr int TRANSITION_INTERNAL_CROSSDISSOLVE = 0;
constexpr int TRANSITION_INTERNAL_LINEARFADE = 1;
constexpr int TRANSITION_INTERNAL_EXPONENTIALFADE	= 2;
constexpr int TRANSITION_INTERNAL_LOGARITHMICFADE = 3;
constexpr int TRANSITION_INTERNAL_CUBE = 4;
constexpr int TRANSITION_INTERNAL_DIPTOCOLOR = 5;
constexpr int TRANSITION_INTERNAL_COUNT = 6;


using TransitionPtr = std::shared_ptr<Transition>;
using TransitionWPtr = std::weak_ptr<Transition>;



using TransitionPtr = std::shared_ptr<Transition>;
using TransitionWPtr = std::weak_ptr<Transition>;

struct InvalidTransitionLengthException : public std::exception
{
    virtual const char* what() const noexcept
    {
      return "Invalid length used";
    }
};


int create_transition(const ClipPtr& c, const ClipPtr& s, const EffectMeta& em, long length = -1);
TransitionPtr get_transition_from_meta(ClipPtr c, ClipPtr s, const EffectMeta& em, const bool setup = true);

class Transition : public Effect {
    Q_OBJECT
  public:
    Transition(const ClipPtr& c, const ClipPtr& s, const EffectMeta& em);

    void setLength(const long value);
    long get_true_length() const;
    long get_length() const;

    void setSecondaryLoadId(const int load_id);

    virtual void setupUi() override;

    ClipPtr secondaryClip();

  private slots:
    void set_length_from_slider();
  private:
    long length {0}; // used only for transitions
    EffectField* length_field{nullptr};
    ClipWPtr secondary_clip; // Presumably the clip (of the same track) which the transition is spread over
    int secondary_load_id {0};

    /*
     * Ensure that the timeline is updated on transition modification
     */
    void field_changed() override;
};


using TransitionPtr = std::shared_ptr<Transition>;

#endif // TRANSITION_H
