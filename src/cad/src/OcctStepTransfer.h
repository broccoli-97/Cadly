#pragma once

#include "cadly/cad/ICadImporter.h"

#include <Standard_Handle.hxx>

class STEPCAFControl_Reader;
class TDocStd_Document;

namespace cadly::cad::occt {

// Requires OcctImportGuard. Workers own their actors, transfer maps and BRep;
// XCAF construction and binding results into the reader remain serial.
bool transfer_step(STEPCAFControl_Reader& reader,
                   Handle(TDocStd_Document)& document,
                   const ImportOptions& options,
                   ImportSummary& summary,
                   IProgressSink& progress);

} // namespace cadly::cad::occt
