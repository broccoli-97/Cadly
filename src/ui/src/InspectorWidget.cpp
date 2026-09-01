#include "InspectorWidget.h"

#include "IconUtils.h"
#include "ImportOptionsWidget.h"
#include "PropertiesPanel.h"
#include "ToolbarButton.h"
#include "cadly/ui/ThemeTokens.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace cadly::ui {

namespace {
// Wrap a pane in a transparent scroll area so the inspector works at small
// window heights without crushing its forms.
QScrollArea* make_scroller(QWidget* pane) {
  auto* scroll = new QScrollArea();
  scroll->setWidget(pane);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->viewport()->setAutoFillBackground(false);
  pane->setAutoFillBackground(false);
  return scroll;
}

// Small accent text button for pane-header actions ("Reset to Defaults") —
// the Qt twin of the prototype's .txtbtn: bare accent text, soft accent pill
// on hover. Painted from ThemeTokens rather than using a flat QPushButton so
// it renders identically under Fusion (Qt 6.4) and qlementine (Qt 6.8).
class TextButton final : public QAbstractButton {
public:
  explicit TextButton(const QString& text, QWidget* parent = nullptr)
      : QAbstractButton(parent) {
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    connect(&ThemeManager::instance(), &ThemeManager::changed,
            this, QOverload<>::of(&QWidget::update));
  }

  QSize sizeHint() const override {
    const QFontMetrics fm(ui_font(11, QFont::Medium));
    return {fm.horizontalAdvance(text()) + 12, 20};
  }

protected:
  void paintEvent(QPaintEvent*) override {
    const auto& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (isEnabled() && (underMouse() || isDown())) {
      // The prototype's --chk-bg hover fill: accent at ~22% (dark) / ~16%
      // (light), nudged stronger while pressed.
      QColor pill = t.accent;
      pill.setAlpha(t.dark ? (isDown() ? 74 : 56) : (isDown() ? 56 : 41));
      p.setPen(Qt::NoPen);
      p.setBrush(pill);
      p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }
    p.setFont(ui_font(11, QFont::Medium));
    p.setPen(isEnabled() ? t.accent : t.text3);
    p.drawText(rect(), Qt::AlignCenter, text());
  }
};

// Right-aligned pane-header row holding a TextButton, mirroring the
// prototype's reset placement above each settings page.
QHBoxLayout* make_header_row(QAbstractButton* button) {
  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->addStretch();
  row->addWidget(button);
  return row;
}
} // namespace

InspectorWidget::InspectorWidget(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 6, 0, 0);
  outer->setSpacing(4);

  // Icon tab row. ToolbarButtons share the look of the toolbar; exclusivity
  // via QButtonGroup.
  auto* tabs = new QHBoxLayout();
  tabs->setContentsMargins(8, 0, 8, 0);
  tabs->setSpacing(4);
  auto* group = new QButtonGroup(this);
  group->setExclusive(true);

  // QT_TR_NOOP marks the literals for lupdate; the actual lookup happens at
  // the tr(specs[i].tip) call sites below, which a bare table entry would
  // hide from extraction.
  struct TabSpec { const char* icon; const char* tip; };
  const TabSpec specs[3] = {
    {"document/properties",            QT_TR_NOOP("Properties")},
    {"navigation/sliders-horizontal",  QT_TR_NOOP("Display")},
    {"action/download",                QT_TR_NOOP("Import")},
  };
  tabs->addStretch();
  for (int i = 0; i < 3; ++i) {
    auto* b = new ToolbarButton(this);
    b->setCheckable(true);
    b->setIcon(themed_icon(QLatin1String(specs[i].icon)));
    b->setText(tr(specs[i].tip));
    b->set_show_text(false);
    b->setToolTip(tr(specs[i].tip));
    group->addButton(b, i);
    tabs->addWidget(b);
    tab_buttons_[i] = b;
  }
  tabs->addStretch();
  outer->addLayout(tabs);

  stack_ = new QStackedWidget(this);
  properties_ = new PropertiesPanel();
  stack_->addWidget(make_scroller(properties_));
  stack_->addWidget(make_scroller(build_display_pane()));
  stack_->addWidget(make_scroller(build_import_pane()));
  outer->addWidget(stack_, 1);

  connect(group, &QButtonGroup::idClicked, this,
          [this](int id) { stack_->setCurrentIndex(id); });
  tab_buttons_[0]->setChecked(true);

  connect(&ThemeManager::instance(), &ThemeManager::changed, this, [this]() {
    refresh_icons();
    update();
  });
  refresh_icons();
}

void InspectorWidget::refresh_icons() {
  const QString names[3] = {
    QStringLiteral("document/properties"),
    QStringLiteral("navigation/sliders-horizontal"),
    QStringLiteral("action/download"),
  };
  for (int i = 0; i < 3; ++i) tab_buttons_[i]->setIcon(themed_icon(names[i]));
}

void InspectorWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const auto& t = tokens();
  p.fillRect(rect(), t.inspector_bg);
  p.fillRect(QRect(0, 0, 1, height()), t.hairline);
}

QWidget* InspectorWidget::build_display_pane() {
  auto* pane = new QWidget();
  auto* outer = new QVBoxLayout(pane);
  outer->setContentsMargins(12, 10, 12, 10);

  auto* reset = new TextButton(tr("Reset to Defaults"), pane);
  reset->setObjectName(QStringLiteral("inspector_display_reset"));
  reset->setToolTip(tr("Restore the default display settings"));
  outer->addLayout(make_header_row(reset));

  auto* form = new QFormLayout();
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(8);

  edge_intensity_ = new QSlider(Qt::Horizontal, pane);
  edge_intensity_->setRange(0, 100);
  edge_intensity_->setValue(65);
  edge_intensity_->setToolTip(tr("BRep edge overlay strength"));

  msaa_ = new QComboBox(pane);
  msaa_->addItem(tr("Off"), 0);
  msaa_->addItem(QStringLiteral("2×"), 2);
  msaa_->addItem(QStringLiteral("4×"), 4);
  msaa_->addItem(QStringLiteral("8×"), 8);
  msaa_->setCurrentIndex(2);
  msaa_->setToolTip(tr("GPU multisampling, limited by hardware"));

  scale_bar_ = new QCheckBox(tr("Scale bar"), pane);
  scale_bar_->setChecked(true);
  axes_ = new QCheckBox(tr("Orientation axes"), pane);
  axes_->setChecked(true);

  nav_scheme_ = new QComboBox(pane);
  for (const auto s : kAllNavigationSchemes) {
    nav_scheme_->addItem(navigation_scheme_name(s),
                         static_cast<int>(s));
  }
  nav_scheme_->setToolTip(
    tr("Mouse bindings for orbit, pan, and zoom, matching a familiar "
       "CAD package"));

  orbit_style_ = new QComboBox(pane);
  using OrbitStyle = CameraController::OrbitStyle;
  for (const auto s : {OrbitStyle::Free, OrbitStyle::Turntable}) {
    orbit_style_->addItem(orbit_style_name(s), static_cast<int>(s));
  }
  orbit_style_->setToolTip(
    tr("Free orbit tumbles in screen space (no pole, horizon may tilt); "
       "turntable keeps up fixed with a clamped elevation"));

  nav_legend_ = new QLabel(pane);
  nav_legend_->setWordWrap(true);
  nav_legend_->setTextFormat(Qt::PlainText);
  // Secondary text: the legend explains, the combo decides.
  nav_legend_->setEnabled(false);
  nav_legend_->setText(
    navigation_scheme_legend(NavigationScheme::Cadly));

  form->addRow(tr("Edge intensity"), edge_intensity_);
  form->addRow(tr("Anti-aliasing"), msaa_);
  form->addRow(scale_bar_);
  form->addRow(axes_);
  form->addRow(tr("Navigation"), nav_scheme_);
  form->addRow(tr("Orbit style"), orbit_style_);
  form->addRow(nav_legend_);
  outer->addLayout(form);
  outer->addStretch();

  connect(edge_intensity_, &QSlider::valueChanged, this,
          [this](int) { emit display_changed(); });
  connect(msaa_, &QComboBox::currentIndexChanged, this,
          [this](int) { emit display_changed(); });
  connect(scale_bar_, &QCheckBox::toggled, this,
          [this](bool) { emit display_changed(); });
  connect(axes_, &QCheckBox::toggled, this,
          [this](bool) { emit display_changed(); });
  connect(nav_scheme_, &QComboBox::currentIndexChanged, this, [this](int) {
    nav_legend_->setText(navigation_scheme_legend(navigation_scheme()));
    emit display_changed();
  });
  connect(orbit_style_, &QComboBox::currentIndexChanged, this,
          [this](int) { emit display_changed(); });
  connect(reset, &QAbstractButton::clicked, this, [this]() {
    // Struct defaults are the single source of truth; load_display blocks
    // the per-control signals, so announce the change once here. The
    // navigation scheme is muscle memory, not a display setting — reset
    // leaves it alone (like the Import pane's review checkbox).
    load_display(renderer::DisplayMode{});
    emit display_changed();
  });
  return pane;
}

CameraController::OrbitStyle InspectorWidget::orbit_style() const {
  return static_cast<CameraController::OrbitStyle>(
    orbit_style_->currentData().toInt());
}

void InspectorWidget::set_orbit_style(CameraController::OrbitStyle style) {
  const QSignalBlocker block(orbit_style_);
  orbit_style_->setCurrentIndex(
    orbit_style_->findData(static_cast<int>(style)));
}

NavigationScheme InspectorWidget::navigation_scheme() const {
  return static_cast<NavigationScheme>(
    nav_scheme_->currentData().toInt());
}

void InspectorWidget::set_navigation_scheme(NavigationScheme scheme) {
  const QSignalBlocker block(nav_scheme_);
  nav_scheme_->setCurrentIndex(nav_scheme_->findData(static_cast<int>(scheme)));
  nav_legend_->setText(navigation_scheme_legend(scheme));
}

QWidget* InspectorWidget::build_import_pane() {
  auto* pane = new QWidget();
  auto* outer = new QVBoxLayout(pane);
  outer->setContentsMargins(12, 10, 12, 10);
  outer->setSpacing(10);

  auto* reset = new TextButton(tr("Reset to Defaults"), pane);
  reset->setObjectName(QStringLiteral("inspector_import_reset"));
  reset->setToolTip(tr("Restore the default import options"));
  outer->addLayout(make_header_row(reset));

  import_options_ = new ImportOptionsWidget(pane);
  outer->addWidget(import_options_);

  review_each_ = new QCheckBox(tr("Review options before each import"), pane);
  review_each_->setToolTip(
    tr("Opens these options for review before each import."));
  outer->addWidget(review_each_);

  reimport_ = new QPushButton(tr("Re-import with these options"), pane);
  reimport_->setEnabled(false);
  outer->addWidget(reimport_);
  outer->addStretch();

  connect(import_options_, &ImportOptionsWidget::options_edited, this,
          [this]() { emit import_options_changed(); });
  connect(review_each_, &QCheckBox::toggled, this,
          [this](bool) { emit import_options_changed(); });
  connect(reimport_, &QPushButton::clicked, this,
          [this]() { emit reimport_requested(); });
  connect(reset, &QAbstractButton::clicked, this, [this]() {
    // Backend defaults from ImportOptions{}. set_options is deliberately
    // silent (options_edited is user-edit-only), so announce it here — the
    // owner persists the values. The "review before import" checkbox is a
    // workflow preference, not an import option; the reset leaves it alone.
    import_options_->set_options(cad::ImportOptions{});
    emit import_options_changed();
  });
  return pane;
}

void InspectorWidget::set_scene(std::shared_ptr<scene::Scene> scene) {
  properties_->set_scene(std::move(scene));
}

void InspectorWidget::show_node(std::uint32_t node_index) {
  properties_->show_node(node_index);
}

void InspectorWidget::apply_display(renderer::DisplayMode& mode) const {
  mode.edge_intensity =
    static_cast<float>(edge_intensity_->value()) / 100.0f;
  mode.msaa_samples   = msaa_->currentData().toInt();
  mode.show_scale_bar = scale_bar_->isChecked();
  mode.show_axes      = axes_->isChecked();
}

void InspectorWidget::load_display(const renderer::DisplayMode& mode) {
  const QSignalBlocker b1(edge_intensity_);
  const QSignalBlocker b2(msaa_);
  const QSignalBlocker b3(scale_bar_);
  const QSignalBlocker b4(axes_);
  edge_intensity_->setValue(
    static_cast<int>(mode.edge_intensity * 100.0f + 0.5f));
  const int idx = msaa_->findData(mode.msaa_samples);
  msaa_->setCurrentIndex(idx >= 0 ? idx : 2);
  scale_bar_->setChecked(mode.show_scale_bar);
  axes_->setChecked(mode.show_axes);
}

cad::ImportOptions InspectorWidget::import_options() const {
  return import_options_->options();
}

void InspectorWidget::set_import_options(const cad::ImportOptions& o) {
  import_options_->set_options(o);
}

bool InspectorWidget::review_before_import() const {
  return review_each_->isChecked();
}

void InspectorWidget::set_review_before_import(bool on) {
  const QSignalBlocker b(review_each_);
  review_each_->setChecked(on);
}

void InspectorWidget::set_reimport_enabled(bool on) {
  reimport_->setEnabled(on);
}

void InspectorWidget::set_current_tab(Tab tab) {
  stack_->setCurrentIndex(static_cast<int>(tab));
  tab_buttons_[static_cast<int>(tab)]->setChecked(true);
}

InspectorWidget::Tab InspectorWidget::current_tab() const {
  return static_cast<Tab>(stack_->currentIndex());
}

} // namespace cadly::ui
