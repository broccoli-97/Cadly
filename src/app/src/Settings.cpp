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

QByteArray Settings::window_geometry() const {
  auto s = settings_handle();
  return s.value("window/geometry").toByteArray();
}

QByteArray Settings::window_state() const {
  auto s = settings_handle();
  return s.value("window/state").toByteArray();
}

void Settings::set_window_geometry(const QByteArray& blob) {
  auto s = settings_handle();
  s.setValue("window/geometry", blob);
}

void Settings::set_window_state(const QByteArray& blob) {
  auto s = settings_handle();
  s.setValue("window/state", blob);
}

} // namespace cadly::app
