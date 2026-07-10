#include "Popover.h"

#include "IconUtils.h"
#include "ToolbarButton.h"
#include "cadly/ui/ThemeTokens.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QVBoxLayout>

#include <cstdlib>

namespace cadly::ui {

namespace {
constexpr int    kMargin  = 12;
constexpr double kRadius  = 10.0;

// Clamp a desired top-left so the whole `size` stays on `screen`.
QPoint clamp_to_screen(QPoint top_left, const QSize& size, const QScreen* screen) {
  if (!screen) return top_left;
  const QRect avail = screen->availableGeometry();
  top_left.setX(std::min(std::max(top_left.x(), avail.left() + 4),
                         avail.right() - size.width() - 4));
  top_left.setY(std::min(std::max(top_left.y(), avail.top() + 4),
                         avail.bottom() - size.height() - 4));
  return top_left;
}

void paint_card(QWidget* w, QPainter& p) {
  const auto& t = tokens();
  p.setRenderHint(QPainter::Antialiasing);
  QColor bg = t.popover_bg;
  if (!popovers_must_be_opaque()) bg.setAlpha(t.dark ? 247 : 251);
  const QRectF r(0.5, 0.5, w->width() - 1.0, w->height() - 1.0);
  p.setPen(QPen(t.dark ? t.hairline_soft : t.hairline, 1.0));
  p.setBrush(bg);
  if (popovers_must_be_opaque()) {
    p.drawRect(r);
  } else {
    p.drawRoundedRect(r, kRadius, kRadius);
  }
}

} // namespace

bool popovers_must_be_opaque() {
  static const bool opaque = [] {
    if (qEnvironmentVariableIsSet("CADLY_OPAQUE_POPOVERS")) return true;
    // Wayland always composites; X11 *usually* does on anything modern
    // (including WSLg). The env var is the escape hatch for bare X servers.
    return false;
  }();
  return opaque;
}

Popover::Popover(QWidget* content, const QString& title, bool pinnable)
  : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint |
                     Qt::NoDropShadowWindowHint),
    content_(content),
    title_(title) {
  setAttribute(Qt::WA_DeleteOnClose);
  if (!popovers_must_be_opaque()) {
    setAttribute(Qt::WA_TranslucentBackground);
  }

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(kMargin, pinnable || !title.isEmpty() ? 8 : kMargin,
                            kMargin, kMargin);
  outer->setSpacing(6);

  if (!title_.isEmpty()) {
    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    auto* lbl = new QLabel(title_, this);
    lbl->setFont(ui_font(11, QFont::DemiBold));
    QPalette pal = lbl->palette();
    pal.setColor(QPalette::WindowText, tokens().text2);
    lbl->setPalette(pal);
    header->addWidget(lbl);
    header->addStretch();
    if (pinnable) {
      auto* pin = new ToolbarButton(this);
      pin->setIcon(themed_icon(QStringLiteral("action/pin")));
      pin->setToolTip(tr("Pin as a floating card"));
      connect(pin, &QAbstractButton::clicked, this, &Popover::pin);
      header->addWidget(pin);
    }
    outer->addLayout(header);
  }

  content_->setParent(this);
  outer->addWidget(content_);
}

void Popover::pin() {
  if (!content_) return;
  // Hand the content to a PinnedCard parked at the popover's position. The
  // main window (any top-level non-popup ancestor of the anchor chain) keeps
  // the card above itself without pinning it over other apps.
  QWidget* main_window = nullptr;
  for (QWidget* w : QApplication::topLevelWidgets()) {
    if (w->isWindow() && w->isVisible() &&
        !(w->windowFlags() & Qt::Popup) && w != this) {
      main_window = w;
      break;
    }
  }
  QWidget* content = content_;
  content_ = nullptr;
  const QPoint at = pos();
  auto* card = new PinnedCard(content, title_, main_window);
  card->move(at);
  card->show();
  close();
}

void Popover::paintEvent(QPaintEvent*) {
  QPainter p(this);
  paint_card(this, p);
}

Popover* Popover::show_for(QWidget* content, QWidget* anchor,
                           const QString& title, bool pinnable) {
  const QRect global(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
  return show_at(content, global, title, pinnable);
}

Popover* Popover::show_at(QWidget* content, const QRect& anchor_global,
                          const QString& title, bool pinnable) {
  auto* pop = new Popover(content, title, pinnable);
  pop->adjustSize();
  const QSize sz = pop->sizeHint().expandedTo(pop->size());

  QScreen* screen = QApplication::screenAt(anchor_global.center());
  if (!screen) screen = QApplication::primaryScreen();

  // Prefer below the anchor, left-aligned; flip above when it would not fit.
  QPoint at(anchor_global.left(), anchor_global.bottom() + 6);
  if (screen &&
      at.y() + sz.height() > screen->availableGeometry().bottom() - 4) {
    at.setY(anchor_global.top() - sz.height() - 6);
  }
  pop->move(clamp_to_screen(at, sz, screen));
  pop->show();
  return pop;
}

PinnedCard::PinnedCard(QWidget* content, const QString& title,
                       QWidget* main_window)
  : QWidget(main_window, Qt::Tool | Qt::FramelessWindowHint) {
  setAttribute(Qt::WA_DeleteOnClose);
  if (!popovers_must_be_opaque()) {
    setAttribute(Qt::WA_TranslucentBackground);
  }

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(kMargin, 8, kMargin, kMargin);
  outer->setSpacing(6);

  auto* header = new QHBoxLayout();
  header->setContentsMargins(0, 0, 0, 0);
  auto* lbl = new QLabel(title, this);
  lbl->setFont(ui_font(11, QFont::DemiBold));
  QPalette pal = lbl->palette();
  pal.setColor(QPalette::WindowText, tokens().text2);
  lbl->setPalette(pal);
  header->addWidget(lbl);
  header->addStretch();
  auto* close_btn = new ToolbarButton(this);
  close_btn->setIcon(themed_icon(QStringLiteral("action/close-small")));
  close_btn->setToolTip(tr("Close"));
  connect(close_btn, &QAbstractButton::clicked, this, &QWidget::close);
  header->addWidget(close_btn);
  outer->addLayout(header);

  content->setParent(this);
  outer->addWidget(content);
  adjustSize();
}

void PinnedCard::paintEvent(QPaintEvent*) {
  QPainter p(this);
  paint_card(this, p);
}

void PinnedCard::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) {
    dragging_ = true;
    drag_offset_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
  }
  QWidget::mousePressEvent(e);
}

void PinnedCard::mouseMoveEvent(QMouseEvent* e) {
  if (dragging_ && (e->buttons() & Qt::LeftButton)) {
    move(e->globalPosition().toPoint() - drag_offset_);
  } else {
    dragging_ = false;
  }
  QWidget::mouseMoveEvent(e);
}

void PinnedCard::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) {
    close();
    return;
  }
  QWidget::keyPressEvent(e);
}

} // namespace cadly::ui
