#pragma once

// The 52px unified toolbar: sidebar toggle · Open split-button (recents in
// the chevron) · Shaded split-menu|Hidden Line|Wireframe segmented control ·
// projection / Views / Fit · appearance · panel toggles · zero-chrome. The
// active filename lives in the document tab bar below, not in the toolbar.

#include <QWidget>

class QAction;
class QMenu;

namespace cadly::ui {

class SegmentedControl;
class ToolbarButton;

class ToolbarWidget : public QWidget {
  Q_OBJECT
public:
  struct Actions {
    QAction* open{nullptr};
    QMenu*   recents_menu{nullptr};
    QAction* fit{nullptr};
    QAction* edges{nullptr};
    QAction* triangle_mesh{nullptr};
    QAction* perspective{nullptr};
    QAction* toggle_sidebar{nullptr};
    QAction* toggle_inspector{nullptr};
    QAction* toggle_strip{nullptr};
    QAction* zero_chrome{nullptr};
    QAction* theme{nullptr};
  };

  explicit ToolbarWidget(const Actions& actions, QWidget* parent = nullptr);

  SegmentedControl* display_segments() { return segments_; }
  ToolbarButton*    views_button()     { return views_btn_; }
  ToolbarButton*    strip_button()     { return strip_btn_; }
  ToolbarButton*    theme_button()     { return theme_btn_; }
  ToolbarButton*    recents_button()   { return recents_btn_; }

protected:
  void paintEvent(QPaintEvent*) override;

private:
  SegmentedControl* segments_{nullptr};
  ToolbarButton*    views_btn_{nullptr};
  ToolbarButton*    strip_btn_{nullptr};
  ToolbarButton*    theme_btn_{nullptr};
  ToolbarButton*    recents_btn_{nullptr};
};

} // namespace cadly::ui
