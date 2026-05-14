#pragma once

#include "cadly/cad/ICadImporter.h"

namespace cadly::cad {

// STEP importer using STEPCAFControl for assembly/colour/name preservation.
class OcctStepImporter final : public ICadImporter {
public:
  bool CanRead(const std::filesystem::path& path) const override;
  ImportResult Import(const ImportRequest& request,
                      IProgressSink& progress) override;
  const char* Name() const override { return "STEP (OCCT XCAF)"; }
};

} // namespace cadly::cad
