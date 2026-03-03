# Changelog

All notable changes to MaxPane will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [1.5.0] - 2026-03-03

### Added
- **Solo/maximize pane** — Temporarily expand any pane to fill the entire container. Toggle via context menu (Solo Pane / Exit Solo). Full tree snapshot is saved and restored when exiting solo mode.
- **Tab reorder within pane** — Drag tabs left/right within the same pane to rearrange their order. Visual insertion indicator shows the drop position.
- **Splitter double-click reset** — Double-click any splitter bar to reset its ratio to 50/50.
- **Keyboard shortcuts via REAPER actions** — Five new actions registered in REAPER's Actions dialog, bindable to any key:
  - MaxPane: Next Tab
  - MaxPane: Previous Tab
  - MaxPane: Next Pane
  - MaxPane: Previous Pane
  - MaxPane: Solo Toggle

### Changed
- **TabEntry owned storage** — All string fields (`name`, `searchTitle`, `actionCmd`) are now owned `char[]` arrays instead of `const char*` pointers. Eliminates dangling pointer risk after tab moves/copies and removes the `FixTabPointers()` workaround.
- **ShiftTabsLeft helper** — Extracted repeated tab-shift-and-clear pattern into a single function, used by CloseTab, MoveTab, and CheckAlive.
- **InvalidateRect(…, FALSE)** everywhere — Semantically correct now that `WM_ERASEBKGND` is handled; avoids unnecessary erase flicker.

### Fixed
- **Open Windows menu validation** — Added `IsWindow()` check before using HWND from the open windows list, preventing crashes from stale window handles.

### Note
- Windows and Linux builds are included but have **not been tested**. Please report any issues at [GitHub Issues](https://github.com/b451c/MaxPane/issues).

---

## [1.4.0] - 2026-03-03

### Added
- **Region Render Matrix** added to known windows (action 41888).
- **Cross-platform support** — Windows x64 and Linux x86_64 builds now compile and are included in releases alongside macOS.
- `platform.h` — Central platform abstraction header replacing 5 scattered `#ifdef _WIN32` blocks.
- `CreateMaxPaneDialog()` — Portable dialog creation helper (native `CreateDialogIndirectParam` on Windows, `SWELL_CreateDialog` on macOS/Linux).

### Fixed
- **Windows compilation** — bridged `GWLP_USERDATA`/`SetWindowLongPtr` (Win64 names) and replaced SWELL-only `SWELL_CreateDialog` with portable helper.
- **Linux compilation** — suppressed SWELL `min`/`max` macro conflict with STL via `WDL_NO_DEFINE_MINMAX`.
- **Linker error on Windows/Linux** — `ForceViewLayoutAndDisplay()` now has an inline no-op for non-macOS platforms (Cocoa code is macOS-only).

### Changed
- Known windows reorganized into logical groups (Mixing & Routing, Browsing & Media, Regions, Editing, Monitoring, Instruments).
- Removed `SWELL_PROVIDED_BY_APP` from CMake global definitions — now set per-platform in `platform.h`.
- Removed CMake `get_target_property`/`list(REMOVE_ITEM)` hack for Windows.

### Removed
- **MIDI Editor** removed from known windows — cannot be toggled without an active MIDI item in the session.

---

## [1.3.0] - 2026-03-03

### Changed
- **Renamed project**: ReDockIt → **MaxPane** — all source, docs, CMake, actions, ExtState keys, RPP chunk tags updated.

### Added
- **Diagonal grid lines** in empty panes — subtle 45° lines on the pane background, disappear when a window is captured.

### Fixed
- **Pane background color** — captured windows (WS_CHILD) have no own background and inherited the parent's dark color. Fixed by setting `COLOR_PANE_BG = RGB(172,172,172)` as a neutral light gray.
- **DoRelease toggle for Actions window** — closing the Actions tab via [x] left REAPER's toggle state on (checkmark stayed). Root cause: SWELL destroys the NSWindow when a window becomes WS_CHILD, so `g_Main_OnCommand` couldn't find the window and opened a new one. Fix: `SetParent(nullptr)` to restore the NSWindow before toggling.
- **Hint text color** — "Click header to assign a window" was white on light gray after the background color fix. Changed to `RGB(80,80,80)`.

---

## [1.2.0] - 2026-03-02

### Added
- **Pane menu button (▼)** — 16 px button at the right edge of every tab bar. Left-click opens the pane context menu (same as right-click). Hit-test returns `-2`; hover highlights the button.
- `CalcTabBarLayout` shared helper — single source of truth for tab geometry used by `DrawTabBar`, `TabHitTest`, and `IsOnTabCloseButton`.
- `GetTabRect` helper — returns the screen rect for a given tab index; used for targeted invalidation on color change.
- `ExpandRect` static helper (both `container.cpp` and `container_input.cpp`) — in-place RECT union, skips empty src/dst to prevent dirty-rect corruption.

### Changed
- **Targeted `InvalidateRect`** — hover/drag operations now invalidate only the affected rect instead of the full window:
  - Splitter hover: union of old + new splitter rect
  - Tab/menu-button hover: union of old + new item rect via `GetTabRect` / inline button rect
  - Drag highlight change: union of old + new highlight pane rect
  - Drag cancel/end: source tab bar + old highlight pane rect
  - Tab color change: only that tab's rect via `GetTabRect`
  - Timer hover timeout: cached old hover rects only
- Tab area now reserves `PANE_MENU_BTN_WIDTH` (16 px) on the right for the menu button; tabs shrink accordingly.

### Fixed
- **`TabHitTest` x-bounds regression** — the menu-button check (`x >= paneRect.right - 16`) was firing for clicks *outside* the pane's x range (e.g. in the adjacent right-side pane), causing the pane context menu to appear instead of tab interactions. Fix: check `x < paneRect.left || x >= paneRect.right` before the menu-button test.
- `ExpandRect` now guards against empty `src` rect (`{0,0,0,0}`) in both call-sites, preventing the dirty rect from expanding into origin unnecessarily.

### Removed
- Scroll arrows (`<` / `>`) for tab overflow — tabs shrink when a pane is narrow (original behaviour). `m_tabScrollOffset`, `TAB_SCROLL_ARROW_WIDTH`, `TAB_OVERFLOW_THRESHOLD` removed.

---

## [1.1.0] - 2026-03-02

### Added
- Splitter hover highlights (white bar on mouseover)
- Tab hover highlights (lighten effect on mouseover)
- Per-project state persistence via RPP files (`project_config_extension_t`)
- `StateAccessor` abstraction for polymorphic state I/O (global, project, RPP)

### Fixed
- Workspace switch rendering: blank/artifact windows (e.g. Routing Matrix) after switching back
- Frameless floating windows when closing tabs via [x]
- Windows reappearing floating after REAPER restart despite being closed
- Shutdown lifecycle: proper toggle state check, reparent-before-toggle sequence
- Quit interception via hookcommand for reliable state save on macOS/Windows
- RepositionAll repaint: `SWP_FRAMECHANGED` + `InvalidateRect` after pane resize

### Changed
- Simplified to global docker model (removed per-project visibility switching)
- RPP state I/O now uses synchronous `project_config_extension_t` (replaced deferred timer)
- `LoadWorkspace` uses `ReleaseAll(false)` for smoother workspace transitions
- `DoCapture` forces frame recalculation via `SWP_FRAMECHANGED`

## [1.0.1] - 2026-03-01

### Fixed
- **Startup deadlock**: REAPER could hang when loading a project with MaxPane docked. The capture queue now defers `Main_OnCommand` calls during `LoadState()` to avoid calling REAPER APIs while the project is still loading.

### Changed
- Replaced raw `new`/`delete` with `std::unique_ptr` for safer resource management
- Replaced all `strncpy` calls with `safe_strncpy` helper for consistent null-termination
- Extracted magic numbers (colors, geometry, timing) into named constants in `config.h`
- Merged duplicate `FindWindowEnumProc`/`FindChildWindowEnumProc` into single function
- Debug logging is now conditional on `CMAKE_BUILD_TYPE=Debug` (no longer always enabled)
- Added `-Wshadow` and `-Wconversion` compiler warnings

### Removed
- Dead code: unused `BuildLists()` and `IsAnyCaptured()` methods

## [1.0.0] - 2025-03-01

### Added
- Native C++ REAPER extension (no script dependencies)
- Binary split tree layout engine with up to 16 panes
- Tabbed window management with drag-and-drop between panes
- 14 known REAPER windows with one-click capture
- Arbitrary window capture (including ReaImGui scripts)
- Dock frame detection for ReaImGui windows
- Async capture queue with retry logic
- Named workspaces (save/restore complete layouts)
- Favorites system with persistent action command strings
- Tab color palette (8 colors)
- 5 built-in layout presets
- Auto-open on REAPER startup (configurable)
- Full state persistence via REAPER ExtState
- Dockable container (integrates with REAPER's docker system)
- Context menus for panes and tabs
- Capture-by-click mode
- Conditional debug logging (Debug builds only)
- Cross-platform architecture via WDL/SWELL
