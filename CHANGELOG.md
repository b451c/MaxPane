# Changelog

All notable changes to MaxPane will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [2.0.3] - 2026-05-26

**Polish hot-fix on top of v2.0.2. Sharpens the nav-bar icons on
Windows, surfaces version / license / support info in the Settings
dialog, and lets you check for new versions without leaving REAPER.**

### Added

- **About section in the Settings dialog.** Now shows the version
  number, MIT license, project URL, and the three support links
  (Ko-fi, Buy Me a Coffee, PayPal) inside the plugin UI — previously
  the support links were buried in the nav-bar Support popup and
  there was no version display anywhere.
- **"Check for updates" button** in the new About section. Hits the
  project's ReaPack manifest on github.com and shows a modal if a
  newer version is out (with a one-click jump to the Releases page).
  Manual-only for now — synchronous HTTPS GET briefly blocks the UI
  for ~1-3 s while the request completes. Automatic check-on-startup
  is planned for a follow-up release.

### Fixed

#### Windows
- **Nav-bar icons no longer render pixelated.** Win32 GDI's
  `StretchDIBits` default mode (`COLORONCOLOR`) drops pixels on
  downscale; now switches to `HALFTONE` + `SetBrushOrgEx` before the
  blit and restores the previous mode after. Bilinear-equivalent
  smoothing; same look as macOS Quartz and Linux LICE.

#### macOS
- **Nav-bar icons get high-quality interpolation.** `BlitBGRABitmapMacOS`
  now sets `kCGInterpolationHigh` before `CGContextDrawImage` —
  matches the Win32 HALFTONE smoothing for visual parity across all
  three platforms.

---

## [2.0.2] - 2026-05-26

**Adds Linux x86_64 + aarch64 binaries to the public Release. Fixes
the three issues Linux smoke surfaced (tofu nav-bar icons, empty
modal dialogs, ReaImGui / Lua scripts not restoring from workspace).
Unifies the nav-bar icon set across all three platforms — macOS and
Windows users will see a one-off icon refresh.**

### Added

- **Linux x86_64 and Linux aarch64 binaries** in the public Release.
  Closes the Linux gap left by v2.0.1; the FX Browser close crash
  (issue [#9](https://github.com/b451c/MaxPane/issues/9)) reproduces
  no longer on REAPER 7.69 (covered by an upstream WDL fix). Linux
  parity sprint validated capture / restore / workspaces / context
  menus on Ubuntu 24.04 aarch64.
- **Unified Phosphor icon set** in the navigation bar across macOS /
  Windows / Linux. Replaces the per-platform Unicode glyphs (which
  rendered as tofu on Linux Noto Sans). Crisp at 20 px, uniform
  hover/active tinting, no per-glyph font-size tuning.

### Fixed

#### Linux native (parity sprint)
- **Nav-bar icons no longer render as tofu (□).** SWELL on Linux
  resolves an empty `CreateFont` family to fontconfig's default
  (Noto Sans Regular on Ubuntu 24.04), which lacks the Unicode
  miscellaneous-symbol codepoints the toolbar used. Bitmap path
  via Phosphor Icons (MIT) embedded as alpha masks.
- **Settings / Save Workspace / Quick Switcher dialogs open with
  content.** REAPER's bundled SWELL doesn't substitute the
  `SWELL_DEF_DLGSCALE2` default the `_BEGIN2` macro emits, so
  every child control sized to zero. All three dialog TUs now use
  the explicit-scale `_BEGIN` form with a local
  `SWELL_DLG_WS_DEFAULT_SCALING` shim (the upstream macro
  references a symbol that's only defined under
  `SWELL_TARGET_OSX`).

#### Cross-platform
- **ReaImGui / Lua scripts whose runtime window title differs from
  the `Script: <filename>.lua` action name now save with the
  correct action and restore from workspace.** The token-scoring
  threshold for the toggle pool stays at ≥ 2 hits, but a separate
  script-pool track allows 1 hit, and a single-active-script
  fallback covers the "title shares zero tokens" case (e.g.
  `lyrics.lua` window titled "MIDI Lyrics"). Workspaces saved
  with `arb:0:<name>` from older builds still need a one-time
  re-capture to populate the action.

#### Windows (Entry 15 follow-up)
- **`FindWindowEnumProc` finds Direct2D plugins with empty
  `WindowText`.** v2.0.1 closed the save side of Entry 15 but
  missed the search side — when `GetWindowText` returned empty
  on Win32, the enum proc hard-skipped, so workspace-load couldn't
  locate the just-opened ReaBeat / Reamix window for capture.
  Now falls back to `TryGetAppNameFromModule` when the title is
  empty (Win32 only; SWELL macOS / Linux keep the v2.0 hard-skip).

### Known issues

- Linux: HiDPI scaling of the nav-bar icons not yet wired to
  REAPER's advisory scaling factor. The 2× icon source is
  selected when the target rect exceeds 38 px logical, which
  looks correct at 1× DPI; HiDPI testing pending.
- macOS / Windows users upgrading from v2.0.1 will see a one-off
  icon refresh in the nav bar (Phosphor pictograms replace the
  Unicode glyphs). Visual only — all functionality unchanged.

---

## [2.0.1] - 2026-05-25

**Adds Windows x64 to the public build matrix and fixes a class of
workspace-restore bugs that affected captured C++ plugins (ReaBeat,
Reamix) and ReaImGui scripts on every platform.**

### Added

- **Windows x64 binary** is now part of the public Release. Closes
  [#8](https://github.com/b451c/MaxPane/issues/8) (Windows mouse input
  not reaching the container) — see _Windows native_ below for the
  list of Win32-specific fixes.
- **Current workspace name in the navigation bar.** The center of the
  nav bar now shows the loaded workspace's name with a `•` dirty
  indicator when the layout has been changed since the last save.
  Hover to see the full name in a tooltip when ellipsis-truncated.
- **Async workspace Save.** "Save Workspace" is now non-blocking — the
  click frame returns immediately with a "Saving '<name>'…" toast and
  the ExtState writes flush on the next REAPER tick (becomes "Saved
  '<name>'" / "Replaced '<name>'" when done). Up to 2 queued saves
  collapse; quit-time flush prevents data loss. Eliminates the 100+ ms
  freeze on click for workspaces with many captures.
- **Plugin window action discovery.** Captures of third-party C++
  plugins and ReaImGui Lua scripts now find and persist the toggle/
  show action automatically, so saved workspaces reopen those windows
  on load — previously they saved without an action and silently
  failed to restore. Cross-platform; uses `kbd_getTextFromCmd`
  (REAPER 6.71+) for action-name scoring.

### Fixed

#### Windows native (B10 / issue #8)
- **Mouse buttons reach `DlgProc`.** `WM_NCHITTEST` now publishes its
  result via `SetWindowLongPtr(DWLP_MSGRESULT, …)` and returns `TRUE`
  per the Win32 `DialogProc` contract — previously `return HTCLIENT`
  was read as `TRUE`-handled with `DWLP_MSGRESULT` defaulting to 0
  (`HTNOWHERE`), so every `WM_LBUTTONDOWN` / `WM_RBUTTONDOWN` /
  `WM_CONTEXTMENU` got routed past us.
- **Captures stop freezing REAPER.** `DoCapture` no longer leaves
  windows with both `WS_POPUP` and `WS_CHILD` set simultaneously
  during reparent (`NtUserSetParent` parked the GUI thread).
- **Captured plugins lose their 3D frame.** `GWL_EXSTYLE` strip
  removes `WS_EX_WINDOWEDGE` / `WS_EX_CLIENTEDGE` / `WS_EX_STATICEDGE`
  / `WS_EX_DLGMODALFRAME` so dialog-class plugins (ReaBeat) sit
  flush in the pane.
- **Save / Settings / Quick Switcher dialogs open.** Native Win32
  `DialogBoxParam` now uses the plugin DLL's `HINSTANCE` (captured
  in `ReaperPluginEntry`) — previously `nullptr` resolved to the
  host EXE which doesn't carry MaxPane's resources, and the call
  returned `-1` silently.
- **Detach to floating works.** Native Win32 implementations of the
  Cocoa-helper API (`ApplyFloatingWindowChrome`, `IsSystemDarkMode`
  via the `AppsUseLightTheme` registry value, `OpenUrlPlatform`
  via `ShellExecuteA`, `ClampRectToVisibleScreen` via
  `MonitorFromRect`, …) — previously all `{}` stubs.
- **Capture-by-click no longer self-triggers** on the menu / button
  click that entered capture mode (rising-edge LMB detection) and
  walks parent chains correctly for **docked plugins** (returns
  the shallowest plugin-DLL-owned HWND instead of the dock frame's
  inner container).
- **Open Windows menu** stops listing foreign-process windows
  (Firefox, Program Manager) and REAPER internal UI children, while
  also surfacing empty-title Direct2D plugins via owning-DLL lookup.
- **GDI captures stop smearing on resize** (`WS_CLIPCHILDREN |
  WS_CLIPSIBLINGS` in the dialog template); Direct2D captures
  stop stacking pixel residue (`SWP_NOCOPYBITS` on arbitrary tabs).
- **`AUTOCHECKBOX` + ASCII em-dashes** in Settings / Quick Switcher
  `.rc` files (native `rc.exe` doesn't auto-toggle plain `CHECKBOX`;
  UTF-8 em-dash rendered as Latin-1 mojibake).
- **Debug log** lands in `%TEMP%\maxpane_debug.log` instead of the
  POSIX `/tmp` path that doesn't exist on Windows.

#### Cross-platform
- **ReaImGui crash on workspace switch fixed.** Scripts (action state
  `-1`) now get a `Main_OnCommand` close-fire before MaxPane reparents
  their window — previously the external `SetParent` left ReaImGui's
  internal `Docker` pointer stale and the next heartbeat crashed in
  `Docker::moveTo` with a null-deref.
- **Workspace restore for captured plugins / scripts.** Cross-platform
  action discovery (title-token scoring against
  `kbd_getTextFromCmd`) now finds the show/hide action for plugins
  with titled windows (ReaBeat, Reamix) and for Lua scripts with
  `RS…`-prefixed named commands (state `-1`). Saved workspaces that
  carry these captures actually reopen them on load.
- **Stale list across workspaces.** Every saved workspace's tab list
  contributes its action IDs to `stale_toggle_actions` so windows
  released by a workspace SWITCH (not still in any active pane)
  survive REAPER restart cleanly instead of returning as floating
  ghosts. State `-1` cleanup now sends `WM_CLOSE` + hide instead of
  silently skipping.
- **Auto-open survives Windows quit.** `was_visible` now persists
  the intent flag (`m_visible`) instead of `IsWindowVisible()` —
  the Win32 parent-chain teardown at quit hid the still-open
  container and overwrote `was_visible=0`.
- **Workspace save round-trips named commands** (`_RS…`). Both
  `GetActionCommandString` (adds the leading `_`) and
  `ResolveActionCommand` (retries with one prepended for backward
  compat) close the SDK contract gap.
- Home-overlay workspace card tooltips now appear (the timer was
  only armed in fresh launcher mode).
- `DoRelease` flips `WS_CHILD` → `WS_POPUP` after `SetParent(nullptr)`
  per MSDN, so released windows land as proper top-level instead of
  silent `WS_CHILD`-of-desktop.

### Known issues

- Linux remains pending — open blocker
  [#9](https://github.com/b451c/MaxPane/issues/9) (FX Browser close
  crash). Linux build is still produced as a workflow artifact for
  internal testing; not yet in the public Release.
- Old workspaces saved before v2.0.1 that carry plugin captures with
  no action info (`arb:0:<name>`) won't auto-restore. Re-save the
  workspace once with the new build to populate the action ID.

---

## [2.0.0] - 2026-05-22

v2.0 reworks the docking core for reliability, adds multi-instance /
floating containers / drag-to-dock / Quick Switcher, and surfaces the
things that used to be hidden in the right-click menu through a
persistent navigation bar.

> **macOS only for v2.0.0.** Windows and Linux builds will be added in
> a follow-up release after they pass smoke testing on those platforms
> — see [#8](https://github.com/b451c/MaxPane/issues/8) (Windows mouse
> input) and [#9](https://github.com/b451c/MaxPane/issues/9) (Linux FX
> Browser close) for the open blockers.

For an architectural overview of the codebase as it stands at v2.0, read
[ARCHITECTURE.md](ARCHITECTURE.md). For the locked v2.0 scope, decision
log, and per-bug investigation notes, the project keeps `docs/v2/V2_SCOPE.md`,
`docs/v2/V2_DECISIONS.md`, and `docs/v2/V2_PROGRESS.md` locally (gitignored
process docs).

### Added

#### Multi-instance (F2)
- **Up to 8 MaxPane containers** in one REAPER session, each with its own
  layout, captured windows, floating geometry, and project-state chunk.
  Instance 0 keeps the legacy ExtState / dock / RPP identifiers verbatim
  — upgraders from v1.5.x see no migration. Instances 1–7 use suffixed
  identifiers (`MaxPane_cpp_N`, `MaxPane_container_N`,
  `<MAXPANE_STATE_N>`).
- **Per-instance Open actions** — `MaxPane: Open Container` (legacy,
  instance 0), `MaxPane: Open Container 2` through `MaxPane: Open
  Container 8` (instances 1–7). Existing v1.5.x keybinds on the legacy
  action continue to work.
- **Workspaces shared, current state per-instance** — the workspace list
  (saved layouts) is a single user-level pool visible from every
  container; what a container is *currently showing* is its own per-
  instance state.

#### Floating mode (F1a)
- **"Detach to Floating" / "Re-dock"** in the pane menu turns the whole
  container into a top-level OS window with native chrome (title bar,
  close, resize). Geometry persists across sessions and float/dock
  transitions.
- **Always-on-top toggle for floating containers** (C5) — keeps the
  detached container above other apps when checked. Per-instance,
  persisted to ExtState.
- Multi-monitor clamping on macOS via `[NSScreen screens]`; centered-on-
  REAPER-main fallback on Win/Linux.

#### Quick Switcher (F4)
- **`MaxPane: Quick Switcher`** action opens a modal SWELL dialog with
  a search box and a unified list of all open tabs (across all
  instances), all workspaces, and all favorites. Fuzzy filter matches
  case-insensitive subsequence + scores prefix and consecutive hits.
  Enter activates, Esc closes. Bind your own hotkey in REAPER's
  Actions dialog.

#### Hotkey slots (F6)
- **32 workspace slot actions** — `MaxPane: Workspace Slot 1` through
  `MaxPane: Workspace Slot 32`. Bind a hotkey, hit it, the
  corresponding workspace loads into the focused (last-clicked) MaxPane
  container.
- **32 favorite slot actions** — `MaxPane: Favorite Slot 1` through
  `MaxPane: Favorite Slot 32`. Bind a hotkey, hit it, the favorite is
  captured into the focused pane.
- **MAX_WORKSPACES bumped 10 → 32** to match the new slot count and
  close a long-standing community ask.

#### Navigation bar + Home overlay + drag-to-dock (ADR-026)
- **Persistent navigation toolbar** at the top of every container:
  `[Home] | [Drag] [Switch] [Save] [Load▾] | [Settings] [Support]`.
  Buttons for the workspace actions that used to live only in the
  right-click menu, plus Settings and Support links. Toggle via
  Settings → "Show navigation bar". Dark/light palette follows the
  rest of the UI.
- **Home overlay** — click `[Home]` to overlay the launcher card grid
  on top of the current layout (non-destructive picker). Click a card
  to load that workspace; click empty space or press Esc to close
  without changing anything.
- **Drag-to-dock** — click `[Drag]`, then grab any REAPER window from
  outside MaxPane and drop it on a pane. Live preview renders during
  the drag showing the four edge zones (split left / right / top /
  bottom), the tab-bar zone (add as tab), and the body-center
  forgiving zone. Shift+drop replaces the active tab. Polling-based,
  no platform hooks — works identically on macOS / Win / Linux SWELL.

#### Mocna piątka — C-series UX wins (ADR-027)
- **C1 Reopen last closed tab** — `MaxPane: Reopen last closed tab`
  action restores the most recently closed tab. 16-entry per-container
  ring buffer, session-scoped (not persisted).
- **C2 Pinned tabs** — right-click a tab → Pin. Pinned tabs sort to
  the left of each pane, render with a bullet (•) prefix, and are
  exempt from "Close Others" / "Close to Right" / "Close All".
  Persisted in workspace saves.
- **C3 Close family for tabs** — right-click a tab → Close ▸ →
  "Close Others", "Close to Right", "Close All". Bulk close that
  respects pinned tabs.
- **C4 Workspace pickup** — single `MaxPane: Workspace pickup`
  action prompts for a slot number (1..32) and loads it. One hotkey
  reaches every workspace slot instead of 32 separate bindings.
- **C5 Always-on-top floating** — see Floating mode above.

#### Settings dialog (ADR-019)
- **Dedicated modal Settings dialog** reachable from the nav bar
  `[Settings]` button or right-click → Settings. Single-page layout
  with four sections: General (auto-open, show nav bar, default
  workspace), Appearance (dark-mode tristate cycle: Auto / Force
  dark / Force light), Hotkeys (link to REAPER Actions), Advanced
  (open actions, reset, version + support links).

#### Other UX
- **Custom Save Workspace dialog** — name input + clickable listbox
  of existing workspaces + dynamic status label ("Will replace
  existing workspace 'X'" / "Will save as new workspace") replaces
  the bare `GetUserInputs` textbox. Toast confirms the result.
- **Toast bar** — bottom-of-container 3-second feedback strip for
  non-fatal events (rename collision, duplicate at capacity,
  workspace saved, etc.). Fades out in the final 500 ms.
- **Workspace launcher hero** (ADR-014) — when a container is empty
  it renders a card grid of saved workspaces with mini layout
  previews. Click a card to load. Right-click for Load / Rename /
  Duplicate / Delete / Bind Hotkey. Footer carries support links.
- **Capture menu flattened** (ADR-020 / ADR-030) — the 15 known
  REAPER windows are reachable directly from the right-click menu's
  "Capture window ▸" submenu (one level), with no extra nesting on
  the most common path.
- **Accelerator hook** (ADR-029) — MaxPane action bindings now fire
  even when MaxPane has focus (or a captured pane has focus). v1.x
  required REAPER's main window to have focus first.

#### Build + test infrastructure (ADR-018 / ADR-023)
- **CI matrix** — `.github/workflows/ci.yml` builds and tests on
  macOS arm64, macOS x86_64 (cross-compile + Rosetta), Ubuntu, and
  Windows for every push to `v2`/`main` and every PR.
- **Unit tests** — Catch2 v2.13.10 vendored; 14 tests cover
  `SplitTree`, `WorkspaceManager` static serialization helpers,
  `safe_strncpy`, `ResolveActionCommand`. Run with
  `ctest --output-on-failure` from `cpp/build/`.
- **`ARCHITECTURE.md`** — full technical onboarding document for
  new contributors.
- **Perl-driven SWELL resgen step** added to CMake (ADR-023) for
  cross-platform `.rc` → `.rc_mac_dlg` conversion.

### Changed

- **Container teardown reworked** (ADR-011, B16/B17) — `WM_CLOSE`
  is now the primary close mechanism; the legacy `Main_OnCommand`
  toggle is a fallback for windows that ignore `WM_CLOSE`. The
  `stale_toggle_actions` ExtState list is merged across paths
  (workspace switch, mid-session close, atexit) so the next
  REAPER startup can close any ghost windows REAPER restored from
  its `wnd_vis` cache.
- **Startup ghost cleanup polls aggressively** (ADR-015) — every
  tick for the first ~75 ticks instead of a single-shot fire,
  cutting the visible flash from ~250 ms to ~30 ms on macOS
  restart.
- **Workspace launcher replaces empty-container UI** (ADR-013) —
  MaxPane opens to the workspace card grid instead of auto-
  restoring captured windows. `was_visible`-driven auto-restore
  was the source of many race conditions and didn't match REAPER's
  native docker behavior.
- **Workspace list is shared across instances; current state is
  per-instance** (ADR-012, supersedes part of ADR-009).
- **Settings dialog supersedes context-menu Auto-open toggle**
  (ADR-019). The legacy menu item handler is preserved so v1.5.x
  keybinds continue to work.
- **`MaxPaneIsDarkMode()` introduced** with cache invalidation
  so the Settings dialog's tristate dark-mode override takes
  effect live without restarting REAPER.

### Fixed

- **B1 — SetParent return value never checked.** Added
  `VerifySetParent` helper at all four call sites in
  `DoCapture` / `DoRelease` so post-reparent mismatches surface in
  the debug log instead of corrupting state silently.
- **B2 — `CheckAlive` doesn't verify parent.** Tabs whose captured
  HWND was externally reparented (e.g. REAPER moved the MIDI Editor
  to another docker on project tab switch) are now reclaimed via
  `DoCapture` or cleanly removed.
- **B3 — MIDI Editor recapture race.** Capturability guard between
  `FindReaperWindow` and `DoCapture` prevents acting on an HWND
  REAPER reparented or closed in the interim.
- **B4 — WM_DESTROY doesn't reparent captured children.** Container
  destruction triggered by REAPER (docker close / undock) now
  reparents captured children before tearing down so they don't
  become frameless ghosts.
- **B6 — Toolbar subclass cleanup.** `UnsubclassToolbar` fires on
  every `DoRelease` branch, including the B2 reclaim-fail branch.
- **B7 — Stale cleanup synchronous fallback** for windows REAPER
  hasn't yet opened from its `wnd_vis` cache.
- **B9 — Narrow tab close-button overlap.** New `TAB_CLOSE_MIN_WIDTH`
  threshold (90 px) hides the × when it would clip the tab name
  ellipsis. Layout-clamp `TAB_MIN_WIDTH` (60 px) unchanged.
- **B12 — Capture queue arbitrary unconditional toggle.** Arbitrary
  captures (toolbars, ReaImGui scripts) now respect an
  `alreadyOpen` guard before firing the toggle, mirroring the
  known-window path. Without this, loading a workspace whose
  scripts were already open closed them and burned 200 retries.
- **B13 — `DoRelease` reads toggle state AFTER `SetParent(nullptr)`.**
  SWELL races the `wnd_vis` reset to 0 even when the window is
  still visible. State is now sampled BEFORE the reparent.
- **B14 — `ShowWindow(SW_HIDE)` doesn't reliably orderOut**
  top-level NSWindows that SWELL recreated via
  `SetParent(nullptr)`. New `ForceHideWindow` helper calls
  `[NSView setHidden:YES] + [NSWindow orderOut:nil]`.
- **B15 — Startup-level stale cleanup.** Ghost windows REAPER
  restored from cached `wnd_vis` are now closed at REAPER startup
  even when MaxPane doesn't auto-open.
- **B16 — `WM_CLOSE` as primary close mechanism.** Replaces the
  unreliable `Main_OnCommand` toggle for windows that don't update
  `wnd_vis` after our reparent (Media Explorer, Actions, FX
  Browser, Undo History).
- **B17 — Toggle stale list merges, doesn't overwrite.** Workspace-
  switch stale entries are no longer dropped by a subsequent
  mid-session close.
- **B18 — Arbitrary capture with action=0 + empty cmd spammed
  retries.** Legacy workspace tabs without a resolvable action now
  probe once via `FindReaperWindow`; if not found, log SKIP and
  return. Killed a 10-second UI freeze + 414-line log spam per
  load.
- **B19 — `ForceHideWindow` hides REAPER main window.** Now only
  calls `orderOut` on NSWindows whose contentView equals the
  captured NSView (i.e. orphan NSWindows we own).
- **B20 — REAPER docker tab placeholder after capture.**
  `g_DockWindowRemove(target)` is called before the intermediate
  `SetParent` in `DoCapture` so REAPER's docker manager forgets
  the captured window.
- **B21 — Save Workspace leaves leftover keys in ExtState.**
  Slot reuse with a smaller layout pre-clears all
  `pane_N_tab_M_*` keys before writing valid panes; phantom panes
  from prior saves no longer resurrect.
- **B22 — Mixer prefix-match catches "Mixer Master".** Special-
  case in `FindWindowEnumProc`: when searching for `Mixer`,
  require exact title match.
- **B23 — Capture-by-click floating: NSView frame not synced.**
  `SetWindowPos(SWP_FRAMECHANGED|…)` after reparent (replacing
  `SWP_NOMOVE|SWP_NOSIZE`) so the view occupies the actual pane
  rect instead of its pre-capture screen coords.
- **B25 — Workspace list per-instance was wrong UX** (ADR-012):
  list is now shared across instances; only current state stays
  per-instance.
- **B27 — Frameless ReaImGui plugins on re-fire.** Capturing a
  third-party plugin (ReaBeat, reamix.me, ReaMD), closing it, and
  re-firing the plugin's action no longer produces a frameless
  window at the default position. New chrome-restore in
  `DoRelease` for `toggleAction == 0` captures: SetParent(null) +
  `ApplyFloatingWindowChrome` + `SetWindowPos` to the original
  rect before WM_CLOSE.
- **Home overlay right-click bug** (ADR-031) — right-click on a
  card now opens the card menu instead of leaking to the pane menu.

### Known issues

- **B8 — Frameless toolbar button misalignment** (issue
  [#6 comment thread](https://github.com/b451c/MaxPane/issues/6))
  persists for the "Frameless floating toolbar windows = ON"
  preference. Needs REAPER-side experimentation; deferred to a
  post-v2.0 patch.
- **Windows + Linux not in this release.** Builds compile on all 5 CI
  platforms but Windows mouse input
  ([#8](https://github.com/b451c/MaxPane/issues/8)) and the Linux FX
  Browser close path ([#9](https://github.com/b451c/MaxPane/issues/9))
  are open blockers. Binaries for those platforms will be added in a
  follow-up release after they're tested and the blockers cleared.
- **Linux symbol-font tofu in the nav bar** (Linux only). GTK default
  font lacks several of the Unicode glyphs used by MaxPane. Will be
  resolved alongside the Windows/Linux release follow-up (likely via
  bundled PNG icons).
- **B24 — Toolbars open offscreen after capture+release**. REAPER
  toolbar manager state inconsistency after our
  `DockWindowRemove`. v2.1 candidate.

---

## [1.5.2] - 2026-03-04

### Fixed
- **Crash when capturing Docker or MaxPane itself** — Added ancestor guard in `DoCapture()` that rejects any window in MaxPane's parent chain, preventing circular reparenting crash. Docker and MaxPane are also filtered from the Open Windows menu. ([#2](https://github.com/b451c/MaxPane/issues/2), [#4](https://github.com/b451c/MaxPane/issues/4))
- **MIDI Editor survives project tab switches** — New `dynamicTitle` mechanism stores a stable search prefix ("MIDI take:") instead of the exact window title. `CheckAlive` automatically recaptures the MIDI Editor when REAPER opens a new one after project switch or restart. ([#3](https://github.com/b451c/MaxPane/issues/3))
- **`was_visible` not cleared on close** — Added `was_visible=0` in `WM_DESTROY` handler as safety net for all window destruction paths. "Close Container" menu now calls `Toggle()` instead of `Shutdown()` directly. ([#5](https://github.com/b451c/MaxPane/issues/5))
- **Visual glitches** — Hide close button on narrow tabs (< 60px); splitter hover timer now uses full hit testing; hover state cleared when context menu closes; adaptive text color (black on bright tab colors like yellow/cyan). ([#7](https://github.com/b451c/MaxPane/issues/7))
- **Toolbar rendering and docking in MaxPane** — Toolbars can now be properly captured and displayed. Removed toolbar filter from `FindWindowEnumProc`. ([#6](https://github.com/b451c/MaxPane/issues/6))
- **Workspace switching: stale windows floating after restart** — Windows from previous workspaces (toolbars, Actions, FX Browser, ReaImGui scripts) no longer appear as floating windows after switching workspaces and restarting REAPER. State-specific cleanup: double-toggle for state=0 windows, ShowWindow fallback for stubborn HWNDs, skip-toggle for script actions (state=-1). ([#6](https://github.com/b451c/MaxPane/issues/6))
- **Toolbar icons disappearing on background click** — Subclassed captured toolbar windows to block REAPER's internal drag-to-undock behavior. Clicking and holding on toolbar background no longer causes icons to vanish. ([#6](https://github.com/b451c/MaxPane/issues/6))
- **Auto-detect toggle action** — `LookupToggleAction` now auto-detects the REAPER toggle action ID for any window title (toolbars + known windows), ensuring proper state management for windows captured via Open Windows menu.

---

## [1.5.1] - 2026-03-03

### Fixed
- **macOS dark mode support** — Pane background color now adapts to macOS dark/light appearance. Reparented windows (Track Manager, Routing Matrix, etc.) no longer show a light grey background when REAPER's dark mode is enabled. ([#1](https://github.com/b451c/MaxPane/issues/1))

---

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
