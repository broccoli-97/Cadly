add_library(cadly_input_qt STATIC
  src/InputPreferences.cpp
  src/NavigationLabels.cpp
  src/QtInputAdapter.cpp
  include/cadly/input_qt/InputPreferences.h
)
add_library(Cadly::InputQt ALIAS cadly_input_qt)
target_include_directories(cadly_input_qt PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(cadly_input_qt PUBLIC cxx_std_17)
target_link_libraries(cadly_input_qt
  PUBLIC Cadly::Input Qt6::Core Qt6::Gui
  PRIVATE Cadly::Warnings
)
