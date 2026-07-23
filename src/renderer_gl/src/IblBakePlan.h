#pragma once

#include <optional>

namespace cadly::renderer_gl::detail {

enum class IblBakePass {
  EnvironmentFace,
  GenerateEnvironmentMips,
  IrradianceTile,
  PrefilterTile,
  BrdfLutTile,
};

struct IblBakeStep {
  IblBakePass pass{IblBakePass::EnvironmentFace};
  int face{0};
  int mip{0};
  int target_size{0};
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

// Produces small, deterministic bake units so expensive IBL convolution is
// spread across frames instead of blocking renderer initialization.
class IblBakePlan {
public:
  IblBakePlan(int environment_size,
              int irradiance_size,
              int prefilter_size,
              int prefilter_mip_count,
              int brdf_lut_size,
              int irradiance_tile_size = 8,
              int convolution_tile_size = 16);

  std::optional<IblBakeStep> current() const;
  void advance();
  bool complete() const { return complete_; }

private:
  void begin_tiled_pass(IblBakePass pass, int target_size);
  void advance_tile(int face_count, bool has_mips);

  int environment_size_{0};
  int irradiance_size_{0};
  int prefilter_size_{0};
  int prefilter_mip_count_{0};
  int brdf_lut_size_{0};
  int irradiance_tile_size_{0};
  int convolution_tile_size_{0};

  IblBakeStep step_{};
  bool complete_{false};
};

} // namespace cadly::renderer_gl::detail
