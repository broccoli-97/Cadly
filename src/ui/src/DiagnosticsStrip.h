#pragma once

// Xcode-debug-area-style diagnostics strip under the viewport (center column
// only): Summary | Log segmented header + close button. Closed by default;
// the owner opens it on import failure and badges its toolbar toggle on
// success. Replaces ImportDiagnosticsDock. Private to the ui module.

#include "cadly/cad/ICadImporter.h"

#include <QWidget>

class QGridLayout;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;

namespace cadly::ui {

class SegmentedControl;
class ToolbarButton;

class DiagnosticsStrip : public QWidget {
  Q_OBJECT
public:
  explicit DiagnosticsStrip(QWidget* parent = nullptr);

  void clear();
  // Fills both panes: the key-value summary grid and the plain-text log
  // (timings, counts, then each diagnostic line).
  void show_summary(const cad::ImportSummary& s);
  void show_log_tab();
  void show_summary_tab();

signals:
  void close_requested();

protected:
  void paintEvent(QPaintEvent*) override;

private:
  void add_summary_cell(QGridLayout* grid, int row, int col,
                        const QString& key, QLabel*& value);

  SegmentedControl* tabs_{nullptr};
  QStackedWidget*   stack_{nullptr};
  QPlainTextEdit*   log_{nullptr};

  QLabel* val_status_{nullptr};
  QLabel* val_total_{nullptr};
  QLabel* val_parse_{nullptr};
  QLabel* val_mesh_{nullptr};
  QLabel* val_shapes_{nullptr};
  QLabel* val_faces_{nullptr};
  QLabel* val_tris_{nullptr};
  QLabel* val_verts_{nullptr};
};

} // namespace cadly::ui
