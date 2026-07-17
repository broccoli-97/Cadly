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
endif()
