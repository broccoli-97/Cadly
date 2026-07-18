#pragma once

// Private to the cad module — OCCT types must not leak past Cadly::Cad.

#include <TDocStd_Application.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>

namespace cadly::cad::occt {

// Owns one XCAF document for the duration of a single import.
//
// XCAFApp_Application is a process-wide singleton and NewDocument registers
// the document with it; until TDocStd_Application::Close is called the
// application session keeps the whole OCAF attribute graph (and every
// triangulation hanging off it) alive. The importers used to leave that to
// chance — no return path called Close, so every import leaked the document
// into the session and repeated imports ratcheted the process RSS upward.
// RAII guarantees Close on success, failure, cancellation, and exceptions
// alike.
//
// Precondition for destruction: nothing built from the document may outlive
// it while still referencing OCCT data. scene::Scene copies everything it
// needs (vertices, indices, names) out of the document, so dropping the
// lease after document_to_scene is safe.
class XcafDocumentLease {
public:
  XcafDocumentLease()
      : app_(XCAFApp_Application::GetApplication()) {
    app_->NewDocument("MDTV-XCAF", doc_);
  }

  ~XcafDocumentLease() {
    if (!doc_.IsNull()) app_->Close(doc_);
  }

  XcafDocumentLease(const XcafDocumentLease&) = delete;
  XcafDocumentLease& operator=(const XcafDocumentLease&) = delete;

  // Non-const ref: STEPCAFControl_Reader::Transfer takes a mutable handle.
  Handle(TDocStd_Document)& doc() { return doc_; }

private:
  Handle(TDocStd_Application) app_;
  Handle(TDocStd_Document)    doc_;
};

} // namespace cadly::cad::occt
