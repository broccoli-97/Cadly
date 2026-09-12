# Optional, ABI-matched replacements for two Linux OCCT 7.6.3 toolkits.
# Build in the workspace; never modify the installed OCCT libraries.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT "${OpenCASCADE_MAJOR_VERSION}.${OpenCASCADE_MINOR_VERSION}.${OpenCASCADE_MAINTENANCE_VERSION}" VERSION_EQUAL "7.6.3")
  message(FATAL_ERROR "CADLY_OCCT_PERFORMANCE_PATCHES requires Linux with shared OCCT 7.6.3. Use OFF with other OCCT versions.")
endif()
if(NOT OpenCASCADE_BUILD_SHARED_LIBS)
  message(FATAL_ERROR "CADLY_OCCT_PERFORMANCE_PATCHES requires shared OCCT libraries.")
endif()

include(FetchContent)
find_program(CADLY_PATCH_EXECUTABLE patch REQUIRED)
FetchContent_Declare(cadly_occt763
  URL https://codeload.github.com/Open-Cascade-SAS/OCCT/tar.gz/refs/tags/V7_6_3
  URL_HASH SHA256=3f95808e2c5060c5b5001770b5e42c7d9a849b23d925272bc70a5a2377413aa9
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  SOURCE_SUBDIR cadly-unused
)
FetchContent_MakeAvailable(cadly_occt763)

set(_patched "${CMAKE_CURRENT_BINARY_DIR}/occt-patched")
set(_patch "${CMAKE_CURRENT_LIST_DIR}/../occt_patches/occt-7.6.3-import.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_patch}")
foreach(source src/StepFile/step.lex src/StepFile/lex.step.cxx src/Intf/Intf_InterferencePolygon2d.cxx)
  configure_file("${cadly_occt763_SOURCE_DIR}/${source}" "${_patched}/${source}" COPYONLY)
endforeach()
execute_process(
  COMMAND "${CADLY_PATCH_EXECUTABLE}" --batch --silent -p1 -i "${_patch}"
  WORKING_DIRECTORY "${_patched}"
  COMMAND_ERROR_IS_FATAL ANY
)

foreach(toolkit TKGeomAlgo TKXSBase)
  file(STRINGS "${cadly_occt763_SOURCE_DIR}/src/${toolkit}/PACKAGES" packages)
  set(sources "")
  set(includes "")
  foreach(package IN LISTS packages)
    file(STRINGS "${cadly_occt763_SOURCE_DIR}/src/${package}/FILES" package_sources REGEX "[.]cxx$")
    foreach(source IN LISTS package_sources)
      if(EXISTS "${_patched}/src/${package}/${source}")
        list(APPEND sources "${_patched}/src/${package}/${source}")
      else()
        list(APPEND sources "${cadly_occt763_SOURCE_DIR}/src/${package}/${source}")
      endif()
    endforeach()
    list(APPEND includes "${cadly_occt763_SOURCE_DIR}/src/${package}")
  endforeach()
  file(STRINGS "${cadly_occt763_SOURCE_DIR}/src/${toolkit}/EXTERNLIB" dependencies)
  add_library(cadly_occt_${toolkit} SHARED ${sources})
  target_include_directories(cadly_occt_${toolkit} PRIVATE ${includes})
  target_include_directories(cadly_occt_${toolkit} SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
  # Match the optimized system kernel even in a Cadly Debug build.
  target_compile_options(cadly_occt_${toolkit} PRIVATE -O2 -Wno-deprecated-declarations)
  target_compile_definitions(cadly_occt_${toolkit} PRIVATE NDEBUG OCC_CONVERT_SIGNALS)
  target_link_options(cadly_occt_${toolkit} PRIVATE -Wl,-Bsymbolic-functions)
  target_link_libraries(cadly_occt_${toolkit} PRIVATE ${dependencies})
  set_target_properties(cadly_occt_${toolkit} PROPERTIES
    AUTOMOC OFF AUTOUIC OFF
    OUTPUT_NAME ${toolkit} SOVERSION 7 VERSION 7.6.3
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/occt-lib"
    INSTALL_RPATH "$ORIGIN"
  )
  install(TARGETS cadly_occt_${toolkit} LIBRARY DESTINATION ${CADLY_INSTALL_LIBDIR})
endforeach()

# Imported higher-level toolkits reference these names too. Redirect those
# link dependencies so CMake uses one library of each SONAME and one rpath.
foreach(toolkit IN LISTS OpenCASCADE_LIBRARIES)
  if(TARGET ${toolkit})
    get_target_property(dependencies ${toolkit} INTERFACE_LINK_LIBRARIES)
    if(dependencies)
      list(TRANSFORM dependencies REPLACE "^TKGeomAlgo$" "cadly_occt_TKGeomAlgo")
      list(TRANSFORM dependencies REPLACE "^TKXSBase$" "cadly_occt_TKXSBase")
      set_target_properties(${toolkit} PROPERTIES INTERFACE_LINK_LIBRARIES "${dependencies}")
    endif()
  endif()
endforeach()
set_target_properties(cadly_occt_TKGeomAlgo PROPERTIES
  CADLY_OCCT_REFERENCE_SOURCE "${cadly_occt763_SOURCE_DIR}/src/Intf/Intf_InterferencePolygon2d.cxx")
install(FILES
  "${cadly_occt763_SOURCE_DIR}/LICENSE_LGPL_21.txt"
  "${cadly_occt763_SOURCE_DIR}/OCCT_LGPL_EXCEPTION.txt"
  "${_patch}"
  DESTINATION "${CADLY_INSTALL_DATADIR}/licenses/occt-7.6.3")
message(STATUS "Cadly OCCT performance patches: batch STEP input and indexed polygon intersection")

file(RELATIVE_PATH _cadly_lib_relative "/${CADLY_INSTALL_BINDIR}" "/${CADLY_INSTALL_LIBDIR}")
set(CMAKE_INSTALL_RPATH "$ORIGIN/${_cadly_lib_relative}")
set(CMAKE_INSTALL_RPATH "$ORIGIN/${_cadly_lib_relative}" PARENT_SCOPE)
