#include "cadly/app/Settings.h"

#include <QSettings>
#include <QStandardPaths>

namespace cadly::app {

namespace {
input::Preferences read_input_preferences(QSettings& settings) {
  return {
    input::navigation_scheme_from_key(
      settings.value("display/navigation_scheme").toString().toStdString()),
    input::orbit_style_from_key(
      settings.value("display/orbit_style").toString().toStdString()),
  };
}
}

Settings::Settings(QObject* parent) : QObject(parent) {}

Settings::Settings(const QString& file_name, QObject* parent)
  : QObject(parent), file_name_(file_name) {}

QSettings Settings::settings_handle() const {
  if (!file_name_.isEmpty()) return QSettings(file_name_, QSettings::IniFormat);
  return QSettings(QSettings::IniFormat, QSettings::UserScope, "Cadly", "Cadly");
}

input::Preferences Settings::input_preferences() const {
  auto s = settings_handle();
  return read_input_preferences(s);
}

void Settings::set_input_preferences(input::Preferences preferences) {
  preferences = input::normalized(preferences);
  auto s = settings_handle();
  const auto previous = read_input_preferences(s);
  // Retain these legacy paths and tokens: external consumers such as Quick
  // Look read them too. Write only changed fields, preserving unknown future
  // tokens in untouched fields and unrelated keys in this shared store.
  if (previous.navigation_scheme != preferences.navigation_scheme) {
    const auto key = input::navigation_scheme_key(preferences.navigation_scheme);
    s.setValue("display/navigation_scheme",
               QString::fromUtf8(key.data(), static_cast<int>(key.size())));
  }
  if (previous.orbit_style != preferences.orbit_style) {
    const auto key = input::orbit_style_key(preferences.orbit_style);
    s.setValue("display/orbit_style",
               QString::fromUtf8(key.data(), static_cast<int>(key.size())));
  }
  s.sync();
}

QString Settings::last_open_directory() const {
  auto s = settings_handle();
  return s.value("io/last_open_directory",
                 QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
          .toString();
}

void Settings::set_last_open_directory(const QString& dir) {
  auto s = settings_handle();
  s.setValue("io/last_open_directory", dir);
}

bool Settings::dark_theme() const {
  auto s = settings_handle();
  return s.value("ui/dark_theme", true).toBool();
}

void Settings::set_dark_theme(bool dark) {
  auto s = settings_handle();
  s.setValue("ui/dark_theme", dark);
}

QString Settings::language() const {
  auto s = settings_handle();
  return s.value("ui/language", QStringLiteral("system")).toString();
}

void Settings::set_language(const QString& code) {
  auto s = settings_handle();
  s.setValue("ui/language", code);
}

QByteArray Settings::window_geometry() const {
  auto s = settings_handle();
  return s.value("window/geometry").toByteArray();
}

void Settings::set_window_geometry(const QByteArray& blob) {
  auto s = settings_handle();
  s.setValue("window/geometry", blob);
}

} // namespace cadly::app
