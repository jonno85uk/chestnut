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
#ifndef UNDO_H
#define UNDO_H

#include <QUndoStack>
#include <QUndoCommand>
#include <QVector>
#include <QVariant>
#include <QModelIndex>
#include <QCheckBox>

#include "project/marker.h"
#include "project/selection.h"
#include "project/effectfield.h"
#include "project/sequence.h"
#include "project/effect.h"
#include "project/clip.h"
#include "project/media.h"
#include "ui/mainwindow.h"

class LabelSlider;
class SourceTable;
class EffectRow;
class EffectField;
class Transition;
class EffectGizmo;

class Footage;
class EffectMeta;


extern QUndoStack e_undo_stack;

class ComboAction : public QUndoCommand {
public:
  ComboAction() = default;
  virtual ~ComboAction() override;
  ComboAction(const ComboAction&) = delete;
  ComboAction(const ComboAction&&) = delete;
  ComboAction& operator=(const ComboAction&) = delete;
  ComboAction& operator=(const ComboAction&&) = delete;

  virtual void undo() override;
  virtual void redo() override;
  void append(QUndoCommand* u);
  void appendPost(QUndoCommand* u);
  int size() const;
private:
  QVector<QUndoCommand*> commands;
  QVector<QUndoCommand*> post_commands;
};

class MoveClipAction : public QUndoCommand {
public:
  MoveClipAction(const ClipPtr& c,
                 const long iin, const long iout, const long iclip_in,
                 const int itrack, const bool irelative);
  virtual void undo() override;
  virtual void redo() override;
private:
  ClipPtr clip;

  long old_in;
  long old_out;
  long old_clip_in;
  int old_track;

  long new_in;
  long new_out;
  long new_clip_in;
  int new_track;

  bool relative;

  bool old_project_changed;
};


class RippleAction : public QUndoCommand {
public:
  RippleAction(SequencePtr is, const long ipoint, const long ilength, QVector<int>  iignore);

  RippleAction(const RippleAction& ) = delete;
  RippleAction& operator=(const RippleAction&) = delete;

  virtual void undo() override;
  virtual void redo() override;
private:
  SequencePtr sqn_;
  long point_;
  long length_;
  QVector<int> ignore_;
  std::unique_ptr<ComboAction> ca_;
};

class DeleteClipAction : public QUndoCommand {
public:
  explicit DeleteClipAction(ClipPtr del_clip);
  virtual void undo() override;
  virtual void redo() override;
private:
  SequencePtr sequence_;
  ClipPtr clip_;

  bool old_project_changed;
};

class ChangeSequenceAction : public QUndoCommand {
public:
  explicit ChangeSequenceAction(SequencePtr  s);
  virtual void undo() override;
  virtual void redo() override;
private:
  SequencePtr old_sequence;
  SequencePtr new_sequence;
};

class AddEffectCommand : public QUndoCommand {
public:
  AddEffectCommand(ClipPtr c, EffectPtr e, const EffectMeta& m, const int insert_pos = -1);
  AddEffectCommand(ClipPtr c, EffectPtr e, const int insert_pos = -1);

  AddEffectCommand(const AddEffectCommand& ) = delete;
  AddEffectCommand& operator=(const AddEffectCommand&) = delete;

  virtual ~AddEffectCommand();
  virtual void undo() override;
  virtual void redo() override;
private:
  ClipPtr clip{};
  EffectMeta meta{};
  EffectPtr ref{};
  int pos{};
  bool done{false};
  bool old_project_changed{};
};

class AddTransitionCommand : public QUndoCommand {
public:
  AddTransitionCommand(ClipPtr parent,
                       ClipPtr secondary,
                       const EffectMeta& meta,
                       const ClipTransitionType type,
                       const int length);

  virtual void undo() override;
  virtual void redo() override;
private:
  ClipPtr parent_{};
  ClipPtr secondary_{};
  EffectMeta meta_{};
  ClipTransitionType type_{};
  int length_{};
  bool old_project_changed{};
  int old_ptransition{};
  int old_stransition{};
};


class ModifyTransitionCommand : public QUndoCommand {
public:
  ModifyTransitionCommand(ClipPtr c, const ClipTransitionType itype, const long ilength);
  virtual void undo() override;
  virtual void redo() override;
private:
  ClipPtr clip_;
  ClipTransitionType type_;
  long new_length_;
  long old_length_{0};
  bool old_project_changed_;
};

class DeleteTransitionCommand : public QUndoCommand {
public:
  DeleteTransitionCommand(ClipPtr clp, const ClipTransitionType type);

  virtual void undo() override;
  virtual void redo() override;
private:
  ClipPtr clip_;
  ClipTransitionType type_;

  struct {
    ClipWPtr opening_{};
    ClipWPtr closing_{};
  } secondary_;

  struct {
    EffectMeta opening_{};
    EffectMeta closing_{};
  } meta_;
  struct {
    long opening_;
    long closing_;
  } lengths_;
  bool old_project_changed;
  void populateOpening();
  void populateClosing();
};

class SetTimelineInOutCommand : public QUndoCommand {
public:
  SetTimelineInOutCommand(SequencePtr  s, const bool enabled, const long in, const long out);
  virtual void undo() override;
  virtual void redo() override;
private:
  SequencePtr seq;

  bool old_workarea_enabled{};

  bool old_enabled{};
  long old_in{};
  long old_out{};

  bool new_enabled;
  long new_in;
  long new_out;

  bool old_project_changed;
};

class NewSequenceCommand : public QUndoCommand {
public:
  NewSequenceCommand(MediaPtr s, MediaPtr iparent, const bool modified);
  virtual ~NewSequenceCommand();
  virtual void undo() override;
  virtual void redo() override;
private:
  friend class UndoTest;
  MediaPtr seq_;
  MediaPtr parent_;
  bool done_;
  bool old_project_changed_;
};

class AddMediaCommand : public QUndoCommand {
public:
  AddMediaCommand(MediaPtr iitem, MediaPtr iparent);
  virtual void undo() override;
  virtual void redo() override;
private:
  MediaPtr item;
  MediaPtr parent;
  bool done;
  bool old_project_changed;
};

class DeleteMediaCommand : public QUndoCommand {
public:
  explicit DeleteMediaCommand(const MediaPtr& i);
  virtual void undo() override;
  virtual void redo() override;
private:
  MediaPtr item;
  MediaPtr parent;
  bool old_project_changed;
  bool done{};
};


class AddClipsCommand : public QUndoCommand {
  public:
    AddClipsCommand(SequencePtr seq, const QVector<ClipPtr>& clips);
    virtual void undo() override;
    virtual void redo() override;
  private:
    SequencePtr sequence_;
    QVector<ClipPtr> clips_;
    bool old_project_changed_;
};

class ClipLinkCommand : public QUndoCommand {
public:
  ClipLinkCommand(const ClipPtr& clp, const QVector<int>& links, const bool link);
  virtual void undo() override;
  virtual void redo() override;
private:
  ClipPtr clip_;
  QVector<int> new_links_;
  QVector<int> old_links_;
  bool old_project_changed;
};


class CheckboxCommand : public QUndoCommand {
public:
  explicit CheckboxCommand(QCheckBox* b);

  CheckboxCommand(const CheckboxCommand& ) = delete;
  CheckboxCommand& operator=(const CheckboxCommand&) = delete;

  virtual void undo() override;
  virtual void redo() override;
private:
  QCheckBox* box;
  bool checked;
  bool done;
  bool old_project_changed;
};

class ReplaceMediaCommand : public QUndoCommand {
public:
  ReplaceMediaCommand(MediaPtr, QString);
  virtual void undo() override;
  virtual void redo() override;
private:
  MediaPtr item;
  QString old_filename;
  QString new_filename;
  bool old_project_changed;
  void replace(QString& filename);
};

class ReplaceClipMediaCommand : public QUndoCommand {
public:
  ReplaceClipMediaCommand(MediaPtr , MediaPtr , const bool);
  virtual void undo() override;
  virtual void redo() override;
  QVector<ClipPtr> clips;
private:
  MediaPtr old_media;
  MediaPtr new_media;
  bool preserve_clip_ins;
  bool old_project_changed;
  QVector<int> old_clip_ins;
  void replace(bool undo);
};

class EffectDeleteCommand : public QUndoCommand {
public:
  EffectDeleteCommand();
  virtual void undo() override;
  virtual void redo() override;
  QVector<ClipPtr> clips;
  QVector<int> fx;
private:
  bool done;
  bool old_project_changed;
  QVector<EffectPtr> deleted_objects;
};

class MediaMove : public QUndoCommand {
public:
  MediaMove();
  QVector<MediaPtr> items;
  MediaPtr to;
  virtual void undo() override;
  virtual void redo() override;
private:
  QVector<MediaPtr> froms;
  bool old_project_changed;
};

class MediaRename : public QUndoCommand {
public:
  MediaRename(const MediaPtr& iitem, QString  ito);
  virtual void undo() override;
  virtual void redo() override;
private:
  bool old_project_changed;
  MediaPtr item;
  QString from;
  QString to;
};

class KeyframeDelete : public QUndoCommand {
public:
  KeyframeDelete(EffectField* ifield, const int iindex);

  virtual void undo() override;
  virtual void redo() override;
private:
  EffectField* field;
  int index;
  bool done{};
  EffectKeyframe deleted_key;
  bool old_project_changed;
};

// a more modern version of the above, could probably replace it
// assumes the keyframe already exists
class KeyframeFieldSet : public QUndoCommand {
public:
  KeyframeFieldSet(EffectField* ifield, const int ii);

  virtual void undo() override;
  virtual void redo() override;
private:
  EffectField* field;
  int index;
  EffectKeyframe key;
  bool done;
  bool old_project_changed;
};

class EffectFieldUndo : public QUndoCommand {
public:
  explicit EffectFieldUndo(EffectField* field);

  virtual void undo() override;
  virtual void redo() override;
private:
  EffectField* field;
  QVariant old_val;
  QVariant new_val;
  bool done;
  bool old_project_changed;
};

class SetAutoscaleAction : public QUndoCommand {
public:
  SetAutoscaleAction();
  virtual void undo() override;
  virtual void redo() override;
  QVector<ClipPtr> clips;
private:
  bool old_project_changed;
};

class AddMarkerAction : public QUndoCommand {
public:
  AddMarkerAction(project::ProjectItemPtr  item, const long t, QString n);
  virtual void undo() override;
  virtual void redo() override;
private:
  project::ProjectItemPtr item_;
  long time;
  QString name;
  QString old_name;
  bool old_project_changed;
  int index{};
};

class MoveMarkerAction : public QUndoCommand {
public:
  MoveMarkerAction(MarkerPtr m, const long o, const long n);

  virtual void undo() override;
  virtual void redo() override;
private:
  MarkerPtr marker;
  long old_time;
  long new_time;
  bool old_project_changed;
};

class DeleteMarkerAction : public QUndoCommand {
public:
  explicit DeleteMarkerAction(SequencePtr s);
  virtual void undo() override;
  virtual void redo() override;
  QVector<int> markers;
private:
  SequencePtr seq;
  QVector<MarkerPtr> copies;
  bool sorted;
  bool old_project_changed;
};

class SetSpeedAction : public QUndoCommand {
public:
  SetSpeedAction(const ClipPtr& c, double speed);
  virtual void undo() override;
  virtual void redo() override;
private:
  ClipPtr clip;
  double old_speed;
  double new_speed;
  bool old_project_changed;
};

class SetBool : public QUndoCommand {
public:
  SetBool(bool* b, const bool setting);

  virtual void undo() override;
  virtual void redo() override;
private:
  bool* boolean;
  bool old_setting;
  bool new_setting;
  bool old_project_changed;
};

class SetSelectionsCommand : public QUndoCommand {
public:
  explicit SetSelectionsCommand(SequencePtr s);
  virtual void undo() override;
  virtual void redo() override;
  QVector<Selection> old_data;
  QVector<Selection> new_data;
private:
  SequencePtr seq;
  bool done;
  bool old_project_changed;
};

class SetEnableCommand : public QUndoCommand {
public:
  SetEnableCommand(const ClipPtr& c, const bool enable);
  virtual void undo() override;
  virtual void redo() override;
private:
  ClipPtr clip;
  bool old_val;
  bool new_val;
  bool old_project_changed;
};

class EditSequenceCommand : public QUndoCommand {

public:
  EditSequenceCommand(MediaPtr i, const SequencePtr& s);
  virtual void undo() override;
  virtual void redo() override;
  void update();

  QString name;
  int width{};
  int height{};
  double frame_rate{};
  int audio_frequency{};
  int audio_layout{};
private:
  MediaPtr item;
  SequencePtr seq;
  bool old_project_changed;

  QString old_name;
  int old_width;
  int old_height;
  double old_frame_rate;
  int old_audio_frequency;
  int old_audio_layout;
};


template <typename T>
class SetValCommand: public QUndoCommand {
public:
  SetValCommand(T& ref, const T new_val)
    : ref_(ref),
      vals_({ref, new_val}),
      old_project_changed(MainWindow::instance().isWindowModified())
  {

  }

  SetValCommand(T& ref, const T old_val, const T new_val)
    : ref_(ref),
      vals_({old_val, new_val}),
      old_project_changed(MainWindow::instance().isWindowModified())
  {

  }

  virtual void undo() override
  {
    ref_ = vals_.old_;
    MainWindow::instance().setWindowModified(old_project_changed);
  }
  virtual void redo() override
  {
    ref_= vals_.new_;
    MainWindow::instance().setWindowModified(true);
  }

private:
  T& ref_;
  struct {
    T old_;
    T new_;
  } vals_;
  bool old_project_changed;
};

class CloseAllClipsCommand : public QUndoCommand {
public:
  virtual void undo() override;
  virtual void redo() override;
};

class UpdateFootageTooltip : public QUndoCommand {
public:
  explicit UpdateFootageTooltip(MediaPtr i);
  virtual void undo() override;
  virtual void redo() override;
private:
  MediaPtr item;
};

class MoveEffectCommand : public QUndoCommand {
public:
  MoveEffectCommand();
  virtual void undo() override;
  virtual void redo() override;
  ClipPtr clip;
  int from{};
  int to{};
private:
  bool old_project_changed;
};

class ResetEffectCommand: public QUndoCommand {
public:
  ResetEffectCommand() = default;
  virtual void undo() override;
  virtual void redo() override;
  std::vector<std::tuple<EffectField*, QVariant, QVariant>> fields_;

private:
  void doCmd(const bool undo_cmd);

};

class RemoveClipsFromClipboard : public QUndoCommand {
public:
  explicit RemoveClipsFromClipboard(int index);
  virtual void undo() override;
  virtual void redo() override;
private:
  int pos;
  ClipPtr clip;
  bool old_project_changed;
  bool done;
};

class RenameClipCommand : public QUndoCommand {
public:
  RenameClipCommand();
  QVector<ClipPtr> clips;
  QString new_name;
  virtual void undo() override;
  virtual void redo() override;
private:
  QVector<QString> old_names;
  bool old_project_changed;
};

class SplitClipCommand : public QUndoCommand {
  public:
    SplitClipCommand(ClipPtr clp, const long position);
    virtual void undo() override;
    virtual void redo() override;
  private:
    ClipPtr pre_clip_;
    long position_;
    QVector<ClipPtr> posts_;
    QMap<int, ClipPtr> mapped_posts_;
    QMap<int, QPair<long,long>> mapped_transition_lengths_;
    bool old_project_changed {false};

    void storeLengths(const int id, Clip& clp);
};


class ReloadEffectsCommand : public QUndoCommand {
public:
  virtual void undo() override;
  virtual void redo() override;
};

class SetQVariant : public QUndoCommand {
public:
  SetQVariant(QVariant* itarget, QVariant  iold, QVariant  inew);

  virtual void undo() override;
  virtual void redo() override;
private:
  QVariant* target;
  QVariant old_val;
  QVariant new_val;
};

class SetKeyframing : public QUndoCommand {
public:
  SetKeyframing(EffectRow* irow, const bool ib);

  virtual void undo() override;
  virtual void redo() override;
private:
  EffectRow* row;
  bool b;
};

class RefreshClips : public QUndoCommand {
public:
  explicit RefreshClips(MediaPtr m);
  virtual void undo() override;
  virtual void redo() override;
private:
  MediaPtr media;
};

class NudgeClipCommand: public QUndoCommand {
  public:
    NudgeClipCommand(ClipPtr clp, const int val);
    virtual void undo() override;
    virtual void redo() override;
  private:
    ClipPtr clip_;
    int nudge_value_;
    bool old_project_changed_;
};

class NudgeSelectionCommand: public QUndoCommand {
public:
  NudgeSelectionCommand(Selection& sel, const int val);
  virtual void undo() override;
  virtual void redo() override;
private:
  Selection& selection_;
  int nudge_value_;
  bool old_project_changed_;
};

#endif // UNDO_H
