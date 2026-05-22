#include "cadly/ui/MainWindow.h"

#include "cadly/ui/CameraController.h"
#include "cadly/ui/ImportDiagnosticsDock.h"
#include "cadly/ui/ImportOptionsDialog.h"
#include "cadly/ui/ModelTreeDock.h"
#include "cadly/ui/PropertiesDock.h"
#include "cadly/ui/ViewportWidget.h"

#include "cadly/cad/ImporterRegistry.h"
#include "cadly/platform/Log.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QIcon>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QtConcurrent>

#include <atomic>
#include <chrono>
#include <memory>

namespace cadly::ui {

namespace {

// Sink bridge: shared between the worker thread and the GUI's QProgressDialog.
// All members are atomic / std-thread-safe; QString operations stay on the GUI
// thread via QMetaObject::invokeMethod queued connections.
class GuiProgressSink final : public cad::IProgressSink {
public:
  std::atomic<float> fraction{0.0f};
  std::atomic<bool>  cancelled_flag{false};
  std::string        latest_message;

  void update(float f, const std::string& msg) override {
    fraction.store(f, std::memory_order_relaxed);
    latest_message = msg;
  }
  bool cancelled() const override {
    return cancelled_flag.load(std::memory_order_relaxed);
  }
};

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(tr("Cadly"));
  resize(1400, 900);

  viewport_ = new ViewportWidget(this);
  setCentralWidget(viewport_);

  build_menus();
  build_toolbar();
  build_status_bar();
  build_docks();
}

MainWindow::~MainWindow() = default;

void MainWindow::build_menus() {
  auto* file_menu = menuBar()->addMenu(tr("&File"));
  act_open_ = new QAction(tr("&Open CAD file..."), this);
  act_open_->setShortcut(QKeySequence::Open);
  connect(act_open_, &QAction::triggered, this, QOverload<>::of(&MainWindow::open_file));
  file_menu->addAction(act_open_);
  file_menu->addSeparator();
  act_quit_ = new QAction(tr("&Quit"), this);
  act_quit_->setShortcut(QKeySequence::Quit);
  connect(act_quit_, &QAction::triggered, qApp, &QApplication::quit);
  file_menu->addAction(act_quit_);

  auto* view_menu = menuBar()->addMenu(tr("&View"));
  act_fit_ = new QAction(tr("Fit to model"), this);
  act_fit_->setShortcut(Qt::Key_F);
  connect(act_fit_, &QAction::triggered, this, &MainWindow::on_fit_view);
  view_menu->addAction(act_fit_);

  act_wireframe_ = new QAction(tr("Wireframe"), this);
  act_wireframe_->setCheckable(true);
  act_wireframe_->setShortcut(Qt::Key_W);
  act_wireframe_->setToolTip(
    tr("Show only the BRep wireframe (analytical edges, auto-refined on zoom)"));
  connect(act_wireframe_, &QAction::toggled, this, &MainWindow::on_toggle_wireframe);
  view_menu->addAction(act_wireframe_);

  act_edges_ = new QAction(tr("Show edges"), this);
  act_edges_->setCheckable(true);
  act_edges_->setChecked(false);
  act_edges_->setShortcut(Qt::Key_E);
  act_edges_->setToolTip(tr("Overlay the BRep edges of the model"));
  connect(act_edges_, &QAction::toggled, this, &MainWindow::on_toggle_edges);
  view_menu->addAction(act_edges_);

  act_triangle_mesh_ = new QAction(tr("Show triangle mesh"), this);
  act_triangle_mesh_->setCheckable(true);
  act_triangle_mesh_->setChecked(false);
  act_triangle_mesh_->setShortcut(Qt::Key_T);
  act_triangle_mesh_->setToolTip(
    tr("Debug overlay: draw every triangle edge of the face triangulation"));
  connect(act_triangle_mesh_, &QAction::toggled,
          this, &MainWindow::on_toggle_triangle_mesh);
  view_menu->addAction(act_triangle_mesh_);

  view_menu->addSeparator();
  act_perspective_ = new QAction(tr("Perspective projection"), this);
  act_perspective_->setCheckable(true);
  act_perspective_->setChecked(false);  // default is orthographic
  act_perspective_->setShortcut(Qt::Key_P);
  act_perspective_->setToolTip(
    tr("Toggle between orthographic (default) and perspective projection"));
  connect(act_perspective_, &QAction::toggled,
          this, &MainWindow::on_toggle_perspective);
  view_menu->addAction(act_perspective_);

  auto* help_menu = menuBar()->addMenu(tr("&Help"));
  act_about_ = new QAction(tr("About Cadly"), this);
  connect(act_about_, &QAction::triggered, this, &MainWindow::on_about);
  help_menu->addAction(act_about_);
}

void MainWindow::build_toolbar() {
  auto* tb = addToolBar(tr("Main"));
  tb->setObjectName("MainToolbar");
  tb->setMovable(false);
  tb->setIconSize({20, 20});
  tb->addAction(act_open_);
  tb->addSeparator();
  tb->addAction(act_fit_);
  tb->addAction(act_wireframe_);
  tb->addAction(act_edges_);
  tb->addAction(act_triangle_mesh_);
}

void MainWindow::build_status_bar() {
  status_file_  = new QLabel(tr("No file loaded"));
  status_stats_ = new QLabel("");
  statusBar()->addWidget(status_file_, 1);
  statusBar()->addPermanentWidget(status_stats_);
}

void MainWindow::build_docks() {
  tree_dock_  = new ModelTreeDock(this);
  props_dock_ = new PropertiesDock(this);
  diag_dock_  = new ImportDiagnosticsDock(this);

  addDockWidget(Qt::LeftDockWidgetArea,  tree_dock_);
  addDockWidget(Qt::RightDockWidgetArea, props_dock_);
  addDockWidget(Qt::BottomDockWidgetArea, diag_dock_);

  connect(tree_dock_, &ModelTreeDock::node_activated,
          props_dock_, &PropertiesDock::show_node);
}

void MainWindow::open_file() {
  const auto start_dir = QStandardPaths::writableLocation(
    QStandardPaths::DocumentsLocation);
  const auto path = QFileDialog::getOpenFileName(
    this, tr("Open CAD file"), start_dir,
    tr("CAD files (*.step *.stp *.iges *.igs);;All files (*)"));
  if (path.isEmpty()) return;
  open_file(path);
}

void MainWindow::open_file(const QString& path) {
  ImportOptionsDialog dlg(this);
  if (dlg.exec() != QDialog::Accepted) return;
  start_import(path, dlg.options());
}

void MainWindow::start_import(const QString& path,
                              const cad::ImportOptions& opts) {
  auto sink = std::make_shared<GuiProgressSink>();
  // Capture options by value so the worker thread sees a stable copy.
  const auto path_std = std::filesystem::path(path.toStdString());

  progress_dialog_ = new QProgressDialog(
    tr("Importing %1...").arg(QFileInfo(path).fileName()),
    tr("Cancel"), 0, 100, this);
  progress_dialog_->setWindowModality(Qt::ApplicationModal);
  progress_dialog_->setMinimumDuration(0);
  progress_dialog_->setAutoClose(false);
  progress_dialog_->setAutoReset(false);
  progress_dialog_->setValue(0);
  connect(progress_dialog_, &QProgressDialog::canceled, this,
          [sink]() { sink->cancelled_flag.store(true); });

  auto* watcher = new QFutureWatcher<cad::ImportResult>(this);
  connect(watcher, &QFutureWatcher<cad::ImportResult>::finished,
          this, [this, watcher]() {
    cad::ImportResult result = watcher->result();
    watcher->deleteLater();
    if (progress_dialog_) {
      progress_dialog_->close();
      progress_dialog_->deleteLater();
    }

    diag_dock_->show_summary(result.summary);

    if (!result.success || !result.scene) {
      QMessageBox::warning(this, tr("Import failed"),
        tr("Could not import the requested file. See the diagnostics dock."));
      return;
    }
    scene_ = std::move(result.scene);
    viewport_->set_scene(scene_);
    tree_dock_->set_scene(scene_);
    props_dock_->set_scene(scene_);
    update_status_for_scene();
  });

  // Poll the sink ~30 Hz to drive the progress dialog. Light, no extra threads.
  auto* poll = new QTimer(this);
  poll->setInterval(33);
  connect(poll, &QTimer::timeout, this, [this, sink, poll]() {
    if (!progress_dialog_) { poll->stop(); poll->deleteLater(); return; }
    progress_dialog_->setValue(
      static_cast<int>(sink->fraction.load() * 100.0f));
    if (!sink->latest_message.empty()) {
      progress_dialog_->setLabelText(
        QString::fromStdString(sink->latest_message));
    }
  });
  poll->start();

  CADLY_LOG_INFO("Importing {}", path.toStdString());
  auto future = QtConcurrent::run([path_std, opts, sink]() -> cad::ImportResult {
    return cad::ImporterRegistry::instance().import(path_std, opts, sink.get());
  });
  watcher->setFuture(future);
}

void MainWindow::update_status_for_scene() {
  if (!scene_) {
    status_file_->setText(tr("No file loaded"));
    status_stats_->setText("");
    return;
  }
  status_file_->setText(QString::fromStdString(scene_->source_file.string()));

  std::size_t triangles = 0;
  std::size_t vertices  = 0;
  for (const auto& m : scene_->meshes) {
    if (!m) continue;
    triangles += m->triangle_count();
    vertices  += m->vertices.size();
  }
  status_stats_->setText(QString("%1 nodes  ·  %2 tris  ·  %3 verts")
    .arg(scene_->nodes.size()).arg(triangles).arg(vertices));
}

void MainWindow::on_fit_view()        { viewport_->fit_view(); }
void MainWindow::on_toggle_wireframe(bool on) {
  // Wireframe, edge overlay, and triangle-mesh overlay all interact with
  // the shaded surface (or replace it). Triangle mesh in particular relies
  // on the filled surface to occlude back-of-part triangles; without it
  // those lines leak through the silhouette and the background shows
  // through, so the three modes are made mutually exclusive at the UI
  // layer. The first toggle turned on wins; the others are silently
  // unchecked through blocked signals so we don't bounce into their slots.
  if (on) {
    if (act_edges_ && act_edges_->isChecked()) {
      QSignalBlocker block(act_edges_);
      act_edges_->setChecked(false);
    }
    if (act_triangle_mesh_ && act_triangle_mesh_->isChecked()) {
      QSignalBlocker block(act_triangle_mesh_);
      act_triangle_mesh_->setChecked(false);
    }
  }
  auto mode = viewport_->display_mode();
  mode.wireframe = on;
  if (on) {
    mode.show_edges         = false;
    mode.show_triangle_mesh = false;
  }
  viewport_->set_display_mode(mode);
}
void MainWindow::on_toggle_triangle_mesh(bool on) {
  // Triangle-mesh overlay needs the shaded surface to occlude back-of-part
  // triangles, so it cannot coexist with wireframe mode (which skips that
  // surface). Force wireframe off when this toggle is turned on.
  if (on && act_wireframe_ && act_wireframe_->isChecked()) {
    QSignalBlocker block(act_wireframe_);
    act_wireframe_->setChecked(false);
  }
  auto mode = viewport_->display_mode();
  mode.show_triangle_mesh = on;
  if (on) mode.wireframe = false;
  viewport_->set_display_mode(mode);
}
void MainWindow::on_toggle_perspective(bool on) {
  auto* ctrl = viewport_->camera_controller();
  if (!ctrl) return;
  ctrl->camera().projection_mode =
    on ? scene::Projection::Perspective : scene::Projection::Orthographic;
  viewport_->update();
}
void MainWindow::on_toggle_edges(bool on) {
  if (on && act_wireframe_ && act_wireframe_->isChecked()) {
    QSignalBlocker block(act_wireframe_);
    act_wireframe_->setChecked(false);
  }
  auto mode = viewport_->display_mode();
  mode.show_edges = on;
  if (on) mode.wireframe = false;
  viewport_->set_display_mode(mode);
}

void MainWindow::on_about() {
  QMessageBox::about(this, tr("About Cadly"),
    tr("<b>Cadly</b><br>Native C++ CAD viewer.<br>"
       "STEP/IGES via OCCT, OpenGL 4.1 PBR renderer.<br>"
       "Qt %1.").arg(QT_VERSION_STR));
}

} // namespace cadly::ui
