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
class PreferencesDialog;
class IsolateBanner;
class SectionBanner;
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

  // Reflects the persisted UI-language choice ("system", "en", "zh_CN") in
  // the View ▸ Language menu. Persistence is app-side, like recents; a menu
  // selection is reported via language_selected() and only takes effect on
  // the next launch (the shell offers a relaunch).
  void set_language(const QString& code);

  // Dev/test aid (paired with the app's --demo flag): drive a named UI state
  // — "wireframe", "hiddenline", "shaded-orbit:<yaw>,<pitch>",
  //   "shadedmenu", "light", "display", "section",
  //   "section:<offset-fraction>", "section-behind",
  // "import", "views", "getinfo", "zerochrome" — without input injection,
  // so headless screenshot checks can exercise real action/popover code paths.
  void run_demo(const QString& name);

signals:
  void file_imported(const QString& path);  // emitted on successful import
  void recents_clear_requested();
  void language_selected(const QString& code);  // "system" | "en" | "zh_CN"

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
  void on_isolate_changed(std::uint32_t isolate_node);
  void on_section_toggled(bool on);
  void on_about();
  void open_preferences();

private:
  enum class SurfaceMode { Shaded = 0, HiddenLine = 1, Wireframe = 2 };

  void build_actions();
  void build_menus();
  void build_shell();
  void build_status_bar();
  void refresh_theme();
  // "action/fullscreen" or its -exit variant, per the zero-chrome state.
  QString zero_chrome_icon_name() const;
  void set_surface_mode(SurfaceMode mode);
  void update_display_mode();
  void update_status_for_scene();
  void rebuild_recents_menu();
  void show_views_popover(QWidget* anchor);
  // Keep the isolate banner centred over the viewport's top edge.
  void position_isolate_banner();
  // Section banner sits bottom-centre, so it never collides with the isolate
  // banner (top-centre) or the HUD (top-right) when several modes are on.
  void position_section_banner();
  // Action lifecycle + banner + display refresh for section mode. Split out of
  // on_section_toggled() so restoring a tab's state never re-runs the
  // first-entry plane setup and clobbers the plane being restored.
  void apply_section_mode_ui(bool on);
  // Point the section plane along `normal`, re-centred on the model. Backs the
  // axis presets and Align to View.
  void set_section_normal(const scene::vec3& normal, bool recentre);
  // Re-derive which axis preset (if any) is checked from the plane's normal.
  // The rotate rings can leave it on no axis at all.
  void sync_section_axis_actions();
  // Refresh the banner's live readout from the current plane, and the enabled
  // state of the commands that depend on it.
  void update_section_banner();

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
  PreferencesDialog* prefs_{nullptr};  // lazily created by open_preferences()
  QSplitter*        split_h_{nullptr};
  QSplitter*        split_v_{nullptr};
  QWidget*          hud_{nullptr};
  ToolbarButton*    hud_views_{nullptr};
  // Floating capsule over the viewport while isolate mode is active; carries
  // the only always-visible way back out (the Back button).
  IsolateBanner*    isolate_banner_{nullptr};
  // Same idea for section mode: live offset readout, Flip, and Exit, pinned to
  // the viewport's bottom edge.
  SectionBanner*    section_banner_{nullptr};

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
  // Hidden Line's one option: show occluded edges dimmed (Creo "Hidden
  // Line") vs removed ("No Hidden"). Lives in the segment's chevron menu.
  QAction* act_hidden_dimmed_{nullptr};
  QAction* act_perspective_{nullptr};
  // Hidden + disabled outside isolate mode; the banner's Back button and the
  // View menu entry both drive it.
  QAction* act_exit_isolate_{nullptr};
  // Isolate's one option: the parts outside the focus as a translucent ghost
  // veil (unchecked, default) or hidden outright (checked). Checkbox state is
  // the persisted preference itself; only enabled/visible track isolate mode.
  QAction* act_isolate_hide_others_{nullptr};
  // Section view (剖切). `act_section_` toggles the mode; the rest live in the
  // toolbar chip's chevron menu and the View ▸ Section submenu, and follow the
  // Dimmed-Hidden-Lines pattern: the checkable ones ARE the persisted
  // preference, so only their enabled/visible state tracks the mode.
  QAction* act_section_{nullptr};
  QAction* act_section_flip_{nullptr};
  QAction* act_section_reset_{nullptr};
  QAction* act_section_show_plane_{nullptr};
  QAction* act_section_hatch_{nullptr};
  QAction* act_section_axis_x_{nullptr};
  QAction* act_section_axis_y_{nullptr};
  QAction* act_section_axis_z_{nullptr};
  QAction* act_section_axis_view_{nullptr};
  QMenu*   section_menu_{nullptr};
  QAction* act_toggle_sidebar_{nullptr};
  QAction* act_toggle_inspector_{nullptr};
  QAction* act_toggle_strip_{nullptr};
  QAction* act_zero_chrome_{nullptr};
  QAction* act_theme_dark_{nullptr};
  QAction* act_lang_system_{nullptr};
  QAction* act_lang_english_{nullptr};
  QAction* act_lang_chinese_{nullptr};
  QAction* act_about_{nullptr};
  QAction* act_preferences_{nullptr};
  QList<QAction*> view_actions_;   // Front…Iso + Fit, for the Views popover
  QMenu* recents_menu_{nullptr};

  // --- state -------------------------------------------------------------
  std::vector<std::unique_ptr<DocumentState>> documents_;
  QString     last_open_dir_;
  QStringList recent_files_;
  QString     language_code_{QStringLiteral("system")};

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
