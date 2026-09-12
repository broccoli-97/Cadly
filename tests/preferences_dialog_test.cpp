#include "PreferencesDialog.h"
#include "cadly/app/Settings.h"
#include "cadly/input_qt/NavigationLabels.h"
#include "cadly/ui/MainWindow.h"
#include "cadly/ui/ViewportWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace cadly::ui {

class PreferencesDialogTest : public QObject {
  Q_OBJECT
private slots:
  void initial_population_is_silent();
  void editors_share_state_without_feedback_loops();
  void edits_persist_through_the_application_boundary();
  void shell_forwards_one_snapshot_and_does_not_persist_navigation();
};

void PreferencesDialogTest::initial_population_is_silent() {
  const input::Preferences initial{input::NavigationScheme::Maya, input::OrbitStyle::Turntable};
  input_qt::InputPreferences preferences(initial);
  QSignalSpy changed(&preferences, &input_qt::InputPreferences::changed);
  PreferencesDialog dialog(preferences);
  auto* scheme = dialog.findChild<QComboBox*>(QStringLiteral("navigationScheme"));
  auto* style = dialog.findChild<QComboBox*>(QStringLiteral("orbitStyle"));
  auto* legend = dialog.findChild<QLabel*>(QStringLiteral("navigationLegend"));
  QVERIFY(scheme && style && legend);
  QCOMPARE(scheme->count(), static_cast<int>(input::navigation_presets().size()));
  QCOMPARE(scheme->currentData().toInt(), static_cast<int>(initial.navigation_scheme));
  QCOMPARE(style->currentData().toInt(), static_cast<int>(initial.orbit_style));
  QCOMPARE(legend->text(), input_qt::navigation_scheme_legend(initial.navigation_scheme));
  QCOMPARE(changed.count(), 0);

  QSignalSpy language(&dialog, &PreferencesDialog::language_selected);
  QSignalSpy dark(&dialog, &PreferencesDialog::dark_toggled);
  dialog.set_language(QStringLiteral("zh_CN"));
  dialog.set_dark(true);
  QCOMPARE(language.count(), 0);
  QCOMPARE(dark.count(), 0);
  auto* language_combo = dialog.findChild<QComboBox*>(QStringLiteral("language"));
  auto* dark_checkbox = dialog.findChild<QCheckBox*>(QStringLiteral("darkAppearance"));
  QVERIFY(language_combo && dark_checkbox);
  language_combo->setCurrentIndex(language_combo->findData(QStringLiteral("en")));
  dark_checkbox->setChecked(false);
  QCOMPARE(language.count(), 1);
  QCOMPARE(language.first().first().toString(), QStringLiteral("en"));
  QCOMPARE(dark.count(), 1);
  QVERIFY(preferences.value() == initial);
  QCOMPARE(changed.count(), 0);
}

void PreferencesDialogTest::editors_share_state_without_feedback_loops() {
  input_qt::InputPreferences preferences({input::NavigationScheme::Rhino, input::OrbitStyle::Turntable});
  PreferencesDialog first(preferences), second(preferences);
  QSignalSpy changed(&preferences, &input_qt::InputPreferences::changed);
  auto* first_scheme = first.findChild<QComboBox*>(QStringLiteral("navigationScheme"));
  auto* second_scheme = second.findChild<QComboBox*>(QStringLiteral("navigationScheme"));
  auto* first_style = first.findChild<QComboBox*>(QStringLiteral("orbitStyle"));
  auto* second_style = second.findChild<QComboBox*>(QStringLiteral("orbitStyle"));
  QVERIFY(first_scheme && second_scheme && first_style && second_style);
  first_scheme->setCurrentIndex(first_scheme->findData(static_cast<int>(input::NavigationScheme::Maya)));
  QCOMPARE(preferences.value().navigation_scheme, input::NavigationScheme::Maya);
  QCOMPARE(preferences.value().orbit_style, input::OrbitStyle::Turntable);
  QCOMPARE(first_scheme->currentData(), second_scheme->currentData());
  QCOMPARE(changed.count(), 1);

  second_style->setCurrentIndex(second_style->findData(static_cast<int>(input::OrbitStyle::Free)));
  QCOMPARE(preferences.value().navigation_scheme, input::NavigationScheme::Maya);
  QCOMPARE(preferences.value().orbit_style, input::OrbitStyle::Free);
  QCOMPARE(first_style->currentData(), second_style->currentData());
  QCOMPARE(changed.count(), 2);

  preferences.set_value({input::NavigationScheme::Fusion360, input::OrbitStyle::Turntable});
  QCOMPARE(first_scheme->currentData().toInt(), static_cast<int>(input::NavigationScheme::Fusion360));
  QCOMPARE(second_style->currentData().toInt(), static_cast<int>(input::OrbitStyle::Turntable));
  QCOMPARE(changed.count(), 3);
}

void PreferencesDialogTest::edits_persist_through_the_application_boundary() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto path = directory.filePath(QStringLiteral("preferences.ini"));
  app::Settings settings(path);
  settings.set_language(QStringLiteral("zh_CN"));
  input_qt::InputPreferences preferences(settings.input_preferences());
  connect(&preferences, &input_qt::InputPreferences::changed, &settings, [&]() {
    settings.set_input_preferences(preferences.value());
  });
  PreferencesDialog dialog(preferences);
  auto* scheme = dialog.findChild<QComboBox*>(QStringLiteral("navigationScheme"));
  auto* style = dialog.findChild<QComboBox*>(QStringLiteral("orbitStyle"));
  QVERIFY(scheme && style);
  scheme->setCurrentIndex(scheme->findData(static_cast<int>(input::NavigationScheme::Blender)));
  style->setCurrentIndex(style->findData(static_cast<int>(input::OrbitStyle::Turntable)));
  const input::Preferences expected{input::NavigationScheme::Blender, input::OrbitStyle::Turntable};
  QVERIFY(app::Settings(path).input_preferences() == expected);
  QCOMPARE(settings.language(), QStringLiteral("zh_CN"));
  dialog.close();
  input_qt::InputPreferences reloaded(app::Settings(path).input_preferences());
  PreferencesDialog reopened(reloaded);
  QVERIFY(reloaded.value() == expected);
}

void PreferencesDialogTest::shell_forwards_one_snapshot_and_does_not_persist_navigation() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  // MainWindow still owns display/layout persistence. Redirect both fallback
  // scopes before constructing it so this integration test cannot touch the
  // real user's configuration on Windows, macOS, or Linux.
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, directory.path());
  QSettings raw(QSettings::IniFormat, QSettings::UserScope, "Cadly", "Cadly");
  raw.setValue("display/navigation_scheme", "future-scheme");
  raw.setValue("display/orbit_style", "future-orbit");
  raw.sync();
  input_qt::InputPreferences preferences({input::NavigationScheme::Maya, input::OrbitStyle::Turntable});
  MainWindow window(preferences);
  auto* viewport = window.findChild<ViewportWidget*>();
  QVERIFY(viewport);
  QVERIFY(viewport->input_preferences() == preferences.value());
  preferences.set_value({input::NavigationScheme::Blender, input::OrbitStyle::Free});
  QVERIFY(viewport->input_preferences() == preferences.value());
  window.run_demo(QStringLiteral("preferences"));
  auto* editor = window.findChild<PreferencesDialog*>();
  QVERIFY(editor);
  auto* scheme = editor->findChild<QComboBox*>(QStringLiteral("navigationScheme"));
  QVERIFY(scheme);
  scheme->setCurrentIndex(scheme->findData(static_cast<int>(input::NavigationScheme::Rhino)));
  QCOMPARE(viewport->input_preferences().navigation_scheme, input::NavigationScheme::Rhino);
  editor->close();
  window.close();
  raw.sync();
  // No app persistence connection was installed here. Closing the shell must
  // not pull values back out of a camera/viewport and overwrite the store.
  QCOMPARE(raw.value("display/navigation_scheme").toString(), QStringLiteral("future-scheme"));
  QCOMPARE(raw.value("display/orbit_style").toString(), QStringLiteral("future-orbit"));
}

} // namespace cadly::ui

QTEST_MAIN(cadly::ui::PreferencesDialogTest)
#include "preferences_dialog_test.moc"
