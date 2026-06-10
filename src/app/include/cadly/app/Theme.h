#pragma once

class QApplication;

namespace cadly::app {

// Applies the application-wide look and registers the qlementine icon
// resources. On Qt >= 6.8 (CADLY_HAS_QLEMENTINE) this installs the full
// qlementine QStyle with the vendored dark theme from themes/dark.json; on
// older Qt it falls back to Fusion + a hand-tuned dark palette. Call once,
// right after QApplication is constructed and before any widget exists.
// Kept behind this helper so a light variant / user setting can slot in
// later without touching the startup sequence — see
// docs/ui-modernization-options.md for the full reasoning.
void apply_theme(QApplication& app);

} // namespace cadly::app
