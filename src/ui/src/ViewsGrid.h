#pragma once

// Content widget for the Views popover: a grid of clickable tiles (Front,
// Back, …, Isometric, Fit) with keycap badges teaching the 1–7/F shortcuts.
// Each tile triggers the QAction it was built from, then dismisses the
// popover it lives in. Private to the ui module.

#include <QAbstractButton>
#include <QList>
#include <QWidget>

class QAction;

namespace cadly::ui {

class ViewTile : public QAbstractButton {
  Q_OBJECT
public:
  explicit ViewTile(QAction* action, QWidget* parent = nullptr);
  QSize sizeHint() const override { return {84, 56}; }

protected:
  void paintEvent(QPaintEvent*) override;
  // QAbstractButton (unlike QToolButton) does not repaint on hover by
  // itself; the tile's hover fill needs these.
  void enterEvent(QEnterEvent*) override { update(); }
  void leaveEvent(QEvent*) override { update(); }

private:
  QString key_label_;
};

class ViewsGrid : public QWidget {
  Q_OBJECT
public:
  explicit ViewsGrid(const QList<QAction*>& actions, QWidget* parent = nullptr);
};

} // namespace cadly::ui
