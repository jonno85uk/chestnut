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
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include "project/sequence.h"


class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(QWidget *parent, const QString& an);
    virtual ~MainWindow() override;
    void initialise();
    static MainWindow& instance(QWidget *parent = nullptr, const QString& an=""); // singleton. eh. only will ever need 1 instance
    static void tearDown();
    MainWindow() = delete;
    MainWindow(const MainWindow&) = delete;
    MainWindow(const MainWindow&&) = delete;
    MainWindow operator=(const MainWindow&) = delete;
    MainWindow operator=(const MainWindow&&) = delete;

    void updateTitle(const QString &url);
    void launch_with_project(const QString& s);

    void make_new_menu(QMenu& parent_menu) const;
    void make_inout_menu(QMenu& parent_menu) const;

    void load_shortcuts(const QString &fn, bool first = false);
    void save_shortcuts(const QString &fn);

    void load_css_from_file(const QString& fn);

    void set_rendering_state(bool rendering);

  public slots:
    void undo();
    void redo();
    void open_speed_dialog();
    void cut();
    void copy();
    void paste();
    void new_project();
    void autorecover_interval();
    void nest();
    void toggle_full_screen();

  protected:
    virtual void closeEvent(QCloseEvent *) override;
    virtual void paintEvent(QPaintEvent *event) override;

  private slots:
    void nudgeForward();
    void nudgeBackward();
    void clear_undo_stack();

    void show_about();
    void show_debug_log();
    void delete_slot();
    void select_all();

    void new_sequence();

    void zoom_in();
    void zoom_out();
    void export_dialog();
    void ripple_delete();

    void open_project();
    bool save_project_as();
    bool save_project();

    void go_to_in();
    void go_to_out();
    void go_to_start();
    void prevFrameFast();
    void prev_frame();
    void playpause();
    void next_frame();
    void nextFrameFast();
    void go_to_end();
    void prev_cut();
    void next_cut();

    void reset_layout();

    void preferences();

    void zoom_in_tracks();

    void zoom_out_tracks();

    void fileMenu_About_To_Be_Shown();
    void fileMenu_About_To_Hide();
    void editMenu_About_To_Be_Shown();
    void windowMenu_About_To_Be_Shown();
    void playbackMenu_About_To_Be_Shown();
    void viewMenu_About_To_Be_Shown();
    void toolMenu_About_To_Be_Shown();

    void duplicate();

    void add_default_transition();

    void new_folder();

    void load_recent_project();

    void ripple_to_in_point();
    void ripple_to_out_point();

    void set_in_point();
    void set_out_point();

    void clear_in();
    void clear_out();
    void clear_inout();
    void delete_inout();
    void ripple_delete_inout();
    void enable_inout();

    // title safe area functions
    void set_tsa_disable();
    void set_tsa_default();
    void set_tsa_43();
    void set_tsa_169();
    void set_tsa_custom();

    void set_marker();

    void toggle_enable_clips();
    void edit_to_in_point();
    void edit_to_out_point();
    void paste_insert();
    void toggle_bool_action();
    void set_autoscroll();
    void menu_click_button();
    void toggle_panel_visibility();
    void set_timecode_view();
    void open_project_worker(const QString &fn, bool autorecovery);

    void load_with_launch();

    void show_action_search();

    void sequenceLoaded(const SequencePtr& new_sequence);

  private:
    friend class MainWindowTest;


    void nudgeClip(const bool forward);
    void setup_layout(bool reset);
    bool can_close_project();
    void setup_menus();

    void set_bool_action_checked(QAction* a);
    void set_int_action_checked(QAction* a, const int i);
    void set_button_action_checked(QAction* a);


    const QString& app_name;

    // menu bar menus
    QMenu* window_menu = nullptr;

    // file menu actions
    QMenu* open_recent = nullptr;
    QAction* clear_open_recent_action = nullptr;

    // view menu actions
    QAction* track_lines = nullptr;
    QAction* frames_action = nullptr;
    QAction* drop_frame_action = nullptr;
    QAction* nondrop_frame_action = nullptr;
    QAction* milliseconds_action = nullptr;
    QAction* no_autoscroll = nullptr;
    QAction* page_autoscroll = nullptr;
    QAction* smooth_autoscroll = nullptr;
    QAction* title_safe_off = nullptr;
    QAction* title_safe_default = nullptr;
    QAction* title_safe_43 = nullptr;
    QAction* title_safe_169 = nullptr;
    QAction* title_safe_custom = nullptr;
    QAction* full_screen = nullptr;
    QAction* show_all = nullptr;

    // tool menu actions
    QAction* pointer_tool_action = nullptr;
    QAction* edit_tool_action = nullptr;
    QAction* ripple_tool_action = nullptr;
    QAction* razor_tool_action = nullptr;
    QAction* slip_tool_action = nullptr;
    QAction* slide_tool_action = nullptr;
    QAction* hand_tool_action = nullptr;
    QAction* transition_tool_action = nullptr;
    QAction* snap_toggle = nullptr;
    QAction* selecting_also_seeks = nullptr;
    QAction* edit_tool_also_seeks = nullptr;
    QAction* edit_tool_selects_links = nullptr;
    QAction* seek_to_end_of_pastes = nullptr;
    QAction* scroll_wheel_zooms = nullptr;
    QAction* rectified_waveforms = nullptr;
    QAction* enable_drag_files_to_timeline = nullptr;
    QAction* autoscale_by_default = nullptr;
    QAction* enable_seek_to_import = nullptr;
    QAction* enable_audio_scrubbing = nullptr;
    QAction* enable_drop_on_media_to_replace = nullptr;
    QAction* enable_hover_focus = nullptr;
    QAction* set_name_and_marker = nullptr;
    QAction* loop_action = nullptr;
    QAction* pause_at_out_point_action = nullptr;
    QAction* seek_also_selects;

    // edit menu actions
    QAction* undo_action = nullptr;
    QAction* redo_action = nullptr;

    bool enable_launch_with_project = false;
    QTimer autorecovery_timer;
    QString config_fn;
    QMenu* edit_menu_{nullptr};

    inline static MainWindow* instance_ = nullptr;
    void setupEditMenu(QMenuBar* menuBar);
};


#endif // MAINWINDOW_H
