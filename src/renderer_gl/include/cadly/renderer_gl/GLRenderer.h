#pragma once

#include "cadly/renderer/IRenderer.h"

#include <functional>
#include <memory>

namespace cadly::renderer_gl {

// Host-supplied OpenGL function-pointer loader. The renderer calls this for
// every GL entry point it uses during `initialize()`. It exists so the GL
// backend stays Qt-free: any host with a current GL context can plug in its
// own loader (QOpenGLContext::getProcAddress, SDL_GL_GetProcAddress,
// glfwGetProcAddress, eglGetProcAddress, glXGetProcAddressARB, …).
//
// A return value of nullptr is treated as "GL function unavailable" and will
// cause `initialize()` to fail loudly.
using GLLoadProc = std::function<void*(const char* name)>;

// Factory. Constructs an IRenderer backed by an OpenGL 4.1 core context.
// The caller is responsible for making the GL context current before invoking
// any method on the returned object, and for supplying a loader that resolves
// names against that same context.
std::unique_ptr<renderer::IRenderer> make_gl_renderer(GLLoadProc load_proc);

} // namespace cadly::renderer_gl
