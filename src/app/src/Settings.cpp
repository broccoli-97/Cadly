#include "cadly/app/Settings.h"

#include <QSettings>
#include <QStandardPaths>

namespace cadly::app {

namespace {
QSettings settings_handle() {
  return QSettings(QSettings::IniFormat, QSettings::UserScope, "Cadly", "Cadly");
}
}

Settings::Settings(QObject* parent) : QObject(parent) {}

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
