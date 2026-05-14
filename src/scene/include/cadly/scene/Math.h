#pragma once

// Single point of glm configuration so every translation unit sees the same
// version of vec3/mat4/quaternion semantics. Including a header that depends
// on cadly::scene types must include this first.

#define GLM_FORCE_RADIANS
// Leave GLM_FORCE_DEPTH_ZERO_TO_ONE undefined for the OpenGL backend so glm
// returns the standard [-1,1] z-clip projection. Define it before including
// glm/glm.hpp when wiring up the Vulkan backend.
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace cadly::scene {

using vec2  = glm::vec2;
using vec3  = glm::vec3;
using vec4  = glm::vec4;
using mat3  = glm::mat3;
using mat4  = glm::mat4;
using quat  = glm::quat;

} // namespace cadly::scene
