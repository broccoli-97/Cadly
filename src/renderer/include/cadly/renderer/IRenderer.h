#pragma once

#include "cadly/renderer/RenderTypes.h"
#include "cadly/scene/SelectionId.h"

#include <cstdint>
#include <memory>

namespace cadly::scene { struct Scene; }

namespace cadly::renderer {

// Renderer abstract interface. Both the OpenGL and the (future) Vulkan
// backends implement it. Anything that needs to know how to draw, but not how
// the backend exposes GPU resources, should depend on IRenderer.
//
// Lifetime:
//   1. The host creates a renderer with `make_gl_renderer()` (or the Vulkan
//      equivalent later).
//   2. After the host has a GL context current, it calls `initialize()`.
//   3. Each frame: optionally `attach_scene()` if it changed, then `render()`.
//   4. `shutdown()` is called from the same context-current thread.
class IRenderer {
public:
  virtual ~IRenderer() = default;

  // Called once after the GL/Vulkan context is current on the calling thread.
  virtual void initialize() = 0;

  // Called when the framebuffer is resized. Width/height are in pixels.
  virtual void resize(int width_px, int height_px) = 0;

  // Replace the active scene. The renderer is allowed (and expected) to
  // upload GPU buffers lazily during `render()` rather than synchronously
  // inside this call.
  virtual void attach_scene(std::shared_ptr<scene::Scene> scene) = 0;

  // Issue a frame. Camera and lighting come from the attached scene.
  virtual void render(const DisplayMode& mode) = 0;

  // Pick the front-most primitive under the given pixel. Returns an invalid
  // SelectionId if nothing was drawn there. The MVP can leave this as a stub.
  virtual scene::SelectionId pick(int /*pixel_x*/, int /*pixel_y*/) {
    return {};
  }

  // Tear down GPU resources. Called while the context is still current.
  virtual void shutdown() = 0;
};

} // namespace cadly::renderer
