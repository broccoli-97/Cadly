#include "cadly/cad/XcafSession.h"

#include <XCAFApp_Application.hxx>

namespace cadly::cad {

std::size_t open_xcaf_document_count() {
  const Standard_Integer n =
    XCAFApp_Application::GetApplication()->NbDocuments();
  return n > 0 ? static_cast<std::size_t>(n) : 0;
}

} // namespace cadly::cad
