#include "DocumentCapsule.h"

#include "IconUtils.h"
#include "cadly/ui/ThemeTokens.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace cadly::ui {

DocumentCapsule::DocumentCapsule(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setMaximumWidth(460);
  setMouseTracking(true);
  flash_timer_.setSingleShot(true);
  flash_timer_.setInterval(900);
  connect(&flash_timer_, &QTimer::timeout, this, [this]() {
    flash_ = false;
    update();
  });
  connect(&ThemeManager::instance(), &ThemeManager::changed,
          this, QOverload<>::of(&QWidget::update));
  set_empty();
}

void DocumentCapsule::set_empty() {
  state_ = State::Empty;
  filename_.clear();
  stats_.clear();
  full_path_.clear();
  flash_ = false;
  setToolTip({});
  setCursor(Qt::ArrowCursor);
  update();
}

void DocumentCapsule::set_document(const QString& filename, const QString& stats,
                                   const QString& full_path) {
  state_     = State::Document;
  filename_  = filename;
  stats_     = stats;
  full_path_ = full_path;
  setToolTip(full_path);
  setCursor(Qt::PointingHandCursor);
  update();
}

void DocumentCapsule::begin_import(const QString& filename) {
  state_    = State::Importing;
  filename_ = filename;
  message_  = tr("Starting…");
  fraction_ = 0.0f;
  flash_    = false;
  setToolTip({});
  setCursor(Qt::ArrowCursor);
  update();
}

void DocumentCapsule::set_progress(float fraction, const QString& message) {
  fraction_ = std::clamp(fraction, 0.0f, 1.0f);
  if (!message.isEmpty()) message_ = message;
  update();
}

void DocumentCapsule::flash_success() {
  flash_ = true;
  flash_timer_.start();
  update();
}

void DocumentCapsule::show_failure(const QString& brief) {
  state_   = State::Failed;
  message_ = brief;
  setCursor(Qt::ArrowCursor);
  update();
}

QRect DocumentCapsule::cancel_rect() const {
  const int s = 18;
  return {width() - s - 9, (height() - s) / 2, s, s};
}

void DocumentCapsule::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  if (state_ == State::Importing && cancel_rect().contains(e->pos())) {
    emit cancel_requested();
    return;
  }
  if (state_ == State::Document) emit clicked();
}

void DocumentCapsule::mouseMoveEvent(QMouseEvent* e) {
  const bool over = state_ == State::Importing &&
                    cancel_rect().contains(e->pos());
  if (over != cancel_hover_) {
    cancel_hover_ = over;
    update();
  }
}

void DocumentCapsule::leaveEvent(QEvent*) {
  if (cancel_hover_) {
    cancel_hover_ = false;
    update();
  }
}

void DocumentCapsule::paintEvent(QPaintEvent*) {
  const auto& t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QRectF r(0.5, 0.5, width() - 1.0, height() - 1.0);
  constexpr qreal rad = 8.0;

  // Recessed well: darker than the toolbar in dark mode, paper-white in
  // light, so the capsule reads as a document slot rather than a button.
  QColor bg = t.dark ? QColor(0x1D, 0x1F, 0x23) : QColor(0xFC, 0xFD, 0xFE);
  if (state_ == State::Failed) {
    bg = t.dark ? QColor(0x36, 0x22, 0x25) : QColor(0xF7, 0xE4, 0xE5);
  } else if (flash_) {
    bg = t.dark ? QColor(0x1E, 0x33, 0x2F) : QColor(0xDF, 0xF1, 0xEC);
  }
  p.setPen(QPen(state_ == State::Failed
                  ? QColor(t.error.red(), t.error.green(), t.error.blue(), 120)
                  : t.hairline_soft,
                1.0));
  p.setBrush(bg);
  p.drawRoundedRect(r, rad, rad);

  // Import progress fill, clipped to the capsule.
  if (state_ == State::Importing && fraction_ > 0.001f) {
    QPainterPath clip;
    clip.addRoundedRect(r, rad, rad);
    p.save();
    p.setClipPath(clip);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(t.accent.red(), t.accent.green(), t.accent.blue(),
                      t.dark ? 52 : 40));
    p.drawRect(QRectF(0, 0, width() * fraction_, height()));
    p.setBrush(t.accent);
    p.drawRect(QRectF(0, height() - 2.0, width() * fraction_, 2.0));
    p.restore();
  }

  // Text block: primary line + secondary line, centered. Reserve room for
  // the inline cancel target while importing.
  const int right_pad = state_ == State::Importing ? 32 : 10;
  const QRect text_area(10, 0, width() - 10 - right_pad, height());

  QString line1;
  QString line2;
  QColor  c1 = t.text1;
  QColor  c2 = t.text2;
  switch (state_) {
    case State::Empty:
      line1 = tr("No model loaded");
      line2 = tr("Open a STEP or IGES file, or drop one here");
      c1    = t.text2;
      c2    = t.text3;
      break;
    case State::Document:
      line1 = filename_;
      line2 = stats_;
      break;
    case State::Importing:
      line1 = tr("Importing %1").arg(filename_);
      line2 = QStringLiteral("%1  ·  %2%")
                .arg(message_)
                .arg(static_cast<int>(fraction_ * 100.0f));
      break;
    case State::Failed:
      line1 = tr("Import failed — %1").arg(filename_);
      line2 = message_;
      c1    = t.error;
      break;
  }

  const QFontMetrics fm1(ui_font(12, QFont::DemiBold));
  const QFontMetrics fm2(ui_font(10, QFont::Normal));
  p.setFont(ui_font(12, QFont::DemiBold));
  p.setPen(c1);
  p.drawText(QRect(text_area.x(), 4, text_area.width(), fm1.height()),
             Qt::AlignHCenter | Qt::AlignVCenter,
             fm1.elidedText(line1, Qt::ElideMiddle, text_area.width()));
  p.setFont(ui_font(10));
  p.setPen(c2);
  p.drawText(QRect(text_area.x(), height() - fm2.height() - 4,
                   text_area.width(), fm2.height()),
             Qt::AlignHCenter | Qt::AlignVCenter,
             fm2.elidedText(line2, Qt::ElideRight, text_area.width()));

  // Inline cancel ✕ while importing.
  if (state_ == State::Importing) {
    const QRect cr = cancel_rect();
    if (cancel_hover_) {
      p.setPen(Qt::NoPen);
      p.setBrush(t.control_hover);
      p.drawEllipse(cr);
    }
    QPen pen(cancel_hover_ ? t.text1 : t.text2, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    const QPointF c = QRectF(cr).center();
    p.drawLine(c + QPointF(-3.4, -3.4), c + QPointF(3.4, 3.4));
    p.drawLine(c + QPointF(-3.4, 3.4), c + QPointF(3.4, -3.4));
  }
}

} // namespace cadly::ui
