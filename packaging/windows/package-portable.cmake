# Run with the MSYS2 UCRT64 cmake from the repository root:
# cmake -DBUILD_DIR=build/windows-msys2-release -DPACKAGE_DIR=package/cadly \
#   -P packaging/windows/package-portable.cmake
cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()

if(NOT WIN32 OR NOT DEFINED BUILD_DIR OR NOT DEFINED PACKAGE_DIR)
  message(FATAL_ERROR "Run on Windows with -DBUILD_DIR=... -DPACKAGE_DIR=...")
endif()
get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE)
get_filename_component(PACKAGE_DIR "${PACKAGE_DIR}" ABSOLUTE)
foreach(executable cadly cad_import_cli)
  if(NOT EXISTS "${BUILD_DIR}/bin/${executable}.exe")
    message(FATAL_ERROR "Release binary not found: ${BUILD_DIR}/bin/${executable}.exe")
  endif()
endforeach()

find_program(windeployqt NAMES windeployqt6 REQUIRED)
find_program(qtpaths NAMES qtpaths6 REQUIRED)
find_program(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND NAMES objdump REQUIRED)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
execute_process(COMMAND "${qtpaths}" --query QT_INSTALL_BINS
  OUTPUT_VARIABLE qt_bin_dir OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)

set(package_marker "${PACKAGE_DIR}/.cadly-windows-package")
file(GLOB existing_contents "${PACKAGE_DIR}/*")
if(PACKAGE_DIR STREQUAL repo_root OR PACKAGE_DIR STREQUAL BUILD_DIR OR
   (existing_contents AND NOT EXISTS "${package_marker}"))
  message(FATAL_ERROR "Refusing to replace unmarked non-empty directory: ${PACKAGE_DIR}")
endif()
file(REMOVE_RECURSE "${PACKAGE_DIR}")
file(MAKE_DIRECTORY "${PACKAGE_DIR}")
file(TOUCH "${package_marker}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --strip --prefix "${PACKAGE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)
# FetchContent dependencies also install headers and CMake metadata.
file(REMOVE_RECURSE "${PACKAGE_DIR}/include" "${PACKAGE_DIR}/lib")

# SVG icons are loaded at runtime; --svg makes their module/plugins explicit.
# CMake collects compiler and other non-Qt runtime dependencies below.
execute_process(COMMAND "${windeployqt}" --release --no-translations
  --no-compiler-runtime --svg --include-plugins qoffscreen
  "${PACKAGE_DIR}/bin/cadly.exe"
  COMMAND_ERROR_IS_FATAL ANY)
foreach(plugin platforms/qwindows.dll platforms/qoffscreen.dll
               imageformats/qsvg.dll iconengines/qsvgicon.dll)
  if(NOT EXISTS "${PACKAGE_DIR}/bin/${plugin}")
    message(FATAL_ERROR "Qt deployment is missing ${plugin}")
  endif()
endforeach()
# Override the Qt installation's baked-in paths for the portable layout.
file(WRITE "${PACKAGE_DIR}/bin/qt.conf" "[Paths]\nPrefix=.\nPlugins=.\n")

# Include plugins as roots: windeployqt does not collect the full non-Qt DLL
# closure of MSYS2 packages (OCCT, ICU, FreeType, GCC runtimes, etc.). Windows
# API sets and system DLLs remain supplied by the user's OS.
file(GLOB_RECURSE deployed_dlls "${PACKAGE_DIR}/bin/*.dll")
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${PACKAGE_DIR}/bin/cadly.exe" "${PACKAGE_DIR}/bin/cad_import_cli.exe"
  LIBRARIES ${deployed_dlls}
  DIRECTORIES "${PACKAGE_DIR}/bin" "${qt_bin_dir}"
  PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
  POST_EXCLUDE_REGEXES
    [[.*[\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\/].*]]
    [[.*[\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\/].*]]
  RESOLVED_DEPENDENCIES_VAR runtime_dlls
  UNRESOLVED_DEPENDENCIES_VAR missing_dlls)
if(missing_dlls)
  message(FATAL_ERROR "Unresolved runtime DLLs: ${missing_dlls}")
endif()
foreach(dll IN LISTS runtime_dlls)
  get_filename_component(name "${dll}" NAME)
  # Preserve Qt DLLs already deployed (and possibly patched) by windeployqt.
  if(NOT EXISTS "${PACKAGE_DIR}/bin/${name}")
    file(COPY "${dll}" DESTINATION "${PACKAGE_DIR}/bin")
  endif()
endforeach()

file(MAKE_DIRECTORY "${PACKAGE_DIR}/sample-files")
file(COPY "${repo_root}/test_files/as1-ug-214.stp" DESTINATION "${PACKAGE_DIR}/sample-files")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/README.txt" DESTINATION "${PACKAGE_DIR}")
message(STATUS "Portable Windows package assembled at ${PACKAGE_DIR}")
