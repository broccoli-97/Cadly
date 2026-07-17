#pragma once

#include "cadly/cad/ICadImporter.h"
#include "cadly/scene/Scene.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>

#include <memory>
#include <vector>

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QMenu;
class QProgressBar;
class QSplitter;
class QTabBar;
class QToolButton;

namespace cadly::ui {

class DiagnosticsStrip;
class InspectorWidget;
class SidebarWidget;
class ToolbarButton;
class ToolbarWidget;
class ViewportWidget;
struct DocumentState;

// The "Graphite" shell (docs/ui-redesign/design-notes.md): a 52px unified
// toolbar and document tabs over QSplitter{sidebar | (viewport / diagnostics
// strip) | inspector} and a 26px status bar. Panels are fixed-position and
// toggle visibility only — the QDockWidget era (drag-docking, saveState blobs,
// the un-float hack) is gone; explicit QSettings keys persist the layout. Each
// open file owns a document tab; the OpenGL viewport is shared and re-attached
// to the active document's scene.
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
  // — "wireframe", "hiddenline", "light", "display", "import", "views",
  // "getinfo", "zerochrome" — without a human or an input-injection tool,
  // so headless screenshot checks can exercise real action/popover code paths.
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
  void on_toggle_perspective(bool on);
  void on_zero_chrome(bool on);
  void on_about();

private:
  enum class SurfaceMode { Shaded = 0, HiddenLine = 1, Wireframe = 2 };

  void build_actions();
  void build_menus();
  void build_shell();
  void build_status_bar();
  void refresh_theme();
  void set_surface_mode(SurfaceMode mode);
  void update_display_mode();
  void update_status_for_scene();
  void rebuild_recents_menu();
  void show_views_popover(QWidget* anchor);

  DocumentState* active_document() const;
  DocumentState* find_document(const QString& path) const;
  DocumentState* add_document(const QString& path);
  int document_index(const DocumentState* document) const;
  void activate_document(int index);
  void close_document(int index);
  void update_document_tab(DocumentState* document);
  void set_import_controls_visible(bool visible);

  // Non-modal import: QtConcurrent worker + 33ms progress polling.
  void start_import(DocumentState* document, const cad::ImportOptions& opts);
  void finish_import(DocumentState* document, const cad::ImportResult& result);
  bool run_preflight_dialog(cad::ImportOptions& opts);

  void load_settings();
  void save_settings() const;

  // --- widgets ---------------------------------------------------------
  ToolbarWidget*    toolbar_{nullptr};
  QTabBar*          tabs_{nullptr};
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
  QProgressBar* import_progress_{nullptr};
  QToolButton*  import_cancel_{nullptr};

  // --- actions -----------------------------------------------------------
  QAction* act_open_{nullptr};
  QAction* act_open_with_options_{nullptr};
  QAction* act_close_tab_{nullptr};
  QAction* act_quit_{nullptr};
  QAction* act_fit_{nullptr};
  QAction* act_wireframe_{nullptr};
  QAction* act_hidden_line_{nullptr};
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
  std::vector<std::unique_ptr<DocumentState>> documents_;
  QString     last_open_dir_;
  QStringList recent_files_;

  bool importing_{false};
  DocumentState* importing_document_{nullptr};
  std::shared_ptr<class GuiImportSink> import_sink_;  // defined in the .cpp
  QPointer<QFutureWatcher<cad::ImportResult>> import_watcher_;

  // Edges/Mesh are disabled-but-remembered in non-shaded surface modes.
  SurfaceMode surface_mode_{SurfaceMode::Shaded};
  bool remembered_edges_{true};
  bool remembered_mesh_{false};

  // Zero-chrome (^.) remembers which panels were visible.
  bool zc_sidebar_{true};
  bool zc_inspector_{true};
  bool zc_strip_{false};
};

} // namespace cadly::ui
