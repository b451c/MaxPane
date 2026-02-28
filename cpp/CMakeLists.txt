cmake_minimum_required(VERSION 3.15)
project(reaper_redockit VERSION 1.0.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(REAPER_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk")
set(WDL_DIR "${CMAKE_SOURCE_DIR}/WDL/WDL")

add_library(reaper_redockit SHARED
  src/main.cpp
  src/globals.cpp
  src/container.cpp
  src/split_tree.cpp
  src/window_manager.cpp
  src/capture_queue.cpp
  src/favorites_manager.cpp
  src/workspace_manager.cpp
  src/context_menu.cpp
)

target_include_directories(reaper_redockit PRIVATE
  ${REAPER_SDK_DIR}
  ${WDL_DIR}
  ${WDL_DIR}/swell
  src
)

target_compile_definitions(reaper_redockit PRIVATE
  SWELL_PROVIDED_BY_APP
  $<$<CONFIG:Debug>:REDOCKIT_DEBUG>
)

target_compile_options(reaper_redockit PRIVATE
  -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
  -Wshadow
  -Wconversion -Wno-sign-conversion
)

if(APPLE)
  set_target_properties(reaper_redockit PROPERTIES
    SUFFIX ".dylib"
    PREFIX ""
    OUTPUT_NAME "reaper_redockit"
  )
  target_link_libraries(reaper_redockit PRIVATE
    "-framework AppKit"
    "-framework CoreFoundation"
  )
  # Use the official macOS SWELL modstub (Objective-C++)
  # It loads SWELL functions via [NSApp delegate] swellGetAPPAPIFunc
  target_sources(reaper_redockit PRIVATE
    ${WDL_DIR}/swell/swell-modstub.mm
  )
endif()
