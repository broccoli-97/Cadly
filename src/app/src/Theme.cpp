#include "cadly/app/Theme.h"

#include "cadly/platform/Log.h"
#include "cadly/platform/Paths.h"
#include "cadly/ui/ThemeTokens.h"

#include <oclero/qlementine/icons/QlementineIcons.hpp>
#if defined(CADLY_HAS_QLEMENTINE)
#include <oclero/qlementine.hpp>
#endif

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#if defined(CADLY_HAS_QLEMENTINE)
#include <QPointer>
#endif
#include <QProxyStyle>
#include <QString>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOption>

namespace cadly::app {

namespace {

QString css_color(const QColor& c) {
  return QStringLiteral("rgba(%1,%2,%3,%4)")
    .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QString fusion_graphite_qss(const cadly::ui::ThemeTokens& t) {
  const QColor field = t.dark ? QColor(0x1B, 0x1D, 0x21) : Qt::white;
  const QColor button = t.dark ? QColor(0x34, 0x36, 0x3B)
                               : QColor(0xE7, 0xE9, 0xED);
  const QColor menu_selection = t.accent;
  return QStringLiteral(R"qss(
    QMenuBar {
      background: %1; color: %2; border: none; spacing: 2px;
    }
    QMenuBar::item {
      background: transparent; padding: 4px 9px; border-radius: 5px;
    }
    QMenuBar::item:selected { background: %3; }
    QMenu {
      background: %4; color: %2; border: 1px solid %5;
      border-radius: 8px; padding: 5px;
    }
    QMenu::item { min-height: 22px; padding: 4px 28px 4px 26px; border-radius: 5px; }
    QMenu::item:selected { background: %6; color: white; }
    QMenu::item:disabled { color: %7; }
    QMenu::separator { height: 1px; background: %5; margin: 5px 8px; }

    QLineEdit, QComboBox, QAbstractSpinBox, QPlainTextEdit {
      background: %8; color: %2; border: 1px solid %5;
      border-radius: 6px; selection-background-color: %6;
    }
    QLineEdit, QComboBox, QAbstractSpinBox { min-height: 24px; padding: 0 7px; }
    QLineEdit:hover, QComboBox:hover, QAbstractSpinBox:hover { border-color: %9; }
    QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { border-color: %6; }
    QComboBox::drop-down, QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {
      width: 18px; border: none; background: transparent;
    }
    QComboBox QAbstractItemView {
      background: %4; color: %2; border: 1px solid %5;
      selection-background-color: %6; selection-color: white; outline: none;
    }

    QPushButton {
      min-height: 26px; padding: 0 12px; color: %2; background: %10;
      border: 1px solid %5; border-radius: 6px;
    }
    QPushButton:hover { background: %3; border-color: %9; }
    QPushButton:pressed { background: %11; }
    QPushButton:disabled { color: %7; background: %12; }

    QGroupBox {
      border: none; margin-top: 20px; padding-top: 5px;
      color: %7; font-size: 11px; font-weight: 600;
    }
    QGroupBox::title { subcontrol-origin: margin; left: 0; padding: 0; }
    QCheckBox, QRadioButton { color: %2; spacing: 7px; min-height: 22px; }

    QSlider::groove:horizontal { height: 3px; border-radius: 1px; background: %11; }
    QSlider::sub-page:horizontal { background: %6; border-radius: 1px; }
    QSlider::handle:horizontal {
      width: 13px; height: 13px; margin: -5px 0; border-radius: 7px;
      background: %2; border: 1px solid %5;
    }

    QScrollBar:vertical { width: 10px; margin: 2px; background: transparent; }
    QScrollBar:horizontal { height: 10px; margin: 2px; background: transparent; }
    QScrollBar::handle { background: %11; border-radius: 4px; min-height: 28px; min-width: 28px; }
    QScrollBar::handle:hover { background: %9; }
    QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
    QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

    QTreeView, QScrollArea { background: transparent; border: none; outline: none; }
    QTreeView::branch { background: transparent; }
    QStatusBar { background: %13; border-top: 1px solid %14; }
    QStatusBar::item { border: none; }
    QProgressBar {
      background: %8; border: 1px solid %5; border-radius: 4px;
      color: %2; text-align: center; font-size: 10px;
    }
    QProgressBar::chunk { background: %6; border-radius: 3px; }
    QToolTip {
      color: %2; background: %4; border: 1px solid %5;
      border-radius: 6px; padding: 5px 7px;
    }
  )qss")
    .arg(css_color(t.toolbar_bg))
    .arg(css_color(t.text1))
    .arg(css_color(t.control_hover))
    .arg(css_color(t.popover_bg))
    .arg(css_color(t.hairline_soft))
    .arg(css_color(menu_selection))
    .arg(css_color(t.text3))
    .arg(css_color(field))
    .arg(css_color(t.text2))
    .arg(css_color(button))
    .arg(css_color(t.control_active))
    .arg(css_color(t.control_bg))
    .arg(css_color(t.status_bg))
    .arg(css_color(t.hairline));
}

class GraphiteFusionStyle final : public QProxyStyle {
public:
  GraphiteFusionStyle() : QProxyStyle(QStyleFactory::create("fusion")) {
    setObjectName(QStringLiteral("cadly-graphite-fusion"));
  }

  int pixelMetric(PixelMetric metric, const QStyleOption* option,
                  const QWidget* widget) const override {
    if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight ||
        metric == PM_ExclusiveIndicatorWidth ||
        metric == PM_ExclusiveIndicatorHeight) return 14;
    return QProxyStyle::pixelMetric(metric, option, widget);
  }

  void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                     QPainter* painter,
                     const QWidget* widget = nullptr) const override {
    const bool checkbox = element == PE_IndicatorCheckBox;
    const bool radio = element == PE_IndicatorRadioButton;
    if (!checkbox && !radio) {
      QProxyStyle::drawPrimitive(element, option, painter, widget);
      return;
    }

    const auto& t = cadly::ui::tokens();
    const bool enabled = option->state.testFlag(State_Enabled);
    const bool checked = option->state.testFlag(State_On);
    const QRectF box(option->rect.adjusted(0, 0, -1, -1));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QColor border = checked ? t.accent : t.hairline_soft;
    QColor fill = checked ? t.accent : t.control_bg;
    if (!enabled) {
      border = t.hairline_soft;
      fill = t.control_bg;
    }
    painter->setPen(QPen(border, 1.0));
    painter->setBrush(fill);
    if (radio) painter->drawEllipse(box);
    else painter->drawRoundedRect(box, 3.0, 3.0);

    if (checked) {
      painter->setPen(QPen(enabled ? Qt::white : t.text3, 1.6,
                           Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      if (radio) {
        painter->setBrush(enabled ? Qt::white : t.text3);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(box.center(), 3.0, 3.0);
      } else {
        const QPointF c = box.center();
        QPainterPath check;
        check.moveTo(c.x() - 3.2, c.y());
        check.lineTo(c.x() - 0.8, c.y() + 2.5);
        check.lineTo(c.x() + 3.8, c.y() - 3.0);
        painter->drawPath(check);
      }
    }
    painter->restore();
  }
};

// Fusion is the one stock style that honours a custom QPalette everywhere;
// platform styles (windowsvista, the GTK shims) hardcode most of their
// colors and would ignore half the palette below. It also renders
// identically on Windows/Linux/macOS, so the app looks the same in every
// screenshot and bug report. Used only when the full qlementine style is not
// compiled in or its theme assets cannot be initialized.
//
// The palette is derived from the Graphite token table so the stock widgets
// (spin boxes, combos, menus, scrollbars) sit on the same surfaces as the
// custom-painted chrome.
void apply_fusion_palette(QApplication& app, const cadly::ui::ThemeTokens& t) {
  // Reinstalling the same style at runtime repolishes every native widget and
  // can recreate QOpenGLWidget's backing surface. Keep the existing Fusion
  // instance across dark/light swaps; palette + QSS are sufficient.
  if (!app.style() ||
      app.style()->objectName() != QLatin1String("cadly-graphite-fusion")) {
    app.setStyle(new GraphiteFusionStyle);
  }

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
  app.setStyleSheet(fusion_graphite_qss(t));
}

#if defined(CADLY_HAS_QLEMENTINE)
// The real deal on Qt >= 6.8: qlementine restyles every widget coherently
// (rounded controls, smooth hover/check animations, focus rings) instead of
// recoloring Fusion shapes. The official showcase Light theme and Cadly Dark
// theme live in <source-root>/themes and ship like the shaders.
//
// Keep one QStyle for the QApplication lifetime. Replacing QStyle on every
// appearance toggle can recreate QOpenGLWidget's backing surface; ThemeManager
// updates the palette, fonts, icons and existing widgets without doing that.
struct QlementineRuntime {
  QPointer<oclero::qlementine::QlementineStyle> style;
  QPointer<oclero::qlementine::ThemeManager> themes;
  bool initialization_attempted{false};
};

QlementineRuntime qlementine_runtime;

bool apply_qlementine_theme(QApplication& app, bool dark) {
  auto& runtime = qlementine_runtime;
  const QString theme_name = dark ? QStringLiteral("Dark")
                                  : QStringLiteral("Light");

  if (!runtime.initialization_attempted) {
    runtime.initialization_attempted = true;
    const auto themes_dir = platform::find_asset_dir("themes");
    if (!themes_dir) {
      CADLY_LOG_WARN("themes/ asset dir not found; falling back to Fusion");
      return false;
    }

    auto* style = new oclero::qlementine::QlementineStyle(&app);
    style->setObjectName(QStringLiteral("cadly-qlementine"));
    style->setAnimationsEnabled(true);
    style->setAutoIconColor(oclero::qlementine::AutoIconColor::TextColor);
    style->setIconPathGetter(oclero::qlementine::icons::fromFreeDesktop);

    auto* themes = new oclero::qlementine::ThemeManager(style);
    themes->loadDirectory(QString::fromStdString(themes_dir->string()));
    if (themes->themeIndex(QStringLiteral("Dark")) < 0 ||
        themes->themeIndex(QStringLiteral("Light")) < 0) {
      CADLY_LOG_WARN("qlementine Dark/Light themes failed to load; "
                     "falling back to Fusion");
      delete style;
      return false;
    }

    themes->setCurrentTheme(theme_name);
    app.setStyleSheet(QString());
    app.setStyle(style);
    runtime.style = style;
    runtime.themes = themes;
    return true;
  }

  if (!runtime.style || !runtime.themes || app.style() != runtime.style) {
    CADLY_LOG_WARN("qlementine is unavailable; falling back to Fusion");
    return false;
  }

  runtime.themes->setCurrentTheme(theme_name);
  return runtime.themes->currentTheme() == theme_name;
}
#endif

// Guards the one-shot resource registration when apply_theme runs again on a
// runtime appearance switch.
bool icons_registered = false;

} // namespace

void apply_theme(QApplication& app, bool dark) {
  const auto& toks = dark ? cadly::ui::ThemeTokens::dark_tokens()
                          : cadly::ui::ThemeTokens::light_tokens();

  // Register resources before qlementine configures automatic icon lookup.
  if (!icons_registered) {
    icons_registered = true;
    oclero::qlementine::icons::initializeIconTheme();
    QIcon::setThemeName(QStringLiteral("qlementine"));
  }

#if defined(CADLY_HAS_QLEMENTINE)
  if (!apply_qlementine_theme(app, dark)) {
    apply_fusion_palette(app, toks);
  }
#else
  apply_fusion_palette(app, toks);
#endif
}

} // namespace cadly::app
