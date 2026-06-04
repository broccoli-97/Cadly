#pragma once

#include "cadly/cad/ICadImporter.h"
#include "cadly/scene/Scene.h"

#include <QMainWindow>
#include <QPointer>

#include <memory>

class QAction;
class QLabel;
class QProgressDialog;
class QShowEvent;

namespace cadly::ui {

class ImportDiagnosticsDock;
class ModelTreeDock;
class PropertiesDock;
class ViewportWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

protected:
  // Floating docks are disabled (see build_docks), but a window layout saved
  // by an older build can still carry a floating panel that restoreState()
  // reinstates before the first show. Un-float any such leftover once here so
  // the user is never stranded with a detached window.
  void showEvent(QShowEvent* e) override;

public slots:
  void open_file();
  void open_file(const QString& path);

private slots:
  void on_fit_view();
  void on_toggle_wireframe(bool on);
  void on_toggle_edges(bool on);
  void on_toggle_triangle_mesh(bool on);
  void on_toggle_perspective(bool on);
  void on_about();

private:
  void build_menus();
  void build_toolbar();
  void build_status_bar();
  void build_docks();
  void update_status_for_scene();

  // Async import — std::thread runs Importer::Import while the GUI shows
  // a cancellable QProgressDialog. Result is delivered on the GUI thread.
  void start_import(const QString& path, const cad::ImportOptions& opts);

  ViewportWidget*         viewport_{nullptr};
  ModelTreeDock*          tree_dock_{nullptr};
  PropertiesDock*         props_dock_{nullptr};
  ImportDiagnosticsDock*  diag_dock_{nullptr};

  QAction* act_open_{nullptr};
  QAction* act_quit_{nullptr};
  QAction* act_fit_{nullptr};
  QAction* act_wireframe_{nullptr};
  QAction* act_edges_{nullptr};
  QAction* act_triangle_mesh_{nullptr};
  QAction* act_perspective_{nullptr};
  QAction* act_about_{nullptr};

  QLabel*  status_stats_{nullptr};
  QLabel*  status_file_{nullptr};

  std::shared_ptr<scene::Scene> scene_;

  // Active import job state — only one at a time.
  QPointer<QProgressDialog> progress_dialog_;

  // Guards the one-shot un-float pass in showEvent().
  bool docks_unfloated_{false};
};

} // namespace cadly::ui
