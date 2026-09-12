#pragma once

#include "cadly/input/Navigation.h"

#include <QObject>
#include <QString>

class QSettings;

namespace cadly::app {

// Persistence for application preferences. Navigation schema/migration stays
// here, never in a viewport or editor. Display/layout state is still owned by
// the shell; this facade only writes the keys it owns in the shared INI store.
class Settings : public QObject {
  Q_OBJECT
public:
  explicit Settings(QObject* parent = nullptr);
  // Explicit store for tests / embedding; never redirects global QSettings
  // paths or touches the user's normal configuration.
  explicit Settings(const QString& file_name, QObject* parent = nullptr);

  input::Preferences input_preferences() const;
  void set_input_preferences(input::Preferences preferences);

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

private:
  QSettings settings_handle() const;
  QString file_name_;
};

} // namespace cadly::app
