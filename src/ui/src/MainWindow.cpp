#include "cadly/ui/MainWindow.h"

#include "cadly/ui/CameraController.h"
#include "cadly/ui/ThemeTokens.h"
#include "cadly/ui/NavigationScheme.h"
#include "cadly/ui/ViewportWidget.h"

#include "DiagnosticsStrip.h"
#include "IconUtils.h"
#include "ImportOptionsWidget.h"
#include "InspectorWidget.h"
#include "Popover.h"
#include "SegmentedControl.h"
#include "SidebarWidget.h"
#include "ToolTip.h"
#include "ToolbarButton.h"
#include "ToolbarWidget.h"
#include "ViewsGrid.h"

#include "cadly/cad/ImporterRegistry.h"
#include "cadly/platform/Log.h"

#include <QAction>
#include <QAbstractButton>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QProcess>
#include <QProgressBar>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>

namespace cadly::ui {

// Sink bridge shared between the worker thread and the GUI progress poll.
// Declared (not defined) in MainWindow.h so the shared_ptr member works;
// defined at namespace scope here. The message is mutex-guarded — the old
// GuiProgressSink wrote the std::string from the worker while the GUI timer
// read it, which was a quiet data race.
class GuiImportSink final : public cad::IProgressSink {
public:
  std::atomic<float> fraction{0.0f};
  std::atomic<bool>  cancelled_flag{false};

  void update(float f, const std::string& msg) override {
    fraction.store(f, std::memory_order_relaxed);
    const std::lock_guard<std::mutex> lock(mutex_);
    latest_message_ = msg;
  }
  bool cancelled() const override {
    return cancelled_flag.load(std::memory_order_relaxed);
  }
  QString message() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return QString::fromStdString(latest_message_);
  }

private:
  mutable std::mutex mutex_;
  std::string        latest_message_;
};

struct DocumentState {
  QString path;
  std::shared_ptr<scene::Scene> scene;
  cad::ImportSummary summary;
  bool has_summary{false};
  bool loading{false};
  bool failed{false};
  bool camera_initialized{false};
  bool close_requested{false};
  // Isolate focus for this tab (kInvalid = full assembly). The ghost flags
  // themselves live in the scene's nodes, but the sidebar resets them on
  // every scene handover, so the shell re-applies this on tab activation.
  std::uint32_t isolate_node{scene::Node::kInvalid};
};

// Floating capsule pinned over the viewport's top-centre while isolate mode
// is active: part glyph + "Isolating <name>" + the Hide Others style toggle
// + the Back button. This is the mode's one guaranteed exit affordance
// (menus and panels can all be hidden, e.g. in zero-chrome), so it lives on
// the viewport itself, painted with the same capsule recipe as the HUD
// cluster so the two read as one family. The style toggle rides along for
// the same reason: it only means anything while isolate is on, so its home
// is the isolate chrome, not the toolbar.
class IsolateBanner final : public QWidget {
public:
  IsolateBanner(QAction* back_action, QAction* hide_others_action,
                QWidget* parent) : QWidget(parent) {
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(11, 4, 5, 4);
    lay->setSpacing(7);
    icon_ = new QLabel(this);
    icon_->setFixedSize(15, 15);
    lay->addWidget(icon_);
    label_ = new QLabel(this);
    label_->setFont(ui_font(12, QFont::DemiBold));
    lay->addWidget(label_);
    // Checkable chip, accent-filled while the "hide others" style is on —
    // the same checked treatment as the toolbar's display-mode chips, so
    // the active style is readable at a glance.
    auto* hide_others = new ToolbarButton(this);
    hide_others->setDefaultAction(hide_others_action);
    hide_others->set_show_text(true);
    hide_others->set_emphasis(ToolbarButton::Emphasis::Accent);
    lay->addWidget(hide_others);
    auto* back = new ToolbarButton(this);
    back->setDefaultAction(back_action);
    back->set_show_text(true);
    back->set_emphasis(ToolbarButton::Emphasis::Accent);
    lay->addWidget(back);
    connect(&ThemeManager::instance(), &ThemeManager::changed,
            this, [this]() { refresh_theme(); });
    refresh_theme();
  }

  void set_part_name(const QString& name) {
    const QFontMetrics fm(label_->font());
    // Not tr(): without Q_OBJECT this class inherits QWidget's tr(), whose
    // runtime context ("QWidget") would never match what lupdate extracts.
    // translate() pins the context explicitly instead of adding moc churn
    // for one string.
    label_->setText(QCoreApplication::translate("IsolateBanner", "Isolating %1")
      .arg(fm.elidedText(name, Qt::ElideMiddle, 240)));
    adjustSize();
  }

protected:
  void paintEvent(QPaintEvent*) override {
    const auto& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor bg = t.toolbar_bg;
    bg.setAlpha(225);
    p.setPen(QPen(t.hairline_soft, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 8, 8);
  }

private:
  void refresh_theme() {
    const auto& t = tokens();
    // The accent-tinted part glyph is the banner's link to the sidebar row
    // glyphs (accent when selected) and to the highlight colour in the
    // viewport.
    icon_->setPixmap(themed_icon(QStringLiteral("shape/cube"),
                                 t.accent, t.accent).pixmap(15, 15));
    QPalette pal = label_->palette();
    pal.setColor(QPalette::WindowText, t.text1);
    label_->setPalette(pal);
    update();
  }

  QLabel* icon_{nullptr};
  QLabel* label_{nullptr};
};

namespace {

QSettings ui_settings() {
  // Same ini store the app module uses (app::Settings/RecentFiles), so the
  // user has one settings file. C++17 guaranteed copy elision makes the
  // by-value return construct in place.
  return QSettings(QSettings::IniFormat, QSettings::UserScope,
                   QStringLiteral("Cadly"), QStringLiteral("Cadly"));
}

scene::vec3 to_vec3(const QColor& c) {
  return {static_cast<float>(c.redF()),
          static_cast<float>(c.greenF()),
          static_cast<float>(c.blueF())};
}

bool is_cad_file(const QString& path) {
  const QString s = QFileInfo(path).suffix().toLower();
  return s == QLatin1String("step") || s == QLatin1String("stp") ||
         s == QLatin1String("iges") || s == QLatin1String("igs");
}

void save_import_options(QSettings& s, const cad::ImportOptions& o) {
  s.beginGroup(QStringLiteral("import"));
  s.setValue("mode", o.tessellation_mode == cad::TessellationMode::Absolute
                       ? "absolute" : "visual");
  s.setValue("linear_deflection",       o.linear_deflection);
  s.setValue("angular_deflection",      o.angular_deflection);
  s.setValue("target_screen_error_px",  o.target_screen_error_px);
  s.setValue("reference_screen_pixels", o.reference_screen_pixels);
  s.setValue("min_linear_deflection",   o.min_linear_deflection);
  s.setValue("max_relative_deflection", o.max_relative_deflection);
  s.setValue("relative_deflection",     o.relative_deflection);
  s.setValue("parallel_meshing",        o.parallel_meshing);
  s.setValue("load_colors",             o.load_colors);
  s.setValue("load_names",              o.load_names);
  s.endGroup();
}

cad::ImportOptions load_import_options(QSettings& s) {
  cad::ImportOptions o;   // backend defaults fill anything missing
  s.beginGroup(QStringLiteral("import"));
  o.tessellation_mode =
    s.value("mode", "visual").toString() == QLatin1String("absolute")
      ? cad::TessellationMode::Absolute
      : cad::TessellationMode::VisualRelative;
  o.linear_deflection  = s.value("linear_deflection",  o.linear_deflection).toDouble();
  o.angular_deflection = s.value("angular_deflection", o.angular_deflection).toDouble();
  o.target_screen_error_px =
    s.value("target_screen_error_px", o.target_screen_error_px).toDouble();
  o.reference_screen_pixels =
    s.value("reference_screen_pixels", o.reference_screen_pixels).toDouble();
  o.min_linear_deflection =
    s.value("min_linear_deflection", o.min_linear_deflection).toDouble();
  o.max_relative_deflection =
    s.value("max_relative_deflection", o.max_relative_deflection).toDouble();
  o.relative_deflection = s.value("relative_deflection", o.relative_deflection).toBool();
  o.parallel_meshing    = s.value("parallel_meshing",    o.parallel_meshing).toBool();
  o.load_colors    = s.value("load_colors",    o.load_colors).toBool();
  o.load_names     = s.value("load_names",     o.load_names).toBool();
  s.endGroup();
  return o;
}

// Small floating cluster over the viewport's top-right: Views popover, Fit,
// projection toggle. Mirrors toolbar actions so it works in zero-chrome.
class ViewportHud final : public QWidget {
public:
  ViewportHud(QWidget* parent) : QWidget(parent) {
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(5, 4, 5, 4);
    lay->setSpacing(2);
  }
  void add(QWidget* w) { layout()->addWidget(w); }

protected:
  void paintEvent(QPaintEvent*) override {
    const auto& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor bg = t.toolbar_bg;
    bg.setAlpha(215);
    p.setPen(QPen(t.hairline_soft, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 8, 8);
  }
};

class TabCloseButton final : public QAbstractButton {
public:
  explicit TabCloseButton(QWidget* parent) : QAbstractButton(parent) {
    setFixedSize(18, 18);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    connect(&ThemeManager::instance(), &ThemeManager::changed,
            this, QOverload<>::of(&QWidget::update));
  }

protected:
  void paintEvent(QPaintEvent*) override {
    const auto& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (underMouse()) {
      p.setPen(Qt::NoPen);
      p.setBrush(t.control_active);
      p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }
    QPen pen(isEnabled() ? (underMouse() ? t.text1 : t.text3) : t.text3, 1.35);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QPointF(5.5, 5.5), QPointF(12.5, 12.5));
    p.drawLine(QPointF(12.5, 5.5), QPointF(5.5, 12.5));
  }
};

class DocumentTabBar final : public QTabBar {
public:
  explicit DocumentTabBar(QWidget* parent) : QTabBar(parent) {
    setMouseTracking(true);
    connect(&ThemeManager::instance(), &ThemeManager::changed,
            this, QOverload<>::of(&QWidget::update));
  }

protected:
  QSize tabSizeHint(int index) const override {
    const QFontMetrics fm(ui_font(12, QFont::Medium));
    const int text_width = fm.horizontalAdvance(tabText(index));
    return {std::clamp(text_width + 62, 180, 240), 32};
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    const int next = tabAt(event->pos());
    if (next != hover_index_) {
      hover_index_ = next;
      update();
    }
    QTabBar::mouseMoveEvent(event);
  }

  void leaveEvent(QEvent* event) override {
    hover_index_ = -1;
    update();
    QTabBar::leaveEvent(event);
  }

  void paintEvent(QPaintEvent*) override {
    const auto& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), t.toolbar_bg);
    p.fillRect(QRect(0, height() - 1, width(), 1), t.hairline);

    auto draw_tab = [&](int index) {
      QRectF r = tabRect(index);
      r.adjust(1.0, 3.0, -1.0, 0.0);
      if (!r.intersects(rect())) return;
      const bool active = index == currentIndex();
      const bool hovered = index == hover_index_;

      QPainterPath shape;
      shape.moveTo(r.left(), r.bottom());
      shape.lineTo(r.left(), r.top() + 7.0);
      shape.quadTo(r.left(), r.top(), r.left() + 7.0, r.top());
      shape.lineTo(r.right() - 7.0, r.top());
      shape.quadTo(r.right(), r.top(), r.right(), r.top() + 7.0);
      shape.lineTo(r.right(), r.bottom());
      shape.closeSubpath();

      if (active || hovered) {
        p.setPen(active ? QPen(t.hairline_soft, 1.0) : Qt::NoPen);
        p.setBrush(active ? t.sidebar_bg : t.control_hover);
        p.drawPath(shape);
      }

      const QString title = tabText(index);
      const bool failed = title.contains(QStringLiteral("(failed)"));
      const QColor icon_color = failed ? t.error : active ? t.accent : t.text3;
      const QRect icon_rect(static_cast<int>(r.left()) + 10,
                            static_cast<int>(r.center().y()) - 7, 14, 14);
      themed_icon(QStringLiteral("shape/cube"), icon_color, icon_color)
        .paint(&p, icon_rect);

      const int left = icon_rect.right() + 7;
      const int right_pad = tabButton(index, QTabBar::RightSide) ? 30 : 10;
      const QRect text_rect(left, static_cast<int>(r.top()),
                            static_cast<int>(r.right()) - left - right_pad,
                            static_cast<int>(r.height()));
      p.setFont(ui_font(12, active ? QFont::Medium : QFont::Normal));
      p.setPen(failed ? t.error : active ? t.text1 : t.text2);
      const QFontMetrics fm(p.font());
      p.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter,
                 fm.elidedText(title, Qt::ElideMiddle, text_rect.width()));
    };

    for (int i = 0; i < count(); ++i) {
      if (i != currentIndex()) draw_tab(i);
    }
    if (currentIndex() >= 0) draw_tab(currentIndex());
  }

private:
  int hover_index_{-1};
};

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("Cadly"));
  resize(1480, 920);
  setAcceptDrops(true);

  // One app-level filter bounds every widget tooltip at show time; call
  // sites keep plain strings (see ToolTip.h).
  install_bounded_tooltips();

  build_actions();
  build_shell();
  build_menus();
  build_status_bar();

  // Esc is the instinctive way out of a modal-ish view state. Isolate is
  // the more nested state, so it unwinds first; a second Esc then restores
  // the panels (the actions' own shortcuts also work).
  auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(esc, &QShortcut::activated, this, [this]() {
    if (sidebar_ && sidebar_->isolate_node() != scene::Node::kInvalid) {
      sidebar_->set_isolate(scene::Node::kInvalid);
      return;
    }
    if (act_zero_chrome_->isChecked()) act_zero_chrome_->setChecked(false);
  });

  load_settings();
  refresh_theme();
  connect(&ThemeManager::instance(), &ThemeManager::changed,
          this, &MainWindow::refresh_theme);
  update_display_mode();
}

MainWindow::~MainWindow() = default;

void MainWindow::build_actions() {
  act_open_ = new QAction(tr("&Open CAD File…"), this);
  act_open_->setIconText(tr("Open"));
  act_open_->setShortcut(QKeySequence::Open);
  connect(act_open_, &QAction::triggered,
          this, QOverload<>::of(&MainWindow::open_file));

  act_open_with_options_ = new QAction(tr("Open with O&ptions…"), this);
  act_open_with_options_->setShortcut(
    QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_O));
  act_open_with_options_->setToolTip(
    tr("Choose a file and review the import options"));
  connect(act_open_with_options_, &QAction::triggered,
          this, &MainWindow::open_file_with_options);

  act_close_tab_ = new QAction(tr("&Close Tab"), this);
  act_close_tab_->setShortcut(QKeySequence::Close);
  act_close_tab_->setEnabled(false);
  connect(act_close_tab_, &QAction::triggered, this, [this]() {
    if (tabs_) close_document(tabs_->currentIndex());
  });

  act_quit_ = new QAction(tr("&Quit"), this);
  act_quit_->setShortcut(QKeySequence::Quit);
  connect(act_quit_, &QAction::triggered, qApp, &QApplication::quit);

  act_fit_ = new QAction(tr("&Fit to Model"), this);
  act_fit_->setIconText(tr("Fit"));
  act_fit_->setShortcut(Qt::Key_F);
  connect(act_fit_, &QAction::triggered,
          this, [this]() { viewport_->fit_view(); });

  // Only ever reachable while isolate mode is on (sidebar double-click);
  // hidden otherwise so the View menu and the banner don't advertise a
  // no-op. The banner's Back button shares this action.
  act_exit_isolate_ = new QAction(tr("Exit &Isolate"), this);
  act_exit_isolate_->setIconText(tr("Back"));
  act_exit_isolate_->setEnabled(false);
  act_exit_isolate_->setVisible(false);
  act_exit_isolate_->setToolTip(tr("Show the full assembly again (Esc)"));
  connect(act_exit_isolate_, &QAction::triggered, this, [this]() {
    if (sidebar_) sidebar_->set_isolate(scene::Node::kInvalid);
  });

  // Isolate's one option, mirroring the Dimmed Hidden Lines pattern: the
  // checkbox IS the persisted preference (display/isolate_hide_others) and
  // is never force-cleared — the renderer ignores it while nothing is
  // ghosted — so only the enabled/visible state tracks isolate mode (see
  // on_isolate_changed). Checked hides the parts outside the isolate focus
  // outright; unchecked (default) keeps them as the translucent ghost veil.
  act_isolate_hide_others_ = new QAction(tr("&Hide Other Parts"), this);
  act_isolate_hide_others_->setIconText(tr("Hide Others"));
  act_isolate_hide_others_->setCheckable(true);
  act_isolate_hide_others_->setChecked(false);
  act_isolate_hide_others_->setToolTip(
    tr("Hide the other parts entirely instead of ghosting them"));
  act_isolate_hide_others_->setEnabled(false);   // follows isolate mode
  act_isolate_hide_others_->setVisible(false);
  connect(act_isolate_hide_others_, &QAction::toggled,
          this, [this](bool) { update_display_mode(); });

  act_wireframe_ = new QAction(tr("&Wireframe"), this);
  act_wireframe_->setCheckable(true);
  act_wireframe_->setShortcut(Qt::Key_W);
  act_wireframe_->setToolTip(
    tr("Show BRep edges only; detail refines as you zoom"));
  connect(act_wireframe_, &QAction::triggered, this, [this](bool on) {
    set_surface_mode(on ? SurfaceMode::Wireframe : SurfaceMode::Shaded);
  });

  act_hidden_line_ = new QAction(tr("&Hidden Line"), this);
  act_hidden_line_->setCheckable(true);
  act_hidden_line_->setShortcut(Qt::Key_H);
  act_hidden_line_->setToolTip(
    tr("Show flat faces with hidden edges removed"));
  connect(act_hidden_line_, &QAction::triggered, this, [this](bool on) {
    set_surface_mode(on ? SurfaceMode::HiddenLine : SurfaceMode::Shaded);
  });

  act_edges_ = new QAction(tr("Show &Edges"), this);
  act_edges_->setIconText(tr("Edges"));
  act_edges_->setCheckable(true);
  act_edges_->setChecked(true);
  act_edges_->setShortcut(Qt::Key_E);
  act_edges_->setToolTip(tr("Overlay BRep edges on shaded faces"));
  connect(act_edges_, &QAction::toggled,
          this, [this](bool) { update_display_mode(); });

  act_triangle_mesh_ = new QAction(tr("Show &Triangle Mesh"), this);
  act_triangle_mesh_->setIconText(tr("Mesh"));
  act_triangle_mesh_->setCheckable(true);
  act_triangle_mesh_->setShortcut(Qt::Key_T);
  act_triangle_mesh_->setToolTip(
    tr("Show every face-triangulation edge for debugging"));
  connect(act_triangle_mesh_, &QAction::toggled,
          this, [this](bool) { update_display_mode(); });

  // Hidden Line's drafting-style choice, presented in the segment's chevron
  // menu the way Edges/Mesh hang off Shaded. Checked (default) is Creo's
  // "Hidden Line" style — occluded edges drawn dimmed; unchecked is the
  // stricter "No Hidden" — occluded edges removed. Unlike the Shaded chips
  // it is never force-cleared: the checkbox IS the persisted preference and
  // the renderer simply ignores it outside hidden-line mode, so only the
  // enabled state tracks the surface mode (see set_surface_mode).
  act_hidden_dimmed_ = new QAction(tr("&Dimmed Hidden Lines"), this);
  act_hidden_dimmed_->setIconText(tr("Dimmed"));
  act_hidden_dimmed_->setCheckable(true);
  act_hidden_dimmed_->setChecked(true);
  act_hidden_dimmed_->setToolTip(
    tr("Draw occluded edges dimmed instead of removing them"));
  act_hidden_dimmed_->setEnabled(false);   // startup surface mode is Shaded
  connect(act_hidden_dimmed_, &QAction::toggled,
          this, [this](bool) { update_display_mode(); });

  act_perspective_ = new QAction(tr("&Perspective Projection"), this);
  act_perspective_->setIconText(tr("Persp"));
  act_perspective_->setCheckable(true);
  act_perspective_->setChecked(false);  // default is orthographic
  act_perspective_->setShortcut(Qt::Key_P);
  act_perspective_->setToolTip(
    tr("Switch between orthographic and perspective projection"));
  connect(act_perspective_, &QAction::toggled,
          this, &MainWindow::on_toggle_perspective);

  // Standard view presets. The camera is reoriented but target+distance are
  // left untouched on purpose: the user can already reset framing with F.
  // The numbering follows Blender's numpad layout (1/3/7 for the three
  // primary axes) which the CAD audience tends to have in their fingers.
  auto add_view_action = [this](const QString& name, QKeySequence shortcut,
                                float yaw_deg, float pitch_deg) {
    auto* a = new QAction(name, this);
    a->setShortcut(shortcut);
    connect(a, &QAction::triggered, this, [this, yaw_deg, pitch_deg]() {
      if (auto* ctrl = viewport_ ? viewport_->camera_controller() : nullptr)
        ctrl->set_view(yaw_deg, pitch_deg);
    });
    view_actions_.append(a);
  };
  add_view_action(tr("&Front"),     Qt::Key_1,    0.0f,   0.0f);
  add_view_action(tr("&Back"),      Qt::Key_2,  180.0f,   0.0f);
  add_view_action(tr("&Right"),     Qt::Key_3,   90.0f,   0.0f);
  add_view_action(tr("&Left"),      Qt::Key_4,  -90.0f,   0.0f);
  add_view_action(tr("&Top"),       Qt::Key_5,    0.0f, -90.0f);
  add_view_action(tr("Bo&ttom"),    Qt::Key_6,    0.0f,  90.0f);
  add_view_action(tr("&Isometric"), Qt::Key_7,   30.0f, -22.0f);

  act_toggle_sidebar_ = new QAction(tr("&Model Tree"), this);
  act_toggle_sidebar_->setCheckable(true);
  act_toggle_sidebar_->setChecked(true);
  act_toggle_sidebar_->setToolTip(tr("Show or hide the model tree"));

  act_toggle_inspector_ = new QAction(tr("&Inspector"), this);
  act_toggle_inspector_->setCheckable(true);
  act_toggle_inspector_->setChecked(true);
  act_toggle_inspector_->setToolTip(tr("Show or hide the inspector"));

  act_toggle_strip_ = new QAction(tr("&Diagnostics"), this);
  act_toggle_strip_->setCheckable(true);
  act_toggle_strip_->setChecked(false);
  act_toggle_strip_->setToolTip(tr("Show or hide the import diagnostics"));

  act_zero_chrome_ = new QAction(tr("&Zero Chrome"), this);
  act_zero_chrome_->setCheckable(true);
#if defined(Q_OS_MACOS)
  // Qt swaps CTRL<->META on macOS, so Qt::CTRL|Period would surface as ⌘. —
  // the system-wide Cancel idiom (QKeySequence::Cancel). Qt::META binds the
  // physical Control key, giving the intended ⌃. without shadowing Cancel.
  act_zero_chrome_->setShortcut(
    QKeySequence(Qt::META | Qt::Key_Period));
#else
  act_zero_chrome_->setShortcut(
    QKeySequence(Qt::CTRL | Qt::Key_Period));
#endif
  act_zero_chrome_->setToolTip(
    tr("Hide all panels; press Esc to restore them"));
  connect(act_zero_chrome_, &QAction::toggled,
          this, &MainWindow::on_zero_chrome);

  act_theme_dark_ = new QAction(tr("&Dark Appearance"), this);
  act_theme_dark_->setCheckable(true);
  act_theme_dark_->setChecked(ThemeManager::instance().dark());
  act_theme_dark_->setToolTip(tr("Switch between the dark and light themes"));
  connect(act_theme_dark_, &QAction::toggled, this, [](bool on) {
    ThemeManager::instance().set_dark(on);
  });

  // Language switcher. Only the System entry is tr()'d: each language names
  // itself, so whatever language is active by accident or default, every
  // entry — and the way back — stays readable. Translators install once at
  // startup (the shell sets all its strings while being built, so a swap
  // now would only reach widgets created later); the honest offer is a
  // relaunch, not a half-translated window.
  act_lang_system_  = new QAction(tr("&System Language"), this);
  act_lang_english_ = new QAction(QStringLiteral("English"), this);
  act_lang_chinese_ = new QAction(QStringLiteral("简体中文"), this);
  act_lang_system_->setData(QStringLiteral("system"));
  act_lang_english_->setData(QStringLiteral("en"));
  act_lang_chinese_->setData(QStringLiteral("zh_CN"));
  auto* lang_group = new QActionGroup(this);
  for (QAction* a : {act_lang_system_, act_lang_english_, act_lang_chinese_}) {
    a->setCheckable(true);
    lang_group->addAction(a);
  }
  act_lang_system_->setChecked(true);
  connect(lang_group, &QActionGroup::triggered, this, [this](QAction* a) {
    const QString code = a->data().toString();
    if (code == language_code_) return;  // re-picking the active language
    language_code_ = code;
    // Direct connection in main.cpp persists the choice before question()
    // spins the event loop, so "Restart Now" relaunches into the new value.
    emit language_selected(code);
    QMessageBox box(QMessageBox::Question, tr("Change Language"),
                    tr("The new language will be used the next time Cadly "
                       "starts."),
                    QMessageBox::NoButton, this);
    auto* restart = box.addButton(tr("Restart Now"), QMessageBox::AcceptRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == restart) {
      QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
      close();
    }
  });

  act_about_ = new QAction(tr("About Cadly"), this);
  connect(act_about_, &QAction::triggered, this, &MainWindow::on_about);
}

void MainWindow::build_shell() {
  viewport_ = new ViewportWidget(this);
  viewport_->setMinimumSize(320, 240);
  viewport_->installEventFilter(this);
  connect(viewport_, &ViewportWidget::frame_timed, this, [this](float ms) {
    if (status_frame_) {
      status_frame_->setText(
        tr("frame %1 ms").arg(static_cast<double>(ms), 0, 'f', 1));
    }
  });

  sidebar_ = new SidebarWidget(this);
  sidebar_->setMinimumWidth(180);
  inspector_ = new InspectorWidget(this);
  inspector_->setMinimumWidth(240);
  strip_ = new DiagnosticsStrip(this);
  strip_->hide();

  recents_menu_ = new QMenu(tr("Open &Recent"), this);
  // QMenu suppresses per-action tooltips by default; the full-path tooltip
  // is the only way to tell two same-named recent files apart. One call
  // covers both entry points (File menu and the toolbar chevron pop the
  // same QMenu instance) and survives rebuild_recents_menu()'s clear().
  recents_menu_->setToolTipsVisible(true);
  rebuild_recents_menu();

  ToolbarWidget::Actions ta;
  ta.open             = act_open_;
  ta.recents_menu     = recents_menu_;
  ta.edges            = act_edges_;
  ta.triangle_mesh    = act_triangle_mesh_;
  ta.hidden_dimmed    = act_hidden_dimmed_;
  ta.toggle_sidebar   = act_toggle_sidebar_;
  ta.toggle_inspector = act_toggle_inspector_;
  ta.toggle_strip     = act_toggle_strip_;
  ta.theme            = act_theme_dark_;
  toolbar_ = new ToolbarWidget(ta, this);

  tabs_ = new DocumentTabBar(this);
  tabs_->setDocumentMode(true);
  tabs_->setDrawBase(false);
  tabs_->setMovable(true);
  tabs_->setExpanding(false);
  tabs_->setUsesScrollButtons(true);
  tabs_->setElideMode(Qt::ElideMiddle);
  tabs_->setFixedHeight(36);
  tabs_->hide();

  // Center column: viewport over the (default-hidden) diagnostics strip.
  split_v_ = new QSplitter(Qt::Vertical, this);
  split_v_->addWidget(viewport_);
  split_v_->addWidget(strip_);
  split_v_->setStretchFactor(0, 1);
  split_v_->setStretchFactor(1, 0);
  split_v_->setCollapsible(0, false);
  split_v_->setCollapsible(1, false);
  split_v_->setHandleWidth(1);
  split_v_->setChildrenCollapsible(false);

  split_h_ = new QSplitter(Qt::Horizontal, this);
  split_h_->addWidget(sidebar_);
  split_h_->addWidget(split_v_);
  split_h_->addWidget(inspector_);
  split_h_->setStretchFactor(0, 0);
  split_h_->setStretchFactor(1, 1);
  split_h_->setStretchFactor(2, 0);
  split_h_->setCollapsible(0, false);
  split_h_->setCollapsible(1, false);
  split_h_->setCollapsible(2, false);
  split_h_->setHandleWidth(1);
  split_h_->setSizes({260, 920, 300});

  auto* central = new QWidget(this);
  central->setAutoFillBackground(true);
  auto* v = new QVBoxLayout(central);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  v->addWidget(toolbar_);
  v->addWidget(tabs_);
  v->addWidget(split_h_, 1);
  setCentralWidget(central);

  // Floating HUD over the viewport's top-right: the sole home of Views / Fit /
  // projection and the zero-chrome toggle (the toolbar deliberately does not
  // duplicate them — they act on the drawing area, so they sit on it). Being
  // on the viewport also means they survive zero-chrome, where the toolbar
  // hides and the HUD copy of the toggle is the visible way back (Esc/^.
  // still work, but need to be known). Child widgets composite fine over
  // QOpenGLWidget (Qt renders the GL surface into an FBO first).
  auto* hud = new ViewportHud(viewport_);
  hud_views_ = new ToolbarButton(hud);
  hud_views_->setToolTip(tr("Standard views (1–7)"));
  connect(hud_views_, &QAbstractButton::clicked, this,
          [this]() { show_views_popover(hud_views_); });
  hud->add(hud_views_);
  auto* hud_fit = new ToolbarButton(hud);
  hud_fit->setDefaultAction(act_fit_);
  hud->add(hud_fit);
  auto* hud_persp = new ToolbarButton(hud);
  hud_persp->setDefaultAction(act_perspective_);
  hud_persp->set_emphasis(ToolbarButton::Emphasis::Accent);
  hud->add(hud_persp);
  auto* hud_zero = new ToolbarButton(hud);
  hud_zero->setDefaultAction(act_zero_chrome_);
  hud_zero->set_emphasis(ToolbarButton::Emphasis::Accent);
  hud->add(hud_zero);
  hud->adjustSize();
  hud_ = hud;

  // Isolate banner: hidden until the sidebar reports an isolate focus.
  isolate_banner_ = new IsolateBanner(act_exit_isolate_,
                                      act_isolate_hide_others_, viewport_);
  isolate_banner_->hide();

  // --- wiring ------------------------------------------------------------
  connect(act_toggle_sidebar_, &QAction::toggled,
          sidebar_, &QWidget::setVisible);
  connect(act_toggle_inspector_, &QAction::toggled,
          inspector_, &QWidget::setVisible);
  connect(act_toggle_strip_, &QAction::toggled, this, [this](bool on) {
    strip_->setVisible(on);
    if (on) toolbar_->strip_button()->set_badge(false);
  });
  connect(strip_, &DiagnosticsStrip::close_requested, this,
          [this]() { act_toggle_strip_->setChecked(false); });

  connect(toolbar_->display_segments(), &SegmentedControl::segment_clicked,
          this, [this](int idx) {
            set_surface_mode(static_cast<SurfaceMode>(idx));
          });

  connect(sidebar_, &SidebarWidget::node_selected,
          inspector_, &InspectorWidget::show_node);
  connect(sidebar_, &SidebarWidget::visibility_changed,
          viewport_, QOverload<>::of(&QWidget::update));
  connect(sidebar_, &SidebarWidget::highlight_changed,
          viewport_, QOverload<>::of(&QWidget::update));
  connect(sidebar_, &SidebarWidget::isolate_changed,
          this, &MainWindow::on_isolate_changed);
  // A left-click in the viewport cancels the highlight. Picking isn't
  // implemented, so the viewport reports every left press as a background
  // click (see ViewportWidget::background_clicked).
  connect(viewport_, &ViewportWidget::background_clicked,
          sidebar_, &SidebarWidget::clear_selection);

  connect(inspector_, &InspectorWidget::display_changed, this, [this]() {
    viewport_->set_navigation_scheme(inspector_->navigation_scheme());
    update_display_mode();
    save_settings();
  });
  connect(inspector_, &InspectorWidget::import_options_changed, this,
          [this]() { save_settings(); });
  connect(inspector_, &InspectorWidget::reimport_requested, this, [this]() {
    auto* document = active_document();
    if (document && !document->path.isEmpty() && !importing_) {
      start_import(document, inspector_->import_options());
    }
  });

  connect(tabs_, &QTabBar::currentChanged,
          this, &MainWindow::activate_document);
  connect(tabs_, &QTabBar::tabCloseRequested,
          this, &MainWindow::close_document);
  connect(tabs_, &QTabBar::tabMoved, this, [this](int from, int to) {
    if (from < 0 || to < 0 || from >= static_cast<int>(documents_.size()) ||
        to >= static_cast<int>(documents_.size()) || from == to) return;
    auto moved = std::move(documents_[static_cast<std::size_t>(from)]);
    documents_.erase(documents_.begin() + from);
    documents_.insert(documents_.begin() + to, std::move(moved));
    activate_document(tabs_->currentIndex());
  });
}

void MainWindow::build_menus() {
  auto* file_menu = menuBar()->addMenu(tr("&File"));
  file_menu->addAction(act_open_);
  file_menu->addAction(act_open_with_options_);
  file_menu->addMenu(recents_menu_);
  file_menu->addAction(act_close_tab_);
  file_menu->addSeparator();
  file_menu->addAction(act_quit_);

  auto* view_menu = menuBar()->addMenu(tr("&View"));
  view_menu->addAction(act_fit_);
  view_menu->addAction(act_exit_isolate_);
  view_menu->addAction(act_isolate_hide_others_);
  view_menu->addSeparator();
  view_menu->addAction(act_wireframe_);
  view_menu->addAction(act_hidden_line_);
  view_menu->addAction(act_edges_);
  view_menu->addAction(act_triangle_mesh_);
  view_menu->addAction(act_hidden_dimmed_);
  view_menu->addSeparator();
  view_menu->addAction(act_perspective_);
  auto* views_menu = view_menu->addMenu(tr("Standard &Views"));
  for (auto* a : view_actions_) views_menu->addAction(a);
  view_menu->addSeparator();
  view_menu->addAction(act_theme_dark_);
  auto* lang_menu = view_menu->addMenu(tr("&Language"));
  lang_menu->addAction(act_lang_system_);
  lang_menu->addAction(act_lang_english_);
  lang_menu->addAction(act_lang_chinese_);
  auto* panels_menu = view_menu->addMenu(tr("&Panels"));
  panels_menu->addAction(act_toggle_sidebar_);
  panels_menu->addAction(act_toggle_inspector_);
  panels_menu->addAction(act_toggle_strip_);
  view_menu->addAction(act_zero_chrome_);

  auto* help_menu = menuBar()->addMenu(tr("&Help"));
  help_menu->addAction(act_about_);
}

void MainWindow::build_status_bar() {
  statusBar()->setSizeGripEnabled(false);
  statusBar()->setFixedHeight(26);
  statusBar()->setAutoFillBackground(true);

  status_path_ = new QLabel(tr("No file loaded"));
  status_path_->setFont(ui_font(11));
  status_stats_ = new QLabel;
  status_stats_->setFont(mono_font(11));
  status_frame_ = new QLabel;
  status_frame_->setFont(mono_font(11));
  import_progress_ = new QProgressBar;
  import_progress_->setRange(0, 1000);
  import_progress_->setFixedSize(180, 16);
  import_progress_->setTextVisible(true);
  import_progress_->hide();
  import_cancel_ = new QToolButton;
  import_cancel_->setAutoRaise(true);
  import_cancel_->setToolTip(tr("Cancel import"));
  import_cancel_->hide();
  connect(import_cancel_, &QAbstractButton::clicked, this, [this]() {
    if (import_sink_) import_sink_->cancelled_flag.store(true);
  });
  statusBar()->addWidget(status_path_, 1);
  statusBar()->addPermanentWidget(import_progress_);
  statusBar()->addPermanentWidget(import_cancel_);
  statusBar()->addPermanentWidget(status_stats_);
  statusBar()->addPermanentWidget(status_frame_);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == viewport_ && event->type() == QEvent::Resize) {
    if (hud_) hud_->move(viewport_->width() - hud_->width() - 12, 12);
    position_isolate_banner();
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refresh_theme() {
  const auto& t = tokens();
  const bool dark = ThemeManager::instance().dark();

  act_open_->setIcon(themed_icon(QStringLiteral("document/open")));
  act_open_with_options_->setIcon(
    themed_icon(QStringLiteral("navigation/settings")));
  act_close_tab_->setIcon(themed_icon(QStringLiteral("action/close-small")));
  act_fit_->setIcon(themed_icon(QStringLiteral("action/zoom-fit")));
  act_exit_isolate_->setIcon(
    themed_icon(QStringLiteral("navigation/arrow-left")));
  // Same crossed-eye the sidebar rows use for "hidden", tying the isolate
  // style to the visibility vocabulary the tree already taught.
  act_isolate_hide_others_->setIcon(
    themed_icon(QStringLiteral("action/eye-crossed")));
  act_wireframe_->setIcon(themed_icon(QStringLiteral("shape/cube")));
  act_hidden_line_->setIcon(themed_icon(QStringLiteral("shape/borders")));
  act_perspective_->setIcon(
    themed_icon(QStringLiteral("misc/function-angle")));
  act_toggle_sidebar_->setIcon(
    themed_icon(QStringLiteral("navigation/ui-panel-left")));
  act_toggle_inspector_->setIcon(
    themed_icon(QStringLiteral("navigation/ui-panel-right")));
  act_toggle_strip_->setIcon(
    themed_icon(QStringLiteral("navigation/ui-panel-bottom")));
  act_zero_chrome_->setIcon(themed_icon(zero_chrome_icon_name()));
  act_about_->setIcon(themed_icon(QStringLiteral("misc/info")));
  act_theme_dark_->setIcon(themed_icon(
    dark ? QStringLiteral("misc/moon") : QStringLiteral("misc/sun")));
  if (hud_views_) {
    hud_views_->setIcon(themed_icon(QStringLiteral("shape/cube")));
  }
  if (hud_) {
    // Buttons report a wider sizeHint once they carry icons; re-fit the HUD
    // card and keep it pinned to the viewport's top-right.
    hud_->adjustSize();
    if (viewport_) {
      hud_->move(viewport_->width() - hud_->width() - 12, 12);
    }
  }
  if (import_cancel_) {
    import_cancel_->setIcon(
      themed_icon(QStringLiteral("action/close-small")));
  }
  {
    const QSignalBlocker block(act_theme_dark_);
    act_theme_dark_->setChecked(dark);
  }

  // The splitter gaps show the central widget through 1px seams — paint it
  // hairline-dark so they read as borders, not holes.
  QPalette central_pal = centralWidget()->palette();
  central_pal.setColor(QPalette::Window,
                       dark ? QColor(0x1A, 0x1B, 0x1E)
                            : QColor(0xD8, 0xDA, 0xDE));
  centralWidget()->setPalette(central_pal);

  if (tabs_) {
    QPalette tab_pal = tabs_->palette();
    tab_pal.setColor(QPalette::Window, t.toolbar_bg);
    tab_pal.setColor(QPalette::WindowText, t.text2);
    tab_pal.setColor(QPalette::Button, t.sidebar_bg);
    tab_pal.setColor(QPalette::ButtonText, t.text1);
    tabs_->setPalette(tab_pal);
    tabs_->setAutoFillBackground(true);
  }

  QPalette status_pal = statusBar()->palette();
  status_pal.setColor(QPalette::Window, t.status_bg);
  status_pal.setColor(QPalette::WindowText, t.text2);
  statusBar()->setPalette(status_pal);
  for (auto* l : {status_path_, status_stats_, status_frame_}) {
    if (!l) continue;
    QPalette pal = l->palette();
    pal.setColor(QPalette::WindowText, l == status_path_ ? t.text2 : t.text3);
    l->setPalette(pal);
  }

  update_display_mode();
}

void MainWindow::update_display_mode() {
  // Start from the viewport's current mode so transient renderer-fed state
  // (rotation pivot) is preserved.
  renderer::DisplayMode mode = viewport_->display_mode();
  mode.wireframe          = surface_mode_ == SurfaceMode::Wireframe;
  mode.hidden_line        = surface_mode_ == SurfaceMode::HiddenLine;
  mode.show_edges         = mode.hidden_line || act_edges_->isChecked();
  mode.show_triangle_mesh = act_triangle_mesh_->isChecked();
  mode.show_hidden_edges  = act_hidden_dimmed_->isChecked();
  // Like show_hidden_edges, fed unconditionally: the renderer only consults
  // it while nodes are ghosted, so the preference survives isolate
  // round-trips without any force-clearing here.
  mode.hide_ghosted       = act_isolate_hide_others_->isChecked();
  if (mode.wireframe || mode.hidden_line) {
    mode.show_edges         = mode.hidden_line;
    mode.show_triangle_mesh = false;
  }
  inspector_->apply_display(mode);
  const auto& t = tokens();
  mode.background_top    = to_vec3(t.viewport_top);
  mode.background_bottom = to_vec3(t.viewport_bottom);
  mode.hidden_line_color = mode.background_top;
  // Highlight tint: the dedicated signal orange, not the accent — see the
  // ThemeTokens::viewport_highlight comment. Re-fed on every theme change.
  mode.selection_color   = to_vec3(t.viewport_highlight);
  if (mode.hidden_line) mode.background_bottom = mode.background_top;
  viewport_->set_display_mode(mode);
  update_status_for_scene();
}

void MainWindow::set_surface_mode(SurfaceMode mode) {
  const bool was_shaded = surface_mode_ == SurfaceMode::Shaded;
  const bool is_shaded  = mode == SurfaceMode::Shaded;

  if (was_shaded && !is_shaded) {
    remembered_edges_ = act_edges_->isChecked();
    remembered_mesh_  = act_triangle_mesh_->isChecked();
  }

  if (!is_shaded) {
    const QSignalBlocker b1(act_edges_);
    const QSignalBlocker b2(act_triangle_mesh_);
    act_edges_->setChecked(false);
    act_triangle_mesh_->setChecked(false);
    act_edges_->setEnabled(false);
    act_triangle_mesh_->setEnabled(false);
  } else if (!was_shaded) {
    act_edges_->setEnabled(true);
    act_triangle_mesh_->setEnabled(true);
    const QSignalBlocker b1(act_edges_);
    const QSignalBlocker b2(act_triangle_mesh_);
    act_edges_->setChecked(remembered_edges_);
    act_triangle_mesh_->setChecked(remembered_mesh_);
  }

  // The dimmed toggle only ever *means* something in hidden-line mode, but
  // its checked state is the preference itself — leave it alone so it
  // survives mode round-trips (the renderer ignores it elsewhere).
  act_hidden_dimmed_->setEnabled(mode == SurfaceMode::HiddenLine);

  surface_mode_ = mode;
  {
    const QSignalBlocker b1(act_wireframe_);
    const QSignalBlocker b2(act_hidden_line_);
    act_wireframe_->setChecked(mode == SurfaceMode::Wireframe);
    act_hidden_line_->setChecked(mode == SurfaceMode::HiddenLine);
  }
  toolbar_->display_segments()->set_current(static_cast<int>(mode));
  update_display_mode();
}

void MainWindow::on_toggle_perspective(bool on) {
  auto* ctrl = viewport_->camera_controller();
  if (!ctrl) return;
  ctrl->set_projection(
    on ? scene::Projection::Perspective : scene::Projection::Orthographic);
  viewport_->update();
}

QString MainWindow::zero_chrome_icon_name() const {
  return act_zero_chrome_->isChecked()
    ? QStringLiteral("action/fullscreen-exit")
    : QStringLiteral("action/fullscreen");
}

void MainWindow::on_zero_chrome(bool on) {
  if (on) {
    zc_sidebar_   = act_toggle_sidebar_->isChecked();
    zc_inspector_ = act_toggle_inspector_->isChecked();
    zc_strip_     = act_toggle_strip_->isChecked();
    act_toggle_sidebar_->setChecked(false);
    act_toggle_inspector_->setChecked(false);
    act_toggle_strip_->setChecked(false);
    act_toggle_sidebar_->setEnabled(false);
    act_toggle_inspector_->setEnabled(false);
    act_toggle_strip_->setEnabled(false);
    toolbar_->hide();
    tabs_->hide();
    statusBar()->hide();
  } else {
    act_toggle_sidebar_->setEnabled(true);
    act_toggle_inspector_->setEnabled(true);
    act_toggle_strip_->setEnabled(true);
    act_toggle_sidebar_->setChecked(zc_sidebar_);
    act_toggle_inspector_->setChecked(zc_inspector_);
    act_toggle_strip_->setChecked(zc_strip_);
    toolbar_->show();
    tabs_->setVisible(!documents_.empty());
    statusBar()->show();
  }
  // The toolbar copy of this toggle disappears in zero-chrome, so the only
  // visible affordance is the HUD copy — flip its glyph to the "exit"
  // variant and update the tooltip so it reads as the way back out.
  act_zero_chrome_->setIcon(themed_icon(zero_chrome_icon_name()));
  act_zero_chrome_->setToolTip(
    on ? tr("Exit zero-chrome (Esc)")
       : tr("Hide all panels; press Esc to restore them"));
}

void MainWindow::position_isolate_banner() {
  if (!isolate_banner_ || !viewport_) return;
  isolate_banner_->move(
    std::max(12, (viewport_->width() - isolate_banner_->width()) / 2), 12);
}

void MainWindow::on_isolate_changed(std::uint32_t isolate_node) {
  const bool active = isolate_node != scene::Node::kInvalid;
  // Remember per document so a tab switch restores the mode (the sidebar
  // resets scene flags on every handover; activate_document re-applies).
  if (auto* document = active_document()) {
    document->isolate_node = isolate_node;
  }
  act_exit_isolate_->setEnabled(active);
  act_exit_isolate_->setVisible(active);
  // The style toggle follows the same lifecycle: outside isolate it is a
  // no-op, so the menu entry vanishes with Exit Isolate. Its checked state
  // is left alone — it is the preference (see build_actions).
  act_isolate_hide_others_->setEnabled(active);
  act_isolate_hide_others_->setVisible(active);
  if (active && isolate_banner_) {
    QString name;
    if (const auto* document = active_document();
        document && document->scene &&
        isolate_node < document->scene->nodes.size()) {
      name = QString::fromStdString(document->scene->nodes[isolate_node].name);
    }
    isolate_banner_->set_part_name(name.isEmpty() ? tr("(unnamed)") : name);
    position_isolate_banner();
    isolate_banner_->show();
    isolate_banner_->raise();
  } else if (isolate_banner_) {
    isolate_banner_->hide();
  }
  viewport_->update();
}

void MainWindow::show_views_popover(QWidget* anchor) {
  QList<QAction*> actions = view_actions_;
  actions.append(act_fit_);
  Popover::show_for(new ViewsGrid(actions), anchor, tr("Standard views"));
}

void MainWindow::run_demo(const QString& name) {
  if (name == QLatin1String("wireframe")) {
    set_surface_mode(SurfaceMode::Wireframe);
  } else if (name == QLatin1String("hiddenline")) {
    set_surface_mode(SurfaceMode::HiddenLine);
  } else if (name == QLatin1String("hiddenline-persp")) {
    // Perspective variant of the hidden-line demo. Ortho is the default
    // everywhere, so without this the silhouette pass's perspective facing
    // function (per-vertex toward-eye rather than a constant direction —
    // see silhouette.geom) would have no screenshot driver at all.
    set_surface_mode(SurfaceMode::HiddenLine);
    act_perspective_->setChecked(true);
  } else if (name == QLatin1String("wireframe-reopen") ||
             name == QLatin1String("hiddenline-reopen")) {
    // Regression drivers for the renderer's lazy GPU upload: switch to a
    // surface mode FIRST, then force a fresh import of the active document.
    // The re-import swaps in a brand-new scene::Scene, so attach_scene()
    // evicts every resident GPU mesh and the next frame must repopulate
    // them from whichever pass actually runs in that mode. Wireframe once
    // rendered nothing here (its early-out skips the surface pass that did
    // the uploading — see GLRenderer draw_edges) until the user toggled
    // back to Shaded; the screenshot must show geometry, not background.
    set_surface_mode(name.startsWith(QLatin1String("wireframe"))
                       ? SurfaceMode::Wireframe : SurfaceMode::HiddenLine);
    if (auto* document = active_document(); document && !importing_) {
      start_import(document, inspector_->import_options());
    }
  } else if (name.startsWith(QLatin1String("shaded-orbit:")) ||
             name.startsWith(QLatin1String("hiddenline-orbit:")) ||
             name.startsWith(QLatin1String("wireframe-orbit:"))) {
    // Drive an arbitrary orbit orientation for screenshot regressions. The
    // line modes use this to exercise view-dependent silhouettes; shaded uses
    // the same path to compare the inspection rig at exact camera angles.
    const bool wire = name.startsWith(QLatin1String("wireframe"));
    const bool hidden = name.startsWith(QLatin1String("hiddenline"));
    set_surface_mode(wire ? SurfaceMode::Wireframe
                          : hidden ? SurfaceMode::HiddenLine
                                   : SurfaceMode::Shaded);
    const QStringList parts =
      name.section(QLatin1Char(':'), 1).split(QLatin1Char(','));
    bool ok_yaw = false, ok_pitch = false;
    const float yaw   = parts.value(0).toFloat(&ok_yaw);
    const float pitch = parts.value(1).toFloat(&ok_pitch);
    if (parts.value(2) == QLatin1String("persp")) {
      act_perspective_->setChecked(true);
    }
    if (ok_yaw && ok_pitch) {
      if (auto* ctrl = viewport_ ? viewport_->camera_controller() : nullptr) {
        ctrl->set_view(yaw, pitch);
      }
    }
  } else if (name == QLatin1String("shadedmenu")) {
    set_surface_mode(SurfaceMode::Shaded);
    toolbar_->display_segments()->show_segment_menu(0);
  } else if (name == QLatin1String("hiddenmenu")) {
    set_surface_mode(SurfaceMode::HiddenLine);
    toolbar_->display_segments()->show_segment_menu(1);
  } else if (name == QLatin1String("hiddenline-nodim")) {
    // The "No Hidden" drafting style: occluded edges removed instead of
    // dimmed. Exercises DisplayMode::show_hidden_edges == false end to end.
    set_surface_mode(SurfaceMode::HiddenLine);
    act_hidden_dimmed_->setChecked(false);
  } else if (name == QLatin1String("display")) {
    act_toggle_inspector_->setChecked(true);
    inspector_->set_current_tab(InspectorWidget::DisplayTab);
  } else if (name == QLatin1String("import")) {
    act_toggle_inspector_->setChecked(true);
    inspector_->set_current_tab(InspectorWidget::ImportTab);
  } else if (name == QLatin1String("light")) {
    ThemeManager::instance().set_dark(false);
  } else if (name == QLatin1String("dark")) {
    ThemeManager::instance().set_dark(true);
  } else if (name == QLatin1String("views")) {
    show_views_popover(hud_views_);
  } else if (name == QLatin1String("zerochrome")) {
    act_zero_chrome_->setChecked(true);
  } else if (name == QLatin1String("deepzoom")) {
    // Regression driver for the zoom clip policy: wheel far past the model's
    // surface, the way a user zooming onto a feature does. Ten steps put the
    // eye well inside the model's bounding sphere; the ortho clip slab must
    // keep the whole model un-sliced, so the screenshot shows magnified
    // surface detail — not a cutaway of the model's interior.
    if (auto* ctrl = viewport_ ? viewport_->camera_controller() : nullptr) {
      const QPoint anchor(viewport_->width() / 2, viewport_->height() / 2);
      for (int i = 0; i < 10; ++i) ctrl->wheel(anchor, 240);
    }
  } else if (name == QLatin1String("highlight") ||
             name == QLatin1String("highlight-wireframe") ||
             name == QLatin1String("highlight-hiddenline") ||
             name.startsWith(QLatin1String("highlight:"))) {
    // Select the first geometry-bearing node the way a tree click would, so
    // the screenshot shows the accent highlight over the part — with
    // surface-mode variants to eyeball the wash in every draw path.
    // `highlight:<node>` selects an explicit scene-node index instead: the
    // occluded-selection treatment only shows on a part that sits partly
    // BEHIND another, and the first node (usually the base body) never does.
    if (name.endsWith(QLatin1String("wireframe"))) {
      set_surface_mode(SurfaceMode::Wireframe);
    } else if (name.endsWith(QLatin1String("hiddenline"))) {
      set_surface_mode(SurfaceMode::HiddenLine);
    }
    if (const auto* document = active_document();
        document && document->scene) {
      const auto& nodes = document->scene->nodes;
      bool selected_explicit = false;
      if (name.contains(QLatin1Char(':'))) {
        bool ok = false;
        const std::uint32_t idx =
          name.section(QLatin1Char(':'), 1).toUInt(&ok);
        if (ok && idx < nodes.size() && nodes[idx].mesh_index) {
          sidebar_->select_node(idx);
          selected_explicit = true;
        }
      }
      if (!selected_explicit) {
        for (std::uint32_t i = 0; i < nodes.size(); ++i) {
          if (nodes[i].mesh_index) {
            sidebar_->select_node(i);
            break;
          }
        }
      }
    }
  } else if (name == QLatin1String("isolate") ||
             name == QLatin1String("isolate-wireframe") ||
             name == QLatin1String("isolate-hiddenline") ||
             name == QLatin1String("isolate-hide") ||
             name == QLatin1String("isolate-hide-wireframe") ||
             name == QLatin1String("isolate-hide-hiddenline")) {
    // Isolate a mid-assembly part (the first is often the base body, which
    // would leave nothing meaningful to ghost) — with surface-mode variants
    // to exercise the ghost handling in every draw path. The -hide names
    // drive the "hide others" isolate style; both spellings set the toggle
    // explicitly so screenshots never depend on the persisted preference.
    if (name.endsWith(QLatin1String("wireframe"))) {
      set_surface_mode(SurfaceMode::Wireframe);
    } else if (name.endsWith(QLatin1String("hiddenline"))) {
      set_surface_mode(SurfaceMode::HiddenLine);
    }
    act_isolate_hide_others_->setChecked(
      name == QLatin1String("isolate-hide") ||
      name.startsWith(QLatin1String("isolate-hide-")));
    if (const auto* document = active_document();
        document && document->scene) {
      const auto& nodes = document->scene->nodes;
      std::vector<std::uint32_t> mesh_nodes;
      for (std::uint32_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].mesh_index) mesh_nodes.push_back(i);
      }
      if (!mesh_nodes.empty()) {
        const auto target = mesh_nodes[mesh_nodes.size() / 2];
        sidebar_->select_node(target);
        sidebar_->set_isolate(target);
      }
    }
  } else if (name == QLatin1String("getinfo")) {
    // Pop Get Info for the first leaf node with geometry, anchored near the
    // top of the sidebar (mirrors a hover-(i) click on a tree row).
    if (const auto* document = active_document();
        document && document->scene) {
      for (std::uint32_t i = 0; i < document->scene->nodes.size(); ++i) {
        if (document->scene->nodes[i].mesh_index) {
          const QRect anchor(mapToGlobal(QPoint(20, 140)), QSize(180, 26));
          sidebar_->show_get_info_for(i, anchor);
          break;
        }
      }
    }
  }
}

DocumentState* MainWindow::active_document() const {
  if (!tabs_) return nullptr;
  const int index = tabs_->currentIndex();
  if (index < 0 || index >= static_cast<int>(documents_.size())) return nullptr;
  return documents_[static_cast<std::size_t>(index)].get();
}

DocumentState* MainWindow::find_document(const QString& path) const {
  QString wanted = QFileInfo(path).canonicalFilePath();
  if (wanted.isEmpty()) wanted = QFileInfo(path).absoluteFilePath();
  // NTFS and the default APFS/HFS+ volumes are case-insensitive; treating
  // Part.step and part.step as distinct would open two tabs on one file.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  constexpr auto sensitivity = Qt::CaseInsensitive;
#else
  constexpr auto sensitivity = Qt::CaseSensitive;
#endif
  for (const auto& document : documents_) {
    QString existing = QFileInfo(document->path).canonicalFilePath();
    if (existing.isEmpty()) existing = QFileInfo(document->path).absoluteFilePath();
    if (QString::compare(existing, wanted, sensitivity) == 0) {
      return document.get();
    }
  }
  return nullptr;
}

int MainWindow::document_index(const DocumentState* document) const {
  for (std::size_t i = 0; i < documents_.size(); ++i) {
    if (documents_[i].get() == document) return static_cast<int>(i);
  }
  return -1;
}

DocumentState* MainWindow::add_document(const QString& path) {
  auto document = std::make_unique<DocumentState>();
  document->path = QFileInfo(path).absoluteFilePath();
  auto* raw = document.get();
  documents_.push_back(std::move(document));

  const QSignalBlocker blocker(tabs_);
  const int index = tabs_->addTab(QFileInfo(path).fileName());
  auto* close = new TabCloseButton(tabs_);
  tabs_->setTabButton(index, QTabBar::RightSide, close);
  connect(close, &QAbstractButton::clicked, this, [this, raw]() {
    close_document(document_index(raw));
  });
  tabs_->setTabToolTip(index, bounded_tooltip(raw->path));
  tabs_->setCurrentIndex(index);
  tabs_->setVisible(!act_zero_chrome_->isChecked());
  activate_document(index);
  return raw;
}

void MainWindow::update_document_tab(DocumentState* document) {
  const int index = document_index(document);
  if (index < 0) return;

  const QString filename = QFileInfo(document->path).fileName();
  QString title = filename;
  if (document->close_requested) {
    title = tr("%1 (canceling…)").arg(filename);
  } else if (document->loading) {
    const int percent = import_sink_
      ? static_cast<int>(std::clamp(import_sink_->fraction.load(), 0.0f, 1.0f) *
                         100.0f) : 0;
    title = tr("%1 (%2%)").arg(filename).arg(percent);
  } else if (document->failed) {
    title = tr("%1 (failed)").arg(filename);
  }
  tabs_->setTabText(index, title);
}

void MainWindow::activate_document(int index) {
  DocumentState* document = nullptr;
  if (index >= 0 && index < static_cast<int>(documents_.size())) {
    document = documents_[static_cast<std::size_t>(index)].get();
  }

  if (!document) {
    viewport_->set_scene(nullptr);
    sidebar_->clear();
    inspector_->set_scene(nullptr);
    inspector_->set_reimport_enabled(false);
    strip_->clear();
    act_close_tab_->setEnabled(false);
    setWindowTitle(QStringLiteral("Cadly"));
    update_status_for_scene();
    return;
  }

  if (document->scene) {
    // Read before the sidebar handover: set_scene → clear() reports
    // isolate-off, and that report writes kInvalid into this very field.
    const std::uint32_t isolate = document->isolate_node;
    viewport_->set_scene(document->scene, !document->camera_initialized);
    document->camera_initialized = true;
    sidebar_->set_scene(document->scene);
    inspector_->set_scene(document->scene);
    // Re-apply this tab's isolate focus (no-op when kInvalid); rebuilds the
    // ghost flags the handover just wiped and re-shows the banner.
    sidebar_->set_isolate(isolate);
  } else {
    viewport_->set_scene(nullptr);
    sidebar_->clear();
    inspector_->set_scene(nullptr);
  }

  if (document->has_summary) {
    strip_->show_summary(document->summary);
    if (document->failed) strip_->show_log_tab();
  } else {
    strip_->clear();
  }

  const auto projection = document->scene
    ? viewport_->camera_controller()->camera().projection_mode
    : scene::Projection::Orthographic;
  {
    const QSignalBlocker blocker(act_perspective_);
    act_perspective_->setChecked(projection == scene::Projection::Perspective);
  }
  inspector_->set_reimport_enabled(!importing_ && !document->path.isEmpty());
  act_close_tab_->setEnabled(true);
  setWindowTitle(QStringLiteral("Cadly"));
  update_status_for_scene();
}

void MainWindow::close_document(int index) {
  if (index < 0 || index >= static_cast<int>(documents_.size())) return;
  auto* document = documents_[static_cast<std::size_t>(index)].get();
  if (document == importing_document_) {
    document->close_requested = true;
    if (import_sink_) import_sink_->cancelled_flag.store(true);
    update_document_tab(document);
    statusBar()->showMessage(tr("Canceling import before closing tab"), 3000);
    return;
  }

  DocumentState* keep_active = active_document();
  if (keep_active == document) keep_active = nullptr;
  {
    const QSignalBlocker blocker(tabs_);
    if (auto* button = tabs_->tabButton(index, QTabBar::RightSide)) {
      button->deleteLater();
    }
    documents_.erase(documents_.begin() + index);
    tabs_->removeTab(index);
    int next = document_index(keep_active);
    if (next < 0 && !documents_.empty()) {
      next = std::min(index, static_cast<int>(documents_.size()) - 1);
    }
    tabs_->setCurrentIndex(next);
    tabs_->setVisible(!documents_.empty() &&
                      !act_zero_chrome_->isChecked());
    activate_document(next);
  }
}

void MainWindow::set_import_controls_visible(bool visible) {
  import_progress_->setVisible(visible);
  import_cancel_->setVisible(visible);
  if (!visible) {
    import_progress_->setValue(0);
    import_progress_->setToolTip(QString());
  }
}

void MainWindow::set_recent_files(const QStringList& paths) {
  recent_files_ = paths;
  rebuild_recents_menu();
}

void MainWindow::set_last_open_directory(const QString& dir) {
  last_open_dir_ = dir;
}

void MainWindow::set_language(const QString& code) {
  language_code_ = code;
  // setChecked never re-emits QActionGroup::triggered (that only fires on
  // user interaction), so no blockers needed. An unknown persisted code
  // simply leaves nothing checked.
  for (QAction* a : {act_lang_system_, act_lang_english_, act_lang_chinese_}) {
    if (a) a->setChecked(a->data().toString() == code);
  }
}

void MainWindow::rebuild_recents_menu() {
  if (!recents_menu_) return;
  recents_menu_->clear();
  if (recent_files_.isEmpty()) {
    auto* none = recents_menu_->addAction(tr("No recent files"));
    none->setEnabled(false);
    return;
  }
  for (const auto& path : recent_files_) {
    auto* a = recents_menu_->addAction(QFileInfo(path).fileName());
    a->setToolTip(bounded_tooltip(path));
    connect(a, &QAction::triggered, this,
            [this, path]() { open_file(path); });
  }
  recents_menu_->addSeparator();
  auto* clear = recents_menu_->addAction(tr("Clear Menu"));
  connect(clear, &QAction::triggered, this,
          [this]() { emit recents_clear_requested(); });
}

void MainWindow::open_file() {
  const QString start_dir = last_open_dir_;
  const auto path = QFileDialog::getOpenFileName(
    this, tr("Open CAD file"), start_dir,
    tr("CAD files (*.step *.stp *.iges *.igs);;All files (*)"));
  if (path.isEmpty()) return;
  open_file(path);
}

void MainWindow::open_file(const QString& path) {
  if (importing_) {
    statusBar()->showMessage(tr("An import is already running"), 3000);
    return;
  }
  // Absolutize before anything downstream records it: a relative CLI path
  // would otherwise end up in recents/last-dir and break once the app is
  // launched from a different working directory.
  const QString abs = QFileInfo(path).absoluteFilePath();
  if (auto* existing = find_document(abs); existing && !existing->failed) {
    tabs_->setCurrentIndex(document_index(existing));
    return;
  }
  cad::ImportOptions opts = inspector_->import_options();
  if (inspector_->review_before_import()) {
    if (!run_preflight_dialog(opts)) return;
  }
  auto* document = find_document(abs);
  if (!document) document = add_document(abs);
  tabs_->setCurrentIndex(document_index(document));
  start_import(document, opts);
}

void MainWindow::open_file_with_options() {
  if (importing_) {
    statusBar()->showMessage(tr("An import is already running"), 3000);
    return;
  }
  const auto path = QFileDialog::getOpenFileName(
    this, tr("Open CAD file"), last_open_dir_,
    tr("CAD files (*.step *.stp *.iges *.igs);;All files (*)"));
  if (path.isEmpty()) return;
  const QString abs = QFileInfo(path).absoluteFilePath();
  cad::ImportOptions opts = inspector_->import_options();
  if (!run_preflight_dialog(opts)) return;
  auto* document = find_document(abs);
  if (!document) document = add_document(abs);
  tabs_->setCurrentIndex(document_index(document));
  start_import(document, opts);
}

bool MainWindow::run_preflight_dialog(cad::ImportOptions& opts) {
  QDialog dlg(this);
  dlg.setWindowTitle(tr("Import Options"));
  dlg.setMinimumWidth(380);
  auto* lay = new QVBoxLayout(&dlg);
  auto* form = new ImportOptionsWidget(&dlg);
  form->set_options(opts);
  lay->addWidget(form);
  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
  lay->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  if (dlg.exec() != QDialog::Accepted) return false;
  opts = form->options();
  inspector_->set_import_options(opts);  // keep the Import tab in step
  save_settings();
  return true;
}

void MainWindow::start_import(DocumentState* document,
                              const cad::ImportOptions& opts) {
  if (importing_ || !document) return;
  importing_ = true;
  importing_document_ = document;
  document->loading = true;
  document->failed = false;
  document->close_requested = false;
  auto sink = std::make_shared<GuiImportSink>();
  import_sink_ = sink;

  // Only the entry points are disabled — the old scene stays orbit-able and
  // every panel keeps working. This replaces Qt::ApplicationModal.
  act_open_->setEnabled(false);
  act_open_with_options_->setEnabled(false);
  toolbar_->recents_button()->setEnabled(false);
  inspector_->set_reimport_enabled(false);
  toolbar_->strip_button()->set_badge(false);

  set_import_controls_visible(true);
  update_document_tab(document);
  update_status_for_scene();

  const auto path_std = std::filesystem::path(document->path.toStdString());

  auto* watcher = new QFutureWatcher<cad::ImportResult>(this);
  import_watcher_ = watcher;
  connect(watcher, &QFutureWatcher<cad::ImportResult>::finished,
          this, [this, watcher, document]() {
    const cad::ImportResult result = watcher->result();
    watcher->deleteLater();
    finish_import(document, result);
  });

  // Poll the sink ~30 Hz to drive the active tab title and status progress.
  auto* poll = new QTimer(this);
  poll->setInterval(33);
  connect(poll, &QTimer::timeout, this, [this, sink, poll]() {
    if (!importing_) {
      poll->stop();
      poll->deleteLater();
      return;
    }
    const float fraction = sink->fraction.load();
    import_progress_->setValue(
      static_cast<int>(std::clamp(fraction, 0.0f, 1.0f) * 1000.0f));
    import_progress_->setToolTip(sink->message());
    update_document_tab(importing_document_);
  });
  poll->start();

  CADLY_LOG_INFO("Importing {}", document->path.toStdString());
  auto future = QtConcurrent::run([path_std, opts, sink]() -> cad::ImportResult {
    return cad::ImporterRegistry::instance().import(path_std, opts, sink.get());
  });
  watcher->setFuture(future);
}

void MainWindow::finish_import(DocumentState* document,
                               const cad::ImportResult& result) {
  importing_ = false;
  act_open_->setEnabled(true);
  act_open_with_options_->setEnabled(true);
  toolbar_->recents_button()->setEnabled(true);
  const bool was_cancelled =
    import_sink_ && import_sink_->cancelled_flag.load();
  const bool close_requested = document && document->close_requested;
  import_sink_.reset();
  importing_document_ = nullptr;
  set_import_controls_visible(false);
  inspector_->set_reimport_enabled(active_document() != nullptr);

  if (!document) return;
  document->loading = false;
  document->summary = result.summary;
  document->has_summary = true;
  update_document_tab(document);
  if (active_document() == document) strip_->show_summary(result.summary);

  if (was_cancelled) {
    statusBar()->showMessage(tr("Import cancelled"), 4000);
    document->close_requested = false;
    update_document_tab(document);
    if (close_requested || !document->scene) {
      close_document(document_index(document));
    } else if (active_document() == document) {
      inspector_->set_reimport_enabled(true);
      update_status_for_scene();
    }
    return;
  }

  if (!result.success || !result.scene) {
    document->failed = true;
    update_document_tab(document);
    // Failure is the one event loud enough to steal panel space: open the
    // strip on its Log tab. Success only badges the toggle.
    if (active_document() == document) {
      strip_->show_log_tab();
      act_toggle_strip_->setChecked(true);
      inspector_->set_reimport_enabled(true);
      update_status_for_scene();
    }
    statusBar()->showMessage(tr("Import failed"), 6000);
    return;
  }

  const bool had_scene = static_cast<bool>(document->scene);
  scene::Camera previous_camera;
  if (had_scene) {
    previous_camera = active_document() == document
      ? viewport_->camera_controller()->camera()
      : document->scene->camera;
  }
  document->scene = result.scene;
  if (had_scene) document->scene->camera = previous_camera;
  document->failed = false;
  document->camera_initialized = had_scene;
  // The fresh scene has fresh node indices; a remembered isolate focus
  // would point at an arbitrary part of the new hierarchy.
  document->isolate_node = scene::Node::kInvalid;
  update_document_tab(document);
  if (active_document() == document) {
    activate_document(document_index(document));
    strip_->show_summary_tab();
  }
  if (!strip_->isVisible()) {
    toolbar_->strip_button()->set_badge(true);
  }
  emit file_imported(document->path);
}

void MainWindow::update_status_for_scene() {
  const auto* document = active_document();
  if (!document) {
    status_path_->setText(tr("No file loaded"));
    status_stats_->setText(QString());
    status_frame_->setText(QString());
    return;
  }
  status_path_->setText(document->path);
  if (!document->scene) {
    status_stats_->setText(document->loading ? tr("Importing…")
                                             : tr("No scene loaded"));
    status_frame_->setText(QString());
    return;
  }

  const auto& scene = *document->scene;

  std::size_t triangles = 0;
  std::size_t vertices  = 0;
  for (const auto& m : scene.meshes) {
    if (!m) continue;
    triangles += m->triangle_count();
    vertices  += m->vertices.size();
  }
  const int msaa = viewport_->display_mode().msaa_samples;
  const QString msaa_text =
    msaa > 1 ? QStringLiteral("MSAA %1×").arg(msaa) : tr("MSAA off");
  status_stats_->setText(tr("%1 · %2 nodes · %3 tris · %4 verts")
    .arg(msaa_text)
    .arg(scene.nodes.size())
    .arg(triangles)
    .arg(vertices));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
  if (importing_ || !e->mimeData()->hasUrls()) return;
  const auto urls = e->mimeData()->urls();
  if (!urls.isEmpty() && urls.first().isLocalFile() &&
      is_cad_file(urls.first().toLocalFile())) {
    e->acceptProposedAction();
  }
}

void MainWindow::dropEvent(QDropEvent* e) {
  if (importing_ || !e->mimeData()->hasUrls()) return;
  const auto urls = e->mimeData()->urls();
  if (urls.isEmpty() || !urls.first().isLocalFile()) return;
  const QString path = urls.first().toLocalFile();
  if (!is_cad_file(path)) return;
  e->acceptProposedAction();
  open_file(path);
}

void MainWindow::closeEvent(QCloseEvent* e) {
  save_settings();
  if (importing_ && import_watcher_) {
    // Ask the worker to stop and wait for it: letting the process exit with
    // an OCCT import mid-flight is a crash lottery. Importers poll the sink
    // between heavy steps, so this returns promptly in practice.
    if (import_sink_) import_sink_->cancelled_flag.store(true);
    import_watcher_->waitForFinished();
  }
  QMainWindow::closeEvent(e);
}

void MainWindow::load_settings() {
  auto s = ui_settings();

  inspector_->set_import_options(load_import_options(s));
  inspector_->set_review_before_import(
    s.value(QStringLiteral("import/review_each"), false).toBool());

  renderer::DisplayMode mode;   // struct defaults
  s.beginGroup(QStringLiteral("display"));
  mode.edge_intensity = s.value("edge_intensity",
                                mode.edge_intensity).toFloat();
  mode.msaa_samples   = s.value("msaa_samples", mode.msaa_samples).toInt();
  mode.show_scale_bar = s.value("scale_bar", mode.show_scale_bar).toBool();
  mode.show_axes      = s.value("axes", mode.show_axes).toBool();
  act_hidden_dimmed_->setChecked(
    s.value("dimmed_hidden", act_hidden_dimmed_->isChecked()).toBool());
  act_isolate_hide_others_->setChecked(
    s.value("isolate_hide_others",
            act_isolate_hide_others_->isChecked()).toBool());
  const auto scheme = navigation_scheme_from_key(
    s.value("navigation_scheme").toString());
  s.endGroup();
  inspector_->load_display(mode);
  inspector_->set_navigation_scheme(scheme);
  viewport_->set_navigation_scheme(scheme);

  s.beginGroup(QStringLiteral("ui"));
  act_toggle_sidebar_->setChecked(s.value("sidebar_visible", true).toBool());
  act_toggle_inspector_->setChecked(
    s.value("inspector_visible", true).toBool());
  // The strip intentionally always starts hidden — it is an annunciator, not
  // a resident panel.
  if (const auto blob = s.value("split_h").toByteArray(); !blob.isEmpty()) {
    split_h_->restoreState(blob);
  }
  if (const auto blob = s.value("split_v").toByteArray(); !blob.isEmpty()) {
    split_v_->restoreState(blob);
  }
  s.endGroup();
}

void MainWindow::save_settings() const {
  auto s = ui_settings();

  save_import_options(s, inspector_->import_options());
  s.setValue(QStringLiteral("import/review_each"),
             inspector_->review_before_import());

  renderer::DisplayMode mode;
  inspector_->apply_display(mode);
  s.beginGroup(QStringLiteral("display"));
  s.setValue("edge_intensity", mode.edge_intensity);
  s.setValue("msaa_samples",   mode.msaa_samples);
  s.setValue("scale_bar",      mode.show_scale_bar);
  s.setValue("axes",           mode.show_axes);
  s.setValue("dimmed_hidden",  act_hidden_dimmed_->isChecked());
  s.setValue("isolate_hide_others", act_isolate_hide_others_->isChecked());
  s.setValue("navigation_scheme",
             navigation_scheme_key(inspector_->navigation_scheme()));
  s.endGroup();

  s.beginGroup(QStringLiteral("ui"));
  // In zero-chrome the live checkboxes are all false; persist the remembered
  // pre-zero-chrome layout instead.
  const bool zc = act_zero_chrome_->isChecked();
  s.setValue("sidebar_visible",
             zc ? zc_sidebar_ : act_toggle_sidebar_->isChecked());
  s.setValue("inspector_visible",
             zc ? zc_inspector_ : act_toggle_inspector_->isChecked());
  s.setValue("split_h", split_h_->saveState());
  s.setValue("split_v", split_v_->saveState());
  s.endGroup();
}

void MainWindow::on_about() {
  QMessageBox::about(this, tr("About Cadly"),
    tr("<b>Cadly</b><br>Native C++ CAD viewer.<br>"
       "STEP/IGES via OCCT, OpenGL 4.1 PBR renderer.<br>"
       "Qt %1.").arg(QT_VERSION_STR));
}

} // namespace cadly::ui
