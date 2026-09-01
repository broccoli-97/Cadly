#pragma once

// Right inspector: icon tab row over a QStackedWidget with three panes —
// Properties (selection), Display (the previously code-only DisplayMode
// knobs), Import (the persistent import options + pre-flight checkbox +
// re-import). Replaces PropertiesDock and ImportOptionsDialog's modal role.
// Private to the ui module.

#include "cadly/cad/ICadImporter.h"
#include "cadly/renderer/RenderTypes.h"
#include "cadly/scene/Scene.h"

#include <QWidget>

#include <cstdint>
#include <memory>

class QCheckBox;
class QComboBox;
class QPushButton;
class QSlider;
class QStackedWidget;

namespace cadly::ui {

class ImportOptionsWidget;
class PropertiesPanel;
class ToolbarButton;

class InspectorWidget : public QWidget {
  Q_OBJECT
public:
  enum Tab { PropertiesTab = 0, DisplayTab = 1, ImportTab = 2 };

  explicit InspectorWidget(QWidget* parent = nullptr);

  // --- Properties pane -------------------------------------------------
  void set_scene(std::shared_ptr<scene::Scene> scene);
  void show_node(std::uint32_t node_index);

  // --- Display pane ----------------------------------------------------
  // Overlay/AA values applied onto a base DisplayMode (the caller owns the
  // mode-exclusive flags like wireframe).
  void apply_display(renderer::DisplayMode& mode) const;
  void load_display(const renderer::DisplayMode& mode);

  // --- Import pane -----------------------------------------------------
  cad::ImportOptions import_options() const;
  void set_import_options(const cad::ImportOptions& o);
  bool review_before_import() const;
  void set_review_before_import(bool on);
  void set_reimport_enabled(bool on);

  void set_current_tab(Tab tab);
  Tab  current_tab() const;

signals:
  void display_changed();          // any Display-pane control moved
  void import_options_changed();   // persisted by the owner
  void reimport_requested();

protected:
  void paintEvent(QPaintEvent*) override;

private:
  void refresh_icons();
  QWidget* build_display_pane();
  QWidget* build_import_pane();

  QStackedWidget*      stack_{nullptr};
  ToolbarButton*       tab_buttons_[3]{};
  PropertiesPanel*     properties_{nullptr};

  QSlider*             edge_intensity_{nullptr};
  QComboBox*           msaa_{nullptr};
  QCheckBox*           scale_bar_{nullptr};
  QCheckBox*           axes_{nullptr};

  ImportOptionsWidget* import_options_{nullptr};
  QCheckBox*           review_each_{nullptr};
  QPushButton*         reimport_{nullptr};
};

} // namespace cadly::ui
