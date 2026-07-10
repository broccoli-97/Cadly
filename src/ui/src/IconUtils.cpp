#include "IconUtils.h"

#include "cadly/ui/ThemeTokens.h"

#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPixmap>

namespace cadly::ui {

QIcon themed_icon(const QString& name, const QColor& color,
                  const QColor& disabled_color) {
  // Cached: delegates call this from paint paths, and re-rendering the SVG
  // at five sizes per repaint would be felt on hover. The key includes both
  // tints so a theme switch naturally misses into fresh entries.
  static QHash<QString, QIcon> cache;
  const QString key = name + QChar(':') + QString::number(color.rgba(), 16) +
                      QChar(':') + QString::number(disabled_color.rgba(), 16);
  if (auto it = cache.constFind(key); it != cache.constEnd()) return *it;

  const QIcon source(QStringLiteral(":/qlementine/icons/16/%1.svg").arg(name));
  const auto tinted = [&source](int size, const QColor& c) {
    QPixmap pm = source.pixmap(QSize(size, size));
    if (!pm.isNull()) {
      QPainter painter(&pm);
      painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
      painter.fillRect(pm.rect(), c);
    }
    return pm;
  };
  QIcon icon;
  for (const int size : {16, 20, 24, 32, 40}) {
    if (auto pm = tinted(size, color); !pm.isNull()) {
      icon.addPixmap(pm, QIcon::Normal);
    }
    if (auto pm = tinted(size, disabled_color); !pm.isNull()) {
      icon.addPixmap(pm, QIcon::Disabled);
    }
  }
  cache.insert(key, icon);
  return icon;
}

QIcon themed_icon(const QString& name) {
  const auto& t = tokens();
  return themed_icon(name, t.text1, t.text3);
}

QFont ui_font(int pixel_size, int weight) {
  QFont f = QGuiApplication::font();
  f.setPixelSize(pixel_size);
  f.setWeight(static_cast<QFont::Weight>(weight));
  return f;
}

QFont mono_font(int pixel_size) {
  QFont f{QStringLiteral("monospace")};
  f.setStyleHint(QFont::TypeWriter);
  f.setPixelSize(pixel_size);
  return f;
}

} // namespace cadly::ui
