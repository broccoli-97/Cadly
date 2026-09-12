#pragma once

// The 52px unified toolbar: sidebar toggle · Open split-button (recents in
// the chevron) · Shaded split-menu|Hidden Line|Wireframe segmented control ·
// Section split-button (options in the chevron) · appearance · panel toggles.
// Camera controls (Views / Fit / projection) and zero-chrome live only in the
// viewport HUD — they act on the drawing area, so they sit on it, and
// duplicating them here read as two competing toolbars. The active filename
// lives in the document tab bar below, not in the toolbar.

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
    QAction* edges{nullptr};
    QAction* triangle_mesh{nullptr};
    QAction* hidden_dimmed{nullptr};
    QAction* section{nullptr};
    QMenu*   section_menu{nullptr};
    QAction* toggle_sidebar{nullptr};
    QAction* toggle_inspector{nullptr};
    QAction* toggle_strip{nullptr};
    QAction* theme{nullptr};
  };

  explicit ToolbarWidget(const Actions& actions, QWidget* parent = nullptr);

  SegmentedControl* display_segments() { return segments_; }
  ToolbarButton*    strip_button()     { return strip_btn_; }
  ToolbarButton*    theme_button()     { return theme_btn_; }
  ToolbarButton*    recents_button()   { return recents_btn_; }
  ToolbarButton*    section_menu_button() { return section_menu_btn_; }

protected:
  void paintEvent(QPaintEvent*) override;

private:
  SegmentedControl* segments_{nullptr};
  ToolbarButton*    strip_btn_{nullptr};
  ToolbarButton*    theme_btn_{nullptr};
  ToolbarButton*    recents_btn_{nullptr};
  ToolbarButton*    section_menu_btn_{nullptr};
};

} // namespace cadly::ui
