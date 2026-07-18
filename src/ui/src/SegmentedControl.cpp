#include "SegmentedControl.h"

#include "IconUtils.h"
#include "cadly/ui/ThemeTokens.h"

#include <QFontMetrics>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

namespace cadly::ui {

namespace {
constexpr int kPadH    = 11;  // horizontal text padding inside a segment
constexpr int kInset   = 2;   // thumb inset from the track
constexpr int kMenuW   = 18;  // split-chevron area on a menu segment
} // namespace

SegmentedControl::SegmentedControl(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  setFixedHeight(26);
  thumb_animation_ = new QVariantAnimation(this);
  thumb_animation_->setDuration(140);
  thumb_animation_->setEasingCurve(QEasingCurve::OutCubic);
  connect(thumb_animation_, &QVariantAnimation::valueChanged,
          this, [this](const QVariant& value) {
            thumb_rect_ = value.toRectF();
            update();
          });
  connect(&ThemeManager::instance(), &ThemeManager::changed,
          this, QOverload<>::of(&QWidget::update));
}

int SegmentedControl::add_segment(const QString& text, const QString& tooltip) {
  segments_.push_back({text, tooltip});
  if (!tooltip.isEmpty() && segments_.size() == 1) setToolTip(tooltip);
  updateGeometry();
  update();
  return static_cast<int>(segments_.size()) - 1;
}

void SegmentedControl::set_segment_menu(int index, QMenu* menu) {
  if (index < 0 || index >= count()) return;
  segments_[static_cast<std::size_t>(index)].menu = menu;
  thumb_rect_ = {};
  updateGeometry();
  update();
}

QMenu* SegmentedControl::segment_menu(int index) const {
  if (index < 0 || index >= count()) return nullptr;
  return segments_[static_cast<std::size_t>(index)].menu;
}

void SegmentedControl::show_segment_menu(int index) {
  QMenu* menu = segment_menu(index);
  if (!menu) return;
  if (index != current_) {
    animate_to(index);
    emit segment_clicked(index);
  }
  const QRectF cell = segment_rect(index);
  menu->popup(mapToGlobal(
    QPoint(static_cast<int>(cell.left()), height() + 4)));
}

void SegmentedControl::set_current(int index) {
  if (index < 0 || index >= count() || index == current_) return;
  animate_to(index);
}

void SegmentedControl::set_compact(bool on) {
  compact_ = on;
  setFixedHeight(compact_ ? 20 : 26);
  thumb_rect_ = {};
  updateGeometry();
  update();
}

int SegmentedControl::segment_width(const Segment& s) const {
  const QFontMetrics fm(ui_font(compact_ ? 11 : 12, QFont::Medium));
  return fm.horizontalAdvance(s.text) + 2 * kPadH + (s.menu ? kMenuW : 0);
}

bool SegmentedControl::menu_at(int index, const QPoint& pos) const {
  if (!segment_menu(index)) return false;
  const QRectF cell = segment_rect(index);
  return pos.x() >= cell.right() - kMenuW && cell.contains(pos);
}

QRectF SegmentedControl::segment_rect(int index) const {
  int x = kInset;
  for (int i = 0; i < index; ++i) {
    x += segment_width(segments_[static_cast<std::size_t>(i)]);
  }
  const int w = segment_width(segments_[static_cast<std::size_t>(index)]);
  return {static_cast<qreal>(x), static_cast<qreal>(kInset),
          static_cast<qreal>(w), static_cast<qreal>(height() - 2 * kInset)};
}

void SegmentedControl::animate_to(int index) {
  const QRectF start = thumb_rect_.isValid() ? thumb_rect_
                                              : segment_rect(current_);
  current_ = index;
  thumb_animation_->stop();
  thumb_animation_->setStartValue(start);
  thumb_animation_->setEndValue(segment_rect(current_));
  thumb_animation_->start();
}

QSize SegmentedControl::sizeHint() const {
  int w = 2 * kInset;
  for (const auto& s : segments_) w += segment_width(s);
  return {w, compact_ ? 20 : 26};
}

int SegmentedControl::index_at(const QPoint& pos) const {
  int x = kInset;
  for (int i = 0; i < count(); ++i) {
    const int w = segment_width(segments_[static_cast<std::size_t>(i)]);
    if (pos.x() >= x && pos.x() < x + w) return i;
    x += w;
  }
  return -1;
}

void SegmentedControl::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  const int idx = index_at(e->pos());
  if (idx >= 0 && menu_at(idx, e->pos())) {
    show_segment_menu(idx);
    return;
  }
  if (idx >= 0 && idx != current_) {
    animate_to(idx);
    emit segment_clicked(idx);
  }
}

void SegmentedControl::mouseMoveEvent(QMouseEvent* e) {
  const int idx = index_at(e->pos());
  const bool over_menu = idx >= 0 && menu_at(idx, e->pos());
  if (idx != hover_ || over_menu != hover_menu_) {
    hover_ = idx;
    hover_menu_ = over_menu;
    if (idx >= 0) {
      setToolTip(segments_[static_cast<std::size_t>(idx)].tooltip);
    } else {
      // Off the segments (track inset): clear, or the last segment's
      // tooltip keeps popping up while pointing at nothing.
      setToolTip(QString());
    }
    update();
  }
}

void SegmentedControl::leaveEvent(QEvent*) {
  hover_ = -1;
  hover_menu_ = false;
  update();
}

void SegmentedControl::paintEvent(QPaintEvent*) {
  const auto& t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QRectF track(0.5, 0.5, width() - 1.0, height() - 1.0);
  const qreal r = track.height() / 2.0 > 7.0 ? 6.0 : track.height() / 2.0;
  p.setPen(Qt::NoPen);
  p.setBrush(t.control_bg);
  p.drawRoundedRect(track, r, r);

  const QRectF thumb = thumb_rect_.isValid() ? thumb_rect_
                                              : segment_rect(current_);
  p.setPen(QPen(t.hairline_soft, 1.0));
  p.setBrush(t.control_active);
  p.drawRoundedRect(thumb.adjusted(0.5, 0.5, -0.5, -0.5),
                    r - 1.5, r - 1.5);

  p.setFont(ui_font(compact_ ? 11 : 12, QFont::Medium));
  int x = kInset;
  for (int i = 0; i < count(); ++i) {
    const auto& s = segments_[static_cast<std::size_t>(i)];
    const int w = segment_width(s);
    const QRectF cell(x, kInset, w, height() - 2 * kInset);
    if (i != current_ && i == hover_) {
      p.setPen(Qt::NoPen);
      p.setBrush(t.control_hover);
      p.drawRoundedRect(cell.adjusted(0.5, 0.5, -0.5, -0.5), r - 1.5, r - 1.5);
    }
    if (i == current_ && i == hover_ && hover_menu_) {
      const QRectF menu_hover(cell.right() - kMenuW + 2, cell.top() + 1,
                              kMenuW - 3, cell.height() - 2);
      p.setPen(Qt::NoPen);
      p.setBrush(t.control_hover);
      p.drawRoundedRect(menu_hover, 3.0, 3.0);
    }
    p.setPen(i == current_ ? t.text1 : t.text2);
    QRectF text_cell = cell;
    if (s.menu) text_cell.adjust(0, 0, -kMenuW, 0);
    p.drawText(text_cell, Qt::AlignCenter, s.text);
    if (s.menu) {
      const qreal split_x = cell.right() - kMenuW;
      p.setPen(QPen(t.hairline_soft, 1.0));
      p.drawLine(QPointF(split_x, cell.top() + 4),
                 QPointF(split_x, cell.bottom() - 4));
      const qreal cx = split_x + kMenuW / 2.0;
      const qreal cy = cell.center().y() - 1.0;
      QPen chevron(i == current_ ? t.text1 : t.text2, 1.3);
      chevron.setCapStyle(Qt::RoundCap);
      p.setPen(chevron);
      p.drawLine(QPointF(cx - 2.7, cy), QPointF(cx, cy + 2.7));
      p.drawLine(QPointF(cx, cy + 2.7), QPointF(cx + 2.7, cy));
    }
    x += w;
  }
}

} // namespace cadly::ui
