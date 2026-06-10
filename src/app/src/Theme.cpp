#include "cadly/app/Theme.h"

#include "cadly/platform/Log.h"
#include "cadly/platform/Paths.h"

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

// Fallback look for Qt < 6.8, where the qlementine QStyle can't be built.
// Fusion is the one stock style that honours a custom QPalette everywhere;
// platform styles (windowsvista, the GTK shims) hardcode most of their
// colors and would ignore half the palette below. It also renders
// identically on Windows/Linux/macOS, so the app looks the same in every
// screenshot and bug report.
void apply_fusion_dark(QApplication& app) {
  app.setStyle(QStyleFactory::create("fusion"));

  // Hand-tuned dark palette, mid-dark rather than pitch black: the viewport
  // draws its own background gradient, and chrome that is darker than the
  // viewport makes the 3D view look washed out instead of letting it pop.
  const QColor window{0x2b, 0x2d, 0x30};    // docks, toolbars, menus, dialogs
  const QColor base{0x1f, 0x21, 0x24};      // item views, line edits
  const QColor alt_base{0x26, 0x29, 0x2c};  // alternating tree/table rows
  const QColor button{0x3a, 0x3d, 0x41};    // pushbuttons, scrollbar handles
  const QColor text{0xd6, 0xd8, 0xdc};      // off-white; pure white glares
  const QColor dim_text{0x7e, 0x83, 0x89};  // disabled text, placeholders
  const QColor accent{0x35, 0x74, 0xf0};    // selection / focus blue

  QPalette pal;
  pal.setColor(QPalette::Window, window);
  pal.setColor(QPalette::WindowText, text);
  pal.setColor(QPalette::Base, base);
  pal.setColor(QPalette::AlternateBase, alt_base);
  pal.setColor(QPalette::ToolTipBase, QColor{0x3a, 0x3d, 0x41});
  pal.setColor(QPalette::ToolTipText, text);
  pal.setColor(QPalette::PlaceholderText, dim_text);
  pal.setColor(QPalette::Text, text);
  pal.setColor(QPalette::Button, button);
  pal.setColor(QPalette::ButtonText, text);
  pal.setColor(QPalette::BrightText, QColor{0xff, 0x6b, 0x68});
  pal.setColor(QPalette::Link, QColor{0x58, 0x9d, 0xf6});
  pal.setColor(QPalette::Highlight, accent);
  pal.setColor(QPalette::HighlightedText, Qt::white);

  pal.setColor(QPalette::Disabled, QPalette::WindowText, dim_text);
  pal.setColor(QPalette::Disabled, QPalette::Text, dim_text);
  pal.setColor(QPalette::Disabled, QPalette::ButtonText, dim_text);
  pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor{0x3c, 0x3f, 0x44});
  pal.setColor(QPalette::Disabled, QPalette::HighlightedText, dim_text);
  // Fusion etches disabled text with a 1px offset copy drawn in Light, a
  // trick that only works on light backgrounds — on a dark palette it reads
  // as a white ghost behind every disabled label. Match Light to the window
  // color so the etch disappears.
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

} // namespace

void apply_theme(QApplication& app) {
#if defined(CADLY_HAS_QLEMENTINE)
  if (!apply_qlementine_dark(app)) {
    apply_fusion_dark(app);
  }
#else
  apply_fusion_dark(app);
#endif

  // Registers the qlementine-icons Qt resources and adds them to QIcon's
  // fallback search paths. The SVGs themselves are monochrome black; the UI
  // layer recolors them against the active palette when it builds its QIcons
  // (see themed_icon() in MainWindow.cpp).
  oclero::qlementine::icons::initializeIconTheme();
}

} // namespace cadly::app
