#include "cadly/app/Theme.h"

#include "cadly/platform/Log.h"
#include "cadly/platform/Paths.h"
#include "cadly/ui/ThemeTokens.h"

#include <oclero/qlementine/icons/QlementineIcons.hpp>
#if defined(CADLY_HAS_QLEMENTINE)
#include <oclero/qlementine.hpp>
#endif

#include <QApplication>
#include <QPalette>
#include <QString>
#include <QStyleFactory>

namespace cadly::app {

namespace {

// Fusion is the one stock style that honours a custom QPalette everywhere;
// platform styles (windowsvista, the GTK shims) hardcode most of their
// colors and would ignore half the palette below. It also renders
// identically on Windows/Linux/macOS, so the app looks the same in every
// screenshot and bug report. Used as the Qt < 6.8 fallback in dark mode and
// for the light variant on every Qt (no light qlementine theme is vendored).
//
// The palette is derived from the Graphite token table so the stock widgets
// (spin boxes, combos, menus, scrollbars) sit on the same surfaces as the
// custom-painted chrome.
void apply_fusion_palette(QApplication& app, const cadly::ui::ThemeTokens& t) {
  app.setStyle(QStyleFactory::create("fusion"));

  const QColor window   = t.inspector_bg;
  const QColor base     = t.log_bg;
  const QColor alt_base = t.strip_bg;
  const QColor button   = t.dark ? QColor{0x3A, 0x3D, 0x41} : QColor{0xE2, 0xE4, 0xE8};
  const QColor text     = t.text1;
  const QColor dim_text = t.text3;
  const QColor accent   = t.accent;

  QPalette pal;
  pal.setColor(QPalette::Window, window);
  pal.setColor(QPalette::WindowText, text);
  pal.setColor(QPalette::Base, base);
  pal.setColor(QPalette::AlternateBase, alt_base);
  pal.setColor(QPalette::ToolTipBase, t.popover_bg);
  pal.setColor(QPalette::ToolTipText, text);
  pal.setColor(QPalette::PlaceholderText, dim_text);
  pal.setColor(QPalette::Text, text);
  pal.setColor(QPalette::Button, button);
  pal.setColor(QPalette::ButtonText, text);
  pal.setColor(QPalette::BrightText, t.error);
  pal.setColor(QPalette::Link, accent.lighter(t.dark ? 115 : 100));
  pal.setColor(QPalette::Highlight, accent);
  pal.setColor(QPalette::HighlightedText, Qt::white);

  pal.setColor(QPalette::Disabled, QPalette::WindowText, dim_text);
  pal.setColor(QPalette::Disabled, QPalette::Text, dim_text);
  pal.setColor(QPalette::Disabled, QPalette::ButtonText, dim_text);
  pal.setColor(QPalette::Disabled, QPalette::Highlight,
               t.dark ? QColor{0x3C, 0x3F, 0x44} : QColor{0xC9, 0xCC, 0xD2});
  pal.setColor(QPalette::Disabled, QPalette::HighlightedText, dim_text);
  // Fusion etches disabled text with a 1px offset copy drawn in Light, a
  // trick that only works on light backgrounds — on a dark palette it reads
  // as a white ghost behind every disabled label. Match Light to the window
  // color so the etch disappears (harmless no-op in the light variant).
  pal.setColor(QPalette::Disabled, QPalette::Light, window);
  app.setPalette(pal);
}

#if defined(CADLY_HAS_QLEMENTINE)
// The real deal on Qt >= 6.8: qlementine restyles every widget coherently
// (rounded controls, smooth hover/check animations, focus rings) instead of
// recoloring Fusion shapes. The look is driven by a JSON theme; ours is
// vendored from qlementine's showcase app in <source-root>/themes/dark.json
// (the library ships no themes of its own, only a built-in light default)
// and shipped like the shaders, via find_asset_dir.
//
// Returns false if the theme JSON can't be located — the caller then falls
// back to Fusion rather than mixing qlementine's light default into an app
// whose icons and viewport assume a dark chrome.
bool apply_qlementine_dark(QApplication& app) {
  const auto themes_dir = platform::find_asset_dir("themes");
  if (!themes_dir) {
    CADLY_LOG_WARN("themes/ asset dir not found; falling back to Fusion dark");
    return false;
  }
  const auto theme_json = *themes_dir / "dark.json";
  auto* style = new oclero::qlementine::QlementineStyle(&app);
  style->setThemeJsonPath(QString::fromStdString(theme_json.string()));
  QApplication::setStyle(style);  // takes ownership via parent
  return true;
}
#endif

// Guards the one-shot resource registration when apply_theme runs again on a
// runtime appearance switch.
bool icons_registered = false;

} // namespace

void apply_theme(QApplication& app, bool dark) {
  const auto& toks = dark ? cadly::ui::ThemeTokens::dark_tokens()
                          : cadly::ui::ThemeTokens::light_tokens();
#if defined(CADLY_HAS_QLEMENTINE)
  if (!dark || !apply_qlementine_dark(app)) {
    apply_fusion_palette(app, toks);
  }
#else
  apply_fusion_palette(app, toks);
#endif

  // Registers the qlementine-icons Qt resources and adds them to QIcon's
  // fallback search paths. The SVGs themselves are monochrome black; the UI
  // layer recolors them against the active tokens when it builds its QIcons
  // (see IconUtils in src/ui).
  if (!icons_registered) {
    icons_registered = true;
    oclero::qlementine::icons::initializeIconTheme();
  }
}

} // namespace cadly::app
