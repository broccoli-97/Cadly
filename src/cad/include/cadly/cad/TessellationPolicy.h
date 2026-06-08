#pragma once

#include "cadly/cad/ICadImporter.h"
#include "cadly/scene/Aabb.h"

namespace cadly::cad {

struct ResolvedTessellation {
  ImportOptions options;
  double model_extent{0.0};
  double resolved_linear_deflection{0.0};
};

double model_extent_for_tessellation(const scene::Aabb& bounds);

ResolvedTessellation resolve_tessellation_policy(
  const ImportOptions& requested,
  const scene::Aabb& model_bounds);

const char* tessellation_mode_name(TessellationMode mode);

} // namespace cadly::cad
