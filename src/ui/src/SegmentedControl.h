#pragma once

// macOS-style segmented control, custom-painted from ThemeTokens so it looks
// identical under Fusion (Qt 6.4) and qlementine (Qt 6.8). Used for
// Shaded|Wireframe in the toolbar, Summary|Log in the diagnostics strip, and
// the inspector's tab row. Private to the ui module.

#include <QStringList>
#include <QWidget>

#include <vector>

namespace cadly::ui {

class SegmentedControl : public QWidget {
  Q_OBJECT
public:
  explicit SegmentedControl(QWidget* parent = nullptr);

  int  add_segment(const QString& text, const QString& tooltip = {});
  void set_current(int index);                    // no signal emitted
  int  current() const { return current_; }
  int  count() const { return static_cast<int>(segments_.size()); }

  // Compact = 20px tall (strip header); default = 26px (toolbar).
  void set_compact(bool on);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return sizeHint(); }

signals:
  // User clicked a different segment (not emitted by set_current()).
  void segment_clicked(int index);

protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void leaveEvent(QEvent*) override;

private:
  struct Segment {
    QString text;
  };
  int index_at(const QPoint& pos) const;
  int segment_width(const Segment& s) const;

  std::vector<Segment> segments_;
  int  current_{0};
  int  hover_{-1};
  bool compact_{false};
};

} // namespace cadly::ui
