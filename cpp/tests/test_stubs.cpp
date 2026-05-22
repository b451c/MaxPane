// Test-side stubs for symbols that the unit-test binary links against the
// real source files for (split_tree.cpp, config.cpp, globals.cpp) but whose
// implementations live in Cocoa/SWELL/REAPER-bound translation units that we
// deliberately do NOT link into tests.
//
// Keep this file minimal — if you find yourself adding REAPER API mocks here,
// reach for the StateAccessor abstraction instead so the production code
// stays decoupled from globals.

#include "platform.h"
#include "window_manager.h"

#ifdef __APPLE__
// config.cpp's IsDarkMode() helper calls IsSystemDarkMode() (declared in
// swell_cocoa_helpers.h, defined in the .mm file we don't link). Tests run
// without a display server, so always report "not dark".
bool IsSystemDarkMode() { return false; }
#endif

// workspace_manager.cpp references WindowManager::GetPaneState from Save()
// and WritePaneTabsStatic(). Our unit tests cover only the tree-nodes
// static helpers (no WindowManager involvement) — provide a minimal stub
// so the linker is satisfied. Returning nullptr makes any accidental call
// path immediately observable instead of silently corrupting state.
const PaneState* WindowManager::GetPaneState(int) const { return nullptr; }
