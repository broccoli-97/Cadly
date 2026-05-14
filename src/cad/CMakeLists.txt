# cad module: OCCT-backed CAD import + tessellation. This is the only target
# that links against OCCT toolkits — keep it that way per the architecture.

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
  TKMesh
  TKBRep TKShHealing TKTopAlgo TKGeomAlgo TKBO TKBool TKPrim
  TKGeomBase TKG3d TKG2d TKMath TKernel
)
set(occt_libs "")
foreach(lib IN LISTS _occt_libs)
  # Newer OCCT installs (>= 7.7) namespace targets under OpenCASCADE::; pick
  # whichever the active find_package exposes.
  if(TARGET ${lib})
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
)
add_library(Cadly::Cad ALIAS cadly_cad)

target_include_directories(cadly_cad
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(cadly_cad PUBLIC cxx_std_17)

target_link_libraries(cadly_cad
  PUBLIC
    Cadly::Scene
  PRIVATE
    Cadly::Platform
    Cadly::Warnings
    ${occt_libs}
)

# OCCT's modular TK* imported targets on Debian don't carry the include
# directory as an INTERFACE_INCLUDE_DIRECTORIES property — fold it in here so
# downstream files can `#include <TDF_Label.hxx>`.
if(OpenCASCADE_INCLUDE_DIR)
  target_include_directories(cadly_cad PRIVATE ${OpenCASCADE_INCLUDE_DIR})
endif()

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
