function(cadly_check_module_dependencies target)
  get_target_property(dependencies ${target} LINK_LIBRARIES)
  set(allowed ${ARGN})
  foreach(dependency IN LISTS dependencies)
    if(NOT dependency IN_LIST allowed)
      message(FATAL_ERROR "Module boundary: ${target} must not depend on ${dependency}")
    endif()
  endforeach()
endfunction()

cadly_check_module_dependencies(cadly_input Cadly::Warnings)
if(CADLY_BUILD_GUI)
  cadly_check_module_dependencies(cadly_input_qt
    Cadly::Input Qt6::Core Qt6::Gui Cadly::Warnings)
  cadly_check_module_dependencies(cadly_app_settings
    Cadly::Input Qt6::Core Cadly::Warnings)
  cadly_check_module_dependencies(cadly_camera_controller
    Cadly::Input Cadly::Scene Qt6::Core Cadly::Warnings)
endif()

add_executable(cadly_navigation_scheme_test navigation_scheme_test.cpp)
target_link_libraries(cadly_navigation_scheme_test PRIVATE Cadly::Input Cadly::Warnings)
set_target_properties(cadly_navigation_scheme_test PROPERTIES AUTOMOC OFF AUTOUIC OFF)
add_test(NAME cadly_navigation_scheme COMMAND cadly_navigation_scheme_test)

add_executable(cadly_pointer_router_test pointer_router_test.cpp)
target_link_libraries(cadly_pointer_router_test PRIVATE Cadly::Input Cadly::Warnings)
set_target_properties(cadly_pointer_router_test PROPERTIES AUTOMOC OFF AUTOUIC OFF)
add_test(NAME cadly_pointer_router COMMAND cadly_pointer_router_test)

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

add_executable(cadly_ibl_bake_plan_test
  ibl_bake_plan_test.cpp
  ${CMAKE_SOURCE_DIR}/src/renderer_gl/src/IblBakePlan.cpp
)
target_include_directories(cadly_ibl_bake_plan_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/renderer_gl/src
)
target_link_libraries(cadly_ibl_bake_plan_test PRIVATE Cadly::Warnings)
target_compile_features(cadly_ibl_bake_plan_test PRIVATE cxx_std_17)
add_test(NAME cadly_ibl_bake_plan COMMAND cadly_ibl_bake_plan_test)

add_executable(cadly_shader_source_test shader_source_test.cpp)
# The private-header include also pulls in GLFunctions.h, whose
# <GL/glcorearb.h> comes from renderer_gl's vendored khronos tree.
target_include_directories(cadly_shader_source_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/renderer_gl/src
)
target_include_directories(cadly_shader_source_test SYSTEM PRIVATE
  ${CMAKE_SOURCE_DIR}/src/renderer_gl/third_party/khronos
)
target_compile_definitions(cadly_shader_source_test
  PRIVATE
    CADLY_TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(cadly_shader_source_test PRIVATE Cadly::RendererGL)
target_compile_features(cadly_shader_source_test PRIVATE cxx_std_17)
add_test(NAME cadly_shader_source COMMAND cadly_shader_source_test)

if(CADLY_BUILD_GUI)
  # Core Qt adapters, persistence, and camera execution do not require Widgets,
  # OpenGL, OCCT, or QtTest. They also run on the Windows/vcpkg Qt build that
  # omits testlib, so platform compatibility is not silently skipped there.
  add_executable(cadly_qt_input_adapter_test qt_input_adapter_test.cpp)
  target_link_libraries(cadly_qt_input_adapter_test PRIVATE Cadly::InputQt Cadly::Warnings)
  add_test(NAME cadly_qt_input_adapter COMMAND cadly_qt_input_adapter_test)
  set_tests_properties(cadly_qt_input_adapter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(cadly_input_settings_test input_settings_test.cpp)
  target_link_libraries(cadly_input_settings_test PRIVATE Cadly::AppSettings Cadly::Warnings)
  add_test(NAME cadly_input_settings COMMAND cadly_input_settings_test)

  add_executable(cadly_camera_controller_test camera_controller_test.cpp)
  target_link_libraries(cadly_camera_controller_test PRIVATE Cadly::CameraController Cadly::Warnings)
  add_test(NAME cadly_camera_controller COMMAND cadly_camera_controller_test)

  add_executable(cadly_section_render_test section_render_test.cpp)
  target_link_libraries(cadly_section_render_test
    PRIVATE Cadly::RendererGL Cadly::Warnings Qt6::Gui Qt6::OpenGL
  )
  target_compile_definitions(cadly_section_render_test
    PRIVATE CADLY_TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
  )
  add_test(NAME cadly_section_render COMMAND cadly_section_render_test)
  # Headless hosts without desktop GL skip; a GL-capable offscreen platform
  # (or a direct run with QT_QPA_PLATFORM=xcb) exercises real framebuffer pixels.
  set_tests_properties(cadly_section_render PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    SKIP_RETURN_CODE 77
    TIMEOUT 60
  )
endif()

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

  add_executable(cadly_viewport_input_test viewport_input_test.cpp)
  target_link_libraries(cadly_viewport_input_test
    PRIVATE
      Cadly::Ui
      Cadly::Warnings
      Qt6::Test
      qlementine-icons
  )
  target_compile_features(cadly_viewport_input_test PRIVATE cxx_std_17)
  add_test(NAME cadly_viewport_input COMMAND cadly_viewport_input_test)
  set_tests_properties(cadly_viewport_input PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )

  add_test(NAME cadly_viewport_input_hidpi COMMAND cadly_viewport_input_test)
  set_tests_properties(cadly_viewport_input_hidpi PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2"
  )

  add_executable(cadly_preferences_dialog_test preferences_dialog_test.cpp)
  target_link_libraries(cadly_preferences_dialog_test
    PRIVATE Cadly::Ui Cadly::AppSettings Cadly::Warnings Qt6::Test qlementine-icons
  )
  target_include_directories(cadly_preferences_dialog_test PRIVATE ${CMAKE_SOURCE_DIR}/src/ui/src)
  add_test(NAME cadly_preferences_dialog COMMAND cadly_preferences_dialog_test)
  set_tests_properties(cadly_preferences_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
