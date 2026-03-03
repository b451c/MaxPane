cmake_minimum_required(VERSION 3.15)
project(reaper_maxpane VERSION 1.3.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(REAPER_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk")
set(WDL_DIR "${CMAKE_SOURCE_DIR}/WDL/WDL")

add_library(reaper_maxpane SHARED
  src/main.cpp
  src/globals.cpp
  src/config.cpp
  src/container.cpp
  src/container_paint.cpp
  src/container_input.cpp
  src/container_state.cpp
  src/split_tree.cpp
  src/window_manager.cpp
  src/capture_queue.cpp
  src/favorites_manager.cpp
  src/workspace_manager.cpp
  src/context_menu.cpp
  src/project_state.cpp
)

target_include_directories(reaper_maxpane PRIVATE
  ${REAPER_SDK_DIR}
  ${WDL_DIR}
  ${WDL_DIR}/swell
  src
)

target_compile_definitions(reaper_maxpane PRIVATE
  SWELL_PROVIDED_BY_APP
  $<$<CONFIG:Debug>:MAXPANE_DEBUG>
  $<$<PLATFORM_ID:Windows>:NOMINMAX>
)

target_compile_options(reaper_maxpane PRIVATE
  -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
  -Wshadow
  -Wconversion -Wno-sign-conversion
)

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
endif()
