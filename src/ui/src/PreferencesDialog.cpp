#include "PreferencesDialog.h"

#include "cadly/input_qt/NavigationLabels.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

namespace cadly::ui {

PreferencesDialog::PreferencesDialog(input_qt::InputPreferences& input_preferences,
                                     QWidget* parent)
    : QDialog(parent), input_preferences_(input_preferences) {
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
  sync_input_preferences();
  connect(&input_preferences_, &input_qt::InputPreferences::changed,
          this, &PreferencesDialog::sync_input_preferences);
}

QWidget* PreferencesDialog::build_general_tab() {
  auto* page = new QWidget(this);
  auto* form = new QFormLayout(page);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

  // Language entries name themselves (same rule as the View menu twin):
  // whatever language is active, the way back stays readable.
  language_ = new QComboBox(page);
  language_->setObjectName(QStringLiteral("language"));
  language_->addItem(tr("System Language"), QStringLiteral("system"));
  language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
  language_->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
  language_->setToolTip(tr("Applies the next time Cadly starts"));
  connect(language_, &QComboBox::currentIndexChanged, this, [this](int) {
    emit language_selected(language_->currentData().toString());
  });

  dark_ = new QCheckBox(tr("Dark appearance"), page);
  dark_->setObjectName(QStringLiteral("darkAppearance"));
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
  nav_scheme_->setObjectName(QStringLiteral("navigationScheme"));
  for (const auto& preset : input::navigation_presets()) {
    nav_scheme_->addItem(input_qt::navigation_scheme_name(preset.scheme),
                         static_cast<int>(preset.scheme));
  }
  nav_scheme_->setToolTip(
    tr("Mouse bindings for orbit, pan, and zoom, matching a familiar "
       "CAD package"));

  using OrbitStyle = input::OrbitStyle;
  orbit_style_ = new QComboBox(page);
  orbit_style_->setObjectName(QStringLiteral("orbitStyle"));
  for (const auto s : {OrbitStyle::Free, OrbitStyle::Turntable}) {
    orbit_style_->addItem(input_qt::orbit_style_name(s), static_cast<int>(s));
  }
  orbit_style_->setToolTip(
    tr("Free orbit tumbles in screen space (no pole, horizon may tilt); "
       "turntable keeps up fixed with a clamped elevation"));

  legend_ = new QLabel(page);
  legend_->setObjectName(QStringLiteral("navigationLegend"));
  legend_->setWordWrap(true);
  legend_->setTextFormat(Qt::PlainText);
  legend_->setEnabled(false);  // secondary text
  connect(nav_scheme_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0) return;
    auto preferences = input_preferences_.value();
    preferences.navigation_scheme = static_cast<input::NavigationScheme>(
      nav_scheme_->currentData().toInt());
    input_preferences_.set_value(preferences);
  });
  connect(orbit_style_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0) return;
    auto preferences = input_preferences_.value();
    preferences.orbit_style = static_cast<input::OrbitStyle>(
      orbit_style_->currentData().toInt());
    input_preferences_.set_value(preferences);
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

void PreferencesDialog::sync_input_preferences() {
  const QSignalBlocker scheme_block(nav_scheme_);
  const QSignalBlocker orbit_block(orbit_style_);
  const auto& preferences = input_preferences_.value();
  nav_scheme_->setCurrentIndex(
    nav_scheme_->findData(static_cast<int>(preferences.navigation_scheme)));
  orbit_style_->setCurrentIndex(
    orbit_style_->findData(static_cast<int>(preferences.orbit_style)));
  legend_->setText(input_qt::navigation_scheme_legend(preferences.navigation_scheme));
}

} // namespace cadly::ui
