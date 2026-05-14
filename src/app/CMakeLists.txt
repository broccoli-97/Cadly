add_executable(cadly
  src/main.cpp
  src/Settings.cpp
  src/RecentFiles.cpp
  include/cadly/app/Settings.h
  include/cadly/app/RecentFiles.h
)

target_include_directories(cadly
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(cadly PRIVATE cxx_std_17)

target_link_libraries(cadly
  PRIVATE
    Cadly::Ui
    Cadly::Platform
    Cadly::Warnings
    Qt6::Widgets
)

set_target_properties(cadly PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
)

install(TARGETS cadly RUNTIME DESTINATION ${CADLY_INSTALL_BINDIR})
