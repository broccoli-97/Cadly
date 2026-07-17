#pragma once

class QApplication;

namespace cadly::app {

// Applies the application-wide look and registers the qlementine icon
// resources. `dark` selects the variant of the Graphite token table the
// custom-painted chrome reads (ui::ThemeTokens); this function keeps the
// stock-widget side (QStyle + QPalette) in step with it:
//
//   Qt >= 6.8 -> one persistent qlementine QStyle, switching its official
//                Light and Cadly Dark themes in place
//   older Qt  -> Fusion + hand-tuned Graphite palette
//   qlementine/theme initialization failure -> Fusion fallback
//
// Call once right after QApplication is constructed, and again whenever the
// user flips the appearance toggle (ui::ThemeManager::changed). Existing
// widgets repolish automatically; the QStyle itself is not replaced.
void apply_theme(QApplication& app, bool dark = true);

} // namespace cadly::app
