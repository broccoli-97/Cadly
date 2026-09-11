add_library(cadly_input STATIC
  src/Navigation.cpp
  src/PointerRouter.cpp
)
add_library(Cadly::Input ALIAS cadly_input)

set_target_properties(cadly_input PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_include_directories(cadly_input PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(cadly_input PUBLIC cxx_std_17)
# Input policy must not depend on Qt, scene/camera math, or a renderer.
target_link_libraries(cadly_input PRIVATE Cadly::Warnings)
