#include "IblBakePlan.h"

#include <cassert>

using cadly::renderer_gl::detail::IblBakePass;
using cadly::renderer_gl::detail::IblBakePlan;

int main() {
  IblBakePlan plan(/*environment_size=*/4,
                   /*irradiance_size=*/4,
                   /*prefilter_size=*/4,
                   /*prefilter_mip_count=*/2,
                   /*brdf_lut_size=*/4,
                   /*irradiance_tile_size=*/2,
                   /*convolution_tile_size=*/2);

  int environment_faces = 0;
  int irradiance_tiles = 0;
  int prefilter_tiles = 0;
  int brdf_tiles = 0;
  int mip_changes = 0;
  int previous_mip = -1;

  while (const auto step = plan.current()) {
    switch (step->pass) {
      case IblBakePass::EnvironmentFace:
        assert(step->target_size == 4);
        assert(step->width == 4 && step->height == 4);
        ++environment_faces;
        break;
      case IblBakePass::GenerateEnvironmentMips:
        ++mip_changes;
        break;
      case IblBakePass::IrradianceTile:
        assert(step->face >= 0 && step->face < 6);
        assert(step->width <= 2 && step->height <= 2);
        ++irradiance_tiles;
        break;
      case IblBakePass::PrefilterTile:
        assert(step->face >= 0 && step->face < 6);
        assert(step->mip >= previous_mip);
        previous_mip = step->mip;
        ++prefilter_tiles;
        break;
      case IblBakePass::BrdfLutTile:
        assert(step->face == 0 && step->mip == 0);
        assert(step->width <= 2 && step->height <= 2);
        ++brdf_tiles;
        break;
    }
    plan.advance();
  }

  assert(plan.complete());
  assert(environment_faces == 6);
  assert(mip_changes == 1);
  assert(irradiance_tiles == 24);
  assert(prefilter_tiles == 30);
  assert(brdf_tiles == 4);
  return 0;
}
