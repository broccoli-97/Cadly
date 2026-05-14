add_library(cadly_renderer INTERFACE)
add_library(Cadly::Renderer ALIAS cadly_renderer)

target_include_directories(cadly_renderer
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(cadly_renderer INTERFACE cxx_std_17)

target_link_libraries(cadly_renderer
  INTERFACE
    Cadly::Scene
)
