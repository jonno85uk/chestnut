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
#ifndef EFFECT_H
#define EFFECT_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QColor>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMutex>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <memory>
#include <random>
#include <set>
#include <mediahandling/gsl-lite.hpp>

#include "effectfield.h"
#include "effectrow.h"
#include "effectgizmo.h"
#include "project/sequenceitem.h"
#include "project/ixmlstreamer.h"

class CollapsibleWidget;
class QLabel;
class QWidget;
class QGridLayout;
class QPushButton;
class QMouseEvent;

class Clip;
class Effect;
class CheckboxEx;

typedef std::shared_ptr<Clip> ClipPtr;


using EffectPtr = std::shared_ptr<Effect>;
using EffectWPtr = std::weak_ptr<Effect>;
using EffectUPtr = std::unique_ptr<Effect>;

class EffectMeta { //TODO: address types
  public:
    EffectMeta();
    bool operator==(const EffectMeta& rhs) const;
    QString name{};
    QString category{};
    QString filename{};
    QString path{};
    int internal{-1};
    int type{-1}; //FIXME: get rid of the use of type>-1 for validity checking
    int subtype{-1};
    int id;

  private:
    inline static int nextId {0};
};

extern bool shaders_are_enabled;

double log_volume(const double linear);
void init_effects();
std::shared_ptr<Effect> create_effect(ClipPtr c, const EffectMeta& em, const bool setup=true);
EffectMeta get_internal_meta(const int internal_id, const int type);

constexpr int EFFECT_TYPE_INVALID = 0;
constexpr int EFFECT_TYPE_VIDEO = 1;
constexpr int EFFECT_TYPE_AUDIO = 2;
constexpr int EFFECT_TYPE_EFFECT = 3;
constexpr int EFFECT_TYPE_TRANSITION = 4;

constexpr int EFFECT_KEYFRAME_LINEAR = 0;
constexpr int EFFECT_KEYFRAME_HOLD = 1;
constexpr int EFFECT_KEYFRAME_BEZIER = 2;

constexpr int EFFECT_INTERNAL_TRANSFORM = 0;
constexpr int EFFECT_INTERNAL_TEXT = 1;
constexpr int EFFECT_INTERNAL_SOLID = 2;
constexpr int EFFECT_INTERNAL_NOISE = 3;
constexpr int EFFECT_INTERNAL_VOLUME = 4;
constexpr int EFFECT_INTERNAL_PAN = 5;
constexpr int EFFECT_INTERNAL_TONE = 6;
constexpr int EFFECT_INTERNAL_SHAKE = 7;
constexpr int EFFECT_INTERNAL_TIMECODE = 8;
constexpr int EFFECT_INTERNAL_FILLLEFTRIGHT = 10;
constexpr int EFFECT_INTERNAL_VST = 11;
constexpr int EFFECT_INTERNAL_CORNERPIN = 12;
constexpr int EFFECT_INTERNAL_TEMPORAL = 13;
constexpr int EFFECT_INTERNAL_COUNT = 14;



enum class Capability {
  SHADER = 0,
  COORDS,
  SUPERIMPOSE,
  IMAGE,
  ALWAYS_UPDATE
};


template <typename T>
struct CartesianCoordinate {
    T x_;
    T y_;
    T z_;
};


struct GLTextureCoords {
    int grid_size;
    CartesianCoordinate<int> vertices_[4]; // 0->3 top-left, top-right, bottom-right, bottom-left
    CartesianCoordinate<float> texture_[4]; // 0->3 top-left, top-right, bottom-right, bottom-left
};

qint16 mix_audio_sample(const qint16 a, const qint16 b);


class Effect : public QObject,  public std::enable_shared_from_this<Effect>, public project::SequenceItem, public project::IXMLStreamer {
    Q_OBJECT
  public:
    static void registerMeta(const EffectMeta& meta);
    static EffectMeta getRegisteredMeta(const QString& name);
    static const QMap<QString, EffectMeta>& getRegisteredMetas();

    explicit Effect(ClipPtr c);
    Effect(ClipPtr c, const EffectMeta& em);
    Effect() = delete;

    bool hasCapability(const Capability flag) const;

    EffectRowPtr add_row(const QString &name_, bool savable = true, bool keyframable = true);
    EffectRowPtr row(const int i);
    EffectRowPtr row(const QString& name);
    const QVector<EffectRowPtr>& getRows() const;

    int row_count();
    /**
     * @brief Create a new EffectGizmo and add to internal list
     * @param type  Type for new Gizmo
     * @return the newly created EffectGizmo
     */
    EffectGizmoPtr add_gizmo(const GizmoType _type);
    const EffectGizmoPtr& gizmo(const int index);
    int gizmo_count() const;

    bool is_enabled() const;
    void set_enabled(const bool b);

    virtual void refresh();

    std::shared_ptr<Effect> copy(ClipPtr c);
    void copy_field_keyframes(const std::shared_ptr<Effect>& e);

    virtual void custom_load(QXmlStreamReader& stream);
    virtual bool load(QXmlStreamReader& stream) override;
    virtual bool save(QXmlStreamWriter& stream) const override;

    // glsl handling
    bool is_open();
    void open();
    void close();
    bool is_glsl_linked();
    virtual void startEffect();
    virtual void endEffect();

    virtual void process_image(double timecode, gsl::span<uint8_t>& data);
    virtual void process_shader(double timecode, GLTextureCoords& coords, const int iteration);
    virtual void process_coords(double timecode, GLTextureCoords& coords, int data);
    virtual GLuint process_superimpose(double timecode);
    virtual void process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes, int channel_count);

    virtual void gizmo_draw(double timecode, GLTextureCoords& coords);

    void gizmo_move(EffectGizmoPtr& sender, const int x_movement, const int y_movement, const double timecode, const bool done);
    void gizmo_world_to_screen();
    bool are_gizmos_enabled() const;

    virtual void setupUi();

    ClipPtr parent_clip; //TODO: make weak
    EffectMeta meta{};
    int id{-1};
    QString name_{};
    CollapsibleWidget* container{nullptr};
    // glsl effect
    struct {
        QString vert_{};
        QString frag_{};
        std::unique_ptr<QOpenGLShaderProgram> program_{};
        int iterations_{1};
    } glsl_{};

    // superimpose effect
    struct {
        QImage img_{};
        std::unique_ptr<QOpenGLTexture> texture_{};
    } superimpose_{};
    bool ui_setup{false};



  public slots:
    virtual void field_changed();
  private slots:
    void show_context_menu(const QPoint&);
    void delete_self();
    void move_up();
    void move_down();
    void reset();
  protected:
    // superimpose functions
    virtual void redraw(double timecode);
    /**
     * @brief Create a new EffectGizmo (virtual+protected to aid in testing)
     * @param type  Type for EffectGizmo to be
     * @return EffectGizmo shared_ptr
     */
    virtual EffectGizmoPtr newEffectGizmo(const GizmoType _type);

    void setCapability(const Capability flag);
    void clearCapability(const Capability flag);

    template <typename T>
    T randomNumber()
    {
      static std::random_device device;
      static std::mt19937 generator(device());
      static std::uniform_int_distribution<> distribution(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
      return distribution(generator);
    }



  private:
    friend class EffectTest;
    // superimpose effect
    QString script;

    QVector<EffectRowPtr> rows;
    QVector<EffectGizmoPtr> gizmos;
    QGridLayout* ui_layout {nullptr};
    QWidget* ui {nullptr};
    QVector<QVariant> cachedValues;
    std::set<Capability> capabilities_;
    bool is_open_{false};
    bool bound_{false};
    inline static QMap<QString, EffectMeta> registered {};

    bool valueHasChanged(const double timecode);
    int get_index_in_clip();
    void validate_meta_path();
    void setupControlWidget(const EffectMeta& em);
    void setupDoubleWidget(const QXmlStreamAttributes& attributes, EffectField& field) const;
    void setupColorWidget(const QXmlStreamAttributes& attributes, EffectField& field) const;
    void setupStringWidget(const QXmlStreamAttributes& attributes, EffectField& field) const;
    void setupBoolWidget(const QXmlStreamAttributes& attributes, EffectField& field) const;
    void setupComboWidget(const QXmlStreamAttributes& attributes, EffectField& field, QXmlStreamReader& reader) const;
    void setupFontWidget(const QXmlStreamAttributes& attributes, EffectField& field) const;
    void setupFileWidget(const QXmlStreamAttributes& attributes, EffectField& field) const;
    std::tuple<EffectFieldType, QString> getFieldType(const QXmlStreamAttributes& attributes) const;
    void extractShaderDetails(const QXmlStreamAttributes& attributes);

};


#endif // EFFECT_H
