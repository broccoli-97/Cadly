#include "cadly/cad/TessellationPolicy.h"

#include <algorithm>
#include <cmath>

namespace cadly::cad {

double model_extent_for_tessellation(const scene::Aabb& bounds) {
  if (!bounds.valid()) return 0.0;
  const auto e = bounds.extent();
  return static_cast<double>(std::max({e.x, e.y, e.z}));
}

ResolvedTessellation resolve_tessellation_policy(
  const ImportOptions& requested,
  const scene::Aabb& model_bounds) {
  ResolvedTessellation resolved;
  resolved.options = requested;
  resolved.model_extent = model_extent_for_tessellation(model_bounds);

  if (requested.tessellation_mode == TessellationMode::Absolute ||
      resolved.model_extent <= 0.0 ||
      requested.reference_screen_pixels <= 0.0 ||
      requested.target_screen_error_px <= 0.0) {
    resolved.options.tessellation_mode = TessellationMode::Absolute;
    resolved.options.relative_deflection = requested.relative_deflection;
    resolved.resolved_linear_deflection = requested.linear_deflection;
    return resolved;
  }

  const double visual_deflection =
    requested.target_screen_error_px * resolved.model_extent /
    requested.reference_screen_pixels;
  const double min_deflection = std::max(0.0, requested.min_linear_deflection);
  const double max_deflection =
    requested.max_relative_deflection > 0.0
      ? std::max(min_deflection,
                 requested.max_relative_deflection * resolved.model_extent)
      : visual_deflection;

  resolved.options.linear_deflection =
    std::clamp(visual_deflection, min_deflection, max_deflection);
  resolved.options.relative_deflection = false;
  resolved.resolved_linear_deflection = resolved.options.linear_deflection;
  return resolved;
}

const char* tessellation_mode_name(TessellationMode mode) {
  switch (mode) {
  case TessellationMode::Absolute:       return "absolute";
  case TessellationMode::VisualRelative: return "visual-relative";
  }
  return "unknown";
}

} // namespace cadly::cad
