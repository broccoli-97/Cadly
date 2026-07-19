#pragma once

#include <QColor>
#include <QObject>

namespace cadly::ui {

// Design tokens for the "Graphite" shell (docs/ui-redesign/design-notes.md).
//
// The custom-painted widgets (toolbar, segmented controls, sidebar
// delegate, popovers, …) read THESE tokens at paint time, not QPalette. The
// global QPalette / QStyle still themes the stock widgets (spin boxes, combo
// boxes, menus) — app::apply_theme keeps the two in step — but the bespoke
// chrome must render identically under Fusion (Qt 6.4 fallback) and
// qlementine (Qt 6.8), and a shared token table is what guarantees that.
//
// One deliberate relationship to preserve: in dark mode the viewport gradient
// is the LIGHTEST surface on screen — chrome sits below it in luminance so
// the part pops (see Theme.cpp). Don't darken the gradient or lighten the
// chrome past each other.
struct ThemeTokens {
  bool dark{true};

  QColor accent;            // the one blue (replaces the old #3574F0/#5086FF split)

  // Region fills.
  QColor toolbar_bg;
  QColor sidebar_bg;
  QColor inspector_bg;
  QColor strip_bg;          // diagnostics strip header / summary
  QColor log_bg;            // diagnostics log pane (recessed)
  QColor status_bg;         // status bar
  QColor popover_bg;        // popovers / menus-like cards

  // Text ramp.
  QColor text1;             // primary
  QColor text2;             // secondary
  QColor text3;             // tertiary / disabled

  // Hairlines.
  QColor hairline;          // between regions (strong)
  QColor hairline_soft;     // inside panels (subtle)

  // Interactive fills for custom controls (alpha colours over region fills).
  QColor control_bg;        // segmented track, chip at rest
  QColor control_hover;
  QColor control_active;    // checked/pressed neutral fill
  QColor selection_bg;      // sidebar row selection pill

  // Status colours (match themes/dark.json in the dark variant).
  QColor ok;
  QColor warn;
  QColor error;
  QColor info;

  // Viewport selection highlight (Node::selected tint). Deliberately NOT the
  // accent: the accent blue sits too close in hue and luminance to the
  // neutral bluish-grey parts and the viewport gradient, so a highlighted
  // part barely read as different. A saturated signal orange is the CAD
  // convention (NX/CATIA) precisely because nothing else in a typical scene
  // or in this chrome is orange — the tree keeps the accent for its
  // selection pill, the viewport uses this.
  QColor viewport_highlight;

  // Viewport gradient, pushed through DisplayMode::background_top/bottom.
  // Renderer-owned: the GL background pass draws it, not a Qt widget.
  QColor viewport_top;
  QColor viewport_bottom;

  static const ThemeTokens& dark_tokens();
  static const ThemeTokens& light_tokens();
};

// Process-wide theme switch. The ui module owns the token tables and the
// current dark/light flag; the app layer listens to `changed()` to re-apply
// the matching QPalette/QStyle and persist the choice (ui must not depend on
// app, so the wiring runs in that direction only).
class ThemeManager : public QObject {
  Q_OBJECT
public:
  static ThemeManager& instance();

  const ThemeTokens& tokens() const {
    return dark_ ? ThemeTokens::dark_tokens() : ThemeTokens::light_tokens();
  }
  bool dark() const { return dark_; }
  void set_dark(bool on);

signals:
  void changed();

private:
  ThemeManager() = default;
  bool dark_{true};
};

// Shorthand for widgets: `tokens().accent` etc.
inline const ThemeTokens& tokens() { return ThemeManager::instance().tokens(); }

} // namespace cadly::ui
