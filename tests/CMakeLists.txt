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

if(CADLY_BUILD_GUI)
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
endif()
