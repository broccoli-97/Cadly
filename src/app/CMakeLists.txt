add_library(cadly_app_settings STATIC
  src/Settings.cpp
  include/cadly/app/Settings.h
)
add_library(Cadly::AppSettings ALIAS cadly_app_settings)
target_include_directories(cadly_app_settings PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(cadly_app_settings
  PUBLIC Cadly::Input Qt6::Core
  PRIVATE Cadly::Warnings
)

add_executable(cadly
  src/main.cpp
  src/RecentFiles.cpp
  src/Theme.cpp
  include/cadly/app/RecentFiles.h
  include/cadly/app/Theme.h
)

target_include_directories(cadly
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(cadly PRIVATE cxx_std_17)

target_link_libraries(cadly
  PRIVATE
    Cadly::Ui
    Cadly::AppSettings
    Cadly::Platform
    Cadly::Warnings
    Qt6::Widgets
    qlementine-icons
)

# The full qlementine QStyle is only built on Qt >= 6.8 (see the root
# CMakeLists); Theme.cpp compiles its Fusion Dark/Light fallback when the
# define is absent.
if(CADLY_HAS_QLEMENTINE)
  target_link_libraries(cadly PRIVATE qlementine)
  target_compile_definitions(cadly PRIVATE CADLY_HAS_QLEMENTINE)
endif()

set_target_properties(cadly PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
)

# Bundle identity + STEP/IGES document types (LaunchServices open-with /
# double-click). The template consumes the MACOSX_BUNDLE_* variables below.
if(APPLE)
  set(MACOSX_BUNDLE_BUNDLE_NAME "Cadly")
  # Deliberately the *development* identifier: on a dev machine the build
  # tree's bundle would otherwise share an id with the installed/staged app
  # and shadow its Launch Services registration (file-type handler, icon).
  # packaging/macos/package-app.sh rewrites this to the release id
  # (io.github.broccoli-97.cadly) while assembling the distributable, so
  # neither builders nor releasers ever pass a flag.
  set(MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.broccoli-97.cadly.dev")
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
  set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
  # cadly.icns is committed, generated from cadly-icon.svg by
  # packaging/macos/make-icns.sh (rerun it after editing the SVG).
  set(MACOSX_BUNDLE_ICON_FILE "cadly.icns")
  target_sources(cadly PRIVATE
    ${CMAKE_SOURCE_DIR}/packaging/macos/cadly.icns)
  set_source_files_properties(${CMAKE_SOURCE_DIR}/packaging/macos/cadly.icns
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(cadly PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in
  )
endif()

# Translations. The .ts catalogs live in <root>/translations and are compiled
# by lrelease into .qm files embedded under the ":/i18n" resource prefix
# (qt_add_translations' default), where main.cpp's QTranslator looks for
# them. Most tr() strings live in the cadly_ui static library, not this
# target, so lupdate must be pointed at both modules' sources explicitly —
# the glob only feeds the dev-time "update_translations" target, never the
# build itself, so a stale glob can't break a build. Refresh catalogs with:
#   cmake --build --preset linux-release --target update_translations
if(Qt6LinguistTools_FOUND)
  file(GLOB_RECURSE cadly_i18n_sources
    ${CMAKE_SOURCE_DIR}/src/ui/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/src/*.h
    ${CMAKE_SOURCE_DIR}/src/ui/include/*.h
    ${CMAKE_SOURCE_DIR}/src/app/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/app/include/*.h
    ${CMAKE_SOURCE_DIR}/src/input_qt/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/input_qt/include/*.h
  )
  qt_add_translations(cadly
    TS_FILES ${CMAKE_SOURCE_DIR}/translations/cadly_zh_CN.ts
    SOURCES ${cadly_i18n_sources}
  )
endif()

# On macOS the target is a .app bundle (MACOSX_BUNDLE above), which installs
# via the BUNDLE rule; RUNTIME still covers the plain executable elsewhere.
install(TARGETS cadly
  RUNTIME DESTINATION ${CADLY_INSTALL_BINDIR}
  BUNDLE  DESTINATION ${CADLY_INSTALL_BINDIR}
)
