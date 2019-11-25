#ifndef RENDERFUNCTIONS_H
#define RENDERFUNCTIONS_H

#include <QOpenGLContext>
#include <QVector>

#include "project/sequence.h"
#include "project/effect.h"
#include "project/clip.h"

class Viewer;


GLuint compose_sequence(Viewer* viewer,
                        QOpenGLContext* ctx,
                        SequencePtr seq,
                        QVector<ClipPtr> &nests,
                        const bool video,
                        const bool render_audio,
                        EffectPtr& gizmos,
                        bool &texture_failed,
                        const bool rendering,
                        const bool use_effects=true);

void compose_audio(Viewer* viewer, SequencePtr seq, bool render_audio);

void viewport_render();

#endif // RENDERFUNCTIONS_H
