#include "PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

namespace cadly::ui {

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(tr("Preferences"));
  // Instant apply: closing is the only "button". Non-modal so the effect of
  // a change (orbit style, theme) is visible in the shell immediately.
  setModal(false);
  auto* layout = new QVBoxLayout(this);
  auto* tabs = new QTabWidget(this);
  tabs->addTab(build_general_tab(), tr("General"));
  tabs->addTab(build_navigation_tab(), tr("Navigation"));
  layout->addWidget(tabs);
  setMinimumWidth(420);
}

QWidget* PreferencesDialog::build_general_tab() {
  auto* page = new QWidget(this);
  auto* form = new QFormLayout(page);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

  // Language entries name themselves (same rule as the View menu twin):
  // whatever language is active, the way back stays readable.
  language_ = new QComboBox(page);
  language_->addItem(tr("System Language"), QStringLiteral("system"));
  language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
  language_->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
  language_->setToolTip(tr("Applies the next time Cadly starts"));
  connect(language_, &QComboBox::currentIndexChanged, this, [this](int) {
    emit language_selected(language_->currentData().toString());
  });

  dark_ = new QCheckBox(tr("Dark appearance"), page);
  connect(dark_, &QCheckBox::toggled, this,
          [this](bool on) { emit dark_toggled(on); });

  form->addRow(tr("Language"), language_);
  form->addRow(dark_);
  return page;
}

QWidget* PreferencesDialog::build_navigation_tab() {
  auto* page = new QWidget(this);
  auto* form = new QFormLayout(page);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

  nav_scheme_ = new QComboBox(page);
  for (const auto s : kAllNavigationSchemes) {
    nav_scheme_->addItem(navigation_scheme_name(s), static_cast<int>(s));
  }
  nav_scheme_->setToolTip(
    tr("Mouse bindings for orbit, pan, and zoom, matching a familiar "
       "CAD package"));

  using OrbitStyle = CameraController::OrbitStyle;
  orbit_style_ = new QComboBox(page);
  for (const auto s : {OrbitStyle::Free, OrbitStyle::Turntable}) {
    orbit_style_->addItem(orbit_style_name(s), static_cast<int>(s));
  }
  orbit_style_->setToolTip(
    tr("Free orbit tumbles in screen space (no pole, horizon may tilt); "
       "turntable keeps up fixed with a clamped elevation"));

  legend_ = new QLabel(page);
  legend_->setWordWrap(true);
  legend_->setTextFormat(Qt::PlainText);
  legend_->setEnabled(false);  // secondary text
  legend_->setText(navigation_scheme_legend(NavigationScheme::Cadly));

  connect(nav_scheme_, &QComboBox::currentIndexChanged, this, [this](int) {
    const auto scheme = static_cast<NavigationScheme>(
      nav_scheme_->currentData().toInt());
    legend_->setText(navigation_scheme_legend(scheme));
    emit navigation_scheme_changed(scheme);
  });
  connect(orbit_style_, &QComboBox::currentIndexChanged, this, [this](int) {
    emit orbit_style_changed(static_cast<CameraController::OrbitStyle>(
      orbit_style_->currentData().toInt()));
  });

  form->addRow(tr("Navigation"), nav_scheme_);
  form->addRow(tr("Orbit style"), orbit_style_);
  form->addRow(legend_);
  return page;
}

void PreferencesDialog::set_language(const QString& code) {
  const QSignalBlocker block(language_);
  const int index = language_->findData(code);
  language_->setCurrentIndex(index < 0 ? 0 : index);
}

void PreferencesDialog::set_dark(bool dark) {
  const QSignalBlocker block(dark_);
  dark_->setChecked(dark);
}

void PreferencesDialog::set_navigation_scheme(NavigationScheme scheme) {
  const QSignalBlocker block(nav_scheme_);
  nav_scheme_->setCurrentIndex(
    nav_scheme_->findData(static_cast<int>(scheme)));
  legend_->setText(navigation_scheme_legend(scheme));
}

void PreferencesDialog::set_orbit_style(CameraController::OrbitStyle style) {
  const QSignalBlocker block(orbit_style_);
  orbit_style_->setCurrentIndex(
    orbit_style_->findData(static_cast<int>(style)));
}

} // namespace cadly::ui
