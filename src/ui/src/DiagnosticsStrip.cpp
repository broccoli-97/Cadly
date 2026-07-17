#include "DiagnosticsStrip.h"

#include "IconUtils.h"
#include "SegmentedControl.h"
#include "ToolbarButton.h"
#include "cadly/ui/ThemeTokens.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace cadly::ui {

DiagnosticsStrip::DiagnosticsStrip(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(120);

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  auto* header = new QWidget(this);
  header->setFixedHeight(28);
  auto* hl = new QHBoxLayout(header);
  hl->setContentsMargins(8, 2, 6, 2);
  tabs_ = new SegmentedControl(header);
  tabs_->set_compact(true);
  tabs_->add_segment(tr("Summary"));
  tabs_->add_segment(tr("Log"));
  hl->addWidget(tabs_);
  hl->addStretch();
  auto* close_btn = new ToolbarButton(header);
  close_btn->setIcon(themed_icon(QStringLiteral("action/close-small")));
  close_btn->setToolTip(tr("Hide diagnostics"));
  hl->addWidget(close_btn);
  outer->addWidget(header);

  stack_ = new QStackedWidget(this);

  // Summary pane: a compact key/value grid like the prototype's strip.
  auto* summary = new QWidget(this);
  summary->setAutoFillBackground(false);
  auto* grid = new QGridLayout(summary);
  grid->setContentsMargins(12, 8, 12, 8);
  grid->setHorizontalSpacing(28);
  grid->setVerticalSpacing(4);
  add_summary_cell(grid, 0, 0, tr("Status"),    val_status_);
  add_summary_cell(grid, 0, 1, tr("Total"),     val_total_);
  add_summary_cell(grid, 0, 2, tr("Parse"),     val_parse_);
  add_summary_cell(grid, 0, 3, tr("Mesh"),      val_mesh_);
  add_summary_cell(grid, 1, 0, tr("Shapes"),    val_shapes_);
  add_summary_cell(grid, 1, 1, tr("Faces"),     val_faces_);
  add_summary_cell(grid, 1, 2, tr("Triangles"), val_tris_);
  add_summary_cell(grid, 1, 3, tr("Vertices"),  val_verts_);
  grid->setColumnStretch(4, 1);
  grid->setRowStretch(2, 1);
  stack_->addWidget(summary);

  log_ = new QPlainTextEdit(this);
  log_->setReadOnly(true);
  log_->setFont(mono_font(12));
  log_->setLineWrapMode(QPlainTextEdit::NoWrap);
  log_->setFrameShape(QFrame::NoFrame);
  stack_->addWidget(log_);

  outer->addWidget(stack_, 1);

  connect(tabs_, &SegmentedControl::segment_clicked, this, [this](int idx) {
    stack_->setCurrentIndex(idx);
    if (idx == 1) {
      log_->verticalScrollBar()->setValue(
        log_->verticalScrollBar()->maximum());
    }
  });
  connect(close_btn, &QAbstractButton::clicked, this,
          [this]() { emit close_requested(); });
  connect(&ThemeManager::instance(), &ThemeManager::changed, this,
          [this, close_btn]() {
            close_btn->setIcon(
              themed_icon(QStringLiteral("action/close-small")));
            update();
          });
}

void DiagnosticsStrip::add_summary_cell(QGridLayout* grid, int row, int col,
                                        const QString& key, QLabel*& value) {
  auto* container = new QWidget(this);
  auto* v = new QVBoxLayout(container);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  auto* k = new QLabel(key, container);
  k->setFont(ui_font(10, QFont::DemiBold));
  value = new QLabel(QStringLiteral("—"), container);
  value->setFont(mono_font(12));
  v->addWidget(k);
  v->addWidget(value);
  grid->addWidget(container, row, col);
}

void DiagnosticsStrip::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const auto& t = tokens();
  p.fillRect(rect(), t.strip_bg);
  p.fillRect(QRect(0, 0, width(), 1), t.hairline);
}

void DiagnosticsStrip::clear() {
  log_->clear();
  for (auto* l : {val_status_, val_total_, val_parse_, val_mesh_,
                  val_shapes_, val_faces_, val_tris_, val_verts_}) {
    if (l) l->setText(QStringLiteral("—"));
  }
}

void DiagnosticsStrip::show_summary(const cad::ImportSummary& s) {
  clear();

  bool has_error = false;
  bool has_warn  = false;
  for (const auto& d : s.diagnostics) {
    has_error |= d.severity == cad::DiagnosticSeverity::Error;
    has_warn  |= d.severity == cad::DiagnosticSeverity::Warning;
  }
  const auto& t = tokens();
  QPalette pal = val_status_->palette();
  pal.setColor(QPalette::WindowText,
               has_error ? t.error : has_warn ? t.warn : t.ok);
  val_status_->setPalette(pal);
  val_status_->setText(has_error ? tr("Failed")
                       : has_warn ? tr("OK, with warnings") : tr("OK"));

  val_total_->setText(QStringLiteral("%1 ms").arg(s.total_time.count()));
  val_parse_->setText(QStringLiteral("%1 ms").arg(s.parse_time.count()));
  val_mesh_->setText(QStringLiteral("%1 ms").arg(s.mesh_time.count()));
  val_shapes_->setText(QString::number(s.shape_count));
  val_faces_->setText(QString::number(s.face_count));
  val_tris_->setText(QString::number(s.triangle_count));
  val_verts_->setText(QString::number(s.vertex_count));

  log_->appendPlainText(QStringLiteral("Parse time   : %1 ms").arg(s.parse_time.count()));
  log_->appendPlainText(QStringLiteral("Mesh time    : %1 ms").arg(s.mesh_time.count()));
  log_->appendPlainText(QStringLiteral("Total time   : %1 ms").arg(s.total_time.count()));
  log_->appendPlainText(QStringLiteral("Shapes/nodes : %1").arg(s.shape_count));
  log_->appendPlainText(QStringLiteral("Faces        : %1").arg(s.face_count));
  log_->appendPlainText(QStringLiteral("Triangles    : %1").arg(s.triangle_count));
  log_->appendPlainText(QStringLiteral("Vertices     : %1").arg(s.vertex_count));
  log_->appendPlainText(QString());
  for (const auto& d : s.diagnostics) {
    const char* tag =
      d.severity == cad::DiagnosticSeverity::Error   ? "[ERROR]" :
      d.severity == cad::DiagnosticSeverity::Warning ? "[WARN] " :
                                                       "[info] ";
    log_->appendPlainText(QString::fromUtf8(tag) + QChar(' ') +
                          QString::fromStdString(d.message));
  }
}

void DiagnosticsStrip::show_log_tab() {
  tabs_->set_current(1);
  stack_->setCurrentIndex(1);
  log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
}

void DiagnosticsStrip::show_summary_tab() {
  tabs_->set_current(0);
  stack_->setCurrentIndex(0);
}

} // namespace cadly::ui
