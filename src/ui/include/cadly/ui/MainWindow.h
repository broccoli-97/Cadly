#pragma once

#include "cadly/cad/ICadImporter.h"
#include "cadly/scene/Scene.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>

#include <memory>

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QMenu;
class QSplitter;

namespace cadly::ui {

class DiagnosticsStrip;
class InspectorWidget;
class SidebarWidget;
class ToolbarButton;
class ToolbarWidget;
class ViewportWidget;

// The "Graphite" shell (docs/ui-redesign/design-notes.md): a 52px unified
// toolbar over QSplitter{sidebar | (viewport / diagnostics strip) |
// inspector} and a 26px status bar. Panels are fixed-position and toggle
// visibility only — the QDockWidget era (drag-docking, saveState blobs, the
// un-float hack) is gone; explicit QSettings keys persist the layout
// instead. Import is non-modal: progress lives in the toolbar's document
// capsule and the previous scene stays interactive throughout.
class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  // Recents / last-dir persistence lives in the app module (app::RecentFiles,
  // app::Settings); the shell only displays and reports.
  void set_recent_files(const QStringList& paths);
  void set_last_open_directory(const QString& dir);

  // Dev/test aid (paired with the app's --demo flag): drive a named UI state
  // — "wireframe", "light", "views", "getinfo", "zerochrome" — without a
  // human or an input-injection tool, so headless screenshot checks can
  // exercise the real action/popover code paths. No-op on unknown names.
  void run_demo(const QString& name);

signals:
  void file_imported(const QString& path);  // emitted on successful import
  void recents_clear_requested();

public slots:
  void open_file();                         // file dialog
  void open_file(const QString& path);      // honours "review before import"
  void open_file_with_options();            // always shows the pre-flight

protected:
  void closeEvent(QCloseEvent* e) override;
  void dragEnterEvent(QDragEnterEvent* e) override;
  void dropEvent(QDropEvent* e) override;
  // Keeps the floating HUD cluster pinned to the viewport's top-right.
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  void on_toggle_wireframe(bool on);
  void on_toggle_perspective(bool on);
  void on_zero_chrome(bool on);
  void on_about();

private:
  void build_actions();
  void build_menus();
  void build_shell();
  void build_status_bar();
  void refresh_theme();
  void update_display_mode();
  void update_status_for_scene();
  void rebuild_recents_menu();
  void show_views_popover(QWidget* anchor);

  // Non-modal import: QtConcurrent worker + 33ms poll into the capsule.
  void start_import(const QString& path, const cad::ImportOptions& opts);
  void finish_import(const QString& path, const cad::ImportResult& result);
  void update_capsule_document();
  bool run_preflight_dialog(cad::ImportOptions& opts);

  void load_settings();
  void save_settings() const;

  // --- widgets ---------------------------------------------------------
  ToolbarWidget*    toolbar_{nullptr};
  SidebarWidget*    sidebar_{nullptr};
  ViewportWidget*   viewport_{nullptr};
  DiagnosticsStrip* strip_{nullptr};
  InspectorWidget*  inspector_{nullptr};
  QSplitter*        split_h_{nullptr};
  QSplitter*        split_v_{nullptr};
  QWidget*          hud_{nullptr};
  ToolbarButton*    hud_views_{nullptr};

  QLabel* status_path_{nullptr};
  QLabel* status_stats_{nullptr};
  QLabel* status_frame_{nullptr};

  // --- actions -----------------------------------------------------------
  QAction* act_open_{nullptr};
  QAction* act_open_with_options_{nullptr};
  QAction* act_quit_{nullptr};
  QAction* act_fit_{nullptr};
  QAction* act_wireframe_{nullptr};
  QAction* act_edges_{nullptr};
  QAction* act_triangle_mesh_{nullptr};
  QAction* act_perspective_{nullptr};
  QAction* act_toggle_sidebar_{nullptr};
  QAction* act_toggle_inspector_{nullptr};
  QAction* act_toggle_strip_{nullptr};
  QAction* act_zero_chrome_{nullptr};
  QAction* act_theme_dark_{nullptr};
  QAction* act_about_{nullptr};
  QList<QAction*> view_actions_;   // Front…Iso + Fit, for the Views popover
  QMenu* recents_menu_{nullptr};

  // --- state -------------------------------------------------------------
  std::shared_ptr<scene::Scene> scene_;
  QString     current_path_;
  QString     last_open_dir_;
  QStringList recent_files_;

  bool importing_{false};
  std::shared_ptr<class GuiImportSink> import_sink_;  // defined in the .cpp
  QPointer<QFutureWatcher<cad::ImportResult>> import_watcher_;

  // Edges/Mesh are disabled-but-remembered while Wireframe is active.
  bool remembered_edges_{true};
  bool remembered_mesh_{false};

  // Zero-chrome (^.) remembers which panels were visible.
  bool zc_sidebar_{true};
  bool zc_inspector_{true};
  bool zc_strip_{false};
};

} // namespace cadly::ui
