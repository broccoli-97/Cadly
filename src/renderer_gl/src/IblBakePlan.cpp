#include "IblBakePlan.h"

#include <algorithm>

namespace cadly::renderer_gl::detail {

IblBakePlan::IblBakePlan(int environment_size,
                         int irradiance_size,
                         int prefilter_size,
                         int prefilter_mip_count,
                         int brdf_lut_size,
                         int irradiance_tile_size,
                         int convolution_tile_size)
    : environment_size_(std::max(1, environment_size)),
      irradiance_size_(std::max(1, irradiance_size)),
      prefilter_size_(std::max(1, prefilter_size)),
      prefilter_mip_count_(std::max(1, prefilter_mip_count)),
      brdf_lut_size_(std::max(1, brdf_lut_size)),
      irradiance_tile_size_(std::max(1, irradiance_tile_size)),
      convolution_tile_size_(std::max(1, convolution_tile_size)) {
  step_.pass = IblBakePass::EnvironmentFace;
  step_.target_size = environment_size_;
  step_.width = environment_size_;
  step_.height = environment_size_;
}

std::optional<IblBakeStep> IblBakePlan::current() const {
  if (complete_) return std::nullopt;
  return step_;
}

void IblBakePlan::begin_tiled_pass(IblBakePass pass, int target_size) {
  step_ = {};
  step_.pass = pass;
  step_.target_size = target_size;
  const int tile_size = pass == IblBakePass::IrradianceTile
    ? irradiance_tile_size_ : convolution_tile_size_;
  step_.width = std::min(tile_size, target_size);
  step_.height = std::min(tile_size, target_size);
}

void IblBakePlan::advance_tile(int face_count, bool has_mips) {
  const int tile_size = step_.pass == IblBakePass::IrradianceTile
    ? irradiance_tile_size_ : convolution_tile_size_;
  step_.x += tile_size;
  if (step_.x < step_.target_size) {
    step_.width = std::min(tile_size, step_.target_size - step_.x);
    return;
  }

  step_.x = 0;
  step_.y += tile_size;
  if (step_.y < step_.target_size) {
    step_.width = std::min(tile_size, step_.target_size);
    step_.height = std::min(tile_size, step_.target_size - step_.y);
    return;
  }

  step_.y = 0;
  step_.face += 1;
  if (step_.face < face_count) {
    step_.width = std::min(tile_size, step_.target_size);
    step_.height = std::min(tile_size, step_.target_size);
    return;
  }

  step_.face = 0;
  if (has_mips && step_.mip + 1 < prefilter_mip_count_) {
    step_.mip += 1;
    step_.target_size = std::max(1, prefilter_size_ >> step_.mip);
    step_.width = std::min(tile_size, step_.target_size);
    step_.height = std::min(tile_size, step_.target_size);
    return;
  }

  if (step_.pass == IblBakePass::IrradianceTile) {
    begin_tiled_pass(IblBakePass::PrefilterTile, prefilter_size_);
  } else if (step_.pass == IblBakePass::PrefilterTile) {
    begin_tiled_pass(IblBakePass::BrdfLutTile, brdf_lut_size_);
  } else {
    complete_ = true;
  }
}

void IblBakePlan::advance() {
  if (complete_) return;

  switch (step_.pass) {
    case IblBakePass::EnvironmentFace:
      if (++step_.face < 6) return;
      step_ = {};
      step_.pass = IblBakePass::GenerateEnvironmentMips;
      return;
    case IblBakePass::GenerateEnvironmentMips:
      begin_tiled_pass(IblBakePass::IrradianceTile, irradiance_size_);
      return;
    case IblBakePass::IrradianceTile:
      advance_tile(6, false);
      return;
    case IblBakePass::PrefilterTile:
      advance_tile(6, true);
      return;
    case IblBakePass::BrdfLutTile:
      advance_tile(1, false);
      return;
  }
}

} // namespace cadly::renderer_gl::detail
