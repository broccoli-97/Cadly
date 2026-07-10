#pragma once

// Icon + font helpers for the Graphite chrome. Private to the ui module.

#include <QFont>
#include <QIcon>
#include <QString>

namespace cadly::ui {

// Builds a QIcon from a qlementine-icons SVG (resource path
// ":/qlementine/icons/16/<name>.svg"), recolored to the given color. The
// shipped SVGs are monochrome with a hardcoded black fill, and plain QIcon
// has no recoloring step — that's a feature of the full qlementine QStyle,
// which needs Qt 6.8+ — so unmodified they would be near-invisible on the
// dark theme. Rendering the SVG and then compositing a flat color with
// SourceIn keeps the glyph's alpha while replacing its color. Baked at a few
// fixed sizes so menus (16px), the toolbar (20px) and their 2x HiDPI
// variants all get a crisp pixmap.
QIcon themed_icon(const QString& name, const QColor& color,
                  const QColor& disabled_color);

// Same, tinted with the current theme's text colours (tokens().text1 /
// text3). NOTE: the result is baked against the tokens at call time — on a
// theme switch the owner must rebuild its icons (MainWindow does this for
// all actions in one pass).
QIcon themed_icon(const QString& name);

// App font at a fixed pixel size (layout is on a px grid like the mock, so
// point sizes would drift between platforms/DPI configurations).
QFont ui_font(int pixel_size, int weight = QFont::Normal);

// Fixed-pitch font for measured values (counts, sizes, times).
QFont mono_font(int pixel_size);

} // namespace cadly::ui
