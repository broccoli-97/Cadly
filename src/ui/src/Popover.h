#pragma once

// Anchored popover + pinnable floating card for the Graphite shell.
//
// Popover is a frameless Qt::Popup window placed under (or over) an anchor
// widget: click-outside and Esc dismiss it, matching the macOS popover
// contract. PinnedCard is what a pinnable popover turns into when the user
// hits its pin button — a floating child card that survives click-outside,
// so two "Get Info" cards can sit side by side for part comparison. Private
// to the ui module.

#include <QPoint>
#include <QPointer>
#include <QWidget>

namespace cadly::ui {

class Popover : public QWidget {
  Q_OBJECT
public:
  // Takes ownership of `content`, shows the popover anchored to `anchor`
  // (below it when there is room, above otherwise), and deletes itself when
  // dismissed. When `title` is non-empty a header row with the title and —
  // if `pinnable` — a pin button is added; pinning reparents the content
  // into a PinnedCard at the same screen position.
  static Popover* show_for(QWidget* content, QWidget* anchor,
                           const QString& title = {}, bool pinnable = false);

  // Same, anchored to an arbitrary global rect (e.g. a tree row).
  static Popover* show_at(QWidget* content, const QRect& anchor_global,
                          const QString& title = {}, bool pinnable = false,
                          QWidget* owner_window = nullptr);

protected:
  void paintEvent(QPaintEvent*) override;

private:
  explicit Popover(QWidget* content, const QString& title, bool pinnable,
                   QWidget* owner_window);
  void pin();

  QWidget* content_{nullptr};
  QString  title_;
  QPointer<QWidget> owner_window_;
};

class PinnedCard : public QWidget {
  Q_OBJECT
public:
  // Takes ownership of `content`. Floats above the main-window contents,
  // draggable by its header, closed by its ✕ button or Esc.
  PinnedCard(QWidget* content, const QString& title, QWidget* main_window);

protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;

private:
  QPoint drag_offset_;
  bool   dragging_{false};
};

// Shared bits: true when frameless windows should not rely on an alpha
// channel (no compositor / explicitly disabled via CADLY_OPAQUE_POPOVERS=1).
// Translucent popovers on a server without ARGB visuals render their rounded
// corners as black blocks, which is worse than square corners.
bool popovers_must_be_opaque();

} // namespace cadly::ui
