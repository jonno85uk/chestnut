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
#include "ui/mainwindow.h"

#include <QStyleFactory>
#include <QMessageBox>
#include <QFileDialog>
#include <QTimer>
#include <QCloseEvent>
#include <QMovie>
#include <QInputDialog>
#include <QRegExp>
#include <QStatusBar>
#include <QMenu>
#include <QMenuBar>
#include <QLayout>
#include <QApplication>
#include <QPushButton>
#include <QStandardPaths>

#include "io/config.h"
#include "io/path.h"

#include "project/footage.h"
#include "project/sequence.h"
#include "project/clip.h"
#include "project/undo.h"
#include "project/media.h"
#include "project/projectfilter.h"

#include "ui/sourcetable.h"
#include "ui/viewerwidget.h"
#include "ui/sourceiconview.h"
#include "ui/timelineheader.h"
#include "ui/defaultshortcut.h"

#include "panels/panelmanager.h"

#include "dialogs/aboutdialog.h"
#include "dialogs/newsequencedialog.h"
#include "dialogs/exportdialog.h"
#include "dialogs/preferencesdialog.h"
#include "dialogs/speeddialog.h"
#include "dialogs/actionsearch.h"
#include "dialogs/debugdialog.h"

#include "playback/audio.h"
#include "playback/playback.h"

#include "debug.h"



using panels::PanelManager;

MainWindow* MainWindow::instance_ = nullptr;

constexpr auto DEFAULT_CSS = "QPushButton::checked { background: rgb(25, 25, 25); }";
constexpr double FULLSCREEN_RATIO = 4.0/3.0;
constexpr double SIXTEENBYNINE_RATIO = 16.0/9.0;

constexpr const char* const PROJ_FILE_EXT = ".nut";
constexpr auto PROJECT_FILE_FILTER("Project File (*.nut)");
constexpr auto AUTORECOVERY_FILE_PATTERN("autorecovery.nut.*");
constexpr auto FILE_AUTORECOVERY("autorecovery.nut");

constexpr auto APP_STYLE("Fusion");


constexpr auto PANEL_CFG_FILE("/layout");
constexpr auto DIR_PREVIEWS("/previews");
constexpr auto DIR_RECENTS("/recents");
constexpr auto FILE_CONFIG("config.xml");

constexpr qint64 MONTH_IN_SECONDS = 2592000000;
constexpr qint64 WEEK_IN_SECONDS = 604800000;
constexpr qint64 AUTORECOVERY_INTERVAL_MILLIS = 60000;

constexpr int NUDGE_VAL = 1;
constexpr int UNDO_STACK_LIMIT = 10;


void MainWindow::nudgeClip(const bool forward)
{
  if (global::sequence == nullptr) {
    return;
  }
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (&PanelManager::timeLine() == focused_panel) {
    PanelManager::timeLine().nudgeClips(forward ? NUDGE_VAL : -NUDGE_VAL);
  }
}

void MainWindow::setup_layout(bool reset)
{
  PanelManager::projectViewer().show();
  PanelManager::markersViewer().show();
  PanelManager::fxControls().show();
  PanelManager::footageViewer().show();
  PanelManager::sequenceViewer().show();
  PanelManager::timeLine().show();
  PanelManager::graphEditor().hide();
  PanelManager::histogram().hide();
  PanelManager::colorScope().hide();

  addDockWidget(Qt::TopDockWidgetArea, &PanelManager::projectViewer());
  tabifyDockWidget(&PanelManager::projectViewer(), &PanelManager::markersViewer());
  PanelManager::projectViewer().raise();
  addDockWidget(Qt::TopDockWidgetArea, &PanelManager::footageViewer());
  tabifyDockWidget(&PanelManager::footageViewer(), &PanelManager::fxControls());
  PanelManager::footageViewer().raise();
  addDockWidget(Qt::TopDockWidgetArea, &PanelManager::sequenceViewer());
  addDockWidget(Qt::BottomDockWidgetArea, &PanelManager::timeLine());
  PanelManager::graphEditor().setFloating(true);
  PanelManager::histogram().setFloating(true);
  PanelManager::colorScope().setFloating(true);

  // workaround for strange Qt dock bug (see https://bugreports.qt.io/browse/QTBUG-65592)
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
  resizeDocks({&PanelManager::projectViewer()}, {40}, Qt::Horizontal);
#endif

  // load panels from file
  QFile panel_config(get_config_path() + PANEL_CFG_FILE);
  if (reset) {
    panel_config.remove();
  } else {
    if (panel_config.exists() && panel_config.open(QFile::ReadOnly)) {
      restoreState(panel_config.readAll(), 0);
      panel_config.close();
    }
  }

  layout()->update();
}

MainWindow::MainWindow(QWidget *parent, const QString &an) :
  QMainWindow(parent),
  app_name(an)
{

}

MainWindow::~MainWindow()
{
  close_debug_file();
  panels::PanelManager::tearDown();
}


void MainWindow::initialise()
{
  PanelManager::setParent(this);
  open_debug_file();

  debug_dialog = new DebugDialog(this);

  // set up style?

  qApp->setStyle(QStyleFactory::create(APP_STYLE));
  setStyleSheet(DEFAULT_CSS);

  //FIXME: remove hard-coded palette
  QPalette darkPalette;
  darkPalette.setColor(QPalette::Window, QColor(53,53,53));
  darkPalette.setColor(QPalette::WindowText, Qt::white);
  darkPalette.setColor(QPalette::Base, QColor(25,25,25));
  darkPalette.setColor(QPalette::AlternateBase, QColor(53,53,53));
  darkPalette.setColor(QPalette::ToolTipBase, QColor(25,25,25));
  darkPalette.setColor(QPalette::ToolTipText, Qt::white);
  darkPalette.setColor(QPalette::Text, Qt::white);
  darkPalette.setColor(QPalette::Button, QColor(53,53,53));
  darkPalette.setColor(QPalette::ButtonText, Qt::white);	darkPalette.setColor(QPalette::BrightText, Qt::red);
  darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(128, 128, 128));
  darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::HighlightedText, Qt::black);

  qApp->setPalette(darkPalette);

  // end style
  QWidget* centralWidget = new QWidget(this);
  centralWidget->setMaximumSize(QSize(0, 0));
  setCentralWidget(centralWidget);

  setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

  setDockNestingEnabled(true);

  layout()->invalidate();

  QString data_dir = get_data_path();
  if (!data_dir.isEmpty()) {
    QDir dir(data_dir);
    dir.mkpath(".");
    if (dir.exists()) {
      qint64 a_month_ago = QDateTime::currentMSecsSinceEpoch() - MONTH_IN_SECONDS;
      qint64 a_week_ago = QDateTime::currentMSecsSinceEpoch() - WEEK_IN_SECONDS;

      // TODO put delete functions in another thread?

      // delete auto-recoveries older than 7 days
      QStringList old_autorecoveries = dir.entryList(QStringList(AUTORECOVERY_FILE_PATTERN), QDir::Files);
      int deleted_ars = 0;
      for (int i=0;i<old_autorecoveries.size();i++) {
        QString file_name = data_dir + "/" + old_autorecoveries.at(i);
        qint64 file_time = QFileInfo(file_name).lastModified().toMSecsSinceEpoch();
        if (file_time < a_week_ago) {
          if (QFile(file_name).remove()) deleted_ars++;
        }
      }
      if (deleted_ars > 0) {
        qInfo() << "Deleted" << deleted_ars << "autorecovery" << ((deleted_ars == 1) ? "file that was" : "files that were")
                << "older than 7 days";
      }

      // delete previews older than 30 days
      QDir preview_dir = QDir(data_dir + DIR_PREVIEWS);
      if (preview_dir.exists()) {
        deleted_ars = 0;
        QStringList old_prevs = preview_dir.entryList(QDir::Files);
        for (int i=0;i<old_prevs.size();i++) {
          QString file_name = preview_dir.absolutePath() + "/" + old_prevs.at(i);
          qint64 file_time = QFileInfo(file_name).lastRead().toMSecsSinceEpoch();
          if (file_time < a_month_ago) {
            if (QFile(file_name).remove()) deleted_ars++;
          }
        }
        if (deleted_ars > 0) {
          qInfo() << "Deleted" << deleted_ars << "preview" << ((deleted_ars == 1) ? "file that was" : "files that were")
                  << "last read over 30 days ago";
        }
      }

      // search for open recents list
      recent_proj_file = data_dir + DIR_RECENTS;
      QFile f(recent_proj_file);
      if (f.exists() && f.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream text_stream(&f);
        while (true) {
          QString line = text_stream.readLine();
          if (line.isNull()) {
            break;
          } else {
            recent_projects.append(line);
          }
        }
        f.close();
      }
    }
  }
  QString config_path = get_config_path();
  if (!config_path.isEmpty()) {
    QDir config_dir(config_path);
    config_dir.mkpath(".");
    config_fn = config_path + "/" + FILE_CONFIG;
    if (QFileInfo::exists(config_fn)) {
      e_config.load(config_fn);

      if (!e_config.css_path.isEmpty()) {
        load_css_from_file(e_config.css_path);
      }
    }
  }

  auto statusBar = new QStatusBar(this);
  statusBar->showMessage("Welcome to " + app_name);
  setStatusBar(statusBar);

  setup_menus();

  if (!data_dir.isEmpty()) {
    // detect auto-recovery file
    autorecovery_filename = data_dir + "/" + FILE_AUTORECOVERY;
    if (QFile::exists(autorecovery_filename)) {
      if (QMessageBox::question(nullptr,
                                tr("Auto-recovery"),
                                tr("Chestnut didn't close properly and an autorecovery file was detected. Would you like to open it?"),
                                QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes) {
        enable_launch_with_project = false;
        open_project_worker(autorecovery_filename, true);
      }
    }
    autorecovery_timer.setInterval(AUTORECOVERY_INTERVAL_MILLIS);
    QObject::connect(&autorecovery_timer, SIGNAL(timeout()), this, SLOT(autorecover_interval()));
    autorecovery_timer.start();
  }

  setup_layout(false);

  init_audio();
  QObject::connect(&PanelManager::timeLine(), &Timeline::newSequenceLoaded, this, &MainWindow::sequenceLoaded);
}

MainWindow& MainWindow::instance(QWidget* parent, const QString& an)
{
  if (instance_ == nullptr) {
    instance_ = new MainWindow(parent, an);
  }
  return *instance_;
}

void MainWindow::tearDown()
{
  delete instance_;
  instance_ = nullptr;
}

void MainWindow::launch_with_project(const QString& s)
{
  if (QDir().exists(s)) {
    project_url = s;
    enable_launch_with_project = true;
  } else {
    QMessageBox msg;
    msg.setText("Project file does not exist: '" + s + "'. Not loading.");
    msg.setIcon(QMessageBox::Warning);
    msg.exec();
  }
}

void MainWindow::make_new_menu(QMenu& parent_menu) const
{
  parent_menu.addAction(tr("&Project"), this, SLOT(new_project()), QKeySequence("Ctrl+N"))->setProperty("id", "newproj");
  parent_menu.addSeparator();
  parent_menu.addAction(tr("&Sequence"), this, SLOT(new_sequence()), QKeySequence("Ctrl+Shift+N"))->setProperty("id", "newseq");
  parent_menu.addAction(tr("&Folder"), this, SLOT(new_folder()))->setProperty("id", "newfolder");
}

void MainWindow::make_inout_menu(QMenu& parent_menu) const
{
  parent_menu.addAction(tr("Set In Point"), this, SLOT(set_in_point()), QKeySequence("I"))->setProperty("id", "setinpoint");
  parent_menu.addAction(tr("Set Out Point"), this, SLOT(set_out_point()), QKeySequence("O"))->setProperty("id", "setoutpoint");
  parent_menu.addAction(tr("Enable/Disable In/Out Point"), this, SLOT(enable_inout()))->setProperty("id", "enableinout");
  parent_menu.addSeparator();
  parent_menu.addAction(tr("Reset In Point"), this, SLOT(clear_in()))->setProperty("id", "resetin");
  parent_menu.addAction(tr("Reset Out Point"), this, SLOT(clear_out()))->setProperty("id", "resetout");
  parent_menu.addAction(tr("Clear In/Out Point"), this, SLOT(clear_inout()), QKeySequence("G"))->setProperty("id", "clearinout");
}

void kbd_shortcut_processor(QByteArray& file, QMenu* menu, bool save, bool first) {
  QList<QAction*> actions = menu->actions();
  for (int i=0;i<actions.size();i++) {
    QAction* a = actions.at(i);
    if (a->menu() != nullptr) {
      kbd_shortcut_processor(file, a->menu(), save, first);
    } else if (!a->isSeparator()) {
      if (save) {
        // saving custom shortcuts
        if (!a->property("default").isNull()) {
          QKeySequence defks(a->property("default").toString());
          if (a->shortcut() != defks) {
            // custom shortcut
            if (!file.isEmpty()) file.append('\n');
            file.append(a->property("id").toString());
            file.append('\t');
            file.append(a->shortcut().toString());
          }
        }
      } else {
        // loading custom shortcuts
        if (first) {
          // store default shortcut
          a->setProperty("default", a->shortcut().toString());
        } else {
          // restore default shortcut
          a->setShortcut(a->property("default").toString());
        }
        if (!a->property("id").isNull()) {
          QString comp_str = a->property("id").toString();
          int shortcut_index = file.indexOf(comp_str);
          if (shortcut_index == 0 || (shortcut_index > 0 && file.at(shortcut_index-1) == '\n')) {
            shortcut_index += comp_str.size() + 1;
            QString shortcut;
            while (shortcut_index < file.size() && file.at(shortcut_index) != '\n') {
              shortcut.append(file.at(shortcut_index));
              shortcut_index++;
            }
            QKeySequence ks(shortcut);
            if (!ks.isEmpty()) {
              a->setShortcut(ks);
            }
          }
        }
      }
    }
  }
}

void MainWindow::load_shortcuts(const QString& fn, bool first) {
  QByteArray shortcut_bytes;
  QFile shortcut_path(fn);
  if (shortcut_path.exists() && shortcut_path.open(QFile::ReadOnly)) {
    shortcut_bytes = shortcut_path.readAll();
    shortcut_path.close();
  }
  QList<QAction*> menus = menuBar()->actions();
  for (int i=0;i<menus.size();i++) {
    QMenu* menu = menus.at(i)->menu();
    kbd_shortcut_processor(shortcut_bytes, menu, false, first);
  }
}

void MainWindow::save_shortcuts(const QString& fn) {
  // save main menu actions
  QList<QAction*> menus = menuBar()->actions();
  QByteArray shortcut_file;
  for (int i=0;i<menus.size();i++) {
    QMenu* menu = menus.at(i)->menu();
    kbd_shortcut_processor(shortcut_file, menu, true, false);
  }
  QFile shortcut_file_io(fn);
  if (shortcut_file_io.open(QFile::WriteOnly)) {
    shortcut_file_io.write(shortcut_file);
    shortcut_file_io.close();
  } else {
    qCritical() << "Failed to save shortcut file";
  }
}

void MainWindow::load_css_from_file(const QString &fn) {
  QFile css_file(fn);
  if (css_file.exists() && css_file.open(QFile::ReadOnly)) {
    setStyleSheet(css_file.readAll());
    css_file.close();
  } else {
    // set default stylesheet
    setStyleSheet(DEFAULT_CSS);
  }
}

void MainWindow::set_rendering_state(bool rendering) {
  audio_rendering = rendering;
  if (rendering) {
    autorecovery_timer.stop();
  } else {
    autorecovery_timer.start();
  }
}

void MainWindow::show_about() {
  AboutDialog a(this);
  a.exec();
}

void MainWindow::show_debug_log() {
  debug_dialog->show();
}

void MainWindow::delete_slot()
{
  if (PanelManager::timeLine().headers->hasFocus()) {
    PanelManager::timeLine().headers->delete_markers();
  } else if (PanelManager::timeLine().focused()) {
    PanelManager::timeLine().delete_selection(global::sequence->selections_, false);
  } else if (PanelManager::fxControls().is_focused()) {
    PanelManager::fxControls().delete_effects();
  } else if (PanelManager::projectViewer().is_focused()) {
    PanelManager::projectViewer().delete_selected_media();
  } else if (PanelManager::fxControls().keyframe_focus()) {
    PanelManager::fxControls().delete_selected_keyframes();
  } else if (PanelManager::graphEditor().view_is_focused()) {
    PanelManager::graphEditor().delete_selected_keys();
  }
}

void MainWindow::select_all()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::timeLine()) {
    PanelManager::timeLine().select_all();
  } else if (focused_panel == &PanelManager::graphEditor()) {
    PanelManager::graphEditor().select_all();
  }
}

void MainWindow::new_sequence()
{
  NewSequenceDialog nsd(this);
  nsd.set_sequence_name(PanelManager::projectViewer().get_next_sequence_name());
  nsd.exec();
}

void MainWindow::zoom_in()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::timeLine()) {
    PanelManager::timeLine().set_zoom(true);
  } else if (focused_panel == &PanelManager::fxControls()) {
    PanelManager::fxControls().set_zoom(true);
  } else if (focused_panel == &PanelManager::footageViewer()) {
    PanelManager::footageViewer().set_zoom(true);
  } else if (focused_panel == &PanelManager::sequenceViewer()) {
    PanelManager::sequenceViewer().set_zoom(true);
  }
}

void MainWindow::zoom_out()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::timeLine()) {
    PanelManager::timeLine().set_zoom(false);
  } else if (focused_panel == &PanelManager::fxControls()) {
    PanelManager::fxControls().set_zoom(false);
  } else if (focused_panel == &PanelManager::footageViewer()) {
    PanelManager::footageViewer().set_zoom(false);
  } else if (focused_panel == &PanelManager::sequenceViewer()) {
    PanelManager::sequenceViewer().set_zoom(false);
  }
}

void MainWindow::export_dialog()
{
  if (global::sequence == nullptr) {
    QMessageBox::information(this,
                             tr("No active sequence"),
                             tr("Please open the sequence you wish to export."),
                             QMessageBox::Ok);
  } else {
    ExportDialog e(global::sequence , this);
    e.exec();
  }
}

void MainWindow::ripple_delete()
{
  if (global::sequence != nullptr) {
    PanelManager::timeLine().delete_selection(global::sequence->selections_, true);
  }
}

void MainWindow::editMenu_About_To_Be_Shown()
{
  undo_action->setEnabled(e_undo_stack.canUndo());
  redo_action->setEnabled(e_undo_stack.canRedo());
}

void MainWindow::undo()
{
  if (!PanelManager::timeLine().importing) { // workaround to prevent crash (and also users should never need to do this)
    e_undo_stack.undo();
    PanelManager::refreshPanels(true);
  }
}

void MainWindow::redo()
{
  e_undo_stack.redo();
  PanelManager::refreshPanels(true);
}

void MainWindow::open_speed_dialog()
{
  if (global::sequence == nullptr) {
    return;
  }
  SpeedDialog s(this);
  for (const auto& c : global::sequence->clips()) {
    if ( (c != nullptr) && c->isSelected(true)) {
      s.clips.append(c);
    }
  }

  if (!s.clips.empty()) {
    s.run();
  }
}

void MainWindow::cut()
{
  if (global::sequence != nullptr) {
    QDockWidget* focused_panel = PanelManager::getFocusedPanel();
    if (&PanelManager::timeLine() == focused_panel) {
      PanelManager::timeLine().copy(true);
    } else if (&PanelManager::fxControls() == focused_panel) {
      PanelManager::fxControls().copy(true);
    }
  }
}

void MainWindow::copy() {
  if (global::sequence != nullptr) {
    QDockWidget* focused_panel = PanelManager::getFocusedPanel();
    if (&PanelManager::timeLine() == focused_panel) {
      PanelManager::timeLine().copy(false);
    } else if (&PanelManager::fxControls() == focused_panel) {
      PanelManager::fxControls().copy(false);
    }
  }
}

void MainWindow::paste()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if ( (&PanelManager::timeLine() == focused_panel || &PanelManager::fxControls() == focused_panel)
       && global::sequence != nullptr) {
    PanelManager::timeLine().paste(false);
  }
}

void MainWindow::new_project()
{
  if (can_close_project()) {
    PanelManager::fxControls().clear_effects(true);
    e_undo_stack.clear();
    e_undo_stack.setUndoLimit(UNDO_STACK_LIMIT);
    project_url.clear();
    PanelManager::projectViewer().new_project();
    updateTitle("");
    PanelManager::refreshPanels(false);
  }
}

void MainWindow::autorecover_interval()
{
  if (!e_rendering && isWindowModified()) {
    PanelManager::projectViewer().save_project(true);
    qInfo() << " Auto-recovery project saved";
  }
}

bool MainWindow::save_project_as() {
  QString fn = QFileDialog::getSaveFileName(this, tr("Save Project As..."), "", PROJECT_FILE_FILTER);
  if (!fn.isEmpty()) {
    if (!fn.endsWith(PROJ_FILE_EXT, Qt::CaseInsensitive)) {
      fn += PROJ_FILE_EXT;
    }
    updateTitle(fn);
    PanelManager::projectViewer().save_project(false);
    return true;
  }
  return false;
}

bool MainWindow::save_project()
{
  if (project_url.isEmpty()) {
    return save_project_as();
  } else {
    PanelManager::projectViewer().save_project(false);
    return true;
  }
}

bool MainWindow::can_close_project()
{
  if (isWindowModified()) {
    QMessageBox* m = new QMessageBox(
          QMessageBox::Question,
          tr("Unsaved Project"),
          tr("This project has changed since it was last saved. Would you like to save it before closing?"),
          QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel,
          this
          );
    m->setWindowModality(Qt::WindowModal);
    int r = m->exec();
    if (r == QMessageBox::Yes) {
      return save_project();
    } else if (r == QMessageBox::Cancel) {
      return false;
    }
  }
  return true;
}

void MainWindow::setupEditMenu(QMenuBar* menuBar)
{
  edit_menu_ = menuBar->addMenu(tr("&Edit"));
  connect(edit_menu_, SIGNAL(aboutToShow()), this, SLOT(editMenu_About_To_Be_Shown()));

  undo_action = edit_menu_->addAction(tr("&Undo"), this, SLOT(undo()), QKeySequence("Ctrl+Z"));
  undo_action->setProperty("id", "undo");
  redo_action = edit_menu_->addAction(tr("Redo"), this, SLOT(redo()), QKeySequence("Ctrl+Shift+Z"));
  redo_action->setProperty("id", "redo");

  edit_menu_->addSeparator();

  edit_menu_->addAction(tr("Cu&t"), this, SLOT(cut()), QKeySequence("Ctrl+X"))->setProperty("id", "cut");
  edit_menu_->addAction(tr("Cop&y"), this, SLOT(copy()), QKeySequence("Ctrl+C"))->setProperty("id", "copy");
  edit_menu_->addAction(tr("&Paste"), this, SLOT(paste()), QKeySequence("Ctrl+V"))->setProperty("id", "paste");
  edit_menu_->addAction(tr("Paste Insert"), this, SLOT(paste_insert()), QKeySequence("Ctrl+Shift+V"))->setProperty("id", "pasteinsert");
  edit_menu_->addAction(tr("Duplicate"), this, SLOT(duplicate()), QKeySequence("Ctrl+D"))->setProperty("id", "duplicate");
  edit_menu_->addAction(tr("Delete"), this, SLOT(delete_slot()), QKeySequence("Del"))->setProperty("id", "delete");
  edit_menu_->addAction(tr("Ripple Delete"), this, SLOT(ripple_delete()), QKeySequence("Shift+Del"))->setProperty("id", "rippledelete");
  edit_menu_->addAction(tr("Split"), &PanelManager::timeLine(), SLOT(split_at_playhead()), QKeySequence("Ctrl+K"))->setProperty("id", "split");
  edit_menu_->addAction(tr("Nudge Forward"), this, &MainWindow::nudgeForward, QKeySequence("Ctrl+Right"))->setProperty("id", "nudgefwd");
  edit_menu_->addAction(tr("Nudge Back"), this, &MainWindow::nudgeBackward, QKeySequence("Ctrl+Left"))->setProperty("id", "nudgebk");

  edit_menu_->addSeparator();

  edit_menu_->addAction(tr("Select &All"), this, SLOT(select_all()), QKeySequence("Ctrl+A"))->setProperty("id", "selectall");

  edit_menu_->addAction(tr("Deselect All"), &PanelManager::timeLine(), SLOT(deselect()), QKeySequence("Ctrl+Shift+A"))->setProperty("id", "deselectall");

  edit_menu_->addSeparator();

  edit_menu_->addAction(tr("Add Default Transition"), this, SLOT(add_default_transition()), QKeySequence("Ctrl+Shift+D"))->setProperty("id", "deftransition");
  edit_menu_->addAction(tr("Link"), &PanelManager::timeLine(), SLOT(linkClips()), QKeySequence("Ctrl+L"))->setProperty("id", "linkclips");
  edit_menu_->addAction(tr("Unlink"), &PanelManager::timeLine(), SLOT(unlinkClips()), QKeySequence("Ctrl+Shift+L"))->setProperty("id", "unlinkclips");
  edit_menu_->addAction(tr("Enable/Disable"), this, SLOT(toggle_enable_clips()), QKeySequence("Shift+E"))->setProperty("id", "enabledisable");
  edit_menu_->addAction(tr("Nest"), this, SLOT(nest()))->setProperty("id", "nest");

  edit_menu_->addSeparator();

  edit_menu_->addAction(tr("Ripple to In Point"), this, SLOT(ripple_to_in_point()), QKeySequence("Q"))->setProperty("id", "rippletoin");
  edit_menu_->addAction(tr("Ripple to Out Point"), this, SLOT(ripple_to_out_point()), QKeySequence("W"))->setProperty("id", "rippletoout");
  edit_menu_->addAction(tr("Edit to In Point"), this, SLOT(edit_to_in_point()), QKeySequence("Ctrl+Alt+Q"))->setProperty("id", "edittoin");
  edit_menu_->addAction(tr("Edit to Out Point"), this, SLOT(edit_to_out_point()), QKeySequence("Ctrl+Alt+W"))->setProperty("id", "edittoout");

  edit_menu_->addSeparator();

  make_inout_menu(*edit_menu_);
  edit_menu_->addAction(tr("Delete In/Out Point"), this, SLOT(delete_inout()), QKeySequence(";"))->setProperty("id", "deleteinout");
  edit_menu_->addAction(tr("Ripple Delete In/Out Point"), this, SLOT(ripple_delete_inout()), QKeySequence("'"))->setProperty("id", "rippledeleteinout");

  edit_menu_->addSeparator();

  edit_menu_->addAction(tr("Set/Edit Marker"), this, SLOT(set_marker()), QKeySequence("M"))->setProperty("id", "marker");

  for (auto& act : edit_menu_->actions()) {
    act->setDisabled(true);
  }
}

void MainWindow::setup_menus() {
  QMenuBar* menuBar = new QMenuBar(this);
  setMenuBar(menuBar);

  // INITIALIZE FILE MENU

  QMenu* file_menu = menuBar->addMenu(tr("&File"));
  connect(file_menu, SIGNAL(aboutToShow()), this, SLOT(fileMenu_About_To_Be_Shown()));

  QMenu* new_menu = file_menu->addMenu(tr("&New"));
  make_new_menu(*new_menu);

  file_menu->addAction(tr("&Open Project"), this, SLOT(open_project()), QKeySequence("Ctrl+O"))->setProperty("id", "openproj");

  clear_open_recent_action = new QAction(tr("Clear Recent List"), this);
  clear_open_recent_action->setProperty("id", "clearopenrecent");
  connect(clear_open_recent_action, SIGNAL(triggered()), &PanelManager::projectViewer(), SLOT(clear_recent_projects()));

  open_recent = file_menu->addMenu(tr("Open Recent"));

  open_recent->addAction(clear_open_recent_action);

  file_menu->addAction(tr("&Save Project"), this, SLOT(save_project()), QKeySequence("Ctrl+S"))->setProperty("id", "saveproj");
  file_menu->addAction(tr("Save Project &As"), this, SLOT(save_project_as()), QKeySequence("Ctrl+Shift+S"))->setProperty("id", "saveprojas");

  file_menu->addSeparator();

  file_menu->addAction(tr("&Import..."), &PanelManager::projectViewer(), SLOT(import_dialog()), QKeySequence("Ctrl+I"))->setProperty("id", "import");

  file_menu->addSeparator();

  file_menu->addAction(tr("&Export..."), this, SLOT(export_dialog()), QKeySequence("Ctrl+M"))->setProperty("id", "export");

  file_menu->addSeparator();

  file_menu->addAction(tr("E&xit"), this, SLOT(close()))->setProperty("id", "exit");

  // INITIALIZE EDIT MENU

  setupEditMenu(menuBar);

  // INITIALIZE VIEW MENU

  QMenu* view_menu = menuBar->addMenu(tr("&View"));
  connect(view_menu, SIGNAL(aboutToShow()), this, SLOT(viewMenu_About_To_Be_Shown()));

  view_menu->addAction(tr("Zoom In"), this, SLOT(zoom_in()), QKeySequence("="))->setProperty("id", "zoomin");
  view_menu->addAction(tr("Zoom Out"), this, SLOT(zoom_out()), QKeySequence("-"))->setProperty("id", "zoomout");
  view_menu->addAction(tr("Increase Track Height"), this, SLOT(zoom_in_tracks()), QKeySequence("Ctrl+="))->setProperty("id", "vzoomin");
  view_menu->addAction(tr("Decrease Track Height"), this, SLOT(zoom_out_tracks()), QKeySequence("Ctrl+-"))->setProperty("id", "vzoomout");

  show_all = view_menu->addAction(tr("Toggle Show All"), &PanelManager::timeLine(), SLOT(toggle_show_all()), QKeySequence("\\"));
  show_all->setProperty("id", "showall");
  show_all->setCheckable(true);

  view_menu->addSeparator();

  track_lines = view_menu->addAction(tr("Track Lines"), this, SLOT(toggle_bool_action()));
  track_lines->setProperty("id", "tracklines");
  track_lines->setCheckable(true);
  track_lines->setData(reinterpret_cast<quintptr>(&e_config.show_track_lines));

  rectified_waveforms = view_menu->addAction(tr("Rectified Waveforms"), this, SLOT(toggle_bool_action()));
  rectified_waveforms->setProperty("id", "rectifiedwaveforms");
  rectified_waveforms->setCheckable(true);
  rectified_waveforms->setData(reinterpret_cast<quintptr>(&e_config.rectified_waveforms));

  view_menu->addSeparator();

  frames_action = view_menu->addAction(tr("Frames"), this, SLOT(set_timecode_view()));
  frames_action->setProperty("id", "modeframes");
  frames_action->setData(TIMECODE_FRAMES);
  frames_action->setCheckable(true);
  drop_frame_action = view_menu->addAction(tr("Drop Frame"), this, SLOT(set_timecode_view()));
  drop_frame_action->setProperty("id", "modedropframe");
  drop_frame_action->setData(TIMECODE_DROP);
  drop_frame_action->setCheckable(true);
  nondrop_frame_action = view_menu->addAction(tr("Non-Drop Frame"), this, SLOT(set_timecode_view()));
  nondrop_frame_action->setProperty("id", "modenondropframe");
  nondrop_frame_action->setData(TIMECODE_NONDROP);
  nondrop_frame_action->setCheckable(true);
  milliseconds_action = view_menu->addAction(tr("Milliseconds"), this, SLOT(set_timecode_view()));
  milliseconds_action->setProperty("id", "milliseconds");
  milliseconds_action->setData(TIMECODE_MILLISECONDS);
  milliseconds_action->setCheckable(true);

  view_menu->addSeparator();

  QMenu* title_safe_area_menu = view_menu->addMenu(tr("Title/Action Safe Area"));

  title_safe_off = title_safe_area_menu->addAction(tr("Off"));
  title_safe_off->setProperty("id", "titlesafeoff");
  title_safe_off->setCheckable(true);
  connect(title_safe_off, SIGNAL(triggered(bool)), this, SLOT(set_tsa_disable()));

  title_safe_default = title_safe_area_menu->addAction(tr("Default"));
  title_safe_default->setProperty("id", "titlesafedefault");
  title_safe_default->setCheckable(true);
  connect(title_safe_default, SIGNAL(triggered(bool)), this, SLOT(set_tsa_default()));

  title_safe_43 = title_safe_area_menu->addAction(tr("4:3"));
  title_safe_43->setProperty("id", "titlesafe43");
  title_safe_43->setCheckable(true);
  connect(title_safe_43, SIGNAL(triggered(bool)), this, SLOT(set_tsa_43()));

  title_safe_169 = title_safe_area_menu->addAction(tr("16:9"));
  title_safe_169->setProperty("id", "titlesafe169");
  title_safe_169->setCheckable(true);
  connect(title_safe_169, SIGNAL(triggered(bool)), this, SLOT(set_tsa_169()));

  title_safe_custom = title_safe_area_menu->addAction(tr("Custom"));
  title_safe_custom->setProperty("id", "titlesafecustom");
  title_safe_custom->setCheckable(true);
  connect(title_safe_custom, SIGNAL(triggered(bool)), this, SLOT(set_tsa_custom()));

  view_menu->addSeparator();

  full_screen = view_menu->addAction(tr("Full Screen"), this, SLOT(toggle_full_screen()), QKeySequence("F11"));
  full_screen->setProperty("id", "fullscreen");
  full_screen->setCheckable(true);

  // INITIALIZE PLAYBACK MENU

  QMenu* playback_menu = menuBar->addMenu(tr("&Playback"));
  connect(playback_menu, SIGNAL(aboutToShow()), this, SLOT(playbackMenu_About_To_Be_Shown()));

  playback_menu->addAction(tr("Go to Start"), this, SLOT(go_to_start()), QKeySequence("Home"))->setProperty("id", "gotostart");
  playback_menu->addAction(tr("Previous Frame"), this, SLOT(prev_frame()), QKeySequence("Left"))->setProperty("id", "prevframe");
  playback_menu->addAction(tr("Previous Frame+++"), this, SLOT(prevFrameFast()), QKeySequence("Shift+Left"))->setProperty("id", "prevframefast");
  playback_menu->addAction(tr("Play/Pause"), this, SLOT(playpause()), QKeySequence("Space"))->setProperty("id", "playpause");
  playback_menu->addAction(tr("Next Frame"), this, SLOT(next_frame()), QKeySequence("Right"))->setProperty("id", "nextframe");
  playback_menu->addAction(tr("Next Frame+++"), this, SLOT(nextFrameFast()), QKeySequence("Shift+Right"))->setProperty("id", "nextframefast");
  playback_menu->addAction(tr("Go to End"), this, SLOT(go_to_end()), QKeySequence("End"))->setProperty("id", "gotoend");
  playback_menu->addSeparator();
  playback_menu->addAction(tr("Go to Previous Cut"), this, SLOT(prev_cut()), QKeySequence("Up"))->setProperty("id", "prevcut");
  playback_menu->addAction(tr("Go to Next Cut"), this, SLOT(next_cut()), QKeySequence("Down"))->setProperty("id", "nextcut");
  playback_menu->addSeparator();
  playback_menu->addAction(tr("Go to In Point"), this, SLOT(go_to_in()), QKeySequence("Shift+I"))->setProperty("id", "gotoin");
  playback_menu->addAction(tr("Go to Out Point"), this, SLOT(go_to_out()), QKeySequence("Shift+O"))->setProperty("id", "gotoout");

  loop_action = playback_menu->addAction(tr("Loop"), this, SLOT(toggle_bool_action()));
  loop_action->setProperty("id", "loop");
  loop_action->setCheckable(true);
  loop_action->setData(reinterpret_cast<quintptr>(&e_config.loop));

  // INITIALIZE WINDOW MENU

  window_menu = menuBar->addMenu(tr("&Window"));
  connect(window_menu, SIGNAL(aboutToShow()), this, SLOT(windowMenu_About_To_Be_Shown()));

  QAction* window_project_action = window_menu->addAction(tr("Project"), this, SLOT(toggle_panel_visibility()));
  window_project_action->setProperty("id", "panelproject");
  window_project_action->setCheckable(true);
  window_project_action->setData(reinterpret_cast<quintptr>(&PanelManager::projectViewer()));

  QAction* window_effectcontrols_action = window_menu->addAction(tr("Effect Controls"), this, SLOT(toggle_panel_visibility()));
  window_effectcontrols_action->setProperty("id", "paneleffectcontrols");
  window_effectcontrols_action->setCheckable(true);
  window_effectcontrols_action->setData(reinterpret_cast<quintptr>(&PanelManager::fxControls()));

  QAction* window_timeline_action = window_menu->addAction(tr("Timeline"), this, SLOT(toggle_panel_visibility()));
  window_timeline_action->setProperty("id", "paneltimeline");
  window_timeline_action->setCheckable(true);
  window_timeline_action->setData(reinterpret_cast<quintptr>(&PanelManager::timeLine()));

  QAction* window_graph_editor_action = window_menu->addAction(tr("Graph Editor"), this, SLOT(toggle_panel_visibility()));
  window_graph_editor_action->setProperty("id", "panelgrapheditor");
  window_graph_editor_action->setCheckable(true);
  window_graph_editor_action->setData(reinterpret_cast<quintptr>(&PanelManager::graphEditor()));

  QAction* window_footageviewer_action = window_menu->addAction(tr("Footage Viewer"), this, SLOT(toggle_panel_visibility()));
  window_footageviewer_action->setProperty("id", "panelfootageviewer");
  window_footageviewer_action->setCheckable(true);
  window_footageviewer_action->setData(reinterpret_cast<quintptr>(&PanelManager::footageViewer()));

  QAction* window_sequenceviewer_action = window_menu->addAction(tr("Sequence Viewer"), this, SLOT(toggle_panel_visibility()));
  window_sequenceviewer_action->setProperty("id", "panelsequenceviewer");
  window_sequenceviewer_action->setCheckable(true);
  window_sequenceviewer_action->setData(reinterpret_cast<quintptr>(&PanelManager::sequenceViewer()));

  QAction* window_histogramviewer_action = window_menu->addAction(tr("Histogram Viewer"), this, SLOT(toggle_panel_visibility()));
  window_histogramviewer_action->setProperty("id", "panelhistogramviewer");
  window_histogramviewer_action->setCheckable(true);
  window_histogramviewer_action->setData(reinterpret_cast<quintptr>(&PanelManager::histogram()));

  QAction* window_scope_action = window_menu->addAction(tr("Scope Viewer"), this, SLOT(toggle_panel_visibility()));
  window_scope_action->setProperty("id", "panelscopeviewer");
  window_scope_action->setCheckable(true);
  window_scope_action->setData(reinterpret_cast<quintptr>(&PanelManager::colorScope()));

  QAction* window_marker_viewer = window_menu->addAction(tr("Markers"), this, SLOT(toggle_panel_visibility()));
  window_marker_viewer->setProperty("id", "markerviewer");
  window_marker_viewer->setCheckable(true);
  window_marker_viewer->setData(reinterpret_cast<quintptr>(&PanelManager::markersViewer()));


  window_menu->addSeparator();

  window_menu->addAction(tr("Reset to Default Layout"), this, SLOT(reset_layout()))->setProperty("id", "resetdefaultlayout");

  // INITIALIZE TOOLS MENU

  QMenu* tools_menu = menuBar->addMenu(tr("&Tools"));
  connect(tools_menu, SIGNAL(aboutToShow()), this, SLOT(toolMenu_About_To_Be_Shown()));

  pointer_tool_action = tools_menu->addAction(tr("Pointer Tool"), this, SLOT(menu_click_button()), QKeySequence("V"));
  pointer_tool_action->setProperty("id", "pointertool");
  pointer_tool_action->setCheckable(true);
  pointer_tool_action->setData(reinterpret_cast<quintptr>(PanelManager::timeLine().toolArrowButton));

  edit_tool_action = tools_menu->addAction(tr("Edit Tool"), this, SLOT(menu_click_button()), QKeySequence("X"));
  edit_tool_action->setProperty("id", "edittool");
  edit_tool_action->setCheckable(true);
  edit_tool_action->setData(reinterpret_cast<quintptr>(PanelManager::timeLine().toolEditButton));

  ripple_tool_action = tools_menu->addAction(tr("Ripple Tool"), this, SLOT(menu_click_button()), QKeySequence("B"));
  ripple_tool_action->setProperty("id", "rippletool");
  ripple_tool_action->setCheckable(true);
  ripple_tool_action->setData(reinterpret_cast<quintptr>(PanelManager::timeLine().toolRippleButton));

  razor_tool_action = tools_menu->addAction(tr("Razor Tool"), this, SLOT(menu_click_button()), QKeySequence("C"));
  razor_tool_action->setProperty("id", "razortool");
  razor_tool_action->setCheckable(true);
  razor_tool_action->setData(reinterpret_cast<quintptr>(PanelManager::timeLine().toolRazorButton));

  slip_tool_action = tools_menu->addAction(tr("Slip Tool"), this, SLOT(menu_click_button()), QKeySequence("Y"));
  slip_tool_action->setProperty("id", "sliptool");
  slip_tool_action->setCheckable(true);
  slip_tool_action->setData(reinterpret_cast<quintptr>(PanelManager::timeLine().toolSlipButton));

  slide_tool_action = tools_menu->addAction(tr("Slide Tool"), this, SLOT(menu_click_button()), QKeySequence("U"));
  slide_tool_action->setProperty("id", "slidetool");
  slide_tool_action->setCheckable(true);
  slide_tool_action->setData(reinterpret_cast<quintptr>(PanelManager::timeLine().toolSlideButton));

  hand_tool_action = tools_menu->addAction(tr("Hand Tool"), this, SLOT(menu_click_button()), QKeySequence("H"));
  hand_tool_action->setProperty("id", "handtool");
  hand_tool_action->setCheckable(true);
  hand_tool_action->setData(reinterpret_cast<quintptr>(PanelManager::timeLine().toolHandButton));

  transition_tool_action = tools_menu->addAction(tr("Transition Tool"), this, SLOT(menu_click_button()), QKeySequence("T"));
  transition_tool_action->setProperty("id", "transitiontool");
  transition_tool_action->setCheckable(true);
  transition_tool_action->setData(reinterpret_cast<quintptr>(PanelManager::timeLine().toolTransitionButton));

  tools_menu->addSeparator();

  snap_toggle = tools_menu->addAction(tr("Enable Snapping"), this, SLOT(menu_click_button()), QKeySequence("S"));
  snap_toggle->setProperty("id", "snapping");
  snap_toggle->setCheckable(true);
  snap_toggle->setData(reinterpret_cast<quintptr>(PanelManager::timeLine().snappingButton));

  tools_menu->addSeparator();

  selecting_also_seeks = tools_menu->addAction(tr("Selecting Also Seeks"), this, SLOT(toggle_bool_action()));
  selecting_also_seeks->setProperty("id", "selectingalsoseeks");
  selecting_also_seeks->setCheckable(true);
  selecting_also_seeks->setData(reinterpret_cast<quintptr>(&e_config.select_also_seeks));

  edit_tool_also_seeks = tools_menu->addAction(tr("Edit Tool Also Seeks"), this, SLOT(toggle_bool_action()));
  edit_tool_also_seeks->setProperty("id", "editalsoseeks");
  edit_tool_also_seeks->setCheckable(true);
  edit_tool_also_seeks->setData(reinterpret_cast<quintptr>(&e_config.edit_tool_also_seeks));

  edit_tool_selects_links = tools_menu->addAction(tr("Edit Tool Selects Links"), this, SLOT(toggle_bool_action()));
  edit_tool_selects_links->setProperty("id", "editselectslinks");
  edit_tool_selects_links->setCheckable(true);
  edit_tool_selects_links->setData(reinterpret_cast<quintptr>(&e_config.edit_tool_selects_links));

  seek_also_selects = tools_menu->addAction(tr("Seek Also Selects"), this, SLOT(toggle_bool_action()));
  seek_also_selects->setProperty("id", "seekalsoselects");
  seek_also_selects->setCheckable(true);
  seek_also_selects->setData(reinterpret_cast<quintptr>(&e_config.seek_also_selects));

  seek_to_end_of_pastes = tools_menu->addAction(tr("Seek to the End of Pastes"), this, SLOT(toggle_bool_action()));
  seek_to_end_of_pastes->setProperty("id", "seektoendofpastes");
  seek_to_end_of_pastes->setCheckable(true);
  seek_to_end_of_pastes->setData(reinterpret_cast<quintptr>(&e_config.paste_seeks));

  scroll_wheel_zooms = tools_menu->addAction(tr("Scroll Wheel Zooms"), this, SLOT(toggle_bool_action()));
  scroll_wheel_zooms->setProperty("id", "scrollwheelzooms");
  scroll_wheel_zooms->setCheckable(true);
  scroll_wheel_zooms->setData(reinterpret_cast<quintptr>(&e_config.scroll_zooms));

  enable_drag_files_to_timeline = tools_menu->addAction(tr("Enable Drag Files to Timeline"), this, SLOT(toggle_bool_action()));
  enable_drag_files_to_timeline->setProperty("id", "enabledragfilestotimeline");
  enable_drag_files_to_timeline->setCheckable(true);
  enable_drag_files_to_timeline->setData(reinterpret_cast<quintptr>(&e_config.enable_drag_files_to_timeline));

  autoscale_by_default = tools_menu->addAction(tr("Auto-Scale By Default"), this, SLOT(toggle_bool_action()));
  autoscale_by_default->setProperty("id", "autoscalebydefault");
  autoscale_by_default->setCheckable(true);
  autoscale_by_default->setData(reinterpret_cast<quintptr>(&e_config.autoscale_by_default));

  enable_seek_to_import = tools_menu->addAction(tr("Enable Seek to Import"), this, SLOT(toggle_bool_action()));
  enable_seek_to_import->setProperty("id", "enableseektoimport");
  enable_seek_to_import->setCheckable(true);
  enable_seek_to_import->setData(reinterpret_cast<quintptr>(&e_config.enable_seek_to_import));

  enable_audio_scrubbing = tools_menu->addAction(tr("Audio Scrubbing"), this, SLOT(toggle_bool_action()));
  enable_audio_scrubbing->setProperty("id", "audioscrubbing");
  enable_audio_scrubbing->setCheckable(true);
  enable_audio_scrubbing->setData(reinterpret_cast<quintptr>(&e_config.enable_audio_scrubbing));

  enable_drop_on_media_to_replace = tools_menu->addAction(tr("Enable Drop on Media to Replace"), this, SLOT(toggle_bool_action()));
  enable_drop_on_media_to_replace->setProperty("id", "enabledropmediareplace");
  enable_drop_on_media_to_replace->setCheckable(true);
  enable_drop_on_media_to_replace->setData(reinterpret_cast<quintptr>(&e_config.drop_on_media_to_replace));

  enable_hover_focus = tools_menu->addAction(tr("Enable Hover Focus"), this, SLOT(toggle_bool_action()));
  enable_hover_focus->setProperty("id", "hoverfocus");
  enable_hover_focus->setCheckable(true);
  enable_hover_focus->setData(reinterpret_cast<quintptr>(&e_config.hover_focus));

  set_name_and_marker = tools_menu->addAction(tr("Ask For Name When Setting Marker"), this, SLOT(toggle_bool_action()));
  set_name_and_marker->setProperty("id", "asknamemarkerset");
  set_name_and_marker->setCheckable(true);
  set_name_and_marker->setData(reinterpret_cast<quintptr>(&e_config.set_name_with_marker));

  pause_at_out_point_action = tools_menu->addAction(tr("Pause At Out Point"), this, SLOT(toggle_bool_action()));
  pause_at_out_point_action->setProperty("id", "pauseoutpoint");
  pause_at_out_point_action->setCheckable(true);
  pause_at_out_point_action->setData(reinterpret_cast<quintptr>(&e_config.pause_at_out_point));

  tools_menu->addSeparator();

  no_autoscroll = tools_menu->addAction(tr("No Auto-Scroll"), this, SLOT(set_autoscroll()));
  no_autoscroll->setProperty("id", "autoscrollno");
  no_autoscroll->setData(AUTOSCROLL_NO_SCROLL);
  no_autoscroll->setCheckable(true);

  page_autoscroll = tools_menu->addAction(tr("Page Auto-Scroll"), this, SLOT(set_autoscroll()));
  page_autoscroll->setProperty("id", "autoscrollpage");
  page_autoscroll->setData(AUTOSCROLL_PAGE_SCROLL);
  page_autoscroll->setCheckable(true);

  smooth_autoscroll = tools_menu->addAction(tr("Smooth Auto-Scroll"), this, SLOT(set_autoscroll()));
  smooth_autoscroll->setProperty("id", "autoscrollsmooth");
  smooth_autoscroll->setData(AUTOSCROLL_SMOOTH_SCROLL);
  smooth_autoscroll->setCheckable(true);

  tools_menu->addSeparator();

  tools_menu->addAction(tr("Preferences"), this, SLOT(preferences()), QKeySequence("Ctrl+."))->setProperty("id", "prefs");

#ifdef QT_DEBUG
  tools_menu->addAction(tr("Clear Undo"), this, SLOT(clear_undo_stack()))->setProperty("id", "clearundo");
#endif

  // INITIALIZE HELP MENU

  QMenu* help_menu = menuBar->addMenu(tr("&Help"));

  help_menu->addAction(tr("A&ction Search"), this, SLOT(show_action_search()), QKeySequence("/"))->setProperty("id", "actionsearch");

  help_menu->addSeparator();

  help_menu->addAction(tr("Debug Log"), this, SLOT(show_debug_log()))->setProperty("id", "debuglog");

  help_menu->addSeparator();

  help_menu->addAction(tr("&About..."), this, SLOT(show_about()))->setProperty("id", "about");

  load_shortcuts(get_config_path() + "/shortcuts", true);
}

void MainWindow::set_bool_action_checked(QAction *a) {
  if (!a->data().isNull()) {
    bool* variable = reinterpret_cast<bool*>(a->data().value<quintptr>());
    a->setChecked(*variable);
  }
}

void MainWindow::set_int_action_checked(QAction *a, const int i) {
  if (!a->data().isNull()) {
    a->setChecked(a->data() == i);
  }
}

void MainWindow::set_button_action_checked(QAction *a) {
  a->setChecked(reinterpret_cast<QPushButton*>(a->data().value<quintptr>())->isChecked());
}

void MainWindow::updateTitle(const QString& url) {
  project_url = url;
  setWindowTitle(app_name + " - " + ((project_url.isEmpty()) ? tr("<untitled>") : project_url) + "[*]");
}

void MainWindow::closeEvent(QCloseEvent *e) {
  if (can_close_project()) {
    PanelManager::fxControls().clear_effects(true);

    set_sequence(nullptr);

    PanelManager::footageViewer().viewer_widget->close_window();
    PanelManager::sequenceViewer().viewer_widget->close_window();

    PanelManager::footageViewer().set_main_sequence();

    QString data_dir = get_data_path();
    QString config_dir = get_config_path();
    if (!data_dir.isEmpty() && !autorecovery_filename.isEmpty()) {
      if (QFile::exists(autorecovery_filename)) {
        QFile::rename(autorecovery_filename, autorecovery_filename + "." + QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmss"));
      }
    }
    if (!config_dir.isEmpty() && !config_fn.isEmpty()) {
      // save settings
      e_config.save(config_fn);

      // save panel layout
      QFile panel_config(config_dir + "/layout");
      if (panel_config.open(QFile::WriteOnly)) {
        panel_config.write(saveState(0));
        panel_config.close();
      } else {
        qCritical() << "Failed to save layout";
      }

      save_shortcuts(config_dir + "/shortcuts");
    }

    stop_audio();

    e->accept();
  } else {
    e->ignore();
  }
}

void MainWindow::paintEvent(QPaintEvent *event) {
  QMainWindow::paintEvent(event);
  if (enable_launch_with_project) {
    QTimer::singleShot(10, this, SLOT(load_with_launch()));
    enable_launch_with_project = false;
  }
}


void MainWindow::nudgeForward()
{
  nudgeClip(true);
}

void MainWindow::nudgeBackward()
{
  nudgeClip(false);
}

void MainWindow::clear_undo_stack() {
  e_undo_stack.clear();
}

void MainWindow::open_project()
{
  const auto paths = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation);
  const QString location = paths.empty() ? "" : paths.first();
  const QString file_name = QFileDialog::getOpenFileName(this, tr("Open Project..."), location, PROJECT_FILE_FILTER);
  if (!file_name.isEmpty() && can_close_project()) {
    open_project_worker(file_name, false);
  }
}

void MainWindow::open_project_worker(const QString& fn, bool autorecovery)
{
  updateTitle(fn);
  PanelManager::projectViewer().load_project(autorecovery);
  e_undo_stack.clear();
}

void MainWindow::load_with_launch()
{
  open_project_worker(project_url, false);
}

void MainWindow::show_action_search() {
  ActionSearch as(this);
  as.exec();
}


void MainWindow::sequenceLoaded(const SequencePtr& new_sequence)
{
  Q_ASSERT(drop_frame_action != nullptr);
  Q_ASSERT(nondrop_frame_action != nullptr);
  Q_ASSERT(edit_menu_ != nullptr);

  if (new_sequence == nullptr) {
    qWarning() << "Sequence instance is null";
    return;
  }

  // ensure "drop-frame" action isn't selected or enabled if not possible
  auto droppable = frame_rate_is_droppable(new_sequence->frameRate());
  if (!droppable && drop_frame_action->isChecked()) {
    drop_frame_action->setChecked(false);
    e_config.timecode_view = TIMECODE_NONDROP;
    nondrop_frame_action->setChecked(true);
  }
  drop_frame_action->setEnabled(droppable);

  // enable all the actions now that a sequence is in the timeline
  for (auto& item : edit_menu_->actions()) {
    Q_ASSERT(item);
    item->setEnabled(true);
  }

}

void MainWindow::reset_layout()
{

  setup_layout(true);
}

void MainWindow::go_to_in()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::footageViewer()) {
    PanelManager::footageViewer().go_to_in();
  } else {
    PanelManager::sequenceViewer().go_to_in();
  }
}

void MainWindow::go_to_out()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::footageViewer()) {
    PanelManager::footageViewer().go_to_out();
  } else {
    PanelManager::sequenceViewer().go_to_out();
  }
}

void MainWindow::go_to_start()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::footageViewer()) {
    PanelManager::footageViewer().go_to_start();
  } else {
    PanelManager::sequenceViewer().go_to_start();
  }
}


void MainWindow::prevFrameFast()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  Viewer& vwr = focused_panel == &PanelManager::footageViewer() ? PanelManager::footageViewer()
                                                                : PanelManager::sequenceViewer();
  vwr.previousFrameFast();
}

void MainWindow::prev_frame()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::footageViewer()) {
    PanelManager::footageViewer().previous_frame();
  } else {
    PanelManager::sequenceViewer().previous_frame();
  }
}

void MainWindow::next_frame()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::footageViewer()) {
    PanelManager::footageViewer().next_frame();
  } else {
    PanelManager::sequenceViewer().next_frame();
  }
}

void MainWindow::nextFrameFast()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  Viewer& vwr = (focused_panel == &PanelManager::footageViewer()) ? PanelManager::footageViewer()
                                                                  : PanelManager::sequenceViewer();
  vwr.nextFrameFast();
}

void MainWindow::go_to_end()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::footageViewer()) {
    PanelManager::footageViewer().go_to_end();
  } else {
    PanelManager::sequenceViewer().go_to_end();
  }
}

void MainWindow::playpause()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::footageViewer()) {
    PanelManager::footageViewer().toggle_play();
  } else {
    PanelManager::sequenceViewer().toggle_play();
  }
}

void MainWindow::prev_cut()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (global::sequence != nullptr
      && (&PanelManager::timeLine() == focused_panel || &PanelManager::sequenceViewer() == focused_panel)) {
    PanelManager::timeLine().previous_cut();
  }
}

void MainWindow::next_cut()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (global::sequence != nullptr
      && (&PanelManager::timeLine() == focused_panel || &PanelManager::sequenceViewer() == focused_panel)) {
    PanelManager::timeLine().next_cut();
  }
}

void MainWindow::preferences()
{
  PreferencesDialog pd(this);
  pd.setup_kbd_shortcuts(menuBar());
  pd.exec();
}

void MainWindow::zoom_in_tracks()
{
  PanelManager::timeLine().increase_track_height();
}

void MainWindow::zoom_out_tracks()
{
  PanelManager::timeLine().decrease_track_height();
}

void MainWindow::windowMenu_About_To_Be_Shown()
{
  QList<QAction*> window_actions = window_menu->actions();
  for (auto& action : window_actions) {
    if (!action->data().isNull()) {
      action->setChecked(reinterpret_cast<QDockWidget*>(action->data().value<quintptr>())->isVisible());
    }
  }
}

void MainWindow::playbackMenu_About_To_Be_Shown()
{
  set_bool_action_checked(loop_action);
}

void MainWindow::viewMenu_About_To_Be_Shown()
{
  set_bool_action_checked(track_lines);

  set_int_action_checked(frames_action, e_config.timecode_view);
  set_int_action_checked(drop_frame_action, e_config.timecode_view);
  set_int_action_checked(nondrop_frame_action, e_config.timecode_view);
  set_int_action_checked(milliseconds_action, e_config.timecode_view);

  title_safe_off->setChecked(!e_config.show_title_safe_area);
  title_safe_default->setChecked(e_config.show_title_safe_area && !e_config.use_custom_title_safe_ratio);
  title_safe_43->setChecked(e_config.show_title_safe_area
                            && e_config.use_custom_title_safe_ratio
                            && qFuzzyCompare(e_config.custom_title_safe_ratio, FULLSCREEN_RATIO));
  title_safe_169->setChecked(e_config.show_title_safe_area
                             && e_config.use_custom_title_safe_ratio
                             && qFuzzyCompare(e_config.custom_title_safe_ratio, SIXTEENBYNINE_RATIO));
  title_safe_custom->setChecked(e_config.show_title_safe_area
                                && e_config.use_custom_title_safe_ratio
                                && !title_safe_43->isChecked()
                                && !title_safe_169->isChecked());

  full_screen->setChecked(windowState() == Qt::WindowFullScreen);

  show_all->setChecked(PanelManager::timeLine().showing_all);
}

void MainWindow::toolMenu_About_To_Be_Shown()
{
  set_button_action_checked(pointer_tool_action);
  set_button_action_checked(edit_tool_action);
  set_button_action_checked(ripple_tool_action);
  set_button_action_checked(razor_tool_action);
  set_button_action_checked(slip_tool_action);
  set_button_action_checked(slide_tool_action);
  set_button_action_checked(hand_tool_action);
  set_button_action_checked(transition_tool_action);
  set_button_action_checked(snap_toggle);

  set_bool_action_checked(selecting_also_seeks);
  set_bool_action_checked(edit_tool_also_seeks);
  set_bool_action_checked(edit_tool_selects_links);
  set_bool_action_checked(seek_to_end_of_pastes);
  set_bool_action_checked(scroll_wheel_zooms);
  set_bool_action_checked(rectified_waveforms);
  set_bool_action_checked(enable_drag_files_to_timeline);
  set_bool_action_checked(autoscale_by_default);
  set_bool_action_checked(enable_seek_to_import);
  set_bool_action_checked(enable_audio_scrubbing);
  set_bool_action_checked(enable_drop_on_media_to_replace);
  set_bool_action_checked(enable_hover_focus);
  set_bool_action_checked(set_name_and_marker);
  set_bool_action_checked(pause_at_out_point_action);
  set_bool_action_checked(seek_also_selects);

  set_int_action_checked(no_autoscroll, e_config.autoscroll);
  set_int_action_checked(page_autoscroll, e_config.autoscroll);
  set_int_action_checked(smooth_autoscroll, e_config.autoscroll);
}

void MainWindow::duplicate()
{
  if (PanelManager::projectViewer().is_focused()) {
    PanelManager::projectViewer().duplicate_selected();
  }
}

void MainWindow::add_default_transition()
{
  if (PanelManager::timeLine().focused()) {
    PanelManager::timeLine().add_transition();
  }
}

void MainWindow::new_folder()
{
  MediaPtr m = PanelManager::projectViewer().newFolder(tr("New Folder"));
  e_undo_stack.push(new AddMediaCommand(m, PanelManager::projectViewer().get_selected_folder()));

  QModelIndex index = Project::model().add(m);
  switch (e_config.project_view_type) {
  case ProjectView::TREE:
    PanelManager::projectViewer().tree_view_->edit(PanelManager::projectViewer().sorter_->mapFromSource(index));
    break;
  case ProjectView::ICON:
    PanelManager::projectViewer().icon_view_->edit(PanelManager::projectViewer().sorter_->mapFromSource(index));
    break;
  default:
    qWarning() << "Unhandled Project View type";
    break;
  }//switch
}

void MainWindow::fileMenu_About_To_Be_Shown()
{
  if (!recent_projects.empty()) {
    open_recent->clear();
    open_recent->setEnabled(true);
    for (int i=0;i<recent_projects.size();i++) {
      QAction* action = open_recent->addAction(recent_projects.at(i));
      action->setProperty("keyignore", true);
      action->setData(i);
      connect(action, SIGNAL(triggered()), this, SLOT(load_recent_project()));
    }
    open_recent->addSeparator();

    open_recent->addAction(clear_open_recent_action);
  } else {
    open_recent->setEnabled(false);
  }
}

void MainWindow::fileMenu_About_To_Hide()
{
  qInfo() << "Method does nothing";
}

void MainWindow::load_recent_project()
{
  int index = static_cast<QAction*>(sender())->data().toInt();
  QString recent_url = recent_projects.at(index);
  if (!QFile::exists(recent_url)) {
    if (QMessageBox::question(
          this,
          tr("Missing recent project"),
          tr("The project '%1' no longer exists. Would you like to remove it from the recent projects list?").arg(recent_url),
          QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes) {
      recent_projects.removeAt(index);
      PanelManager::projectViewer().save_recent_projects();
    }
  } else if (can_close_project()) {
    open_project_worker(recent_url, false);
  }
}

void MainWindow::ripple_to_in_point()
{
  if (PanelManager::timeLine().focused()) {
    PanelManager::timeLine().ripple_to_in_point(true, true);
  }
}

void MainWindow::ripple_to_out_point() {
  if (PanelManager::timeLine().focused()) {
    PanelManager::timeLine().ripple_to_in_point(false, true);
  }
}

void MainWindow::set_in_point()
{
  if (PanelManager::timeLine().focused() || PanelManager::sequenceViewer().is_focused()) {
    PanelManager::sequenceViewer().set_in_point();
  } else if (PanelManager::footageViewer().is_focused()) {
    PanelManager::footageViewer().set_in_point();
  }
}

void MainWindow::set_out_point()
{
  if (PanelManager::timeLine().focused() || PanelManager::sequenceViewer().is_focused()) {
    PanelManager::sequenceViewer().set_out_point();
  } else if (PanelManager::footageViewer().is_focused()) {
    PanelManager::footageViewer().set_out_point();
  }
}

void MainWindow::clear_in()
{
  if (PanelManager::timeLine().focused() || PanelManager::sequenceViewer().is_focused()) {
    PanelManager::sequenceViewer().clear_in();
  } else if (PanelManager::footageViewer().is_focused()) {
    PanelManager::footageViewer().clear_in();
  }
}

void MainWindow::clear_out()
{
  if (PanelManager::timeLine().focused() || PanelManager::sequenceViewer().is_focused()) {
    PanelManager::sequenceViewer().clear_out();
  } else if (PanelManager::footageViewer().is_focused()) {
    PanelManager::footageViewer().clear_out();
  }
}

void MainWindow::clear_inout()
{
  if (PanelManager::timeLine().focused() || PanelManager::sequenceViewer().is_focused()) {
    PanelManager::sequenceViewer().clear_inout_point();
  } else if (PanelManager::footageViewer().is_focused()) {
    PanelManager::footageViewer().clear_inout_point();
  }
}

void MainWindow::toggle_full_screen()
{
  if (windowState() == Qt::WindowFullScreen) {
    setWindowState(Qt::WindowNoState); // seems to be necessary for it to return to Maximized correctly on Linux
    setWindowState(Qt::WindowMaximized);
  } else {
    setWindowState(Qt::WindowFullScreen);
  }
}

void MainWindow::delete_inout()
{
  if (PanelManager::timeLine().focused()) {
    PanelManager::timeLine().delete_in_out(false);
  }
}

void MainWindow::ripple_delete_inout()
{
  if (PanelManager::timeLine().focused()) {
    PanelManager::timeLine().delete_in_out(true);
  }
}

void MainWindow::enable_inout()
{
  if (PanelManager::timeLine().focused() || PanelManager::sequenceViewer().is_focused()) {
    PanelManager::sequenceViewer().toggle_enable_inout();
  } else if (PanelManager::footageViewer().is_focused()) {
    PanelManager::footageViewer().toggle_enable_inout();
  }
}

void MainWindow::set_tsa_default()
{
  e_config.show_title_safe_area = true;
  e_config.use_custom_title_safe_ratio = false;
  PanelManager::sequenceViewer().viewer_widget->update();
}

void MainWindow::set_tsa_disable()
{
  e_config.show_title_safe_area = false;
  PanelManager::sequenceViewer().viewer_widget->update();
}

void MainWindow::set_tsa_43()
{
  e_config.show_title_safe_area = true;
  e_config.use_custom_title_safe_ratio = true;
  e_config.custom_title_safe_ratio = FULLSCREEN_RATIO;
  PanelManager::sequenceViewer().viewer_widget->update();
}

void MainWindow::set_tsa_169()
{
  e_config.show_title_safe_area = true;
  e_config.use_custom_title_safe_ratio = true;
  e_config.custom_title_safe_ratio = SIXTEENBYNINE_RATIO;
  PanelManager::sequenceViewer().viewer_widget->update();
}

void MainWindow::set_tsa_custom() {
  QString input;
  bool invalid = false;
  QRegExp arTest("[0-9.]+:[0-9.]+");

  do {
    if (invalid) {
      QMessageBox::critical(this, tr("Invalid aspect ratio"),
                            tr("The aspect ratio '%1' is invalid. Please try again.").arg(input));
    }

    input = QInputDialog::getText(this, tr("Enter custom aspect ratio"),
                                  tr("Enter the aspect ratio to use for the title/action safe area (e.g. 16:9):"));
    invalid = !arTest.exactMatch(input) && !input.isEmpty();
  } while (invalid);

  if (!input.isEmpty()) {
    QStringList inputList = input.split(':');

    e_config.show_title_safe_area = true;
    e_config.use_custom_title_safe_ratio = true;
    e_config.custom_title_safe_ratio = inputList.at(0).toDouble()/inputList.at(1).toDouble();
    PanelManager::sequenceViewer().viewer_widget->update();
  }
}

void MainWindow::set_marker()
{
  QDockWidget* panel = PanelManager::getFocusedPanel();
  if (panel == &PanelManager::timeLine()
      || panel == &PanelManager::sequenceViewer()
      || panel == &PanelManager::footageViewer()) {
    ui::MarkerDockWidget* mark_panel = dynamic_cast<ui::MarkerDockWidget*>(panel);
    mark_panel->setMarker();
  }
}

void MainWindow::toggle_enable_clips() {
  if (global::sequence != nullptr) {
    auto ca = new ComboAction();
    bool push_undo = false;
    for (int i=0;i<global::sequence->clips().size();i++) {
      ClipPtr c = global::sequence->clips().at(i);
      if (c != nullptr && c->isSelected(true)) {
        ca->append(new SetEnableCommand(c, !c->timeline_info.enabled));
        push_undo = true;
      }
    }
    if (push_undo) {
      e_undo_stack.push(ca);
      PanelManager::refreshPanels(true);
    } else {
      delete ca;
    }
  }
}

void MainWindow::edit_to_in_point()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::timeLine()) {
    PanelManager::timeLine().ripple_to_in_point(true, false);
  }
}

void MainWindow::edit_to_out_point()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::timeLine()) {
    PanelManager::timeLine().ripple_to_in_point(false, false);
  }
}

void MainWindow::nest()
{
  if (global::sequence == nullptr) {
    return;
  }
  QVector<ClipPtr> selected_clips;
  int64_t earliest_point = LONG_MAX;

  // get selected clips
  for (const auto& seq_clip : global::sequence->clips()) {
    if (seq_clip != nullptr && seq_clip->isSelected(true)) {
      selected_clips.append(seq_clip);
      earliest_point = qMin(seq_clip->timeline_info.in.load(), earliest_point);
    }
  }

  // nest them
  if (!selected_clips.isEmpty()) {
    auto ca = new ComboAction();
    auto s = std::make_shared<Sequence>();

    // create "nest" sequence
    s->setName(PanelManager::projectViewer().get_next_sequence_name(tr("Nested Sequence")));
    s->setWidth(global::sequence->width());
    s->setHeight(global::sequence->height());
    s->setFrameRate(global::sequence->frameRate());
    s->setAudioFrequency(global::sequence->audioFrequency());
    s->setAudioLayout(global::sequence->audioLayout());

    // copy all selected clips to the nest
    for (const auto& sel_clip : selected_clips) {
      // delete clip from old sequence
      ca->append(new DeleteClipAction(sel_clip));

      // copy to new
      ClipPtr copy = sel_clip->copy(s);
      copy->timeline_info.in -= earliest_point;
      copy->timeline_info.out -= earliest_point;
      s->addClip(copy);
    }

    // add sequence to project
    MediaPtr m = PanelManager::projectViewer().newSequence(ca, s, false, nullptr);

    // add nested sequence to active sequence
    QVector<MediaPtr> media_list;
    media_list.append(m);
    PanelManager::timeLine().createGhostsFromMedia(global::sequence, earliest_point, media_list);
    PanelManager::timeLine().addClipsFromGhosts(ca, global::sequence);

    PanelManager::fxControls().clear_effects(true);
    global::sequence->selections_.clear();

    e_undo_stack.push(ca);

    PanelManager::refreshPanels(true);
  }
}

void MainWindow::paste_insert()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::timeLine() && global::sequence != nullptr) {
    PanelManager::timeLine().paste(true);
  }
}

void MainWindow::toggle_bool_action()
{
  auto action = dynamic_cast<QAction*>(sender());
  bool* variable = reinterpret_cast<bool*>(action->data().value<quintptr>());
  *variable = !(*variable);
  PanelManager::refreshPanels(false);
}

void MainWindow::set_autoscroll()
{
  auto action = dynamic_cast<QAction*>(sender());
  e_config.autoscroll = action->data().toInt();
}

void MainWindow::menu_click_button()
{
  QDockWidget* focused_panel = PanelManager::getFocusedPanel();
  if (focused_panel == &PanelManager::timeLine()
      || focused_panel == &PanelManager::fxControls()
      || focused_panel == &PanelManager::footageViewer()
      || focused_panel == &PanelManager::sequenceViewer())
    reinterpret_cast<QPushButton*>(dynamic_cast<QAction*>(sender())->data().value<quintptr>())->click();
}

void MainWindow::toggle_panel_visibility()
{
  auto w = reinterpret_cast<QDockWidget*>(dynamic_cast<QAction*>(sender())->data().value<quintptr>());
  w->setVisible(!w->isVisible());
}

void MainWindow::set_timecode_view()
{
  auto action = dynamic_cast<QAction*>(sender());
  e_config.timecode_view = action->data().toInt();
  PanelManager::refreshPanels(false);
}
