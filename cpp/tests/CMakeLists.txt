# cpp/tests/CMakeLists.txt — Unit tests for MaxPane v2.0 (ADR-018 / T1.1).
#
# Built when MAXPANE_BUILD_TESTS=ON (default). Catch2 v2.13.10 single-header
# vendored under tests/catch2/. Tests target pure-logic translation units
# (SplitTree, NodeSnapshot serialization, WorkspaceManager static helpers,
# ResolveActionCommand) — no SWELL window calls, no REAPER plugin entry.
#
# CTest entry: `ctest` from cpp/build/ runs the suite. Direct invocation:
# `./tests/maxpane_tests` for verbose Catch2 output.

add_executable(maxpane_tests
  test_main.cpp
  test_stubs.cpp
  test_split_tree.cpp
  test_globals.cpp
  test_config.cpp
  test_action_list.cpp
  test_state_persistence.cpp
  test_workspace_manager.cpp
  ${PROJECT_SOURCE_DIR}/src/action_list.cpp
  ${PROJECT_SOURCE_DIR}/src/split_tree.cpp
  ${PROJECT_SOURCE_DIR}/src/globals.cpp
  ${PROJECT_SOURCE_DIR}/src/config.cpp
  ${PROJECT_SOURCE_DIR}/src/workspace_manager.cpp
)

target_include_directories(maxpane_tests PRIVATE
  ${REAPER_SDK_DIR}
  ${WDL_DIR}
  ${WDL_DIR}/swell
  ${PROJECT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(maxpane_tests PRIVATE
  $<$<NOT:$<PLATFORM_ID:Windows>>:SWELL_PROVIDED_BY_APP>
  $<$<PLATFORM_ID:Windows>:NOMINMAX>
  MAXPANE_TEST_BUILD
)

if(MSVC)
  target_compile_options(maxpane_tests PRIVATE /W3)
else()
  target_compile_options(maxpane_tests PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
    # Catch2 v2 single-header triggers many -Wshadow / -Wconversion warnings
    # that we cannot fix without forking catch2. Suppress for the test binary
    # only — the production target keeps the strict flags.
    -Wno-shadow -Wno-conversion -Wno-sign-conversion
  )
endif()

if(APPLE)
  # config.cpp pulls Cocoa transitively through swell_cocoa_helpers.h declarations.
  # We provide a stub for IsSystemDarkMode() in test_stubs.cpp instead of linking
  # the Cocoa .mm file, so no -framework AppKit needed.
  target_link_libraries(maxpane_tests PRIVATE
    "-framework CoreFoundation"
  )
endif()

include(CTest)
add_test(NAME maxpane_unit_tests COMMAND maxpane_tests)
