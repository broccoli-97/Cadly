add_library(cadly_ui STATIC
  src/CameraController.cpp
  src/ImportDiagnosticsDock.cpp
  src/ImportOptionsDialog.cpp
  src/MainWindow.cpp
  src/ModelTreeDock.cpp
  src/PropertiesDock.cpp
  src/ViewportWidget.cpp

  include/cadly/ui/CameraController.h
  include/cadly/ui/ImportDiagnosticsDock.h
  include/cadly/ui/ImportOptionsDialog.h
  include/cadly/ui/MainWindow.h
  include/cadly/ui/ModelTreeDock.h
  include/cadly/ui/PropertiesDock.h
  include/cadly/ui/ViewportWidget.h
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
    Cadly::Scene
    Cadly::Renderer
    Cadly::RendererGL
    Cadly::Cad
  PRIVATE
    Cadly::Platform
    Cadly::Warnings
)
