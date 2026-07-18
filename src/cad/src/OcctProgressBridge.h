#pragma once

// Private to the cad module — OCCT types must not leak past Cadly::Cad.

#include "cadly/cad/ICadImporter.h"

#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressScope.hxx>

#include <string>
#include <utility>

namespace cadly::cad::occt {

// Adapts IProgressSink onto OCCT's Message_ProgressIndicator so the long
// OCCT-internal stages (XCAF Transfer, whole-document meshing) become
// cancellable and report forward progress. Without this bridge the importer
// could only poll cancellation *between* stages — a 231 MB STEP spends ~50 s
// inside Transfer alone, during which a cancel click (or window close, which
// blocks on the worker) went unanswered.
//
// Both overrides are called from inside OCCT, potentially from mesher worker
// threads; IProgressSink implementations are already required to be
// thread-safe for the same reason (the importer itself runs on a worker).
//
// The indicator's [0,1] position is mapped linearly onto the
// [stage_from, stage_to] slice of the overall import so the progress bar
// advances through the stage instead of parking at its start fraction.
class OcctProgressBridge final : public Message_ProgressIndicator {
public:
  OcctProgressBridge(IProgressSink& sink,
                     float stage_from,
                     float stage_to,
                     std::string message)
      : sink_(sink),
        from_(stage_from),
        span_(stage_to - stage_from),
        message_(std::move(message)) {}

  Standard_Boolean UserBreak() override {
    return sink_.cancelled() ? Standard_True : Standard_False;
  }

  void Show(const Message_ProgressScope&, const Standard_Boolean) override {
    sink_.update(from_ + static_cast<float>(GetPosition()) * span_, message_);
  }

private:
  IProgressSink& sink_;
  float          from_;
  float          span_;
  std::string    message_;
};

} // namespace cadly::cad::occt
