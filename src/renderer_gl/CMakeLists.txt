# renderer_gl is the OpenGL 4.1 backend. It is deliberately Qt-free — the host
# supplies a GL function-pointer loader (see cadly/renderer_gl/GLRenderer.h),
# which lets the same library run under Qt, SDL, GLFW, EGL, raw glX/wgl, or any
# future framework. The Qt host's wiring lives in src/ui/ViewportWidget.cpp.
add_library(cadly_renderer_gl STATIC
  src/GLFunctions.cpp
  src/GLRenderer.cpp
  src/GLShader.cpp
)
add_library(Cadly::RendererGL ALIAS cadly_renderer_gl)

target_include_directories(cadly_renderer_gl
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(cadly_renderer_gl PUBLIC cxx_std_17)

# OpenGL::GL contributes the include path for <GL/glcorearb.h> on the platforms
# where it lives next to <GL/gl.h>. No actual GL symbols are linked — every
# entry point is resolved at runtime through the host-supplied loader.
target_link_libraries(cadly_renderer_gl
  PUBLIC
    Cadly::Renderer
    Cadly::Scene
  PRIVATE
    Cadly::Platform
    Cadly::Warnings
    OpenGL::GL
)
