#pragma once

#include <QObject>
#include <QString>

namespace cadly::app {

// Thin wrapper over QSettings that exposes only the values the rest of the app
// cares about. Keeps direct QSettings calls out of MainWindow and friends so
// rename/migration stays local.
class Settings : public QObject {
  Q_OBJECT
public:
  explicit Settings(QObject* parent = nullptr);

  QString last_open_directory() const;
  void    set_last_open_directory(const QString& dir);

  // Window geometry / dock state are stored as opaque blobs.
  QByteArray window_geometry() const;
  QByteArray window_state() const;
  void       set_window_geometry(const QByteArray& blob);
  void       set_window_state(const QByteArray& blob);
};

} // namespace cadly::app
