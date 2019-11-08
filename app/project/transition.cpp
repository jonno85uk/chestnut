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
#include "transition.h"

#include <QMessageBox>
#include <QCoreApplication>

#include "ui/mainwindow.h"
#include "clip.h"
#include "sequence.h"
#include "debug.h"
#include "io/clipboard.h"
#include "ui/labelslider.h"
#include "panels/panelmanager.h"

#include "effects/internal/crossdissolvetransition.h"
#include "effects/internal/linearfadetransition.h"
#include "effects/internal/exponentialfadetransition.h"
#include "effects/internal/logarithmicfadetransition.h"
#include "effects/internal/cubetransition.h"
#include "effects/internal/diptocolourtransition.h"


constexpr long DEFAULT_TRANSITION_LENGTH = 30;
constexpr long MINIMUM_TRANSITION_LENGTH = 1;


Transition::Transition(const ClipPtr& c, const ClipPtr& s, const EffectMeta& em) :
  Effect(c, em),
  length(DEFAULT_TRANSITION_LENGTH),
  secondary_clip(s)
{
}

void Transition::setLength(const long value)
{
  Q_ASSERT(length_field != nullptr);
  Q_ASSERT(Effect::parent_clip != nullptr);

  if (value <= 0) {
    throw InvalidTransitionLengthException();
  }
  length = value;
  length_field->set_double_value(value);
}

long Transition::get_true_length() const {
  return length;
}

long Transition::get_length() const {
  if (!secondary_clip.expired()) {
    return length * 2;
  }
  return length;
}


void Transition::setSecondaryLoadId(const int load_id)
{
  secondary_load_id = load_id;
}

void Transition::setupUi()
{
  if (ui_setup) {
    return;
  }
  Effect::setupUi();
  length_field = add_row(tr("Length:"), false)->add_field(EffectFieldType::DOUBLE, "length");
  connect(length_field, &EffectField::changed, this, &Transition::set_length_from_slider);
  length_field->set_double_default_value(DEFAULT_TRANSITION_LENGTH);
  length_field->set_double_minimum_value(MINIMUM_TRANSITION_LENGTH);

  auto length_ui_ele = dynamic_cast<LabelSlider*>(length_field->ui_element);
  length_ui_ele->set_display_type(SliderType::FRAMENUMBER);
  length_ui_ele->set_frame_rate(parent_clip->sequence == nullptr
                                ? parent_clip->timeline_info.cached_fr : parent_clip->sequence->frameRate());
}


ClipPtr Transition::secondaryClip()
{
  if (auto s_clip = secondary_clip.lock()) {
    return s_clip;
  }
  if ( (Effect::parent_clip != nullptr)
       && (Effect::parent_clip->sequence != nullptr)
       && (secondary_load_id >= 0) ) {
    // use the id at file load
    return Effect::parent_clip->sequence->clip(secondary_load_id);
  }
  return nullptr;
}

void Transition::set_length_from_slider()
{
  setLength(qRound(length_field->get_double_value(0)));
  panels::PanelManager::refreshPanels(false);
}

void Transition::field_changed()
{
  Effect::field_changed();
  panels::PanelManager::timeLine().repaint_timeline();
}


TransitionPtr get_transition_from_meta(ClipPtr c, ClipPtr s, const EffectMeta& em, const bool setup)
{
  TransitionPtr trans;
  if (!em.filename.isEmpty()) {
    // load effect from file
    trans = std::make_shared<Transition>(c, s, em);
  } else if ( (em.internal >= 0) && (em.internal < TRANSITION_INTERNAL_COUNT) ) {
    // must be an internal effect
    switch (em.internal) {
      case TRANSITION_INTERNAL_CROSSDISSOLVE:
        trans = std::make_shared<CrossDissolveTransition>(c, s, em);
        break;
      case TRANSITION_INTERNAL_DIPTOCOLOR:
        trans = std::make_shared<fx::DipToColourTransition>(c, s, em);
        break;
      case TRANSITION_INTERNAL_LINEARFADE:
        trans = std::make_shared<LinearFadeTransition>(c, s, em);
        break;
      case TRANSITION_INTERNAL_EXPONENTIALFADE:
        trans = std::make_shared<ExponentialFadeTransition>(c, s, em);
        break;
      case TRANSITION_INTERNAL_LOGARITHMICFADE:
        trans = std::make_shared<LogarithmicFadeTransition>(c, s, em);
        break;
      case TRANSITION_INTERNAL_CUBE:
        trans = std::make_shared<CubeTransition>(c, s, em);
        break;
      default:
        qWarning() << "Unhandled transition" << em.internal;
        break;
    }
  } else {
    qCritical() << "Invalid transition data";
    QMessageBox::critical(&MainWindow::instance(),
                          QCoreApplication::translate("transition", "Invalid transition"),
                          QCoreApplication::translate("transition", "No candidate for transition '%1'. This transition "
                                                                    "may be corrupt. Try reinstalling it or Chestnut.").arg(em.name)
                          );
  }
  if ((trans != nullptr) && setup) {
    trans->setupUi();
  }
  return trans;
}

int create_transition(const ClipPtr& c, const ClipPtr& s, const EffectMeta& em, long length) {
  auto t = get_transition_from_meta(c, s, em);
  if (t != nullptr) {
    if (length >= 0) {
      t->setLength(length);
    }
    QVector<TransitionPtr>& transition_list = (c->sequence == nullptr) ? e_clipboard_transitions : c->sequence->transitions_;
    transition_list.append(t);
    return transition_list.size() - 1;
  }
  return -1;
}
