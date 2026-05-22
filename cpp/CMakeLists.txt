cmake_minimum_required(VERSION 3.15)

if(APPLE)
  project(reaper_maxpane VERSION 2.0.0 LANGUAGES CXX OBJCXX)
else()
  project(reaper_maxpane VERSION 2.0.0 LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


# SDK path: supports both direct (cpp/sdk/reaper_plugin.h) and cloned (cpp/sdk/sdk/reaper_plugin.h)
if(EXISTS "${CMAKE_SOURCE_DIR}/sdk/sdk/reaper_plugin.h")
  set(REAPER_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk/sdk")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/sdk/reaper_plugin.h")
  set(REAPER_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk")
else()
  message(FATAL_ERROR "REAPER SDK not found. Clone into cpp/sdk/: git clone https://github.com/justinfrankel/reaper-sdk.git cpp/sdk")
endif()
set(WDL_DIR "${CMAKE_SOURCE_DIR}/WDL/WDL")

add_library(reaper_maxpane SHARED
  src/main.cpp
  src/globals.cpp
  src/config.cpp
  src/container.cpp
  src/container_paint.cpp
  src/container_input.cpp
  src/container_state.cpp
  src/container_nav.cpp
  src/split_tree.cpp
  src/window_manager.cpp
  src/capture_queue.cpp
  src/favorites_manager.cpp
  src/workspace_manager.cpp
  src/context_menu.cpp
  src/project_state.cpp
  src/instance_manager.cpp
  src/launcher.cpp
  src/save_workspace_dialog.cpp
  src/settings_dialog.cpp
  src/quick_switcher.cpp
  src/nav_bar.cpp
  src/drag_dock.cpp
)

# SWELL dialog template generation (Sprint 3.3 / ADR-020).
# On macOS/Linux, swell_resgen.pl (Perl, preinstalled on these platforms +
# GitHub Actions runners) converts .rc → .rc_mac_dlg/.rc_mac_menu, which
# the .cpp files include at TU scope. On Windows the .rc compiles directly
# via MSVC's rc.exe; no Perl step needed.
if(NOT WIN32)
  find_package(Perl REQUIRED)
  set(SWELL_RESGEN ${WDL_DIR}/swell/swell_resgen.pl)

  # List every .rc that needs SWELL processing. Adding a new dialog =
  # one entry here + the .cpp that #includes the generated .rc_mac_dlg.
  set(MAXPANE_RC_FILES
    ${CMAKE_SOURCE_DIR}/resources/save_workspace_dialog.rc
    ${CMAKE_SOURCE_DIR}/resources/settings_dialog.rc
    ${CMAKE_SOURCE_DIR}/resources/quick_switcher.rc
  )

  set(MAXPANE_RC_GENERATED)
  foreach(rc ${MAXPANE_RC_FILES})
    set(gen_dlg "${rc}_mac_dlg")
    set(gen_menu "${rc}_mac_menu")
    add_custom_command(
      OUTPUT ${gen_dlg} ${gen_menu}
      COMMAND ${PERL_EXECUTABLE} ${SWELL_RESGEN} ${rc}
      DEPENDS ${rc} ${SWELL_RESGEN}
      COMMENT "swell_resgen ${rc}"
      VERBATIM
    )
    list(APPEND MAXPANE_RC_GENERATED ${gen_dlg} ${gen_menu})
  endforeach()

  # Add generated files as sources so CMake tracks them as build dependencies.
  # They're #include'd from .cpp; CMake never compiles them directly.
  set_source_files_properties(${MAXPANE_RC_GENERATED} PROPERTIES
    HEADER_FILE_ONLY TRUE
  )
  target_sources(reaper_maxpane PRIVATE ${MAXPANE_RC_GENERATED})
elseif(WIN32)
  target_sources(reaper_maxpane PRIVATE
    ${CMAKE_SOURCE_DIR}/resources/save_workspace_dialog.rc
    ${CMAKE_SOURCE_DIR}/resources/settings_dialog.rc
  )
endif()

target_include_directories(reaper_maxpane PRIVATE
  ${REAPER_SDK_DIR}
  ${WDL_DIR}
  ${WDL_DIR}/swell
  src
  resources
)

target_compile_definitions(reaper_maxpane PRIVATE
  $<$<NOT:$<PLATFORM_ID:Windows>>:SWELL_PROVIDED_BY_APP>
  $<$<CONFIG:Debug>:MAXPANE_DEBUG>
  $<$<PLATFORM_ID:Windows>:NOMINMAX>
)

if(MSVC)
  target_compile_options(reaper_maxpane PRIVATE /W3)
else()
  target_compile_options(reaper_maxpane PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
    -Wshadow
    -Wconversion -Wno-sign-conversion
  )
endif()

if(APPLE)
  set_target_properties(reaper_maxpane PROPERTIES
    SUFFIX ".dylib"
    PREFIX ""
    OUTPUT_NAME "reaper_maxpane"
  )
  target_link_libraries(reaper_maxpane PRIVATE
    "-framework AppKit"
    "-framework CoreFoundation"
  )
  # Use the official macOS SWELL modstub (Objective-C++)
  # It loads SWELL functions via [NSApp delegate] swellGetAPPAPIFunc
  target_sources(reaper_maxpane PRIVATE
    ${WDL_DIR}/swell/swell-modstub.mm
    src/swell_cocoa_helpers.mm
  )
elseif(WIN32)
  set_target_properties(reaper_maxpane PROPERTIES
    SUFFIX ".dll"
    PREFIX ""
    OUTPUT_NAME "reaper_maxpane"
  )
  target_compile_definitions(reaper_maxpane PRIVATE
    WIN32_LEAN_AND_MEAN
  )
elseif(UNIX)
  set_target_properties(reaper_maxpane PROPERTIES
    SUFFIX ".so"
    PREFIX ""
    OUTPUT_NAME "reaper_maxpane"
  )
  # Linux uses SWELL via custom modstub
  target_sources(reaper_maxpane PRIVATE
    src/swell_modstub.cpp
  )
  target_link_libraries(reaper_maxpane PRIVATE
    dl
    pthread
  )
endif()

# Unit tests (ADR-018 / T1.1). Default ON for local dev + CI.
option(MAXPANE_BUILD_TESTS "Build MaxPane unit tests" ON)
if(MAXPANE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
