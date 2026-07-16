cmake_minimum_required(VERSION 3.15)

if(APPLE)
  project(reaper_maxpane VERSION 2.4.0 LANGUAGES CXX OBJCXX)
else()
  project(reaper_maxpane VERSION 2.4.0 LANGUAGES CXX)
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
  src/action_list.cpp
  src/container.cpp
  src/container_paint.cpp
  src/container_input.cpp
  src/container_state.cpp
  src/container_nav.cpp
  src/split_tree.cpp
  src/window_manager.cpp
  src/capture_queue.cpp
  src/fx_capture.cpp
  src/favorites_manager.cpp
  src/workspace_manager.cpp
  src/context_menu.cpp
  src/project_state.cpp
  src/screenset.cpp
  src/instance_manager.cpp
  src/launcher.cpp
  src/save_workspace_dialog.cpp
  src/settings_dialog.cpp
  src/quick_switcher.cpp
  src/nav_bar.cpp
  src/nav_icons.cpp
  resources/nav_icons.gen.cpp
  src/drag_dock.cpp
  src/overlay_frame.cpp
  src/updater.cpp
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
    ${CMAKE_SOURCE_DIR}/resources/quick_switcher.rc
  )
endif()

# Vendored headers (REAPER SDK + WDL/SWELL) are SYSTEM includes: not ours to
# fix, so they must sit below the MAXPANE_WERROR gate. On GCC/clang SYSTEM
# silences their warnings outright; on MSVC it maps to /external:I, which
# needs the /external:W0 pairing added with the warning flags below
# (ADR-060 session catch — /W4 /WX died inside reaper_plugin.h C4100s
# before reaching our code; the M2.6 gate had never run on MSVC).
target_include_directories(reaper_maxpane SYSTEM PRIVATE
  ${REAPER_SDK_DIR}
  ${WDL_DIR}
  ${WDL_DIR}/swell
)
target_include_directories(reaper_maxpane PRIVATE
  src
  resources
)

target_compile_definitions(reaper_maxpane PRIVATE
  $<$<NOT:$<PLATFORM_ID:Windows>>:SWELL_PROVIDED_BY_APP>
  $<$<CONFIG:Debug>:MAXPANE_DEBUG>
  # _CRT_SECURE_NO_WARNINGS: portable CRT calls (sscanf, fopen, ...) are
  # deliberate — C4996 would fail the /WX gate (updater.cpp sscanf).
  $<$<PLATFORM_ID:Windows>:NOMINMAX _CRT_SECURE_NO_WARNINGS>
)

# Audit M2.6 — CI warning gate. OFF by default so a stray new-toolchain
# warning can't brick a release/tag build; ci.yml turns it ON for every
# push/PR, which is where warning rot gets caught.
option(MAXPANE_WERROR "Treat compiler warnings as errors" OFF)

if(MSVC)
  # /wd4100 mirrors the GCC side's -Wno-unused-parameter. /external:W0
  # pairs with the SYSTEM include dirs above (CMake emits /external:I for
  # them on MSVC) so vendored SDK/WDL headers stay below the gate.
  target_compile_options(reaper_maxpane PRIVATE
    $<IF:$<BOOL:${MAXPANE_WERROR}>,/W4 /WX /wd4100 /external:W0,/W3>)
else()
  target_compile_options(reaper_maxpane PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
    -Wshadow
    -Wconversion -Wno-sign-conversion
    $<$<BOOL:${MAXPANE_WERROR}>:-Werror>
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
  # Vendored WDL source — not ours to fix; exclude from the warning gate
  # (it trips -Wsign-compare and AppKit deprecation notes under -Werror).
  set_source_files_properties(${WDL_DIR}/swell/swell-modstub.mm
    PROPERTIES COMPILE_OPTIONS "-w")
  # swell_cocoa_helpers.mm includes the vendored swell-internal.h, whose
  # NSOpenGLContext fields are deprecated since macOS 10.14 — keep the note
  # visible but below the -Werror gate for this one TU.
  set_source_files_properties(src/swell_cocoa_helpers.mm
    PROPERTIES COMPILE_OPTIONS "-Wno-error=deprecated-declarations")
elseif(WIN32)
  set_target_properties(reaper_maxpane PROPERTIES
    SUFFIX ".dll"
    PREFIX ""
    OUTPUT_NAME "reaper_maxpane"
  )
  target_compile_definitions(reaper_maxpane PRIVATE
    WIN32_LEAN_AND_MEAN
  )
  # Sprint 1 Entry 12 — Psapi for EnumProcessModules / GetModuleInformation
  # used by the plugin module-DLL display-name resolver.
  # v2.0.3 — winhttp for the synchronous update check (updater.cpp).
  target_link_libraries(reaper_maxpane PRIVATE Psapi Winhttp)
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
