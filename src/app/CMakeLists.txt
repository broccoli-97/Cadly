add_executable(cadly
  src/main.cpp
  src/Settings.cpp
  src/RecentFiles.cpp
  src/Theme.cpp
  include/cadly/app/Settings.h
  include/cadly/app/RecentFiles.h
  include/cadly/app/Theme.h
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
    qlementine-icons
)

# The full qlementine QStyle is only built on Qt >= 6.8 (see the root
# CMakeLists); Theme.cpp compiles its Fusion + dark palette fallback when the
# define is absent.
if(CADLY_HAS_QLEMENTINE)
  target_link_libraries(cadly PRIVATE qlementine)
  target_compile_definitions(cadly PRIVATE CADLY_HAS_QLEMENTINE)
endif()

set_target_properties(cadly PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
)

install(TARGETS cadly RUNTIME DESTINATION ${CADLY_INSTALL_BINDIR})
