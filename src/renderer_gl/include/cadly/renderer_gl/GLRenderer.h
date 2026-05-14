#pragma once

#include "cadly/renderer/IRenderer.h"

#include <memory>

namespace cadly::renderer_gl {

// Factory. Constructs an IRenderer backed by a Qt OpenGL 4.1 core context.
// The caller is responsible for making the GL context current before invoking
// any method on the returned object.
std::unique_ptr<renderer::IRenderer> make_gl_renderer();

} // namespace cadly::renderer_gl
