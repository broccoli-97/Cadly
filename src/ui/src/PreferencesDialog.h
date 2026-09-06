#pragma once

// Application preferences: a non-modal, instant-apply window (macOS
// Settings… convention; acceptable everywhere) for the settings a user
// chooses once — as opposed to the Inspector, which holds live
// view/document controls. Reached via the Preferences action (relocated to
// the app menu with ⌘, on macOS by its PreferencesRole; File ▸ Preferences…
// with Ctrl+, elsewhere). Private to the ui module; MainWindow owns the
// instance, populates it on open, and persists changes.

#include "cadly/ui/CameraController.h"
#include "cadly/ui/NavigationScheme.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;

namespace cadly::ui {

class PreferencesDialog : public QDialog {
  Q_OBJECT
public:
  explicit PreferencesDialog(QWidget* parent = nullptr);

  // Silent setters for populating from current state.
  void set_language(const QString& code);
  void set_dark(bool dark);
  void set_navigation_scheme(NavigationScheme scheme);
  void set_orbit_style(CameraController::OrbitStyle style);

signals:
  // Emitted on user edits only (instant apply; the owner persists).
  void language_selected(const QString& code);
  void dark_toggled(bool dark);
  void navigation_scheme_changed(NavigationScheme scheme);
  void orbit_style_changed(CameraController::OrbitStyle style);

private:
  QWidget* build_general_tab();
  QWidget* build_navigation_tab();

  QComboBox* language_{nullptr};
  QCheckBox* dark_{nullptr};
  QComboBox* nav_scheme_{nullptr};
  QComboBox* orbit_style_{nullptr};
  QLabel*    legend_{nullptr};
};

} // namespace cadly::ui
