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
#include "effect.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QMessageBox>
#include <QOpenGLContext>
#include <QDir>
#include <QPainter>
#include <QtMath>
#include <QMenu>
#include <QApplication>
#include <thread>
#include <utility>

#include "panels/panelmanager.h"
#include "ui/viewerwidget.h"
#include "ui/collapsiblewidget.h"
#include "project/undo.h"
#include "project/sequence.h"
#include "project/clip.h"
#include "ui/checkboxex.h"
#include "debug.h"
#include "io/path.h"
#include "ui/mainwindow.h"
#include "io/math.h"
#include "transition.h"

#include "effects/internal/transformeffect.h"
#include "effects/internal/texteffect.h"
#include "effects/internal/timecodeeffect.h"
#include "effects/internal/solideffect.h"
#include "effects/internal/audionoiseeffect.h"
#include "effects/internal/toneeffect.h"
#include "effects/internal/volumeeffect.h"
#include "effects/internal/paneffect.h"
#include "effects/internal/shakeeffect.h"
#include "effects/internal/cornerpineffect.h"
#include "effects/internal/fillleftrighteffect.h"
#include "effects/internal/temporalsmootheffect.h"


constexpr auto EFFECT_EXT = "*.xml";
constexpr auto EFFECT_PATH_ENV = "CHESTNUT_EFFECTS_PATH";
constexpr auto SYSTEM_EFFECT_PATH = "../share/chestnut/effects";
constexpr auto LOCAL_EFFECT_PATH = "effects";

bool shaders_are_enabled = true;


using panels::PanelManager;

EffectPtr create_effect(ClipPtr c, const EffectMeta& em, const bool setup)
{
  EffectPtr eff;
  if (!em.filename.isEmpty()) {
    // load effect from file
    eff = std::make_shared<Effect>(c, em);
  } else if (em.internal >= 0 && em.internal < EFFECT_INTERNAL_COUNT) {
    // must be an internal effect
    switch (em.internal) {
      case EFFECT_INTERNAL_TRANSFORM:
        eff = std::make_shared<TransformEffect>(c, em);
        break;
      case EFFECT_INTERNAL_TEXT:
        eff = std::make_shared<TextEffect>(c, em);
        break;
      case EFFECT_INTERNAL_TIMECODE:
        eff = std::make_shared<TimecodeEffect>(c, em);
        break;
      case EFFECT_INTERNAL_SOLID:
        eff = std::make_shared<SolidEffect>(c, em);
        break;
      case EFFECT_INTERNAL_NOISE:
        eff = std::make_shared<AudioNoiseEffect>(c, em);
        break;
      case EFFECT_INTERNAL_VOLUME:
        eff = std::make_shared<VolumeEffect>(c, em);
        break;
      case EFFECT_INTERNAL_PAN:
        eff = std::make_shared<PanEffect>(c, em);
        break;
      case EFFECT_INTERNAL_TONE:
        eff = std::make_shared<ToneEffect>(c, em);
        break;
      case EFFECT_INTERNAL_SHAKE:
        eff = std::make_shared<ShakeEffect>(c, em);
        break;
      case EFFECT_INTERNAL_CORNERPIN:
        eff = std::make_shared<CornerPinEffect>(c, em);
        break;
      case EFFECT_INTERNAL_FILLLEFTRIGHT:
        eff = std::make_shared<FillLeftRightEffect>(c, em);
        break;
      case EFFECT_INTERNAL_TEMPORAL:
        eff = std::make_shared<TemporalSmoothEffect>(c, em);
        break;
      default:
        qWarning() << "Unknown Effect Type" << em.internal;
        break;
    }//switch
  } else {
    qCritical() << "Invalid effect data";
    QMessageBox::critical(&MainWindow::instance(),
                          QCoreApplication::translate("Effect", "Invalid effect"),
                          QCoreApplication::translate("Effect", "No candidate for effect '%1'. This effect may be corrupt. "
                                                                "Try reinstalling it for Chestnut.").arg(em.name));
  }
  if ( (eff != nullptr) && setup) {
    eff->setupUi();
  }
  return eff;
}

EffectMeta get_internal_meta(const int internal_id, const int type)
{
  EffectMeta meta;
  for (const auto& eff : Effect::getRegisteredMetas()) {
    if ( (eff.internal == internal_id) && (eff.type == type) ) {
      meta = eff;
    }
  }
  return meta;
}

QString get_system_effects_path()
{
  return qgetenv(EFFECT_PATH_ENV);
}

QList<QString> get_effects_paths()
{
  QList<QString> effects_paths;
  // Ensure the ordering remains, local, system, env
  effects_paths.append(get_app_dir() + "/" + LOCAL_EFFECT_PATH);
  effects_paths.append(get_app_dir() + "/" + SYSTEM_EFFECT_PATH);
  QString env_path(get_system_effects_path());
  if (!env_path.isEmpty()) {
    effects_paths.append(env_path);
  }
  return effects_paths;
}

QString get_effect_path()
{
  for (auto& path : get_effects_paths()) {
    if (QDir(path).exists()) {
      return path;
    }
  }
  return "";
}


//FIXME: do something cleaner
void load_internal_effects()
{
  if (!shaders_are_enabled) {
    qWarning() << "Shaders are disabled, some effects may be nonfunctional";
  }

  EffectMeta em;
  // Some internal effects have shader
  em.path = get_effect_path();

  // internal effects
  em.type = EFFECT_TYPE_EFFECT;
  em.subtype = EFFECT_TYPE_AUDIO;

  em.name = "Volume";
  em.internal = EFFECT_INTERNAL_VOLUME;
  Effect::registerMeta(em);

  em.name = "Pan";
  em.internal = EFFECT_INTERNAL_PAN;
  Effect::registerMeta(em);

  em.name = "Tone";
  em.internal = EFFECT_INTERNAL_TONE;
  Effect::registerMeta(em);

  em.name = "Audio Noise";
  em.internal = EFFECT_INTERNAL_NOISE;
  Effect::registerMeta(em);

  em.name = "Fill Left/Right";
  em.internal = EFFECT_INTERNAL_FILLLEFTRIGHT;
  Effect::registerMeta(em);

  em.subtype = EFFECT_TYPE_VIDEO;

  em.name = "Transform";
  em.category = "Distort";
  em.internal = EFFECT_INTERNAL_TRANSFORM;
  Effect::registerMeta(em);

  em.name = "Corner Pin";
  em.internal = EFFECT_INTERNAL_CORNERPIN;
  Effect::registerMeta(em);

  em.name = "Shake";
  em.internal = EFFECT_INTERNAL_SHAKE;
  Effect::registerMeta(em);

  em.name = "Temporal Smooth";
  em.internal = EFFECT_INTERNAL_TEMPORAL;
  Effect::registerMeta(em);

  em.name = "Text";
  em.category = "Render";
  em.internal = EFFECT_INTERNAL_TEXT;
  Effect::registerMeta(em);

  em.name = "Timecode";
  em.internal = EFFECT_INTERNAL_TIMECODE;
  Effect::registerMeta(em);

  em.name = "Solid";
  em.internal = EFFECT_INTERNAL_SOLID;
  Effect::registerMeta(em);

  // internal transitions
  em.type = EFFECT_TYPE_TRANSITION;
  em.category = "";

  em.name = "Cross Dissolve";
  em.internal = TRANSITION_INTERNAL_CROSSDISSOLVE;
  Effect::registerMeta(em);

  em.subtype = EFFECT_TYPE_AUDIO;

  em.name = "Linear Fade";
  em.internal = TRANSITION_INTERNAL_LINEARFADE;
  Effect::registerMeta(em);

  em.name = "Exponential Fade";
  em.internal = TRANSITION_INTERNAL_EXPONENTIALFADE;
  Effect::registerMeta(em);

  em.name = "Logarithmic Fade";
  em.internal = TRANSITION_INTERNAL_LOGARITHMICFADE;
  Effect::registerMeta(em);
}


bool addEffect(QXmlStreamReader& reader,
               const QString& file_name,
               const QString& effects_path)
{
  QString effect_name = "";
  QString effect_cat = "";
  const QXmlStreamAttributes attribs = reader.attributes();
  for (const auto& attrib : attribs) {
    if (attrib.name() == "name") {
      effect_name = attrib.value().toString();
    } else if (attrib.name() == "category") {
      effect_cat = attrib.value().toString();
    } else {
      qWarning() << "Unhandled attribute" << attrib.name();
    }
  }
  if (!effect_name.isEmpty()) {
    EffectMeta em;
    em.type = EFFECT_TYPE_EFFECT;
    em.subtype = EFFECT_TYPE_VIDEO;
    em.name = effect_name;
    em.category = effect_cat;
    em.filename = file_name;
    em.path = effects_path;
    Effect::registerMeta(em);
    return true;
  }

  return false;

}

//TODO:
void load_shader_effects()
{
  for (const auto& effects_path : get_effects_paths()) {
    const QDir effects_dir(effects_path);
    if (!effects_dir.exists()) {
      qWarning() << "Effects directory does not exist, " << effects_dir.absolutePath();
      continue;
    }
    const QList<QString> entries = effects_dir.entryList(QStringList(EFFECT_EXT), QDir::Files);
    for (const auto& entry : entries) {
      QFile file(effects_path + "/" + entry);
      if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "Could not open" << entry;
        return;
      }

      QXmlStreamReader reader(&file);
      while (!reader.atEnd()) {
        if (reader.name() == "effect") {
          if (!addEffect(reader, file.fileName(), effects_path)) {
            qCritical() << "Invalid effect found in" << entry;
          }
          break;
        }
        reader.readNext();
      }//while

      file.close();
    }//for
  }//for
}

void init_effects()
{
  // TODO: remove
  PanelManager::fxControls().effects_loaded.lock();
  auto lmb = []() {
    qInfo() << "Initializing effects...";
    load_internal_effects();
    load_shader_effects();
    qInfo() << "Finished initializing effects";
  };
  std::thread t(lmb);
  t.join();

  PanelManager::fxControls().effects_loaded.unlock();
}

EffectMeta::EffectMeta() : id (nextId++)
{

}


bool EffectMeta::operator==(const EffectMeta& rhs) const
{
  return rhs.id == id;
}

void Effect::registerMeta(const EffectMeta& meta)
{
  if (Effect::registered.contains(meta.name.toLower())) {
    qWarning() << "Effect Meta name already used:" << meta.name;
    return;
  }
  Effect::registered.insert(meta.name.toLower(), meta);
}


EffectMeta Effect::getRegisteredMeta(const QString& name)
{
  EffectMeta meta;
  if (Effect::registered.count(name.toLower()) > 0)  {
    return Effect::registered.value(name.toLower());
  }
  return meta;
}

const QMap<QString, EffectMeta>& Effect::getRegisteredMetas()
{
  return Effect::registered;
}

Effect::Effect(ClipPtr c)
  : SequenceItem(project::SequenceItemType::CLIP),
    parent_clip(std::move(c))
{
}

Effect::Effect(ClipPtr c, const EffectMeta& em) :
  Effect(c)
{
  meta = em;

}

bool Effect::hasCapability(const Capability flag) const
{
  return capabilities_.count(flag) > 0;
}

void Effect::setCapability(const Capability flag)
{
  capabilities_.insert(flag);
}


void Effect::clearCapability(const Capability flag)
{
  capabilities_.erase(flag);
}


void Effect::copy_field_keyframes(const std::shared_ptr<Effect>& e)
{
  for (int i=0; i<rows.size(); ++i) {
    EffectRowPtr row(rows.at(i));
    EffectRowPtr copy_row(e->rows.at(i));
    copy_row->setKeyframing(row->isKeyframing());
    for (int j=0;j<row->fieldCount();j++) {
      EffectField* field = row->field(j);
      EffectField* copy_field = copy_row->field(j);
      copy_field->keyframes = field->keyframes;
      copy_field->set_current_data(field->get_current_data());
    }
  }
}

EffectRowPtr Effect::add_row(const QString& name, bool savable, bool keyframable)
{
  EffectRowPtr row;
  if (ui_layout != nullptr) {
    row = std::make_shared<EffectRow>(this, savable, *ui_layout, name, rows.size(), keyframable);
    rows.append(row);
  }
  return row;
}

EffectRowPtr Effect::row(const int i)
{
  return rows.at(i);
}


EffectRowPtr Effect::row(const QString& name)
{
  for (auto row : rows) {
    if (row != nullptr && (row->name.toLower() == name.toLower()) ) {
      return row;
    }
  }
  return nullptr;
}


const QVector<EffectRowPtr> &Effect::getRows() const
{
  return rows;
}

int Effect::row_count()
{
  return rows.size();
}

/**
 * @brief Create a new EffectGizmo and add to internal list
 * @param type  Type for new Gizmo
 * @return the newly created EffectGizmo
 */
EffectGizmoPtr Effect::add_gizmo(const GizmoType type)
{
  EffectGizmoPtr gizmo = this->newEffectGizmo(type);
  gizmos.append(gizmo);
  return gizmo;
}

const EffectGizmoPtr& Effect::gizmo(const int index)
{
  return gizmos.at(index);
}

int Effect::gizmo_count() const
{
  return gizmos.size();
}

void Effect::refresh()
{
  //Implemented by its subclasses
}

void Effect::field_changed()
{
  PanelManager::sequenceViewer().viewer_widget->frame_update();
  panels::PanelManager::graphEditor().update_panel();
}

void Effect::show_context_menu(const QPoint& pos)
{
  if (meta.type == EFFECT_TYPE_EFFECT) {
    QMenu menu(&MainWindow::instance());

    int index = get_index_in_clip();

    if (index > 0) {
      QAction* move_up = menu.addAction(tr("Move &Up"));
      connect(move_up, SIGNAL(triggered(bool)), this, SLOT(move_up()));
    }

    if (index < parent_clip->effects.size() - 1) {
      QAction* move_down = menu.addAction(tr("Move &Down"));
      connect(move_down, SIGNAL(triggered(bool)), this, SLOT(move_down()));
    }

    menu.addSeparator();

    QAction* del_action = menu.addAction(tr("D&elete"));
    connect(del_action, SIGNAL(triggered(bool)), this, SLOT(delete_self()));

    menu.exec(container->title_bar->mapToGlobal(pos));
  }
}

void Effect::delete_self()
{
  auto command = new EffectDeleteCommand();
  command->clips.append(parent_clip);
  command->fx.append(get_index_in_clip());
  e_undo_stack.push(command);
  PanelManager::refreshPanels(true);
}

void Effect::move_up()
{
  //FIXME: not visually working
  auto command = new MoveEffectCommand();
  command->clip = parent_clip;
  command->from = get_index_in_clip();
  command->to = command->from - 1;
  e_undo_stack.push(command);
  PanelManager::refreshPanels(true);
}

void Effect::move_down()
{
  //FIXME: not visually working
  auto command = new MoveEffectCommand();
  command->clip = parent_clip;
  command->from = get_index_in_clip();
  command->to = command->from + 1;
  e_undo_stack.push(command);
  PanelManager::refreshPanels(true);
}

void Effect::reset()
{
  auto command = new ResetEffectCommand();
  for (const auto& row : rows) {
    for (auto i=0; i < row->fieldCount(); ++i) {
      auto field = row->field(i);
      if (field == nullptr) {
        continue;
      }

      command->fields_.emplace_back(std::make_tuple(field, field->get_current_data(), field->getDefaultData()));
    }
  }

  e_undo_stack.push(command);
  PanelManager::refreshPanels(true);
}

/**
 * @brief Create a new EffectGizmo (virtual+protected to aid in testing)
 * @param type  Type for EffectGizmo to be
 * @return EffectGizmo shared_ptr
 */
EffectGizmoPtr Effect::newEffectGizmo(const GizmoType type)
{
  return std::make_shared<EffectGizmo>(type);
}

int Effect::get_index_in_clip()
{
  if (parent_clip != nullptr) {
    for (int i=0;i<parent_clip->effects.size();i++) {
      if (parent_clip->effects.at(i).operator ->() == this) {
        return i;
      }
    }
  }
  return -1;
}

bool Effect::is_enabled() const
{
  if ( (container != nullptr) && (container->enabled_check != nullptr) ) {
    return container->enabled_check->isChecked();
  }
  return false;
}

void Effect::set_enabled(const bool b)
{
  if ( (container != nullptr) && (container->enabled_check != nullptr) ) {
    container->enabled_check->setChecked(b);
  }
}

QVariant load_data_from_string(const EffectFieldType type, const QString& string)
{
  switch (type) {
    case EffectFieldType::DOUBLE: return string.toDouble();
    case EffectFieldType::COLOR: return QColor(string);
    case EffectFieldType::BOOL: return string == "1";
    case EffectFieldType::COMBO: return string.toInt();
    case EffectFieldType::STRING:
    case EffectFieldType::FONT:
    case EffectFieldType::FILE_T:
      return string;
    default:
      qWarning() << "Effect type not handled" << static_cast<int>(type);
      break;
  }
  return QVariant();
}

QString save_data_to_string(const EffectFieldType type, const QVariant& data)
{
  switch (type) {
    case EffectFieldType::DOUBLE: return QString::number(data.toDouble());
    case EffectFieldType::COLOR: return data.value<QColor>().name();
    case EffectFieldType::BOOL: return QString::number(data.toBool());
    case EffectFieldType::COMBO: return QString::number(data.toInt());
    case EffectFieldType::STRING:
    case EffectFieldType::FONT:
    case EffectFieldType::FILE_T:
      return data.toString();
    default:
      break;
  }
  return QString();
}

bool Effect::load(QXmlStreamReader& stream)
{
  // may need to store the cfg and then use it on setupUI
  while (stream.readNextStartElement()) {
    auto name = stream.name().toString().toLower();
    if (name == "row") {
      EffectRowPtr eff_row;
      while (stream.readNextStartElement()) {
        name = stream.name().toString().toLower();
        if (name == "name") {
          auto row_name = stream.readElementText();
          eff_row = row(row_name);
          if (eff_row == nullptr) {
            qWarning() << "No row found for this effect. name=" << row_name;
            stream.skipCurrentElement();
          }
        } else if (name == "field" && eff_row != nullptr) {
          eff_row->load(stream);
        }
        else {
          qWarning() << "Unhandled Element" << name;
          stream.skipCurrentElement();
        }
      }
    } else {
      qCritical() << "Unhandled element" << name;
      return false;
    }
  }
  return true;
}

void Effect::custom_load(QXmlStreamReader& /*stream*/)
{
  // Does nothing
}

bool Effect::save(QXmlStreamWriter& stream) const
{
  stream.writeStartElement("effect");

  stream.writeAttribute("name", meta.name);
  stream.writeAttribute("enabled", is_enabled() ? "true" : "false");

  for (const auto& row : rows) {
    row->save(stream);
  }
  stream.writeEndElement(); //effect
  return true;
}

bool Effect::is_open() {
  return is_open_;
}

void Effect::validate_meta_path()
{
  if (!meta.path.isEmpty() || (glsl_.vert_.isEmpty() && glsl_.frag_.isEmpty())) return;
  QList<QString> effects_paths = get_effects_paths();
  const QString& test_fn = glsl_.vert_.isEmpty() ? glsl_.frag_ : glsl_.vert_;
  for (const auto& effects_path : effects_paths) {
    if (QFileInfo::exists(effects_path + "/" + test_fn)) {
      //FIXME:
//      for (int j=0;j<effects.size();j++) {
//        if (effects.at(j) == meta) {
//          effects[j].path = effects_path;
//          return;
//        }
//      }
      return;
    }
  }
}

void Effect::open()
{
  if (is_open_) {
    qWarning() << "Tried to open an effect that was already open";
    close();
  }
  if (shaders_are_enabled &&
      hasCapability(Capability::SHADER)
      && (QOpenGLContext::currentContext() != nullptr)) {
    glsl_.program_ = std::make_unique<QOpenGLShaderProgram>();
    validate_meta_path();
    bool glsl_compiled = true;
    if (!glsl_.vert_.isEmpty()) {
      if (glsl_.program_->addShaderFromSourceFile(QOpenGLShader::Vertex, meta.path + "/" + glsl_.vert_)) {
        qInfo() << "Vertex shader added successfully";
      } else {
        glsl_compiled = false;
        qWarning() << "Vertex shader could not be added";
      }
    }
    if (!glsl_.frag_.isEmpty()) {
      if (glsl_.program_->addShaderFromSourceFile(QOpenGLShader::Fragment, meta.path + "/" + glsl_.frag_)) {
        qInfo() << "Fragment shader added successfully";
      } else {
        glsl_compiled = false;
        qWarning() << "Fragment shader could not be added";
      }
    }
    if (glsl_compiled) {
      if (glsl_.program_->link()) {
        qInfo() << "Shader program linked successfully";
      } else {
        qWarning() << "Shader program failed to link";
      }
    }
    is_open_ = true;
  } else if (QOpenGLContext::currentContext() == nullptr) {
    qWarning() << "No current context to create a shader program for - will retry next repaint";
  } else {
    is_open_ = true;
  }

  if (hasCapability(Capability::SUPERIMPOSE)) {
    superimpose_.texture_ = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
  }
}

void Effect::close() {
  if (!is_open_) {
    qWarning() << "Tried to close an effect that was already closed";
  }
  glsl_.program_ = nullptr;
  is_open_ = false;
}

bool Effect::is_glsl_linked() {
  return (glsl_.program_ != nullptr) && glsl_.program_->isLinked();
}

void Effect::startEffect()
{
  if (!is_open_) {
    open();
    qWarning() << "Tried to start a closed effect - opening";
  }
  if (shaders_are_enabled
      && hasCapability(Capability::SHADER)
      && glsl_.program_->isLinked()) {
    bound_ = glsl_.program_->bind();
  }
}

void Effect::endEffect()
{
  if (bound_) {
    glsl_.program_->release();
  }
  bound_ = false;
}

void Effect::process_image(const double, gsl::span<uint8_t>& /*data*/)
{
  // Does nothing
}

EffectPtr Effect::copy(ClipPtr c)
{
  EffectPtr copy_effect = create_effect(std::move(c), meta);
  copy_effect->setupUi();
  copy_effect->set_enabled(is_enabled());
  copy_field_keyframes(copy_effect);
  return copy_effect;
}

void Effect::process_shader(const double timecode, GLTextureCoords& /*coords*/, const int iteration)
{
  glsl_.program_->setUniformValue("resolution", parent_clip->width(), parent_clip->height());
  glsl_.program_->setUniformValue("time", static_cast<GLfloat>(timecode));
  glsl_.program_->setUniformValue("iteration", iteration);

  for (const auto& row: rows) {
    for (int j=0;j<row->fieldCount();j++) {
      EffectField* field = row->field(j);
      if (!field->id.isEmpty()) {
        switch (field->type) {
          case EffectFieldType::DOUBLE:
            glsl_.program_->setUniformValue(field->id.toUtf8().constData(),
                                            static_cast<GLfloat>(field->get_double_value(timecode)));
            break;
          case EffectFieldType::COLOR:
            glsl_.program_->setUniformValue(
                  field->id.toUtf8().constData(),
                  static_cast<GLfloat>(field->get_color_value(timecode).redF()),
                  static_cast<GLfloat>(field->get_color_value(timecode).greenF()),
                  static_cast<GLfloat>(field->get_color_value(timecode).blueF())
                  );
            break;
          case EffectFieldType::BOOL:
            glsl_.program_->setUniformValue(field->id.toUtf8().constData(), field->get_bool_value(timecode));
            break;
          case EffectFieldType::COMBO:
            glsl_.program_->setUniformValue(field->id.toUtf8().constData(), field->get_combo_index(timecode));
            break;
          case EffectFieldType::FONT:
            [[fallthrough]];
          case EffectFieldType::FILE_T:
            [[fallthrough]];
          case EffectFieldType::STRING:
            // not possible to send a string to a uniform value
            break;
          default:
            qWarning() << "Unknown EffectField type" << static_cast<int>(field->type);
            break;
        }
      }
    }//for
  }//for
}

void Effect::process_coords(const double, GLTextureCoords&, const int /*data*/)
{
  qInfo() << "Method does nothing";
}

GLuint Effect::process_superimpose(double timecode) {
  bool recreate_texture = false;
  int width = parent_clip->width();
  int height = parent_clip->height();

  if (width != superimpose_.img_.width() || height != superimpose_.img_.height()) {
    superimpose_.img_ = QImage(width, height, QImage::Format_RGBA8888);
    recreate_texture = true;
  }

  if (valueHasChanged(timecode) || recreate_texture || hasCapability(Capability::ALWAYS_UPDATE)) {
    redraw(timecode);
  }

  if (superimpose_.texture_ != nullptr) {
    if (recreate_texture
        || superimpose_.texture_->width() != superimpose_.img_.width()
        || superimpose_.texture_->height() != superimpose_.img_.height()) {
      superimpose_.texture_ = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
      superimpose_.texture_->setData(superimpose_.img_);
    } else {
      superimpose_.texture_->setData(0, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, superimpose_.img_.constBits());
    }
    return superimpose_.texture_->textureId();
  }
  return 0;
}

void Effect::process_audio(const double, const double, quint8*, const int, const int)
{
  qInfo() << "Method does nothing";
}

void Effect::gizmo_draw(double, GLTextureCoords &)
{
  qInfo() << "Method does nothing";
}

void Effect::gizmo_move(EffectGizmoPtr &gizmo, const int x_movement, const int y_movement, const double timecode, const bool done)
{
  for (auto& giz : gizmos) {
    if (giz != gizmo) {
      continue;
    }
    const auto ca = done ? new ComboAction : nullptr;
    if (gizmo->x_field1 != nullptr) {
      gizmo->x_field1->set_double_value(gizmo->x_field1->get_double_value(timecode) + x_movement*gizmo->x_field_multi1);
      gizmo->x_field1->make_key_from_change(ca);
    }
    if (gizmo->y_field1 != nullptr) {
      gizmo->y_field1->set_double_value(gizmo->y_field1->get_double_value(timecode) + y_movement*gizmo->y_field_multi1);
      gizmo->y_field1->make_key_from_change(ca);
    }
    if (gizmo->x_field2 != nullptr) {
      gizmo->x_field2->set_double_value(gizmo->x_field2->get_double_value(timecode) + x_movement*gizmo->x_field_multi2);
      gizmo->x_field2->make_key_from_change(ca);
    }
    if (gizmo->y_field2 != nullptr) {
      gizmo->y_field2->set_double_value(gizmo->y_field2->get_double_value(timecode) + y_movement*gizmo->y_field_multi2);
      gizmo->y_field2->make_key_from_change(ca);
    }
    if (done) {
      e_undo_stack.push(ca);
    }
    break;
  }
}

void Effect::gizmo_world_to_screen()
{
  GLfloat view_val[16];
  GLfloat projection_val[16];
  glGetFloatv(GL_MODELVIEW_MATRIX, view_val);
  glGetFloatv(GL_PROJECTION_MATRIX, projection_val);

  QMatrix4x4 view_matrix(view_val);
  QMatrix4x4 projection_matrix(projection_val);

  for (const auto& g : gizmos) {
    for (auto j=0;j<g->get_point_count(); ++j) {
      QVector4D screen_pos = QVector4D(g->world_pos[j].x(), g->world_pos[j].y(), 0, 1.0) * (view_matrix * projection_matrix);

      const int adjusted_sx1 = qRound(((screen_pos.x() * 0.5F) + 0.5F) * parent_clip->sequence->width());
      const int adjusted_sy1 = qRound((1.0F - ((screen_pos.y() * 0.5F) + 0.5F)) * parent_clip->sequence->height());

      g->screen_pos[j] = QPoint(adjusted_sx1, adjusted_sy1);
    }
  }
}

bool Effect::are_gizmos_enabled() const {
  return (!gizmos.empty());
}


void Effect::setupUi()
{
  if (ui_setup) {
    // Don't re-init UI
    return;
  }
  ui_setup = true;
  container = new CollapsibleWidget();
  // set up base UI
  connect(container->enabled_check, SIGNAL(clicked(bool)), this, SLOT(field_changed()));
  connect(container->reset_button_, &QPushButton::clicked, this, &Effect::reset);
  ui = new QWidget();
  ui_layout = new QGridLayout();
  ui_layout->setSpacing(4);
  ui->setLayout(ui_layout);
  container->setContents(ui);

  connect(container->title_bar, SIGNAL(customContextMenuRequested(const QPoint&)),
          this, SLOT(show_context_menu(const QPoint&)));

  if (meta.type >= 0) {
    // set up UI from effect file
    setupControlWidget(meta);
    if (meta.internal < 0) {
      // shader effect. no extra ui setup
    }
  } else {
    qWarning() << "Unable to set up control widget for unknown type";
  }

}

void Effect::redraw(double)
{
  qInfo() << "Method does nothing";
}

bool Effect::valueHasChanged(const double timecode)
{
  bool changed = false;
  if (cachedValues.empty()) {
    for (int i=0;i<row_count();i++) {
      EffectRowPtr crow(row(i));
      for (int j=0;j<crow->fieldCount();j++) {
        cachedValues.append(crow->field(j)->get_current_data());
      }
    }
    changed = true;
  } else {
    int index = 0;
    for (int i=0;i<row_count();i++) {
      EffectRowPtr crow(row(i));
      for (int j=0;j<crow->fieldCount();j++) {
        EffectField* field = crow->field(j);
        field->validate_keyframe_data(timecode);
        if (cachedValues.at(index) != field->get_current_data()) {
          changed = true;
        }
        cachedValues[index] = field->get_current_data();
        index++;
      }
    }
  }
  return changed;
}

void Effect::setupDoubleWidget(const QXmlStreamAttributes& attributes, EffectField& field) const
{
  for (const auto& attr : attributes) {
    if (attr.name() == "default") {
      field.setDefaultValue(attr.value().toDouble());
      field.set_double_default_value(attr.value().toDouble());
    } else if (attr.name() == "min") {
      field.set_double_minimum_value(attr.value().toDouble());
    } else if (attr.name() == "max") {
      field.set_double_maximum_value(attr.value().toDouble());
    } else if (attr.name() == "step") {
      field.set_double_step_value(attr.value().toDouble());
    }
  }
}

void Effect::setupColorWidget(const QXmlStreamAttributes& attributes, EffectField& field) const
{
  QColor color;
  for (const auto& attr : attributes) {
    if (attr.name() == "r") {
      color.setRed(attr.value().toInt());
    } else if (attr.name() == "g") {
      color.setGreen(attr.value().toInt());
    } else if (attr.name() == "b") {
      color.setBlue(attr.value().toInt());
    } else if (attr.name() == "rf") {
      color.setRedF(attr.value().toDouble());
    } else if (attr.name() == "gf") {
      color.setGreenF(attr.value().toDouble());
    } else if (attr.name() == "bf") {
      color.setBlueF(attr.value().toDouble());
    } else if (attr.name() == "hex") {
      color.setNamedColor(attr.value().toString());
    }
  }
  field.setDefaultValue(color);
  field.set_color_value(color);
}

void Effect::setupStringWidget(const QXmlStreamAttributes& attributes, EffectField& field) const
{
  for (const auto& attr : attributes) {
    if (attr.name() == "default") {
      field.setDefaultValue(attr.value().toString());
      field.set_string_value(attr.value().toString());
    }
  }
}

void Effect::setupBoolWidget(const QXmlStreamAttributes& attributes, EffectField& field) const
{
  for (const auto& attr : attributes) {
    if (attr.name() == "default") {
      field.setDefaultValue(attr.value() == "1");
      field.set_bool_value(attr.value() == "1");
    }
  }
}

void Effect::setupComboWidget(const QXmlStreamAttributes& attributes, EffectField& field, QXmlStreamReader& reader) const
{
  int combo_index = 0;
  for (const auto& attr : attributes) {
    if (attr.name() == "default") {
      combo_index = attr.value().toInt();
      break;
    }
  }
  while (!reader.atEnd() && !(reader.name() == "field" && reader.isEndElement())) {
    reader.readNext();
    if (reader.name() == "option" && reader.isStartElement()) {
      reader.readNext();
      field.add_combo_item(reader.text().toString(), 0);
    }
  }
  field.setDefaultValue(combo_index);
  field.set_combo_index(combo_index);
}

void Effect::setupFontWidget(const QXmlStreamAttributes& attributes, EffectField& field) const
{
  for (const auto& attr : attributes) {
    if (attr.name() == "default") {
      field.setDefaultValue(attr.value().toString());
      field.set_font_name(attr.value().toString());
    }
  }
}

void Effect::setupFileWidget(const QXmlStreamAttributes& attributes, EffectField& field) const
{
  for (const auto& attr : attributes) {
    if (attr.name() == "filename") {
      field.setDefaultValue(attr.value().toString());
      field.set_filename(attr.value().toString());
    }
  }
}

std::tuple<EffectFieldType, QString> Effect::getFieldType(const QXmlStreamAttributes& attributes) const
{
  EffectFieldType field_type = EffectFieldType::UNKNOWN;
  QString attr_id;
  for (const auto& readerAttr : attributes) {
    if (readerAttr.name() == "type") {
      const auto comp = readerAttr.value().toString().toUpper();
      if (comp == "DOUBLE") {
        field_type = EffectFieldType::DOUBLE;
      } else if (comp == "BOOL") {
        field_type = EffectFieldType::BOOL;
      } else if (comp == "COLOR") {
        field_type = EffectFieldType::COLOR;
      } else if (comp == "COMBO") {
        field_type = EffectFieldType::COMBO;
      } else if (comp == "FONT") {
        field_type = EffectFieldType::FONT;
      } else if (comp == "STRING") {
        field_type = EffectFieldType::STRING;
      } else if (comp == "FILE") {
        field_type = EffectFieldType::FILE_T;
      }
    } else if (readerAttr.name() == "id") {
      attr_id = readerAttr.value().toString();
    }
  }

  return std::make_tuple(field_type, attr_id);
}

void Effect::extractShaderDetails(const QXmlStreamAttributes& attributes)
{
  setCapability(Capability::SHADER);
  for (const auto& attr : attributes) {
    if (attr.name() == "vert") {
      glsl_.vert_ = attr.value().toString();
    } else if (attr.name() == "frag") {
      glsl_.frag_ = attr.value().toString();
    } else if (attr.name() == "iterations") {
      glsl_.iterations_ = attr.value().toInt();
    } else {
      qWarning() << "Unknown attribute" << attr.name();
    }
  }
}

void Effect::setupControlWidget(const EffectMeta& em)
{
  container->setText(em.name);

  if (!em.filename.isEmpty()) {
    QFile effect_file(em.filename);
    if (effect_file.open(QFile::ReadOnly)) {
      QXmlStreamReader reader(&effect_file);

      while (!reader.atEnd()) {
        auto attributes = reader.attributes();
        if (reader.name() == "row" && reader.isStartElement()) {
          QString row_name;
          for (const auto& attr : attributes){
            if (attr.name() == "name") {
              row_name = attr.value().toString();
            }
          }
          if (!row_name.isEmpty()) {
            EffectRowPtr row(add_row(row_name));
            while (!reader.atEnd() && !((reader.name() == "row") && reader.isEndElement())) {
              reader.readNext();
              if (reader.name() == "field" && reader.isStartElement()) {
                attributes = reader.attributes();
                // get field type
                auto [fieldType, attrId] = getFieldType(attributes);

                if (attrId.isEmpty()) {
                  qCritical() << "Couldn't load field from" << em.filename << "- ID cannot be empty.";
                } else if (fieldType != EffectFieldType::UNKNOWN) {
                  auto field = row->add_field(fieldType, attrId);
                  connect(field, SIGNAL(changed()), this, SLOT(field_changed()));
                  switch (fieldType) {
                    case EffectFieldType::DOUBLE:
                      setupDoubleWidget(attributes, *field);
                      break;
                    case EffectFieldType::COLOR:
                      setupColorWidget(attributes, *field);
                      break;
                    case EffectFieldType::STRING:
                      setupStringWidget(attributes, *field);
                      break;
                    case EffectFieldType::BOOL:
                      setupBoolWidget(attributes, *field);
                      break;
                    case EffectFieldType::COMBO:
                      setupComboWidget(attributes, *field, reader);
                      break;
                    case EffectFieldType::FONT:
                      setupFontWidget(attributes, *field);
                      break;
                    case EffectFieldType::FILE_T:
                      setupFileWidget(attributes, *field);
                      break;
                    default:
                      qWarning() << "Unknown EffectField type" << static_cast<int>(fieldType);
                      break;
                  }//switch
                }
              }
            }//while
          }
        } else if ( (reader.name() == "shader") && reader.isStartElement()) {
          extractShaderDetails(attributes);
        }
        reader.readNext();
      }//while

      effect_file.close();
    } else {
      qCritical() << "Failed to open effect file" << em.filename;
    }
  } else {
    qDebug() << "Effect" << em.name << "has no file name";
  }
}

qint16 mix_audio_sample(const qint16 a, const qint16 b)
{
  qint32 mixed_sample = static_cast<qint32>(a) + static_cast<qint32>(b);
  mixed_sample = qMax(qMin(mixed_sample, static_cast<qint32>(INT16_MAX)), static_cast<qint32>(INT16_MIN));
  return static_cast<qint16>(mixed_sample);
}

double log_volume(const double linear)
{
  // expects a value between 0 and 1 (or more if amplifying)
  return (qExp(linear)-1)/(M_E-1);
}
