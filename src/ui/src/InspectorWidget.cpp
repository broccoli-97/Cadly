#include "InspectorWidget.h"

#include "IconUtils.h"
#include "ImportOptionsWidget.h"
#include "PropertiesPanel.h"
#include "ToolbarButton.h"
#include "cadly/ui/ThemeTokens.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
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

  struct TabSpec { const char* icon; const char* tip; };
  const TabSpec specs[3] = {
    {"document/properties",            "Properties"},
    {"navigation/sliders-horizontal",  "Display"},
    {"action/download",                "Import"},
  };
  tabs->addStretch();
  for (int i = 0; i < 3; ++i) {
    auto* b = new ToolbarButton(this);
    b->setCheckable(true);
    b->setIcon(themed_icon(QLatin1String(specs[i].icon)));
    b->setText(tr(specs[i].tip));
    b->set_show_text(true);
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

  connect(&ThemeManager::instance(), &ThemeManager::changed,
          this, QOverload<>::of(&QWidget::update));
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

  auto* form = new QFormLayout();
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(8);

  edge_intensity_ = new QSlider(Qt::Horizontal, pane);
  edge_intensity_->setRange(0, 100);
  edge_intensity_->setValue(65);
  edge_intensity_->setToolTip(tr("Ink strength of the BRep edge overlay"));

  msaa_ = new QComboBox(pane);
  msaa_->addItem(tr("Off"), 0);
  msaa_->addItem(QStringLiteral("2×"), 2);
  msaa_->addItem(QStringLiteral("4×"), 4);
  msaa_->addItem(QStringLiteral("8×"), 8);
  msaa_->setCurrentIndex(2);
  msaa_->setToolTip(
    tr("Renderer-owned multisampling; clamped to the GPU's maximum"));

  scale_bar_ = new QCheckBox(tr("Scale bar"), pane);
  scale_bar_->setChecked(true);
  axes_ = new QCheckBox(tr("Orientation axes"), pane);
  axes_->setChecked(true);

  form->addRow(tr("Edge intensity"), edge_intensity_);
  form->addRow(tr("Anti-aliasing"), msaa_);
  form->addRow(scale_bar_);
  form->addRow(axes_);
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
  return pane;
}

QWidget* InspectorWidget::build_import_pane() {
  auto* pane = new QWidget();
  auto* outer = new QVBoxLayout(pane);
  outer->setContentsMargins(12, 10, 12, 10);
  outer->setSpacing(10);

  import_options_ = new ImportOptionsWidget(pane);
  outer->addWidget(import_options_);

  review_each_ = new QCheckBox(tr("Review options before each import"), pane);
  review_each_->setToolTip(
    tr("Opens these options for confirmation whenever a file is opened "
       "(the old always-on dialog, now opt-in)."));
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
