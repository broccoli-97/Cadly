#include "ViewsGrid.h"

#include "IconUtils.h"
#include "cadly/ui/ThemeTokens.h"

#include <QAction>
#include <QGridLayout>
#include <QPainter>

namespace cadly::ui {

ViewTile::ViewTile(QAction* action, QWidget* parent)
  : QAbstractButton(parent),
    key_label_(action->shortcut().toString(QKeySequence::NativeText)) {
  setText(action->text().remove(QChar('&')));
  setCursor(Qt::PointingHandCursor);
  connect(this, &QAbstractButton::clicked, action, [this, action]() {
    action->trigger();
    // Tiles live inside a Popover (a Qt::Popup window): clicking inside a
    // popup does not auto-dismiss it, so close explicitly after acting.
    window()->close();
  });
}

void ViewTile::paintEvent(QPaintEvent*) {
  const auto& t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QRectF r(0.5, 0.5, width() - 1.0, height() - 1.0);
  p.setPen(QPen(t.hairline_soft, 1.0));
  p.setBrush(underMouse() ? t.control_hover : t.control_bg);
  p.drawRoundedRect(r, 8.0, 8.0);

  p.setFont(ui_font(12, QFont::Medium));
  p.setPen(t.text1);
  p.drawText(rect().adjusted(0, 0, 0, -12), Qt::AlignCenter, text());

  if (!key_label_.isEmpty()) {
    const QFont key_font = mono_font(10);
    const QFontMetrics fm(key_font);
    const int kw = fm.horizontalAdvance(key_label_) + 10;
    const int kh = fm.height() + 2;
    const QRectF key(width() - kw - 6.0, height() - kh - 6.0, kw, kh);
    p.setFont(key_font);
    p.setPen(QPen(t.hairline_soft, 1.0));
    p.setBrush(t.dark ? QColor(0, 0, 0, 60) : QColor(255, 255, 255, 170));
    p.drawRoundedRect(key, 4.0, 4.0);
    p.setPen(t.text2);
    p.drawText(key, Qt::AlignCenter, key_label_);
  }
}

ViewsGrid::ViewsGrid(const QList<QAction*>& actions, QWidget* parent)
  : QWidget(parent) {
  auto* grid = new QGridLayout(this);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(6);
  int i = 0;
  for (auto* action : actions) {
    auto* tile = new ViewTile(action, this);
    grid->addWidget(tile, i / 4, i % 4);
    ++i;
  }
}

} // namespace cadly::ui
