#include "cadly/ui/ThemeTokens.h"

namespace cadly::ui {

// Values from docs/ui-redesign/design-notes.md ("Visual tokens"). Alpha
// colours are expressed with QColor's argb constructor so they composite over
// whichever region fill they land on.
const ThemeTokens& ThemeTokens::dark_tokens() {
  static const ThemeTokens t = [] {
    ThemeTokens d;
    d.dark           = true;
    d.accent         = QColor(0x50, 0x86, 0xFF);
    d.toolbar_bg     = QColor(0x26, 0x28, 0x2D);
    d.sidebar_bg     = QColor(0x21, 0x23, 0x28);
    d.inspector_bg   = QColor(0x23, 0x25, 0x29);
    d.strip_bg       = QColor(0x20, 0x22, 0x26);
    d.log_bg         = QColor(0x17, 0x18, 0x1C);
    d.status_bg      = QColor(0x20, 0x22, 0x26);
    d.popover_bg     = QColor(0x2A, 0x2C, 0x31);
    d.text1          = QColor(0xE6, 0xE8, 0xEC);
    d.text2          = QColor(0xA0, 0xA6, 0xB0);
    d.text3          = QColor(0x6E, 0x74, 0x7E);
    d.hairline       = QColor(0, 0, 0, 115);        // rgba(0,0,0,.45)
    d.hairline_soft  = QColor(255, 255, 255, 18);   // rgba(255,255,255,.07)
    d.control_bg     = QColor(255, 255, 255, 15);
    d.control_hover  = QColor(255, 255, 255, 26);
    d.control_active = QColor(255, 255, 255, 36);
    d.selection_bg   = QColor(0x50, 0x86, 0xFF, 66);
    d.ok             = QColor(0x2B, 0xB5, 0xA0);
    d.warn           = QColor(0xFB, 0xC0, 0x64);
    d.error          = QColor(0xE9, 0x6B, 0x72);
    d.info           = QColor(0x1B, 0xA8, 0xD5);
    d.viewport_highlight = QColor(0xFF, 0x7A, 0x26);
    // Pale copper with darker hatch ink makes the cut distinct from the shell.
    d.viewport_section   = QColor(0xE7, 0xBF, 0x96);
    d.viewport_top    = QColor(0xAC, 0xB0, 0xB7);   // unchanged from Theme.cpp era
    d.viewport_bottom = QColor(0x80, 0x83, 0x8A);
    return d;
  }();
  return t;
}

const ThemeTokens& ThemeTokens::light_tokens() {
  static const ThemeTokens t = [] {
    ThemeTokens l;
    l.dark           = false;
    l.accent         = QColor(0x3D, 0x6F, 0xE0);
    l.toolbar_bg     = QColor(0xF2, 0xF3, 0xF5);
    l.sidebar_bg     = QColor(0xEC, 0xED, 0xF0);
    l.inspector_bg   = QColor(0xF6, 0xF7, 0xF9);
    l.strip_bg       = QColor(0xEC, 0xED, 0xF0);
    l.log_bg         = QColor(0xFA, 0xFB, 0xFC);
    l.status_bg      = QColor(0xEC, 0xED, 0xF0);
    l.popover_bg     = QColor(0xFF, 0xFF, 0xFF);
    l.text1          = QColor(0x1E, 0x21, 0x26);
    l.text2          = QColor(0x5A, 0x61, 0x6C);
    l.text3          = QColor(0x9A, 0xA0, 0xAA);
    l.hairline       = QColor(0, 0, 0, 41);         // rgba(0,0,0,.16)
    l.hairline_soft  = QColor(0, 0, 0, 26);         // rgba(0,0,0,.10)
    l.control_bg     = QColor(0, 0, 0, 13);
    l.control_hover  = QColor(0, 0, 0, 21);
    l.control_active = QColor(0, 0, 0, 31);
    l.selection_bg   = QColor(0x3D, 0x6F, 0xE0, 56);
    l.ok             = QColor(0x1E, 0x8A, 0x7A);
    l.warn           = QColor(0xB0, 0x7D, 0x2A);
    l.error          = QColor(0xC8, 0x4F, 0x56);
    l.info           = QColor(0x15, 0x7F, 0xA0);
    // A notch darker than the dark variant so it keeps its punch against the
    // lighter viewport gradient.
    l.viewport_highlight = QColor(0xE8, 0x62, 0x10);
    // A notch deeper than the dark theme's, for the same reason the highlight
    // is: it has to keep its weight against a lighter viewport gradient.
    l.viewport_section   = QColor(0xDF, 0xB3, 0x89);
    l.viewport_top    = QColor(0xDA, 0xDD, 0xE2);
    l.viewport_bottom = QColor(0xAB, 0xAF, 0xB7);
    return l;
  }();
  return t;
}

ThemeManager& ThemeManager::instance() {
  static ThemeManager mgr;
  return mgr;
}

void ThemeManager::set_dark(bool on) {
  if (dark_ == on) return;
  dark_ = on;
  emit changed();
}

} // namespace cadly::ui
