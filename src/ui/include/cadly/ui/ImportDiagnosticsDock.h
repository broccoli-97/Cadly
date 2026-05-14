#pragma once

#include "cadly/cad/ICadImporter.h"

#include <QDockWidget>

class QPlainTextEdit;

namespace cadly::ui {

class ImportDiagnosticsDock : public QDockWidget {
  Q_OBJECT
public:
  explicit ImportDiagnosticsDock(QWidget* parent = nullptr);

  void show_summary(const cad::ImportSummary& summary);
  void append(const cad::Diagnostic& diag);
  void clear();

private:
  QPlainTextEdit* log_{nullptr};
};

} // namespace cadly::ui
