#include "cadly/ui/ImportDiagnosticsDock.h"

#include <QFontDatabase>
#include <QPlainTextEdit>

namespace cadly::ui {

ImportDiagnosticsDock::ImportDiagnosticsDock(QWidget* parent)
  : QDockWidget(tr("Import Diagnostics"), parent) {
  setObjectName("ImportDiagnosticsDock");
  log_ = new QPlainTextEdit(this);
  log_->setReadOnly(true);
  log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  log_->setLineWrapMode(QPlainTextEdit::NoWrap);
  setWidget(log_);
}

void ImportDiagnosticsDock::clear() {
  log_->clear();
}

void ImportDiagnosticsDock::append(const cad::Diagnostic& diag) {
  const char* tag =
    diag.severity == cad::DiagnosticSeverity::Error   ? "[ERROR]" :
    diag.severity == cad::DiagnosticSeverity::Warning ? "[WARN] " :
                                                        "[info] ";
  log_->appendPlainText(QString::fromUtf8(tag) + " " +
                        QString::fromStdString(diag.message));
}

void ImportDiagnosticsDock::show_summary(const cad::ImportSummary& s) {
  clear();
  log_->appendPlainText(QString("Parse time   : %1 ms").arg(s.parse_time.count()));
  log_->appendPlainText(QString("Mesh time    : %1 ms").arg(s.mesh_time.count()));
  log_->appendPlainText(QString("Total time   : %1 ms").arg(s.total_time.count()));
  log_->appendPlainText(QString("Shapes/nodes : %1").arg(s.shape_count));
  log_->appendPlainText(QString("Faces        : %1").arg(s.face_count));
  log_->appendPlainText(QString("Triangles    : %1").arg(s.triangle_count));
  log_->appendPlainText(QString("Vertices     : %1").arg(s.vertex_count));
  log_->appendPlainText("");
  for (const auto& d : s.diagnostics) append(d);
}

} // namespace cadly::ui
