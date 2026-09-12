add_library(cadly_camera_controller STATIC
  src/CameraController.cpp
  include/cadly/ui/CameraController.h
)
add_library(Cadly::CameraController ALIAS cadly_camera_controller)
target_include_directories(cadly_camera_controller PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(cadly_camera_controller
  PUBLIC Cadly::Input Cadly::Scene Qt6::Core
  PRIVATE Cadly::Warnings
)

add_library(cadly_ui STATIC
  src/DiagnosticsStrip.cpp
  src/IconUtils.cpp
  src/ImportOptionsWidget.cpp
  src/InspectorWidget.cpp
  src/PreferencesDialog.cpp
  src/PreferencesDialog.h
  src/MainWindow.cpp
  src/Popover.cpp
  src/PropertiesPanel.cpp
  src/SegmentedControl.cpp
  src/SidebarWidget.cpp
  src/ThemeTokens.cpp
  src/ToolTip.cpp
  src/ToolbarButton.cpp
  src/ToolbarWidget.cpp
  src/ViewportWidget.cpp
  src/ViewsGrid.cpp

  include/cadly/ui/MainWindow.h
  include/cadly/ui/ThemeTokens.h
  include/cadly/ui/ViewportWidget.h

  src/DiagnosticsStrip.h
  src/IconUtils.h
  src/ImportOptionsWidget.h
  src/InspectorWidget.h
  src/Popover.h
  src/PropertiesPanel.h
  src/SegmentedControl.h
  src/SidebarWidget.h
  src/ToolTip.h
  src/ToolbarButton.h
  src/ToolbarWidget.h
  src/ViewsGrid.h
)
add_library(Cadly::Ui ALIAS cadly_ui)

target_include_directories(cadly_ui
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(cadly_ui PUBLIC cxx_std_17)

target_link_libraries(cadly_ui
  PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    Cadly::InputQt
    Cadly::CameraController
    Cadly::Scene
    Cadly::Renderer
    Cadly::RendererGL
    Cadly::Cad
  PRIVATE
    Cadly::Platform
    Cadly::Warnings
)
