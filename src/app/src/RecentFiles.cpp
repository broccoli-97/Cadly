#include "cadly/app/RecentFiles.h"

#include <QSettings>

namespace cadly::app {

namespace {
QSettings settings_handle() {
  return QSettings(QSettings::IniFormat, QSettings::UserScope, "Cadly", "Cadly");
}
}

RecentFiles::RecentFiles(QObject* parent, int max_entries)
  : QObject(parent), max_entries_(max_entries) {}

QStringList RecentFiles::entries() const {
  return settings_handle().value("io/recent_files").toStringList();
}

void RecentFiles::add(const QString& path) {
  auto s = settings_handle();
  auto list = s.value("io/recent_files").toStringList();
  list.removeAll(path);
  list.prepend(path);
  while (list.size() > max_entries_) list.removeLast();
  s.setValue("io/recent_files", list);
  emit changed();
}

void RecentFiles::clear() {
  auto s = settings_handle();
  s.remove("io/recent_files");
  emit changed();
}

} // namespace cadly::app
