#pragma once

class QApplication;

namespace cadly::app {

// Applies the application-wide look and registers the qlementine icon
// resources. `dark` selects the variant of the Graphite token table the
// custom-painted chrome reads (ui::ThemeTokens); this function keeps the
// stock-widget side (QStyle + QPalette) in step with it:
//
//   dark  + Qt >= 6.8  -> full qlementine QStyle with themes/dark.json
//   dark  + older Qt   -> Fusion + hand-tuned dark palette
//   light (any Qt)     -> Fusion + light palette (no light qlementine theme
//                         is vendored, and mixing the qlementine light
//                         default into the Graphite tokens looks wrong)
//
// Call once right after QApplication is constructed, and again whenever the
// user flips the appearance toggle (ui::ThemeManager::changed) — Qt supports
// runtime style/palette swaps, existing widgets repolish automatically.
void apply_theme(QApplication& app, bool dark = true);

} // namespace cadly::app
