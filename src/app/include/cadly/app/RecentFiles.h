#pragma once

#include <QObject>
#include <QStringList>

namespace cadly::app {

// Recent-files list, capped at a small constant. Persisted to QSettings under
// "io/recent_files".
class RecentFiles : public QObject {
  Q_OBJECT
public:
  explicit RecentFiles(QObject* parent = nullptr, int max_entries = 8);

  QStringList entries() const;
  void add(const QString& path);
  void clear();

signals:
  void changed();

private:
  int max_entries_;
};

} // namespace cadly::app
