#pragma once

// Custom-painted QToolButton for the Graphite toolbar: rounded hover/checked
// fills from ThemeTokens, optional text label, optional notification badge
// dot, optional menu chevron. Subclassing QToolButton (rather than QWidget)
// keeps defaultAction() sync — checked/enabled/tooltip/shortcut state flows
// from the QAction shared with the menu bar — and menu popup handling for
// free; only the painting is replaced. Private to the ui module.

#include <QToolButton>

namespace cadly::ui {

class ToolbarButton : public QToolButton {
  Q_OBJECT
public:
  enum class Emphasis {
    Neutral,   // hover/checked fills are neutral greys (panel toggles, fit…)
    Accent,    // checked state tints with the accent (display-mode chips)
  };

  explicit ToolbarButton(QWidget* parent = nullptr);

  void set_emphasis(Emphasis e) { emphasis_ = e; update(); }
  void set_show_text(bool on);
  // Notification badge dot (top-right). Used by the Diagnostics toggle after
  // a successful import ("there is news, but nothing demands your focus").
  void set_badge(bool on);
  bool badge() const { return badge_; }

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  void paintEvent(QPaintEvent*) override;

private:
  Emphasis emphasis_{Emphasis::Neutral};
  bool show_text_{false};
  bool badge_{false};
};

} // namespace cadly::ui
