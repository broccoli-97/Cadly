#pragma once

#include <cstddef>

namespace cadly::cad {

// Number of XCAF documents currently open in the process-wide OCCT
// application session. Importers must leave this at zero after every
// import — success, failure, or cancellation. A nonzero steady state means
// an import leaked its OCAF document (and the whole attribute graph plus
// triangulations hanging off it) into the session. Exposed as a public
// diagnostic seam so tests can assert the invariant without linking OCCT.
std::size_t open_xcaf_document_count();

} // namespace cadly::cad
