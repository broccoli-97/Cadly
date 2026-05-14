# renderer_gl uses Qt6::OpenGL (not Widgets) for function loading + shader/program
# helpers. The viewport widget (in ui/) is the one tied to QOpenGLWidget — we keep
# the renderer free of UI types so headless tests can drive it later.
add_library(cadly_renderer_gl STATIC
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

target_link_libraries(cadly_renderer_gl
  PUBLIC
    Cadly::Renderer
    Cadly::Scene
    Qt6::Core
    Qt6::Gui
    Qt6::OpenGL
  PRIVATE
    Cadly::Platform
    Cadly::Warnings
    OpenGL::GL
)
