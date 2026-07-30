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

  // Appearance variant for the Graphite shell (ui::ThemeTokens).
  bool dark_theme() const;
  void set_dark_theme(bool dark);

  // UI language: "system" (follow QLocale::system()), or a locale code with
  // a shipped catalog ("en", "zh_CN"). Read once at startup, before any
  // widget is constructed — changing it takes effect on the next launch.
  QString language() const;
  void    set_language(const QString& code);

  // Window geometry is stored as an opaque blob. (The old window *state*
  // blob — QMainWindow dock/toolbar layout — died with the QDockWidget
  // shell; panel layout now persists as explicit keys written by MainWindow.)
  QByteArray window_geometry() const;
  void       set_window_geometry(const QByteArray& blob);
};

} // namespace cadly::app
