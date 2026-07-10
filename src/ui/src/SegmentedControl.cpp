#include "SegmentedControl.h"

#include "IconUtils.h"
#include "cadly/ui/ThemeTokens.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace cadly::ui {

namespace {
constexpr int kPadH    = 11;  // horizontal text padding inside a segment
constexpr int kInset   = 2;   // thumb inset from the track
} // namespace

SegmentedControl::SegmentedControl(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  connect(&ThemeManager::instance(), &ThemeManager::changed,
          this, QOverload<>::of(&QWidget::update));
}

int SegmentedControl::add_segment(const QString& text, const QString& tooltip) {
  segments_.push_back({text});
  if (!tooltip.isEmpty() && segments_.size() == 1) setToolTip(tooltip);
  updateGeometry();
  update();
  return static_cast<int>(segments_.size()) - 1;
}

void SegmentedControl::set_current(int index) {
  if (index < 0 || index >= count() || index == current_) return;
  current_ = index;
  update();
}

void SegmentedControl::set_compact(bool on) {
  compact_ = on;
  updateGeometry();
  update();
}

int SegmentedControl::segment_width(const Segment& s) const {
  const QFontMetrics fm(ui_font(compact_ ? 11 : 12, QFont::Medium));
  return fm.horizontalAdvance(s.text) + 2 * kPadH;
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
  if (idx >= 0 && idx != current_) {
    current_ = idx;
    update();
    emit segment_clicked(idx);
  }
}

void SegmentedControl::mouseMoveEvent(QMouseEvent* e) {
  const int idx = index_at(e->pos());
  if (idx != hover_) {
    hover_ = idx;
    update();
  }
}

void SegmentedControl::leaveEvent(QEvent*) {
  hover_ = -1;
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

  p.setFont(ui_font(compact_ ? 11 : 12, QFont::Medium));
  int x = kInset;
  for (int i = 0; i < count(); ++i) {
    const auto& s = segments_[static_cast<std::size_t>(i)];
    const int w = segment_width(s);
    const QRectF cell(x, kInset, w, height() - 2 * kInset);
    if (i == current_) {
      // The raised "thumb": neutral fill + a soft hairline so it reads as a
      // physical segment rather than a text highlight.
      p.setPen(QPen(t.hairline_soft, 1.0));
      p.setBrush(t.control_active);
      p.drawRoundedRect(cell.adjusted(0.5, 0.5, -0.5, -0.5), r - 1.5, r - 1.5);
    } else if (i == hover_) {
      p.setPen(Qt::NoPen);
      p.setBrush(t.control_hover);
      p.drawRoundedRect(cell.adjusted(0.5, 0.5, -0.5, -0.5), r - 1.5, r - 1.5);
    }
    p.setPen(i == current_ ? t.text1 : t.text2);
    p.drawText(cell, Qt::AlignCenter, s.text);
    x += w;
  }
}

} // namespace cadly::ui
