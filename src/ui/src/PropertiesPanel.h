#pragma once

// Selection properties form. One instance lives in the inspector's
// Properties tab; the sidebar's "Get Info" popover creates short-lived
// instances of the same widget so the two surfaces can never drift apart.
// Private to the ui module.

#include "cadly/scene/Scene.h"

#include <QWidget>

#include <cstdint>
#include <memory>

class QLabel;

namespace cadly::ui {

class PropertiesPanel : public QWidget {
  Q_OBJECT
public:
  explicit PropertiesPanel(QWidget* parent = nullptr);

  void set_scene(std::shared_ptr<scene::Scene> scene);
  void show_node(std::uint32_t node_index);
  void clear();

private:
  QLabel* lbl_source_{nullptr};
  QLabel* lbl_name_{nullptr};
  QLabel* lbl_label_{nullptr};
  QLabel* lbl_mesh_{nullptr};
  QLabel* lbl_material_{nullptr};
  QLabel* lbl_triangles_{nullptr};
  QLabel* lbl_bounds_{nullptr};

  std::shared_ptr<scene::Scene> scene_;
};

} // namespace cadly::ui
