#pragma once

#include "cadly/cad/ICadImporter.h"

namespace cadly::cad {

class OcctIgesImporter final : public ICadImporter {
public:
  bool CanRead(const std::filesystem::path& path) const override;
  ImportResult Import(const ImportRequest& request,
                      IProgressSink& progress) override;
  const char* Name() const override { return "IGES (OCCT XCAF)"; }
};

} // namespace cadly::cad
