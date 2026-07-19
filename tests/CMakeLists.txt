add_executable(cadly_smoke_test smoke_test.cpp)
target_link_libraries(cadly_smoke_test
  PRIVATE
    Cadly::Scene
    Cadly::Renderer
    Cadly::Cad
    Cadly::Platform
    Cadly::Warnings
)
target_compile_features(cadly_smoke_test PRIVATE cxx_std_17)
target_compile_definitions(cadly_smoke_test
  PRIVATE
    CADLY_TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME cadly_smoke COMMAND cadly_smoke_test)

# The widget tests need Qt6::Test, which the vcpkg-built Qt deliberately
# omits (qtbase is built without "testlib" to keep the Windows from-source
# build down; see vcpkg.json). They also exercise pure widget logic, nothing
# platform-specific, so running them on the system-Qt Linux configs is the
# real gate — on vcpkg configs they are skipped rather than failing configure.
if(CADLY_BUILD_GUI AND NOT TARGET Qt6::Test)
  message(STATUS "Qt6Test not available - skipping GUI widget tests "
    "(vcpkg builds qtbase without the testlib feature)")
endif()

if(CADLY_BUILD_GUI AND TARGET Qt6::Test)
  add_executable(cadly_popover_test popover_test.cpp)
  target_link_libraries(cadly_popover_test
    PRIVATE
      Cadly::Ui
      Cadly::Warnings
      Qt6::Test
      qlementine-icons
  )
  target_include_directories(cadly_popover_test
    PRIVATE
      ${CMAKE_SOURCE_DIR}/src/ui/src
  )
  target_compile_features(cadly_popover_test PRIVATE cxx_std_17)
  add_test(NAME cadly_popover COMMAND cadly_popover_test)
  set_tests_properties(cadly_popover PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )

  add_executable(cadly_toolbar_test toolbar_test.cpp)
  target_link_libraries(cadly_toolbar_test
    PRIVATE
      Cadly::Ui
      Cadly::Warnings
      Qt6::Test
      qlementine-icons
  )
  target_include_directories(cadly_toolbar_test
    PRIVATE
      ${CMAKE_SOURCE_DIR}/src/ui/src
  )
  target_compile_features(cadly_toolbar_test PRIVATE cxx_std_17)
  add_test(NAME cadly_toolbar COMMAND cadly_toolbar_test)
  set_tests_properties(cadly_toolbar PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )

  add_executable(cadly_inspector_test inspector_test.cpp)
  target_link_libraries(cadly_inspector_test
    PRIVATE
      Cadly::Ui
      Cadly::Warnings
      Qt6::Test
      qlementine-icons
  )
  target_include_directories(cadly_inspector_test
    PRIVATE
      ${CMAKE_SOURCE_DIR}/src/ui/src
  )
  target_compile_features(cadly_inspector_test PRIVATE cxx_std_17)
  add_test(NAME cadly_inspector COMMAND cadly_inspector_test)
  set_tests_properties(cadly_inspector PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )

  add_executable(cadly_sidebar_test sidebar_test.cpp)
  target_link_libraries(cadly_sidebar_test
    PRIVATE
      Cadly::Ui
      Cadly::Warnings
      Qt6::Test
      qlementine-icons
  )
  target_include_directories(cadly_sidebar_test
    PRIVATE
      ${CMAKE_SOURCE_DIR}/src/ui/src
  )
  target_compile_features(cadly_sidebar_test PRIVATE cxx_std_17)
  add_test(NAME cadly_sidebar COMMAND cadly_sidebar_test)
  set_tests_properties(cadly_sidebar PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )

  add_executable(cadly_camera_controller_test camera_controller_test.cpp)
  target_link_libraries(cadly_camera_controller_test
    PRIVATE
      Cadly::Ui
      Cadly::Warnings
      Qt6::Test
      qlementine-icons
  )
  target_compile_features(cadly_camera_controller_test PRIVATE cxx_std_17)
  add_test(NAME cadly_camera_controller COMMAND cadly_camera_controller_test)
  set_tests_properties(cadly_camera_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )
endif()
