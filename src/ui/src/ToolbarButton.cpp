#include "ToolbarButton.h"

#include "IconUtils.h"
#include "cadly/ui/ThemeTokens.h"

#include <QFontMetrics>
#include <QPainter>
#include <QStyleOptionToolButton>
#include <QVariantAnimation>

#include <algorithm>

namespace cadly::ui {

namespace {
constexpr int kHeight  = 28;
constexpr int kIconPx  = 18;
constexpr int kPadH    = 7;
constexpr int kGap     = 5;   // icon-to-text
constexpr int kChevron = 9;   // width reserved for the menu chevron
} // namespace

ToolbarButton::ToolbarButton(QWidget* parent) : QToolButton(parent) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  hover_animation_ = new QVariantAnimation(this);
  hover_animation_->setDuration(120);
  hover_animation_->setEasingCurve(QEasingCurve::OutCubic);
  connect(hover_animation_, &QVariantAnimation::valueChanged,
          this, [this](const QVariant& value) {
            hover_progress_ = value.toReal();
            update();
          });
  connect(&ThemeManager::instance(), &ThemeManager::changed,
          this, QOverload<>::of(&QWidget::update));
}

void ToolbarButton::animate_hover(qreal target) {
  hover_animation_->stop();
  hover_animation_->setStartValue(hover_progress_);
  hover_animation_->setEndValue(target);
  hover_animation_->start();
}

void ToolbarButton::enterEvent(QEnterEvent* event) {
  animate_hover(1.0);
  QToolButton::enterEvent(event);
}

void ToolbarButton::leaveEvent(QEvent* event) {
  animate_hover(0.0);
  QToolButton::leaveEvent(event);
}

void ToolbarButton::set_show_text(bool on) {
  show_text_ = on;
  updateGeometry();
  update();
}

void ToolbarButton::set_badge(bool on) {
  if (badge_ == on) return;
  badge_ = on;
  update();
}

QSize ToolbarButton::sizeHint() const {
  int w = 2 * kPadH;
  if (!icon().isNull()) w += kIconPx;
  if (show_text_ && !text().isEmpty()) {
    const QFontMetrics fm(ui_font(12, QFont::Medium));
    w += fm.horizontalAdvance(text());
    if (!icon().isNull()) w += kGap;
  }
  if (menu() != nullptr && popupMode() == QToolButton::InstantPopup) {
    w += kChevron;
  }
  return {std::max(w, 22), kHeight};
}

void ToolbarButton::paintEvent(QPaintEvent*) {
  const auto& t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // QToolButton keeps isDown()/underMouse()/isChecked() current; we only
  // repaint the shell. Note `defaultAction()` drives checked + enabled.
  const bool down    = isDown();
  const bool hover   = hover_progress_ > 0.0 && isEnabled();
  const bool checked = isChecked();

  const QRectF r(0.5, 0.5, width() - 1.0, height() - 1.0);
  QColor fill = Qt::transparent;
  if (checked) {
    fill = emphasis_ == Emphasis::Accent
      ? QColor(t.accent.red(), t.accent.green(), t.accent.blue(), t.dark ? 56 : 46)
      : t.control_active;
  } else if (down) {
    fill = t.control_active;
  } else if (hover) {
    fill = t.control_hover;
    fill.setAlphaF(fill.alphaF() * hover_progress_);
  }
  if (fill.alpha() > 0) {
    p.setPen(checked ? QPen(emphasis_ == Emphasis::Accent
                              ? QColor(t.accent.red(), t.accent.green(),
                                       t.accent.blue(), 90)
                              : t.hairline_soft, 1.0)
                     : QPen(Qt::NoPen));
    p.setBrush(fill);
    p.drawRoundedRect(r, 6.0, 6.0);
  }

  const QColor fg =
    !isEnabled() ? t.text3
    : (checked && emphasis_ == Emphasis::Accent)
      ? (t.dark ? t.accent.lighter(118) : t.accent.darker(108))
      : t.text1;

  int x = kPadH;
  if (!icon().isNull()) {
    const QRect ir(x, (height() - kIconPx) / 2, kIconPx, kIconPx);
    icon().paint(&p, ir, Qt::AlignCenter,
                 isEnabled() ? QIcon::Normal : QIcon::Disabled);
    x += kIconPx + (show_text_ && !text().isEmpty() ? kGap : 0);
  }
  if (show_text_ && !text().isEmpty()) {
    p.setFont(ui_font(12, QFont::Medium));
    p.setPen(fg);
    p.drawText(QRect(x, 0, width() - x - kPadH, height()),
               Qt::AlignLeft | Qt::AlignVCenter, text());
  }
  if (menu() != nullptr && popupMode() == QToolButton::InstantPopup) {
    // Small chevron, drawn by hand so it tints with the theme.
    const qreal cx = width() - kPadH - 2.0;
    const qreal cy = height() / 2.0 - 1.0;
    QPen pen(isEnabled() ? t.text2 : t.text3, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QPointF(cx - 3.2, cy), QPointF(cx, cy + 3.2));
    p.drawLine(QPointF(cx, cy + 3.2), QPointF(cx + 3.2, cy));
  }
  if (badge_) {
    p.setPen(QPen(t.toolbar_bg, 1.5));
    p.setBrush(t.ok);
    p.drawEllipse(QPointF(width() - 6.5, 6.5), 3.0, 3.0);
  }
}

} // namespace cadly::ui
