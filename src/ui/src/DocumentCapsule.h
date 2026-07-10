#pragma once

// Center-of-toolbar document capsule: filename + live stats in the idle
// state, the import progress surface while a job runs (with an inline cancel
// target), and the success/failure annunciator afterwards. Replaces both the
// app-modal QProgressDialog and the old status-bar file label as the primary
// "what is loaded / what is happening" affordance. Private to the ui module.

#include <QString>
#include <QTimer>
#include <QWidget>

namespace cadly::ui {

class DocumentCapsule : public QWidget {
  Q_OBJECT
public:
  explicit DocumentCapsule(QWidget* parent = nullptr);

  void set_empty();
  void set_document(const QString& filename, const QString& stats,
                    const QString& full_path);
  void begin_import(const QString& filename);
  void set_progress(float fraction, const QString& message);
  // Brief green confirmation tint; the caller follows up with
  // set_document() for the new scene.
  void flash_success();
  // Sticky red state until the next set_document/begin_import/set_empty.
  void show_failure(const QString& brief);

  bool importing() const { return state_ == State::Importing; }

  QSize sizeHint() const override { return {380, 36}; }
  QSize minimumSizeHint() const override { return {220, 36}; }

signals:
  void cancel_requested();   // user clicked the inline ✕ during an import
  void clicked();            // idle-state click (owner shows the path popover)

protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void leaveEvent(QEvent*) override;

private:
  enum class State { Empty, Document, Importing, Failed };

  QRect cancel_rect() const;

  State   state_{State::Empty};
  QString filename_;
  QString stats_;
  QString full_path_;
  QString message_;
  float   fraction_{0.0f};
  bool    flash_{false};
  bool    cancel_hover_{false};
  QTimer  flash_timer_;
};

} // namespace cadly::ui
