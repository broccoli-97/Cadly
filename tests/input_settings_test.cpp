#include "cadly/app/Settings.h"
#include "TestChecks.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMap>
#include <QSettings>
#include <QTemporaryDir>

using namespace cadly;

namespace {
QMap<QString, QVariant> snapshot(QSettings& settings) {
  QMap<QString, QVariant> result;
  settings.sync();
  for (const auto& key : settings.allKeys()) result.insert(key, settings.value(key));
  return result;
}

void defaults_do_not_write(const QString& path) {
  app::Settings settings(path);
  CHECK(settings.input_preferences() == input::Preferences{});
  CHECK(settings.dark_theme());
  CHECK(settings.language() == QStringLiteral("system"));
  CHECK(settings.window_geometry().isEmpty());
  CHECK(!QFileInfo::exists(path));
  settings.set_input_preferences({});
  CHECK(!QFileInfo::exists(path));
}

void legacy_read_and_roundtrip(const QString& path) {
  using S = input::NavigationScheme;
  using O = input::OrbitStyle;
  const std::pair<S, const char*> schemes[] = {
    {S::Cadly, "cadly"}, {S::Blender, "blender"}, {S::Rhino, "rhino"},
    {S::Fusion360, "fusion360"}, {S::Maya, "maya"},
  };
  for (const auto& scheme : schemes) {
    for (const auto style : {O::Free, O::Turntable}) {
      QSettings legacy(path, QSettings::IniFormat);
      legacy.setValue("display/navigation_scheme", QLatin1String(scheme.second));
      legacy.setValue("display/orbit_style", style == O::Free ? "free" : "turntable");
      legacy.sync();
      CHECK(legacy.status() == QSettings::NoError);
      app::Settings settings(path);
      CHECK(settings.input_preferences() == input::Preferences{scheme.first, style});
      const auto before = snapshot(legacy);
      settings.set_input_preferences(settings.input_preferences());
      CHECK(snapshot(legacy) == before);

      const input::Preferences changed{scheme.first == S::Maya ? S::Cadly : S::Maya,
                                        style == O::Free ? O::Turntable : O::Free};
      settings.set_input_preferences(changed);
      CHECK(app::Settings(path).input_preferences() == changed);
      settings.set_input_preferences({scheme.first, style});
      CHECK(snapshot(legacy) == before);  // spelling and group names unchanged
    }
  }
}

void unknown_values_and_unrelated_keys_survive(const QString& path) {
  QSettings raw(path, QSettings::IniFormat);
  raw.setValue("display/navigation_scheme", "future-scheme");
  raw.setValue("display/orbit_style", "future-orbit");
  raw.setValue("display/msaa_samples", 8);
  raw.setValue("display/section_hatch", false);
  raw.setValue("display/section_show_plane", true);
  raw.setValue("navigation/future_option", "do-not-touch");
  raw.setValue("import/review_each", true);
  raw.setValue("import/linear_deflection", 0.003);
  raw.setValue("ui/dark_theme", false);
  raw.setValue("ui/language", "zh_CN");
  raw.setValue("ui/split_h", QByteArray("\0layout\1", 8));
  raw.setValue("window/geometry", QByteArray("\0geometry\1", 10));
  raw.setValue("io/last_open_directory", QString::fromUtf8("/test/\xe6\xa8\xa1\xe5\x9e\x8b folder"));
  raw.setValue("io/recent_files", QStringList{"one.step", "two.iges"});
  const auto before = snapshot(raw);
  app::Settings settings(path);
  CHECK(settings.input_preferences() == input::Preferences{});
  CHECK(!settings.dark_theme() && settings.language() == QStringLiteral("zh_CN"));
  CHECK(settings.window_geometry() == before.value("window/geometry").toByteArray());
  CHECK(settings.last_open_directory() == before.value("io/last_open_directory").toString());
  settings.set_input_preferences({});
  CHECK(snapshot(raw) == before);  // fallback on read is not a migration on disk

  auto value = settings.input_preferences();
  value.navigation_scheme = input::NavigationScheme::Blender;
  settings.set_input_preferences(value);
  auto after = snapshot(raw);
  CHECK(after.value("display/navigation_scheme") == QStringLiteral("blender"));
  CHECK(after.value("display/orbit_style") == QStringLiteral("future-orbit"));
  for (auto it = before.cbegin(); it != before.cend(); ++it) {
    if (it.key() != QLatin1String("display/navigation_scheme")) CHECK(after.value(it.key()) == it.value());
  }
  value.orbit_style = input::OrbitStyle::Turntable;
  settings.set_input_preferences(value);
  after = snapshot(raw);
  CHECK(after.value("display/orbit_style") == QStringLiteral("turntable"));
  CHECK(after.size() == before.size());

  // Existing general settings still use the same facade and do not rewrite navigation.
  settings.set_dark_theme(true);
  settings.set_language(QStringLiteral("en"));
  settings.set_last_open_directory(QStringLiteral("C:/CAD folder"));
  settings.set_window_geometry(QByteArray("new geometry"));
  CHECK(settings.input_preferences() == value);
  CHECK(settings.dark_theme() && settings.language() == QStringLiteral("en"));
  CHECK(settings.last_open_directory() == QStringLiteral("C:/CAD folder"));
  CHECK(settings.window_geometry() == QByteArray("new geometry"));
}

void independent_stores(const QString& first_path, const QString& second_path) {
  app::Settings first(first_path), second(second_path);
  first.set_input_preferences({input::NavigationScheme::Rhino, input::OrbitStyle::Turntable});
  second.set_input_preferences({input::NavigationScheme::Fusion360, input::OrbitStyle::Free});
  CHECK(first.input_preferences().navigation_scheme == input::NavigationScheme::Rhino);
  CHECK(second.input_preferences().navigation_scheme == input::NavigationScheme::Fusion360);
  CHECK(first.input_preferences().orbit_style == input::OrbitStyle::Turntable);
  CHECK(second.input_preferences().orbit_style == input::OrbitStyle::Free);
}
} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  if (!CHECK(directory.isValid())) return tests::report();
  defaults_do_not_write(directory.filePath("defaults.ini"));
  legacy_read_and_roundtrip(directory.filePath("legacy.ini"));
  unknown_values_and_unrelated_keys_survive(directory.filePath("future.ini"));
  independent_stores(directory.filePath("one.ini"), directory.filePath("two.ini"));
  return tests::report();
}
