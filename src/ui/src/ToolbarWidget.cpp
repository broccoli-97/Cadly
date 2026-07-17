#include "ToolbarWidget.h"

#include "SegmentedControl.h"
#include "ToolbarButton.h"
#include "cadly/ui/ThemeTokens.h"

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>

namespace cadly::ui {

namespace {

// Thin vertical hairline between toolbar groups.
class ToolbarSeparator final : public QWidget {
public:
  explicit ToolbarSeparator(QWidget* parent) : QWidget(parent) {
    setFixedSize(9, 28);
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.fillRect(QRect(width() / 2, 5, 1, height() - 10),
               tokens().hairline_soft);
  }
};

class SplitButtonFrame final : public QWidget {
public:
  explicit SplitButtonFrame(QWidget* parent) : QWidget(parent) {
    setFixedHeight(28);
  }
  void set_right_widget(QWidget* widget) { right_ = widget; }

protected:
  void paintEvent(QPaintEvent*) override {
    const auto& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(t.hairline_soft, 1.0));
    p.setBrush(t.control_bg);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    if (right_) {
      const int x = right_->geometry().left();
      p.drawLine(QPointF(x, 5), QPointF(x, height() - 5));
    }
  }

private:
  QWidget* right_{nullptr};
};

ToolbarButton* make_button(QAction* action, QWidget* parent,
                           bool show_text = false,
                           ToolbarButton::Emphasis emphasis =
                             ToolbarButton::Emphasis::Neutral) {
  auto* b = new ToolbarButton(parent);
  b->setDefaultAction(action);   // text comes from the action's iconText()
  b->set_show_text(show_text);
  b->set_emphasis(emphasis);
  return b;
}

} // namespace

ToolbarWidget::ToolbarWidget(const Actions& a, QWidget* parent)
  : QWidget(parent) {
  setFixedHeight(52);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(10, 0, 10, 0);
  layout->setSpacing(6);

  layout->addWidget(make_button(a.toggle_sidebar, this));

  // Open split-button: the main button opens the file dialog, the joined
  // chevron pops the recent-files menu.
  auto* open_frame = new SplitButtonFrame(this);
  auto* open_group = new QHBoxLayout(open_frame);
  open_group->setContentsMargins(0, 0, 0, 0);
  open_group->setSpacing(1);
  open_group->addWidget(make_button(a.open, open_frame, /*show_text=*/true));
  recents_btn_ = new ToolbarButton(open_frame);
  recents_btn_->setMenu(a.recents_menu);
  recents_btn_->setPopupMode(QToolButton::InstantPopup);
  recents_btn_->setToolTip(tr("Recent files"));
  open_group->addWidget(recents_btn_);
  open_frame->set_right_widget(recents_btn_);
  layout->addWidget(open_frame);

  layout->addStretch(1);

  segments_ = new SegmentedControl(this);
  segments_->add_segment(tr("Shaded"),
                         tr("Shaded surfaces (W toggles wireframe)"));
  segments_->add_segment(tr("Hidden Line"),
                         tr("Technical drawing with hidden edges removed (H)"));
  segments_->add_segment(tr("Wireframe"),
                         tr("BRep wireframe only (W)"));
  layout->addWidget(segments_);

  layout->addWidget(make_button(a.edges, this, true,
                                ToolbarButton::Emphasis::Accent));
  layout->addWidget(make_button(a.triangle_mesh, this, true,
                                ToolbarButton::Emphasis::Accent));

  layout->addWidget(new ToolbarSeparator(this));

  layout->addWidget(make_button(a.perspective, this, false,
                                ToolbarButton::Emphasis::Accent));
  views_btn_ = new ToolbarButton(this);
  views_btn_->setToolTip(tr("Standard views (1–7)"));
  layout->addWidget(views_btn_);
  layout->addWidget(make_button(a.fit, this));

  layout->addWidget(new ToolbarSeparator(this));

  theme_btn_ = make_button(a.theme, this);
  layout->addWidget(theme_btn_);

  layout->addWidget(new ToolbarSeparator(this));

  layout->addWidget(make_button(a.toggle_inspector, this));
  strip_btn_ = make_button(a.toggle_strip, this);
  layout->addWidget(strip_btn_);
  layout->addWidget(make_button(a.zero_chrome, this));

  connect(&ThemeManager::instance(), &ThemeManager::changed,
          this, QOverload<>::of(&QWidget::update));
}

void ToolbarWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const auto& t = tokens();
  p.fillRect(rect(), t.toolbar_bg);
  p.fillRect(QRect(0, height() - 1, width(), 1), t.hairline);
}

} // namespace cadly::ui
