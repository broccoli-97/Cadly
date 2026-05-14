cmake_minimum_required(VERSION 3.24)

project(Cadly
  VERSION 0.1.0
  LANGUAGES CXX
  DESCRIPTION "Native C++ desktop CAD viewer"
)

# --- Global compile settings ----------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt auto-tooling. AUTORCC stays off — we ship resources explicitly.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# Default install layout (used by the packaging milestone).
include(GNUInstallDirs)
if(NOT CADLY_INSTALL_BINDIR)
  set(CADLY_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()
if(NOT CADLY_INSTALL_LIBDIR)
  set(CADLY_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()
if(NOT CADLY_INSTALL_DATADIR)
  set(CADLY_INSTALL_DATADIR ${CMAKE_INSTALL_DATAROOTDIR}/cadly)
endif()

# --- Options ---------------------------------------------------------------
option(CADLY_BUILD_TESTS         "Build CTest-based smoke tests" ON)
option(CADLY_BUILD_CLI           "Build cad_import_cli helper" ON)
option(CADLY_BUILD_GUI           "Build the Qt GUI application" ON)
option(CADLY_ENABLE_VULKAN       "Enable Vulkan backend (placeholder, off in MVP)" OFF)
option(CADLY_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors" OFF)

# --- Compiler warning baseline --------------------------------------------
add_library(cadly_warnings INTERFACE)
add_library(Cadly::Warnings ALIAS cadly_warnings)

if(MSVC)
  target_compile_options(cadly_warnings INTERFACE
    /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor
    /wd4127 # conditional expression is constant (Qt headers)
    /wd4459 # declaration hides global
  )
  if(CADLY_WARNINGS_AS_ERRORS)
    target_compile_options(cadly_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(cadly_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
    -Wno-deprecated-declarations  # OCCT 7.6 has many
  )
  if(CADLY_WARNINGS_AS_ERRORS)
    target_compile_options(cadly_warnings INTERFACE -Werror)
  endif()
endif()

# --- Third-party packages --------------------------------------------------
# Qt 6
if(CADLY_BUILD_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Concurrent)
endif()

# OCCT (system: libocct-*-dev provides OpenCASCADE CMake config files)
find_package(OpenCASCADE REQUIRED)

# glm — header-only math
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
  # Debian ships pkg-config but no glmConfig; fall back to a header search.
  find_path(GLM_INCLUDE_DIR glm/glm.hpp REQUIRED)
  add_library(glm::glm INTERFACE IMPORTED)
  set_target_properties(glm::glm PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${GLM_INCLUDE_DIR}")
endif()

# fmt / spdlog — diagnostics
find_package(fmt CONFIG QUIET)
if(NOT fmt_FOUND)
  find_package(fmt REQUIRED)
endif()
find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
  find_package(spdlog REQUIRED)
endif()

# OpenGL — Qt's OpenGLWidgets needs the host GL implementation.
find_package(OpenGL REQUIRED)

# --- Subdirectories --------------------------------------------------------
add_subdirectory(src/platform)
add_subdirectory(src/scene)
add_subdirectory(src/renderer)
add_subdirectory(src/renderer_gl)
add_subdirectory(src/cad)
if(CADLY_BUILD_GUI)
  add_subdirectory(src/ui)
  add_subdirectory(src/app)
endif()

if(CADLY_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Install shader assets so the runtime can find them without source tree access.
install(DIRECTORY shaders/glsl
  DESTINATION ${CADLY_INSTALL_DATADIR}/shaders)

message(STATUS "Cadly configured:")
message(STATUS "  Qt6:    ${Qt6_VERSION}")
message(STATUS "  OCCT:   ${OpenCASCADE_VERSION}")
message(STATUS "  GUI:    ${CADLY_BUILD_GUI}")
message(STATUS "  CLI:    ${CADLY_BUILD_CLI}")
message(STATUS "  Tests:  ${CADLY_BUILD_TESTS}")
