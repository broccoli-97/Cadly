#pragma once

#include "cadly/scene/Scene.h"

#include <QDockWidget>
#include <cstdint>
#include <memory>

class QFormLayout;
class QLabel;

namespace cadly::ui {

// Properties dock — shows name/material/transform for the selected node and
// summary stats for the scene. Read-only in the MVP (no editing yet).
class PropertiesDock : public QDockWidget {
  Q_OBJECT
public:
  explicit PropertiesDock(QWidget* parent = nullptr);

  void set_scene(std::shared_ptr<scene::Scene> scene);

public slots:
  void show_node(std::uint32_t node_index);
  void clear();

private:
  std::shared_ptr<scene::Scene> scene_;
  QFormLayout* form_{nullptr};
  QLabel* lbl_source_{nullptr};
  QLabel* lbl_name_{nullptr};
  QLabel* lbl_label_{nullptr};
  QLabel* lbl_mesh_{nullptr};
  QLabel* lbl_material_{nullptr};
  QLabel* lbl_triangles_{nullptr};
  QLabel* lbl_bounds_{nullptr};
};

} // namespace cadly::ui
