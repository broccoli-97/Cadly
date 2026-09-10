# cad module: OCCT-backed CAD import + tessellation. This is the only target
# that links against OCCT toolkits — keep it that way per the architecture.

set(_cadly_occt_perf_default OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OpenCASCADE_BUILD_SHARED_LIBS AND
   "${OpenCASCADE_MAJOR_VERSION}.${OpenCASCADE_MINOR_VERSION}.${OpenCASCADE_MAINTENANCE_VERSION}" VERSION_EQUAL "7.6.3")
  set(_cadly_occt_perf_default ON)
endif()
option(CADLY_OCCT_PERFORMANCE_PATCHES "Build patched Linux OCCT 7.6.3 import toolkits locally" ${_cadly_occt_perf_default})
if(CADLY_OCCT_PERFORMANCE_PATCHES)
  include(cmake/OcctPerformance.cmake)
endif()

# OCCT toolkit list, ordered from highest-level to lowest. The find_package
# call exposes individual TK* targets; we link a curated subset, not the
# omnibus OpenCASCADE::* alias, so unused toolkits don't get pulled into the
# downstream binary.
set(_occt_libs
  TKDESTEP TKDEIGES                       # OCCT 7.7+ split names
  TKXDESTEP TKXDEIGES                     # OCCT 7.6 XDE-aware STEP/IGES readers
  TKSTEP TKSTEPAttr TKSTEPBase TKSTEP209  # OCCT 7.6 step toolkits
  TKSTEPCAF
  TKIGES
  TKXSBase TKXCAF TKLCAF TKCAF TKCDF
  TKV3d                                    # UV/surface-aware mesh normals
  TKMesh
  TKBRep TKShHealing TKTopAlgo TKGeomAlgo TKBO TKBool TKPrim
  TKGeomBase TKG3d TKG2d TKMath TKernel
)
set(occt_libs "")
foreach(lib IN LISTS _occt_libs)
  # Newer OCCT installs (>= 7.7) namespace targets under OpenCASCADE::; pick
  # whichever the active find_package exposes.
  if(TARGET cadly_occt_${lib})
    list(APPEND occt_libs cadly_occt_${lib})
  elseif(TARGET ${lib})
    list(APPEND occt_libs ${lib})
  elseif(TARGET OpenCASCADE::${lib})
    list(APPEND occt_libs OpenCASCADE::${lib})
  endif()
endforeach()
if(NOT occt_libs)
  message(FATAL_ERROR
    "find_package(OpenCASCADE) succeeded but none of the expected TK* targets "
    "were found. Check that libocct-data-exchange-dev (or the upstream OCCT "
    "install) is fully present.")
endif()

add_library(cadly_cad STATIC
  src/ImporterRegistry.cpp
  src/OcctIgesImporter.cpp
  src/OcctShapeToMesh.cpp
  src/OcctStepImporter.cpp
  src/OcctStepTransfer.cpp
  src/TessellationPolicy.cpp
  src/XcafSession.cpp
)
add_library(Cadly::Cad ALIAS cadly_cad)

target_include_directories(cadly_cad
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(cadly_cad PUBLIC cxx_std_17)

if(MINGW)
  # OCCT headers use M_PI/M_PI_2, which MinGW hides in strict C++17 mode.
  # Define this before any consumer includes <cmath>, including CAD tests.
  target_compile_definitions(cadly_cad PUBLIC _USE_MATH_DEFINES)
endif()

target_link_libraries(cadly_cad
  PUBLIC
    Cadly::Scene
  PRIVATE
    Cadly::Platform
    Cadly::Warnings
    ${occt_libs}
)

if(CADLY_OCCT_PERFORMANCE_PATCHES)
  # TKGeomAlgo is otherwise only an indirect dependency. Keep both patched
  # SONAMEs in the executable so its RUNPATH selects them without preload or
  # a machine-wide library replacement.
  target_link_options(cadly_cad INTERFACE -Wl,--no-as-needed)
endif()

# OCCT's modular TK* imported targets on Debian don't carry the include
# directory as an INTERFACE_INCLUDE_DIRECTORIES property — fold it in here so
# downstream files can `#include <TDF_Label.hxx>`.
if(OpenCASCADE_INCLUDE_DIR)
  target_include_directories(cadly_cad SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
endif()

find_package(Threads REQUIRED)
target_link_libraries(cadly_cad PRIVATE Threads::Threads)

# Optional CLI used to validate import without launching the GUI. Reads a
# STEP/IGES file from argv[1] and prints geometry stats.
if(CADLY_BUILD_CLI)
  add_executable(cad_import_cli src/cad_import_cli.cpp)
  target_link_libraries(cad_import_cli
    PRIVATE
      Cadly::Cad
      Cadly::Platform
      Cadly::Warnings
  )
  set_target_properties(cad_import_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  install(TARGETS cad_import_cli RUNTIME DESTINATION ${CADLY_INSTALL_BINDIR})
endif()
