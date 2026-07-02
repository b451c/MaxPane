# MaxPane — Architecture

> Living technical reference for the MaxPane v2.0 codebase. Audience: OSS
> contributors and future maintainers landing on the project cold. Pairs with
> [README.md](README.md) (user-facing) and [CONTRIBUTING.md](CONTRIBUTING.md)
> (workflow). For frozen scope and decision history see `docs/v2/V2_SCOPE.md`
> and `docs/v2/V2_DECISIONS.md` (local-only; gitignored).

---

## 1. What MaxPane is, in one paragraph

MaxPane is a native REAPER extension that turns the host application's own
top-level windows (Mixer, FX Browser, MIDI Editor, toolbars, ReaImGui scripts,
…) into a tiled multi-pane workspace. The mechanism is OS-level window
reparenting via `SetParent` (Win32) / SWELL's macOS+Linux shim — REAPER
windows literally become children of MaxPane's container HWND, painted inside
a binary-tree layout with tabbed leaves, splitter bars, and a persistent
navigation toolbar. No drawing engine, no scripting layer: the captured
windows are still the genuine REAPER UI, just hosted somewhere else.

That single trick — `SetParent` plus a layout engine — is the entire
foundation. Everything else (workspaces, favorites, multi-instance, floating
mode, drag-to-dock, Quick Switcher) is built on top of it.

---

## 2. High-level system view

```
┌─────────────────────────────────────────────────────────────────┐
│                         REAPER process                          │
│                                                                 │
│   ┌──────────────┐   ┌──────────────┐   ┌────────────────────┐  │
│   │  Mixer HWND  │   │ FX Brws HWND │   │  Toolbar 1 HWND    │  │
│   └──────┬───────┘   └──────┬───────┘   └─────────┬──────────┘  │
│          │ SetParent        │ SetParent           │             │
│          ▼                  ▼                     ▼             │
│   ┌──────────────────────────────────────────────────────────┐  │
│   │            MaxPane container (HWND, DlgProc)             │  │
│   │  ┌────────────────────────────────────────────────────┐  │  │
│   │  │     Nav bar  [Home][Drag][Switch][Save][Load▾]     │  │  │
│   │  ├──────────────────────┬─────────────────────────────┤  │  │
│   │  │                      │                             │  │  │
│   │  │  pane 0  ▾           │   pane 1  ▾                 │  │  │
│   │  │  [Mixer*][FXBrws]    │   [Toolbar 1*]              │  │  │
│   │  │  ┌─────────────────┐ │   ┌───────────────────────┐ │  │  │
│   │  │  │   Mixer window  │ │   │    Toolbar window     │ │  │  │
│   │  │  │   (reparented)  │ │   │    (reparented)       │ │  │  │
│   │  │  └─────────────────┘ │   └───────────────────────┘ │  │  │
│   │  └──────────────────────┴─────────────────────────────┘  │  │
│   └─────────────┬────────────────────────────────────────────┘  │
│                 │                                               │
│                 ▼ DockWindowAddEx                               │
│   ┌──────────────────────────────────────────────────────────┐  │
│   │              REAPER native docker tab                    │  │
│   └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

Three independent layers sit on top of each other:

1. **OS / WDL+SWELL.** Window APIs. On Win32 these are native; on macOS and
   Linux they come from WDL's SWELL library — `swell.h` provides `HWND`,
   `SetParent`, `EnumWindows`, `SWELL_CreateDialog`, etc., implemented over
   Cocoa (macOS) and GTK (Linux). MaxPane treats SWELL and Win32 as one API
   surface via `cpp/src/platform.h`.
2. **REAPER plugin contract.** A single exported `ReaperPluginEntry` (see
   `cpp/src/main.cpp`) wires the extension into REAPER: imports SDK function
   pointers, registers actions and a `hookcommand` callback, registers a
   `project_config_extension_t` for per-project state, registers an
   `accelerator_register_t` for keyboard routing, and installs an `atexit`
   hook. REAPER calls back through those registrations for the rest of the
   session.
3. **MaxPane runtime.** Up to 8 `MaxPaneContainer` instances coordinated by an
   `InstanceManager` singleton. Each container owns its own `SplitTree`,
   `WindowManager`, `CaptureQueue`, plus shared-but-overridable `FavoritesManager`
   and `WorkspaceManager`.

---

## 3. Module map

All paths are relative to `cpp/src/`. Files marked `(.mm)` are Objective-C++
(macOS only); `_modstub.cpp` is Linux-only.

### Bootstrap and global state

| File | Role |
|------|------|
| `main.cpp` | `ReaperPluginEntry`. Imports REAPER API function pointers, registers all REAPER actions (`MaxPane_OpenContainer_*`, `MaxPane_NextTab`, `MaxPane_WsSlot_1..32`, `MaxPane_FavSlot_1..32`, `MaxPane_QuickSwitcher`, `MaxPane_ReopenTab`, `MaxPane_WsPickup`, …). `hookCommandProc` dispatches actions to the right container via `InstanceManager`. Installs `atexit` and `accelerator_register_t`. Owns the startup polling timer that processes ghost cleanup. |
| `globals.h/cpp` | REAPER SDK function-pointer storage (`g_GetExtState`, `g_Main_OnCommand`, …), `g_reaperMainHwnd`, helpers (`safe_strncpy`, `clamp_i`, `clamp_f`, `ResolveActionCommand`). |
| `platform.h` | `#ifdef _WIN32` switch between native `<windows.h>` and SWELL. Provides `CreateMaxPaneDialog` cross-platform wrapper and `GWLP_USERDATA → GWL_USERDATA` aliases for SWELL. |
| `config.h/cpp` | Compile-time constants (`MAX_PANES`, `MAX_TABS_PER_PANE`, `MAX_WORKSPACES = 32`, `MAX_FAVORITES = 32`, `MAX_INSTANCES = 8`, colors, geometry, timing). `KNOWN_WINDOWS` array (the 15 one-click capture targets + their REAPER toggle action IDs). `MaxPaneIsDarkMode()` + cache invalidation. |
| `debug.h` | `DBG(...)` macro — writes to `/tmp/maxpane_debug.log` in Debug builds; no-op in Release. |
| `swell_cocoa_helpers.h/.mm` | Cross-platform window helpers: `ForceViewLayoutAndDisplay`, `ForceHideWindow`, `ApplyFloatingWindowChrome`, `ClampRectToVisibleScreen`, `OpenUrlPlatform`, dark-mode detection, capture-by-click safety probes + crosshair/highlight overlay (ADR-048). macOS implementations live in the `.mm`; Win32 and Linux get inline native implementations in the header. The remaining per-platform stubs are documented in-header as registered gaps — see §15. |
| `swell_modstub.cpp` | Linux-only SWELL loader stub (macOS uses `swell-modstub.mm` from WDL). |

### Layout engine

| File | Role |
|------|------|
| `split_tree.h/cpp` | Binary tree of `SplitNode`s. Each `NODE_BRANCH` has an orientation (horizontal / vertical), a ratio (0..1), two children, and a precomputed splitter rect. Each `NODE_LEAF` carries a `paneId` (stable across re-recalcs, used as the key everywhere else). `SplitLeaf` / `MergeNode` / `Recalculate` / `HitTestSplitter` / `LeafAtPoint` are the primitives. `SaveSnapshot`/`LoadSnapshot` (de)serialize the tree as a flat `NodeSnapshot[]` for ExtState/RPP I/O. `SetOrigin(x, y)` reserves space at the top of the tree's root rect (used for the nav bar). |

### Container — the central class

The container is split across five translation units that share `container.h`.
The split is by responsibility, not by inheritance; every file accesses the
same `MaxPaneContainer*` via `this->`.

| File | Role |
|------|------|
| `container.h` | Class declaration. `MaxPaneContainer` holds `m_tree`, `m_winMgr`, `m_captureQueue`, `m_favMgr`, `m_wsMgr`, plus per-instance ExtState section / dock ident / RPP chunk tag strings, floating-mode geometry, nav-bar / drag / Home-overlay state, recently-closed-tab ring buffer, toast bar state, brushes/pens. |
| `container.cpp` | Lifecycle (`Create`, `Shutdown`, `Toggle`), `DlgProc` (message dispatch), two timer paths: `TIMER_ID_CHECK` (500 ms — CheckAlive tick, RPP-state poll) and `OnCaptureTimerTick` on `TIMER_ID_CAPTURE` (50 ms — capture-by-click state machine + capture-queue drain; extracted from the DlgProc in the 2026-06 audit). Context menu construction sits in `context_menu.cpp`; menu command dispatch sits here in `HandleTabMenuCommand` / `HandlePaneMenuCommand`. |
| `container_paint.cpp` | `OnPaint`, `DrawTabBar`, `PaintToast`. Reads dark/light palette from `MaxPaneIsDarkMode()`. Paints nav bar, pane grid, drag-to-dock preview, Home overlay, tooltips, toast in that z-order. |
| `container_input.cpp` | `OnMouseMove`, `OnLButtonUp`, `CalcTabBarLayout`, `GetTabRect`, `TabHitTest`, `IsOnTabCloseButton`, tab drag/drop state machine. |
| `container_state.cpp` | `SaveState` / `LoadState` (ExtState round-trip), `ApplyPaneState` (deferred capture queue enqueue), `SaveWorkspace` / `LoadWorkspace` / `DeleteWorkspace`, stale-list helpers (`ProcessStaleActionsForSection`, `MergeCapturesIntoStaleListForSection`, `AppendActionToStaleListForSection`). |
| `container_nav.cpp` | ADR-026 implementation. Nav bar dispatch (`OnNavBarClick`, `OpenLoadDropdown`, …), Home overlay open/close (`OpenHomeOverlay` / `CloseHomeOverlay`), drag-to-dock state machine (`EnterDragMode`, `DragModeTick`, `RefreshDragPreview`, `CommitDrop`), native HMENU Support popup. |

### Window reparenting & capture pipeline

| File | Role |
|------|------|
| `window_manager.h/cpp` | The heart of capture. `TabEntry` is the per-window record (name, searchTitle, action, HWND, originalParent, originalRect, captured flag, pinned flag, …). `PaneState[MAX_PANES]` holds the per-pane tab arrays. `DoCapture` / `DoRelease` perform the SetParent dance. `CheckAlive` is the per-tick liveness scan. `FindReaperWindow` walks the REAPER window tree by title (with the dock-frame priority described in §6 of this doc). |
| `capture_queue.h/cpp` | Async retry queue. `EnqueueKnown` / `EnqueueArbitrary` queue a target; `Tick` polls `FindReaperWindow` until either the window appears (≤30 retries for known, ≤200 for arbitrary) or we give up. Handles the dock-frame wait for ReaImGui scripts (`"Title (docked)"` precedence) and the script-action guard (`alreadyOpen` check before firing the toggle). Failures (retry exhaustion, pane full, already captured) surface to the user via `PopFailureToast` — see §7. |
| `fx_capture.h/cpp` | v2.0.4 (ADR-037) — AU/VST/JSFX plugin-window capture identity. Persists the same `(track GUID, FX GUID)` pair REAPER itself stores in RPP `FXID {…}` blocks (`fx@…` / `takefx@…` encodings inside the `arb:` envelope; `master` sentinel for master-track FX), so a captured plugin UI survives FX reorder, rename, and restart. Resolve / show / hide via `TrackFX_Show`. Pure REAPER SDK — zero platform `#ifdef`s. v2.3.0 (ADR-070) adds `ListTrackFxIdentities` — enumerates a track's main chain into identity strings for one-click "Capture track FX chain" and the experimental follow-selected-track mode. |
| `overlay_frame.h/cpp` | v2.2.0 (ADR-060) — Win32/Linux visual feedback: a frame of four thin top-level strips drawn around a rect (interior uncovered, so no alpha or click-through tricks needed). Backs the capture hover outline and the drag-to-dock drop-zone preview. macOS keeps its native overlay path in `swell_cocoa_helpers.mm`. |
| `context_menu.h/cpp` | Pane- and tab-context menu construction. `MenuIds` namespace centralizes command IDs. `EnumOpenWindowsProc` builds the "Open Windows" submenu by enumerating top-level windows. Capture submenus, Workspace submenus, Favorites submenus, Color submenu, Close submenu (ADR-030). |

### Persistence

| File | Role |
|------|------|
| `state_accessor.h` | Polymorphic state I/O: `StateAccessor` interface with four implementations — `GlobalStateAccessor` (REAPER ExtState; user-global, persisted to reaper.ini), `ProjectStateAccessor` (per-project ExtState), `RppWriteAccessor` (collects key/value pairs for the RPP `<MAXPANE_STATE>` chunk on save), `RppReadAccessor` (parses lines from a buffered chunk on load). Same `WriteTreeNodesStatic`/`WritePaneTabsStatic` code paths drive all four. |
| `workspace_manager.h/cpp` | Named-workspace CRUD (`Save`, `Find`, `Delete`, `Rename`, `Duplicate`). Per-instance current-state I/O (`SaveCurrentState` / `LoadCurrentState`) + project-RPP I/O. ADR-012 split: `m_section` is per-instance (current tree, project state); `m_listSection` is always `EXT_SECTION` so the workspace list is shared across instances. |
| `favorites_manager.h/cpp` | Shared favorites list (always `EXT_SECTION` per ADR-009). `Add` / `Remove` / `Get`, persisted to ExtState. |
| `project_state.h/cpp` | `project_config_extension_t` callbacks. `OnProcessExtensionLine` parses `<MAXPANE_STATE[_N]>` chunks line-by-line into the per-instance `g_pendingProjectState[N]` buffer. `OnSaveExtensionConfig` serializes via `RppWriteAccessor`. The actual apply is deferred to a one-shot timer in container.cpp so the container is already created when we replay state. |
| `state_limits.h` | Compile-time caps for serialization buffers (`RPP_MAX_LINES`, `RPP_MAX_LINE_LEN`, `RPP_KV_MAX`, …). |
| `screenset.h/cpp` | v2.1.0 (ADR-050) — REAPER "Window sets" (screenset) integration via `screenset_registerNew`, one id per instance slot. `SAVE_STATE` serializes the same tree+tabs blob as the RPP `<MAXPANE_STATE>` chunk; `LOAD_STATE` drives the existing restore funnel and defers to an in-flight project-load restore so the two never fight. |
| `action_list.h/cpp` | 2026-06 audit — single source of truth for parsing / serializing the comma-separated `stale_toggle_actions` action-ID list (`ParseActionList` / `AppendUniqueAction` / `SerializeActionList`, one shared capacity). Previously hand-rolled four times in `container_state.cpp` with divergent reader/writer capacities — a legally-written list could be silently truncated on the next startup. Pure logic, unit-tested in `tests/test_action_list.cpp`. |

### Multi-instance

| File | Role |
|------|------|
| `instance_manager.h/cpp` | Singleton owning `std::array<unique_ptr<MaxPaneContainer>, 8>`. `GetOrCreate(id)` / `GetExisting(id)` / `ForEach(fn)`. Tracks focused-instance id for routing F6 slot actions (last user-clicked container wins; defaults to 0). |

### v2.0-specific features

| File | Role |
|------|------|
| `nav_bar.h/cpp` | ADR-026 persistent toolbar. Layout, paint, hit-test, tooltip. Stateless; container owns the state struct and passes it in. |
| `drag_dock.h/cpp` | ADR-026 drag-to-dock state machine + drop-zone geometry + live-preview painter. Polling-based (no platform hooks). |
| `launcher.h/cpp` | ADR-014 empty-container launcher hero. Card grid with mini layout previews, tooltip on hover, click → load workspace. Also rendered by the Home overlay (ADR-026) over a populated layout. |
| `quick_switcher.h/cpp` | F4 / ADR-024-era Quick Switcher. Modal SWELL dialog with fuzzy filter across open tabs + workspaces + favorites. Bound to `MaxPane_QuickSwitcher`. |
| `save_workspace_dialog.h/cpp` | Sprint 3.3 custom Save dialog. Name input + clickable listbox of existing workspaces + dynamic status label ("Will replace…" / "Will save as new…"). |
| `settings_dialog.h/cpp` | ADR-019/022 Settings dialog. Single page, SWELL macro-only widgets, cycle-button for tristate dark-mode (Auto / Force dark / Force light). v2.0.3 added an About section (version, MIT license, support links) + manual "Check for updates" button. |
| `nav_icons.h/cpp` + `cpp/resources/icons/` | v2.0.2 unified Phosphor PNG icon set for the nav bar. Replaced the previous Unicode-glyph path on all three platforms (the glyph fonts on Linux Noto Sans were missing the codepoints — tofu boxes). 7 SVG sources, generator Python script (PyGI/Rsvg + macOS rsvg-convert/Pillow fallback), 24 KB raw alpha tables emitted to `cpp/resources/nav_icons.gen.cpp`. Per-platform blit paths: Linux `StretchBltFromMem`, Win32 `StretchDIBits` (HALFTONE in v2.0.3 for smooth downscale), macOS `BlitBGRABitmapMacOS` in `swell_cocoa_helpers.mm` (CGImage + CGContextDrawImage with high interpolation). |
| `updater.h/cpp` | "Check for updates" against the project's ReaPack manifest: parses the first `<version name="vX.Y.Z">` tag, compares against `MAXPANE_VERSION_STRING` from `config.h`. Two paths: **async startup check** (v2.0.4 — `StartAsyncCheck` spawns a worker thread; the result lands in an atomic enum polled by container `OnTimer`, which fires the modal on the main thread per Cocoa's NSAlert rule) and **manual synchronous check** from Settings (v2.0.3). Per-platform HTTPS: macOS `FetchUrlSyncMacOS` (NSURLSession), Win32 WinHttp chunked read, Linux `popen("curl …")` — all with 10 s timeouts and a 1 MB response cap. The worker is joined at plugin unload via `ShutdownAsyncCheck` (audit M1.5) so quitting REAPER mid-check can't execute unmapped code. |
| `hotkey_helper.h` | v2.0.4 (ADR-038) — wraps REAPER's native `DoActionShortcutDialog` so every right-click "Bind hotkey" entry opens a keystroke-capture modal scoped to one specific command ID (instead of sending the user to the full Actions list). Implementation lives in `main.cpp` where the slot tables are file-local. |

### Build artifacts

| File | Role |
|------|------|
| `CMakeLists.txt` | One target (`reaper_maxpane` → `.dylib`/`.so`/`.dll`) + one test target (`maxpane_tests`). Perl-driven `swell_resgen.pl` step (ADR-023) emits `.rc_mac_dlg` / `.rc_mac_menu` from `cpp/resources/*.rc` on non-Windows builds; on Windows the `.rc` compiles directly via `rc.exe`. |
| `cpp/resources/*.rc` | SWELL dialog templates for the three modal dialogs (Save Workspace, Settings, Quick Switcher). |
| `cpp/tests/` | Catch2 v2.13.10 single-header vendored under `tests/catch2/`. Test TUs link directly against `split_tree.cpp`, `globals.cpp`, `config.cpp`, `workspace_manager.cpp` (pure-logic only — no SWELL window calls). |

---

## 4. Lifecycle — what happens when

### Plugin load

```
REAPER ─► load reaper_maxpane.dylib
        │
        ▼
   ReaperPluginEntry(rec)
        │
        ├─ Import REAPER API function pointers via rec->GetFunc(...)
        │  → globals.h sees g_GetExtState, g_Main_OnCommand, etc.
        │
        ├─ plugin_register("API_*", …)         ─ none currently
        ├─ plugin_register("hookcommand", …)   ─ command dispatch
        ├─ plugin_register("hookpostcommand", …)
        ├─ plugin_register("projectconfig", &g_projectConfig)
        ├─ plugin_register("accelerator", &g_accelReg)   ─ ADR-029
        ├─ Register custom actions: g_cmdOpenContainer[0..7],
        │  g_cmdNextTab, g_cmdQuickSwitcher, g_cmdWsSlot[0..31],
        │  g_cmdFavSlot[0..31], g_cmdReopenTab, g_cmdWsPickup, …
        │
        ├─ atexit(onAtExit)
        │
        └─ plugin_register("timer", startupTimerFunc)
                         │
                         ▼
            Each REAPER tick (~30 ms):
              tick 1..N:
                ProcessStaleActionsForSection(EXT_SECTION_i) for i in 0..7
                  → closes ghost windows REAPER restored from its wnd_vis cache
              tick STARTUP_DELAY_TICKS (~15):
                if IsAutoOpenEnabled() → InstanceManager::Get().GetOrCreate(0)->Create()
              tick STARTUP_DELAY_TICKS + STARTUP_POLL_TICKS (~75):
                unregister startup timer
```

Notes:
- `IsAutoOpenEnabled()` defaults to **true** when ExtState key is missing
  (ADR-020, first-install ergonomics). The ExtState write of `auto_open=0`
  is the only thing that disables auto-open.
- Stale cleanup polls aggressively (every tick for the first ~75 ticks)
  because REAPER restores its `wnd_vis` cache asynchronously; a single-shot
  cleanup at fixed delay leaves a visible flicker window (ADR-015).

### User triggers "MaxPane: Open Container"

```
REAPER action dispatch ─► hookCommandProc(command, …)
                              │
                              ▼
                   inst = OpenCommandToInstance(command)
                              │
                              ▼
                   c = InstanceManager::Get().GetOrCreate(inst)
                   if c->GetHwnd() → c->Toggle()           (hide if visible)
                   else            → c->Create()
                              │
                              ▼
                MaxPaneContainer::Create()
                              │
                              ├─ Allocate brushes/pens (m_brush*, m_pen*)
                              ├─ Build per-instance ident strings
                              │  m_extSection   = "MaxPane_cpp"   or "MaxPane_cpp_N"
                              │  m_dockIdent    = "MaxPane_container" or "MaxPane_container_N"
                              │  m_rppChunkTag  = "MAXPANE_STATE" or "MAXPANE_STATE_N"
                              ├─ m_favMgr->SetSection(EXT_SECTION)         (shared)
                              ├─ m_wsMgr->SetSection(m_extSection)         (per-instance state)
                              ├─ LoadFloatingState()
                              ├─ if m_floating:
                              │     SWELL_CreateDialog + SetParent(nullptr)
                              │     ApplyFloatingWindowChrome
                              │     SetWindowPos to m_floatX/Y/W/H
                              │  else:
                              │     SWELL_CreateDialog (parent=null) +
                              │     DockWindowAddEx(m_dockIdent, "MaxPane", true)
                              ├─ SetTimer(WM_TIMER, CAPTURE_TICK_MS, …)
                              ├─ LoadNavBarPref()
                              ├─ m_wsMgr->LoadList()
                              ├─ m_favMgr->Load()
                              └─ LoadState()                                (replays last tree + tabs)
```

### Container teardown (Toggle close / WM_DESTROY)

This is the most subtle path in the codebase, with three+ years of bug history
behind every line. See §6 for the close-mechanism deep-dive.

```
User clicks docker X / re-runs "Open Container" / Cmd+Q
        │
        ▼
  Toggle() (mid-session)        WM_DESTROY (REAPER-driven)         onAtExit (Cmd+Q / quit)
        │                              │                                   │
        ▼                              ▼                                   ▼
  SaveState()                    SaveState() if visible              for each instance:
  MergeCapturesIntoStale         ReleaseAll(false) if captured>0       SaveState()
    ListForSection(...)          (no toggle — reparent + hide)         MergeCapturesIntoStale
  ReleaseAll(toggleOff=true)                                             ListForSection(...)
   → DoRelease per tab                                                   ReleaseAll(false)
   → see §6                                                              g_atexitSaved = true
  ShowWindow(SW_HIDE)
```

The `stale_toggle_actions` ExtState key is the safety net: every close path
writes captured action IDs into it; the **next** REAPER startup reads it and
closes any windows REAPER restored from `wnd_vis` that we still consider
closed. Mechanism documented in §6.4.

---

## 5. The layout engine — `SplitTree`

`SplitTree` (`split_tree.h/cpp`) is a fixed-capacity binary tree of `MAX_TREE_NODES`
(currently 31) nodes. The tree has two node types: `NODE_LEAF` (carries a
`paneId`) and `NODE_BRANCH` (carries an orientation + ratio + two child
indices).

```
                  root (branch, vertical, ratio=0.55)
                   /                       \
       branch (horizontal, 0.5)       leaf paneId=2
         /              \
   leaf paneId=0   leaf paneId=1
```

Key invariants:
- `paneId ∈ [0, MAX_PANES)`, allocated via `AllocPaneId` (free-list scan over
  `m_paneIdUsed`). PaneIds are *stable identifiers* used outside the tree —
  `WindowManager::m_panes[paneId]` indexes by them. The tree may relocate
  nodes (during merge) but `paneId` survives.
- Each `Recalculate(w, h)` rebuilds `m_leafList` / `m_branchList` and every
  node's `rect` from the root rect `(originX, originY) → (originX+w, originY+h)`.
- `m_originX/Y` defaults to (0, 0). The container sets origin to
  `(0, NavBarReservedHeight())` so the pane grid begins below the nav bar
  (ADR-026).
- Splitter geometry has a fixed thickness (`SPLITTER_THICKNESS` in `config.h`).
- Drag clamping enforces a minimum split ratio so a pane can't become 0-pixels
  wide.

### Serialization

`SaveSnapshot(NodeSnapshot* out, int& nodeCount)` serializes the tree to a
flat array (one `NodeSnapshot` per used node; node indices in the snapshot are
identical to tree indices). `LoadSnapshot` validates structure (tree-shape,
no cycles, parent links consistent) before swapping the tree in. Corruption
falls back to `Reset()` (a single empty leaf).

Workspaces store one `NodeSnapshot[]` plus a `PaneSnapshot[MAX_PANES]` (tab
data). The on-disk format is keyed ExtState lines like:

```
ws_3_tree_count=7
ws_3_tree_0_type=2
ws_3_tree_0_orient=0
ws_3_tree_0_ratio=0.5
ws_3_tree_0_childA=1
ws_3_tree_0_childB=2
...
ws_3_pane_0_tab_count=2
ws_3_pane_0_tab_0=...
```

The same `WriteTreeNodesStatic` / `ReadTreeNodesStatic` and
`WritePaneTabsStatic` / `ReadPaneTabsStatic` static methods write to either
ExtState or an RPP `<MAXPANE_STATE>` chunk, depending on the `StateAccessor`
passed in. Single code path, four destinations.

---

## 6. Window reparenting — the hard part

This is where MaxPane spends most of its complexity and where most v2.0 bug
fixes (B1, B2, B3, B4, B13, B14, B15, B16, B17, B19, B20, B27) landed.

### 6.1 The three SetParent paths

WDL/SWELL implements `SetParent` differently depending on the source and
destination relationship. On macOS three paths exist with distinct semantics
(see `swell-cocoa.mm` in WDL):

- **Path A — top-level → child.** Source HWND is a stand-alone `NSWindow`.
  Target parent is non-null. SWELL extracts the source NSView from its
  NSWindow, **destroys the original NSWindow**, and inserts the NSView into
  the target parent's NSView. This is `DoCapture`. The destroyed NSWindow
  cannot be revived; if MaxPane later wants to give the captured window back
  to REAPER as floating, the SWELL machinery creates a fresh orphan NSWindow
  with default style mask (no chrome — see B27 below).
- **Path B — child → top-level (`SetParent(hwnd, nullptr)`).** SWELL creates
  a new orphan `NSWindow`, makes the captured NSView its `contentView`. This
  is the first half of `DoRelease`. On macOS the new NSWindow appears at
  default position (lower-left), with default style mask. On Linux the GTK
  equivalent has different semantics (B11 — unfixed, gated on Sprint 1).
- **Path C — child → child (different MaxPane pane).** Tab move within or
  across panes uses this path. SWELL removes the NSView from one parent's
  subview list and inserts into another's. Caveat: SWELL does NOT trigger
  `setNeedsLayout:` or `setNeedsDisplay:` after this — we have to call
  `ForceViewLayoutAndDisplay()` (`swell_cocoa_helpers.mm`) manually.

### 6.2 `DoCapture` — pulling a REAPER window in

```
WindowManager::DoCapture(TabEntry& tab, HWND target, HWND container)
  │
  ├─ tab.originalParent = GetParent(target)           // remember for B-path release
  ├─ GetWindowRect(target, &tab.originalRect)         // B27 — restore pos on release
  │
  ├─ if target is a toolbar: SubclassToolbar(target)  // intercept drag-to-undock
  │
  ├─ if tab.originalParent is REAPER's docker:
  │     g_DockWindowRemove(target)                    // B20 — REAPER's docker
  │                                                   //       holds a stale ref;
  │                                                   //       removing prevents
  │                                                   //       grey placeholder
  ├─ SetParent(target, container)                     // Path A (or C)
  ├─ VerifySetParent(...)                             // B1 — log mismatch
  │                                                   //       no-op on macOS where
  │                                                   //       Path A doesn't equal
  │                                                   //       container exactly
  ├─ Strip non-child styles (WS_CAPTION, WS_THICKFRAME, …)
  ├─ SetWindowPos(target, 0, 0, paneW, paneH,
  │               SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE)
  │                                                   // B23 — without sync,
  │                                                   //       view sits off-screen
  │                                                   //       at old screen coords
  ├─ InvalidateRect(target, nullptr, TRUE)
  ├─ ForceViewLayoutAndDisplay(target)                // SWELL doesn't cascade
  │                                                   //       layout to children
  ├─ tab.captured = true
  └─ Subsequent RefreshLayout will tighten geometry to the actual pane rect.
```

### 6.3 `DoRelease` — giving a captured window back

This is the most-debugged routine in the codebase — and the project's known
regression magnet. The implementation lives in `window_manager.cpp:DoRelease`:

```
WindowManager::DoRelease(TabEntry& tab, bool toggleOff = true,
                         bool returnVisible = false)
```

Two orthogonal modes select the *outcome*:

- **Close path** (`returnVisible == false`, the default) — the captured
  window is closed/hidden and REAPER's `wnd_vis` tracking is re-synced. Used
  by "Close Tab", `Toggle()` teardown, and workspace switches
  (`toggleOff == false` variant: reparent + hide only, no toggle, so the next
  workspace load can recapture without a round-trip).
- **Release-Window path** (`returnVisible == true`, ADR-046) — the window is
  *detached and left visible* as a floating REAPER window: the toggle /
  `WM_CLOSE` dispatch is deliberately skipped (REAPER's toggle state stays 1,
  FX showFlag stays 3), the original screen rect is restored and clamped
  on-screen, and — critically — **no stale-list entry is written**, so the
  startup ghost-cleanup (§6.4) leaves the deliberately-open window alone.
  "Release Window" in the tab menu uses this; "Close Tab" remains the
  destructive action.

Three *protocols* then handle the actual detach, selected by what we know
about the window. All three funnel into a shared tail (this was a
`goto fx_done` until the 2026-06 audit removed the label; the protocol
bodies were kept byte-identical — see audit M2.2):

```
  ├─ Protocol 1 — FX identity (FxCapture::IsFxIdentity(tab.actionCmd)):
  │     close path:    FxCapture::Hide → TrackFX_Show(hide); REAPER's own
  │                    tracker updates cleanly (ADR-037)
  │     returnVisible: skip the hide — the FX UI stays floating
  │     both:          DetachToTopLevel + restore originalRect (clamped
  │                    on-screen for returnVisible). No chrome restore —
  │                    REAPER manages FX-window chrome itself.
  │
  ├─ Protocol 2 — toggle-known (toggleOff && tab.toggleAction > 0):
  │     preState read BEFORE any reparent (B13 — SetParent(nullptr) races
  │     wnd_vis tracking)
  │     close path:    fire the toggle while still WS_CHILD; WM_CLOSE as
  │                    fallback if the toggle no-ops; pre-detach hide (F-G —
  │                    manager windows like Routing Matrix ignore both while
  │                    they are our child and would re-float as ghosts);
  │                    detach; post-detach toggle RETRY if state is still 1
  │                    (F-G); restore originalRect; chrome for arbitrary
  │                    non-toolbars
  │     returnVisible: skip toggle/WM_CLOSE entirely; detach; clamp rect
  │                    on-screen (toolbars keep their own geometry — their
  │                    captured rect is the degenerate docked 42x42, B24)
  │
  ├─ Protocol 3 — no action known (else; arbitrary / ReaImGui):
  │     if toggle state == -1 (live ReaImGui script): fire the script's own
  │       action even on workspace switch — detaching behind ImGui Docker's
  │       back leaves a stale pointer and crashes in Docker::moveTo
  │     else, close path: WM_CLOSE while WS_CHILD (B27 v7 sequence)
  │     both: detach → ApplyFloatingWindowChrome on the fresh orphan →
  │       restore originalRect (see §6.5 for why this order is load-bearing)
  │
  └─ Shared tail (all three protocols):
        UnsubclassToolbar          // F-D — after the toggle/reparent has
                                   //   finished pumping messages; earlier
                                   //   unsubclassing let the release click
                                   //   fire a toolbar button
        returnVisible ? SW_SHOW + ForceViewLayoutAndDisplay
                      : SW_HIDE + ForceHideWindow   // B14 — orderOut: direct
```

The reason for the protocol split: each captured window's REAPER-side action
behaves slightly differently. Some toggle correctly off after a reparent
(Mixer); some refuse to update `wnd_vis` while reparented (Actions, Media
Explorer, the manager windows); FX UIs have a dedicated SDK close
(`TrackFX_Show`); some windows have no action at all (arbitrary captures
with `toggleAction == 0`); and ReaImGui scripts (state `-1`) must tear their
own window down. The B27 diagnostic `GetWindowRect` probes in this routine
compile only in Debug builds since the audit (their only consumers are `DBG`
calls, which are no-ops in Release).

### 6.4 Stale-list defense-in-depth

The stale-list ExtState key (`stale_toggle_actions` under each instance's
section) is the safety net for everything else. Three writers, one reader:

| Writer | When | Why |
|--------|------|-----|
| `Toggle()` | User closes container mid-session | Captured tabs may not have closed cleanly. |
| `LoadWorkspace()` | User switches workspace | Tabs in the old workspace are reparented out with `toggleOff=false`. We need REAPER to close them at *next* startup if it restores them from wnd_vis. |
| `onAtExit` | REAPER quitting | Cmd+Q bypasses some destruction paths on macOS. Defense-in-depth. |
| `AppendActionToStaleListForSection` | Per-tab **close** (X button, "Close Tab", replace-on-drop) | Catch the case where mid-session DoRelease succeeded but REAPER caches an out-of-date wnd_vis. Note: the "Release Window" path (`returnVisible`, ADR-046) deliberately does **not** write an entry — the window is meant to stay open, and a stale entry would make startup cleanup close it. |

| Reader | When | Action |
|--------|------|--------|
| `ProcessStaleActionsForSection` (called from `startupTimerFunc`) | Every tick during startup polling | For each cached action: if `state==1` → toggle off + force-hide via reverse title lookup; if `state==0` but window visible → double-toggle to resync (defer if not found yet); if `state==-1` (fire-and-show actions like Region Render Matrix, ReaImGui scripts) → `WM_CLOSE` + hide when the window is currently visible, defer to a later tick when it isn't (Sprint 1 Entry 8 — the original hard "skip" left these as floating ghosts after restart). |

Entries still deferred when the startup poll window closes (~75 ticks) are
**pruned** (2026-06 audit, M3.3): they reference windows REAPER did not
restore this session — typically an uninstalled script or a removed toolbar —
and used to survive forever, re-running the full probe loop at every future
startup. Nothing else reads the list until the next startup, so dropping the
remainder is behaviorally equivalent; `LoadWorkspace` re-arms fresh entries
mid-session unaffected.

The mechanism survived being deleted once (ADR-013 initial pass) and was
restored after a regression (ADR-013-A correction). Future maintainers: do
**not** remove this without re-running the close-restart smoke test on a
workspace with toolbars + Media Explorer + a ReaImGui script.

### 6.5 B27 — frameless re-fire of ReaImGui plugins

When a user captures a third-party plugin (ReaBeat, reamix.me, ReaMD) via
drag-to-dock or click-to-capture, `tab.toggleAction == 0` (we don't know how
to close it via REAPER). The historical close path was: SetParent to null +
ShowWindow(SW_HIDE). Two problems compounded:

1. The script's tracker never receives a "closed" signal — its toolbar
   button stays active, the View menu stays checked.
2. SWELL's `SetParent(nullptr)` creates a fresh orphan NSWindow with default
   style mask (no chrome). When the user re-fires the plugin (via the still-
   active toolbar button), the script logic shows the existing NSView's
   *current* host NSWindow — which is our bare orphan — so the window appears
   frameless at the default position.

Fix (v7 chrome-restore, late 2026-05-23):
1. `SetParent(tab.hwnd, nullptr)` — makes the NSView contentView of a fresh
   orphan NSWindow.
2. `ApplyFloatingWindowChrome(tab.hwnd)` — sets the orphan NSWindow's
   `styleMask` to titled + closable + miniaturizable + resizable. Only works
   on contentView NSWindows, which is why it must come *after* SetParent.
3. `SetWindowPos(tab.hwnd, 0, 0, originalRect, …)` — restore pre-capture
   screen position so REAPER's wnd_vis cache (if it tracks this window)
   saves sensible coords.
4. `SendMessage(WM_CLOSE)` then `SW_HIDE` + `ForceHideWindow`.

On re-fire: the script reuses the existing NSView's host NSWindow — which is
now our chromed, positioned orphan — and the window appears at its original
location with a proper frame.

Dead ends ruled out (do not retry without new evidence):
- Toggling the action while window is still WS_CHILD: there's no action ID
  for these plugins, nothing to toggle.
- `DestroyWindow` on the orphan NSWindow: REAPER's tracker holds the now-
  dangling HWND, crashes on next access.
- Restoring `originalStyle` bits via `SetWindowLong` before SetParent: no-op
  for Cocoa chrome — `styleMask` isn't propagated through `GWL_STYLE`.

---

## 7. Capture pipeline — `CaptureQueue`

Direct synchronous capture works for windows that are already open. For
windows that need to be opened first (via `Main_OnCommand(toggleAction)`)
the timing is tricky:

- The toggle action fires asynchronously; the window doesn't exist yet when
  the call returns.
- Some windows have a dock-frame wrapper (`"Title (docked)"` on top of an
  inner `"Title"`) that REAPER creates only when actually docking. We want
  the dock frame, not the inner shell — it carries the dock tab bar and the
  actual rendered UI.
- Re-using an `_RSxxx` named-command ID across REAPER restarts requires
  `ReverseNamedCommandLookup` to resolve to a stable string and
  `NamedCommandLookup` to resolve back to a (possibly new) numeric ID.

`CaptureQueue::Tick` runs on the dedicated 50 ms capture timer — it is called
from `MaxPaneContainer::OnCaptureTimerTick` (`TIMER_ID_CAPTURE`,
`TIMER_CAPTURE_INTERVAL = 50 ms`), **not** from the 500 ms `TIMER_ID_CHECK`
tick that drives `CheckAlive` and the RPP-state poll. It iterates the pending
queue:

```
for each PendingCapture in m_queue:
  if state == WAITING:
    if ++tickCount > INITIAL_WAIT_TICKS:    // ~500 ms
      state = RETRYING

  if state == RETRYING:
    target = FindReaperWindow(searchTitle, container)
           // searches for "Title (docked)" first, then "Title",
           // both as top-level and as children of REAPER_dock
    if target:
      WindowManager::CaptureArbitraryWindow(pane, target, displayName, ...)
      state = DONE
    else:
      if ++retryCount > maxRetries:
        log SKIP / fail
        state = FAILED
```

Two retry budgets:
- `MAX_RETRIES = 30` (~1.5 s) for known windows. REAPER's known windows open
  predictably; if they're not there in 1.5 s, something's wrong.
- `MAX_RETRIES_ARBITRARY = 200` (~10 s) for arbitrary captures. ReaImGui
  scripts can take a few seconds to register their HWNDs.

Failures are no longer silent (2026-06 audit): retry exhaustion, a full pane,
and already-captured targets stage a message that the container polls via
`CaptureQueue::PopFailureToast()` after each Tick and shows as a toast
(generalized from the earlier FX-only `PopFxFailureToast` side-channel).
Previously these paths logged only to the Debug-build file — a Release user
saw "nothing happened".

B12 / B18 fix: arbitrary captures now respect an `alreadyOpen` guard before
firing the toggle, mirroring the known-window path. Without this, loading a
workspace that contained a script which the user happened to have already
opened would toggle the script *off* and then spend 200 retries trying to find
it. The guard checks `FindReaperWindow(searchTitle)` once before firing the
toggle and skips the toggle if it returns a valid HWND.

---

## 8. State persistence — `StateAccessor` polymorphism

All persistent state goes through the `StateAccessor` interface
(`state_accessor.h`). Four implementations, one set of write/read functions:

```
              StateAccessor (virtual)
                     ▲
        ┌────────────┼────────────┬─────────────────┐
        │            │            │                 │
GlobalStateAccessor  ProjectStateAccessor  RppWriteAccessor  RppReadAccessor
        │            │            │                 │
        ▼            ▼            ▼                 ▼
   ExtState    ProjExtState    KV list →        Lines  ←
   (reaper.ini) (.rpp ProjExt)  RPP chunk        RPP chunk
                                writer            parser
```

Why polymorphism here: workspace serialization is the same data structure
for all four destinations. `WriteTreeNodesStatic` reads from a `SplitTree`
snapshot, calls `accessor.Set(section, key, value, persist)` for each line.
The accessor decides whether that lands in `reaper.ini`, in the current
project's ExtState, in a collected KV buffer that the project_config callback
emits as `<MAXPANE_STATE>` chunk lines, or is discarded (RppRead is write-
only-discard since it's the read side).

`section` argument:
- `GlobalStateAccessor` — REAPER ExtState section name; per-instance
  (`"MaxPane_cpp"` for instance 0; `"MaxPane_cpp_N"` for N>0). ADR-003 +
  ADR-009 + ADR-012 together govern this naming.
- `ProjectStateAccessor` — ignored (`SetProjExtState` is keyed by project
  pointer, not section, but our calls still pass it for consistency).
- `RppWriteAccessor` / `RppReadAccessor` — ignored (the RPP chunk is one flat
  list per instance, no nested sections).

### Per-project state

`project_config_extension_t` (registered in `main.cpp`) gives REAPER two
callbacks:

- `ProcessExtensionLine` — called for every line of every RPP chunk on
  project load. We watch for `<MAXPANE_STATE>` / `<MAXPANE_STATE_N>` open
  tokens, buffer subsequent lines into `g_pendingProjectState[N].lines[]`
  until the `>` close token.
- `SaveExtensionConfig` — called when REAPER saves the project. We
  instantiate `RppWriteAccessor`, run `WriteTreeNodesStatic` and
  `WritePaneTabsStatic` against the active container's state, then emit each
  collected KV pair as one chunk line.

Apply is deferred: `OnRppStateReady` (in container) consumes the buffered
chunk via `RppReadAccessor`, builds a tree+pane snapshot, and replays it
through `ApplyPaneState(deferActions=true)`. The `deferActions` flag is
critical — at chunk-read time the captured windows may not exist yet (REAPER
opens them lazily, or they need to be toggled by Main_OnCommand which is
illegal during project load). The capture queue absorbs the deferral.

---

## 9. Multi-instance architecture (F2)

Up to 8 `MaxPaneContainer` instances can coexist. `InstanceManager` is the
singleton owner. Each instance gets three identifier strings, computed once
in the constructor:

| Instance id | ExtState section    | Dock ident           | RPP chunk tag       |
|-------------|--------------------|--------------------- |---------------------|
| 0           | `MaxPane_cpp`      | `MaxPane_container`  | `MAXPANE_STATE`     |
| 1           | `MaxPane_cpp_1`    | `MaxPane_container_1`| `MAXPANE_STATE_1`   |
| 2           | `MaxPane_cpp_2`    | `MaxPane_container_2`| `MAXPANE_STATE_2`   |
| …           | …                  | …                    | …                   |
| 7           | `MaxPane_cpp_7`    | `MaxPane_container_7`| `MAXPANE_STATE_7`   |

Instance 0 uses the legacy v1.5.x identifiers verbatim (ADR-003 backward
compat). Upgraders see no change; their old layout, favorites, and project
chunks load into instance 0 automatically.

What is shared, what is per-instance (ADR-009 + ADR-012):

| Data | Scope | Section read by |
|------|-------|-----------------|
| Tree, tabs, current state | per-instance | `m_section` |
| Project RPP chunk | per-instance | `m_section` |
| Floating geometry, always-on-top | per-instance | `m_section` |
| `was_visible` | per-instance | `m_section` |
| Workspace **list** (ws_count, ws_N_*) | shared | `m_listSection = EXT_SECTION` |
| Favorites | shared | always `EXT_SECTION` |
| `stale_toggle_actions` | per-instance | `m_section` |

Rationale: workspaces describe layouts and are intrinsically reusable across
containers; favorites are user-level "things I capture often"; current state
and project RPP chunks describe what *this* container is showing right now.

### Action routing

`hookCommandProc` (in main.cpp) maps every command id to a target instance:

- `MaxPane_OpenContainer` (legacy unsuffixed) → instance 0.
- `MaxPane_OpenContainer_2..8` → instances 1..7 (note: user-visible naming
  is 1-indexed; internal indexing is 0-based).
- `MaxPane_WsSlot_N`, `MaxPane_FavSlot_N`, `MaxPane_NextTab`, `MaxPane_PrevTab`,
  `MaxPane_QuickSwitcher`, `MaxPane_ReopenTab`, `MaxPane_WsPickup` →
  resolved via `ResolveSlotTargetInstance()`:
  ```
  c = InstanceManager::GetFocused()
  if c && c->GetHwnd() return c
  c = InstanceManager::GetOrCreate(0)
  if !c->GetHwnd() c->Create()
  return c
  ```
  Focused instance tracking: `InstanceManager::SetFocused(id)` is called on
  every user click in a container's client area. Defaults to 0 so a fresh
  session routes to the legacy instance.

---

## 10. Navigation bar + drag-to-dock (ADR-026)

### Nav bar

`NavBar::Compute(containerRect)` lays out a 30 px strip across the top:

```
[Home] | [Drag] [Switch] [Save] [Load▾] |                [Settings] [Support]
```

Left group is the **workspace actions** (capture group + load). Right group
is **utilities**. Vertical divider lines separate the groups.

The bar is stateless — `NavBar::Compute` returns a `Layout` struct, the
container owns a `NavBar::State` struct that holds hover button id, drag-mode
armed flag, home-active flag. `NavBar::Paint(hdc, layout, state, darkMode)`
renders; `NavBar::HitTest(layout, x, y)` returns the `ButtonId` under a
cursor.

Tooltip on 600 ms hover delay (`TIMER_ID_NAVBAR_TIP`). Tooltip text comes
from `NavBar::ButtonTooltip(buttonId, dragArmed)`.

Icons are Unicode glyphs (⌂ ⊕ ⌕ ⬇ ≡ ⚙ ♥) with a per-icon `{font_px, y_offset}`
table to compensate for natural visual-weight variance between glyphs. An
earlier procedural-icon approach (GDI strokes) was reverted as too angular
relative to system-font glyph anti-aliasing.

The nav bar reserves `NavBarReservedHeight()` pixels from the tree's root
rect via `SplitTree::SetOrigin`. Toggle visible via Settings → "Show
navigation bar" (ExtState key `show_nav_bar` per-instance, default ON).

### Home overlay

Right-click → "Home" or click `[Home]` opens a workspace-card overlay over
the current layout (non-destructive picker). When opened we
`ShowWindow(SW_HIDE)` all currently-captured tabs so they don't paint above
our overlay (Cocoa z-order: captured child NSViews paint above their parent
NSView's `drawRect:`, see §10.3 below). On close we `SW_SHOWNA` them back.
On card-click we *don't* re-show — `LoadWorkspace` releases the old captures
cleanly itself.

### Drag-to-dock (Approach 2 — polling)

Click `[Drag]` arms the state machine (`DragDock::State::mode = ARMED`). A
16 ms timer (`TIMER_ID_DRAG_DOCK`) polls `GetCursorPos` + `GetAsyncKeyState
(VK_LBUTTON / VK_SHIFT)`:

```
ARMED → wait for L-button transition up→down outside MaxPane
      → on transition: sourceHwnd = WindowFromPoint(cursor); walk to top-level
                       mode = TRACKING
TRACKING → every tick:
            compute target pane + zone
              ZONE_TAB_BAR / BODY_CENTER → add as tab
              ZONE_SPLIT_{LEFT,RIGHT,TOP,BOTTOM} → split + capture
              ZONE_REPLACE (Shift held) → close active tab, capture
            invalidate dirty rect → live preview repaints
          on L-button up→down:
            if cursor inside MaxPane pane && zone != NONE:
              CommitDrop()
            mode = IDLE
ESC at any point → mode = IDLE
```

Why polling beats hooks: no subclassing third-party HWNDs, works identically
on macOS / Win32 / Linux SWELL with the same code, ~16 ms cost is negligible.

`CommitDrop` dispatches one of four outcomes:
- Tab-bar / body-center / forgiving → `WindowManager::CaptureArbitraryWindow`
- `ZONE_REPLACE` → record active tab in stale list → close active tab →
  capture
- Edge splits → `SplitTree::SplitLeaf` (with the appropriate orientation +
  L/R or T/B ordering) → capture into the newly created child pane.
  Fallback to tab-append if split fails (MAX_LEAVES hit).

### Cocoa z-order constraint

On macOS, captured child NSViews always paint above their parent NSView's
`drawRect:`. There is no `WS_CLIPCHILDREN` equivalent. This means any overlay
rendered by the parent NSView (drag preview, Home overlay backdrop, Support
popup) would be occluded by captured panes. Two coping strategies:
1. **Hide the captured children while overlay is up.** Used by Home overlay
   (cheap, captures are restored cleanly on close).
2. **Render in a separate NSWindow.** Used by Support popup (HMENU becomes
   NSMenu, which is already a separate NSPanel — works for free).
3. **Render preview rects on top of pane grid in our `OnPaint`, accepting
   that fully-painted captures will overlap.** Used by drag-to-dock preview,
   which is mostly translucent and acceptable visually.

---

## 11. Workspace launcher hero (ADR-014)

When a container is in "empty + 0 tabs + not solo + not capture mode + not
drag mode", `MaxPaneContainer::IsInLauncherMode()` returns true and
`OnPaint` early-returns into `Launcher::Paint`. The pane grid is replaced by
a card grid:

- Each workspace is one card. The card shows the workspace name + a
  miniaturized rendering of its tree layout (recursive over
  `NodeSnapshot[]`).
- Hover delay → tooltip listing the captured window names in that workspace.
- Click → `LoadWorkspace(name)` → the capture queue fires, the container
  exits launcher mode.
- A "Capture Window" CTA card sits at the end of the grid to enter capture
  mode directly.
- Right-click on a card opens a per-card menu: Load / Rename / Duplicate /
  Delete / Bind Hotkey.
- Footer line with support links (Ko-fi / Buy Me a Coffee / PayPal — ADR-021).

The launcher is rendered from the same `Launcher::Paint` routine when the
Home overlay (§10.2) is open over a populated layout. Same hit-test, same
hover model — just composited on top of the pane grid (after captures are
hidden) instead of replacing it.

---

## 12. Floating mode (F1a / ADR-024)

`m_floating == false` → container is docked in REAPER via `DockWindowAddEx`
(default, backward-compatible). `m_floating == true` → container is a
top-level OS window at `m_floatX/Y/W/H` with native chrome.

Transition (`DetachToFloating` / `RedockToContainer`):
1. Save current geometry (if dock→float, default to a centered-on-REAPER-main
   rect).
2. `ReleaseAll(false)` — reparent + hide captures, don't toggle (we'll
   reattach them after the container moves).
3. `g_DockWindowRemove(m_hwnd)` if leaving the docker.
4. `SetParent(m_hwnd, nullptr)` (Path B) + `ApplyFloatingWindowChrome` if
   floating; `DockWindowAddEx` if re-docking.
5. `SetWindowPos` to saved geometry; multi-monitor clamping via
   `MonitorBoundsForRect` (`[NSScreen screens]` enum on macOS, fallback
   centered-on-REAPER-main on Win/Linux).
6. Re-capture each released tab into its prior pane via the capture queue.

C5 (ADR-027) — when floating, optional `NSWindow setLevel:NSFloatingWindowLevel`
keeps the window above other apps. ExtState key `float_always_on_top`,
restored on both `Create()` and `DetachToFloating()`.

F1b (per-pane detach) was implemented in Sprint 4 and reverted same day.
Narrow unique use case + ~800 LOC + bug-risk classes didn't justify the
redundancy with REAPER's native "Float window" UI. Revisit in v2.1 if
post-release feedback shows real demand.

---

## 13. Accelerator hook (ADR-029)

By default, REAPER's keyboard accelerator processing skips when focus is on
a non-REAPER HWND — which, for our captured panes and the MaxPane container
itself, means MaxPane action bindings don't fire when the user is clicked
into a MaxPane window.

`accelerator_register_t` registered in main.cpp:
```
translateAccel:
  if message targets any live MaxPane container HWND or any descendant:
    return -666   // forces REAPER's main accel table to process the key
  else:
    return 0      // unrelated window, leave alone
```

The `-666` return code is REAPER's convention for "I claim this and want main
accel processing". The descendant walk uses `IsChild(container, target)` so
both the container itself and any captured child windows route correctly.

---

## 14. Feature surface, v2.0–v2.3 (one-liners)

For each feature: where it lives + what ADR justifies it. Detail lives in
`docs/v2/V2_DECISIONS.md` (local-only).

| Feature | Files | ADR |
|---------|-------|-----|
| F1a per-container floating | `container.{h,cpp}` + `swell_cocoa_helpers.mm` | ADR-024 |
| F2 multi-instance (8 containers) | `instance_manager.{h,cpp}`, ident strings in `container.h` | ADR-003/004/009/010/012 |
| F4 Quick Switcher (Cmd+P-style) | `quick_switcher.{h,cpp}` | — |
| F6 hotkey slots (32 ws + 32 fav) | `main.cpp` action registry + `ResolveSlotTargetInstance` | — |
| C1 reopen last closed tab | `container.h` ring buffer + dispatch | ADR-027 |
| C2 pinned tabs | `TabEntry::pinned`, stable-partition in `WindowManager` | ADR-027 |
| C4 workspace pickup | `MaxPane_WsPickup` action + `GetUserInputs` | ADR-027 |
| C5 always-on-top floating | NSWindow setLevel: + ExtState | ADR-027 |
| Nav bar + Home overlay + drag-to-dock | `nav_bar.{h,cpp}`, `drag_dock.{h,cpp}`, `container_nav.cpp` | ADR-026 |
| Settings dialog | `settings_dialog.{h,cpp}`, `resources/settings_dialog.rc` | ADR-019/022 |
| Custom Save Workspace dialog | `save_workspace_dialog.{h,cpp}` | — |
| Toast bar | `container.h` toast fields, `PaintToast` in `container_paint.cpp` | — |
| Launcher hero | `launcher.{h,cpp}` | ADR-014 |
| Capture menu flattening | `context_menu.cpp` | ADR-020/030 |
| MAX_WORKSPACES 32 | `config.h` | ADR-020 |
| Accelerator hook | `main.cpp` `g_accelReg` | ADR-029 |
| FX-identity capture (AU/VST/JSFX save+restore) | `fx_capture.{h,cpp}` | ADR-037 |
| Inline hotkey binding | `hotkey_helper.h` + `main.cpp` | ADR-038 |
| Capture-by-click (modal/core-window guards, crosshair, hover preview + outline) | `container.cpp` `OnCaptureTimerTick`, `window_manager.cpp` `IsCapturableTarget`, `swell_cocoa_helpers.{h,mm}` | ADR-048; ADR-053 — the Win32/Linux "embedded in main" test requires a real `WS_CHILD` link (`GetParent` returns the *owner* for popups, so v2.1.0 silently rejected floating script windows there) |
| "Release Window" floating return | `window_manager.cpp` `DoRelease(returnVisible)` — see §6.3 | ADR-046 |
| Screenset ("Window sets") integration | `screenset.{h,cpp}` | ADR-050 |
| ReaImGui size-guard (dock min-clamp + pane floor-hide), scoped to `isReaImGui` hosts only | `window_manager.{h,cpp}`, `container.cpp` | ADR-045 + ADR-051 (v2.1.1 narrowing — docked toolbars/FX are never floor-hidden) |
| Main-toolbar capture exclusion + corrected toolbar action table (`TOOLBAR_ACTION_RANGES`, Toolbars 17–32) | `config.{h,cpp}`, exclusion checks in the capture entry points | ADR-052 |
| Capture/drag visual feedback on Win32/Linux (hover outline + drop-zone frame) | `overlay_frame.{h,cpp}` | ADR-060 |
| Linux GL-ReaImGui conditional capture (gated on ReaImGui software rendering) + capture-refusal reason toasts | `window_manager.{h,cpp}` (`SetCaptureRefusal`/`TakeCaptureRefusal`) | ADR-061/062/063 |
| Startup stale-cleanup cheap-by-default (per-tick top-level probe, deep walk on 3 sweep ticks, Win32 foreign-process guard) | `window_manager.cpp` (`FindTopLevelGhost`, `FindWindowEnumProc`), `container.cpp` `startupTimerFunc` | ADR-064 |
| Startup/restore orchestration: `open_at_save` gate, cross-instance capture arbitration, CaptureQueue WAITING short-circuit, tri-state auto-open, dock self-heal | `container_state.cpp`, `window_manager.cpp` (`IsCapturedByAnotherInstance`), `capture_queue.cpp`, `project_state.cpp` | ADR-065 |
| Floating geometry restore fixed on Win32 (`m_inCreate` guard vs WM_SIZE-during-Create clobber) + true-maximize persistence | `container.cpp` | ADR-066 |
| ReaImGui move-veto subclass (Win32) + script-sticky tab recapture | `window_manager.cpp` (`ImGuiMoveGuardProc`, `TabSurvivesWindowClose`) | ADR-067 |
| Clean mode (hide occupied-pane tab bars), splitter color presets, pane-remainder paint fill, DPI-scaled tooltips | `window_manager.cpp` (`PaneHeaderHeight`), `container_paint.cpp`, `config.cpp`, `swell_cocoa_helpers.h` (`MaxPaneDpiScaleForDC`) | ADR-068 |
| Toolbar release-gesture click swallow; bindable always-on-top action; hide-from-taskbar pref (Win32 `WS_EX_TOOLWINDOW`) | `window_manager.cpp`, `main.cpp`, `container.cpp` (`ApplyFloatingWindowChrome`) | ADR-069 |
| Track FX chain capture (one-shot) + follow-selected-track mode (experimental; transient tabs excluded from all persistence writers) | `fx_capture.{h,cpp}`, `container.cpp` (`FollowTick`, `CaptureTrackFxChain`) | ADR-070 |

---

## 15. Cross-platform status

| Platform | Architecture | Status | Notes |
|----------|-------------|--------|-------|
| macOS arm64 | Apple Silicon native | Stable | — |
| macOS x86_64 | Intel native (cross-compile + Rosetta on arm64 hosts) | Stable | — |
| Windows x64 | Native Win32 | Stable as of v2.0.1 | B10 closed — `WM_NCHITTEST` now publishes via `DWLP_MSGRESULT`; `DoCapture` style transform reordered; `GWL_EXSTYLE` strips plugin 3D frame; `DialogBoxParam` uses plugin HINSTANCE. |
| Linux x86_64 | GTK via SWELL | Stable as of v2.0.2 | B11 closed — FX Browser close crash no longer reproduces on REAPER 7.69 (upstream WDL `0072725b`). Validated on Ubuntu 24.04 aarch64. |
| Linux aarch64 | GTK via SWELL | Stable as of v2.0.2 | Same as Linux x86_64. |

Per ADR-017, the v2.0 release tag required B10 + B11 functional on all
platforms — no "alpha" labels. Closed by v2.0.1 (Windows) + v2.0.2 (Linux)
respectively. The CI matrix builds all five platforms on every PR push.

Architectural rules to keep cross-platform parity:

1. **No direct Cocoa or GTK calls in `cpp/src/*.cpp`.** Anything Cocoa lives
   in `swell_cocoa_helpers.{h,mm}` with a flat C++ interface; the rest of
   the codebase calls into those helpers without knowing about NSWindow /
   NSView.
2. **No direct Win32 calls.** Everything goes through `platform.h` shims.
   `GWL_USERDATA` (SWELL) vs `GWLP_USERDATA` (Win64) is bridged there.
3. **No `std::min` / `std::max`.** SWELL defines `min` / `max` macros that
   collide. Use `clamp_i` / `clamp_f` from `globals.h`.
4. **No `strncpy`.** Use `safe_strncpy` from `globals.h` — guarantees null
   termination.
5. **Resource files (`.rc`) go through the Perl resgen step** on non-Windows
   (ADR-023). New dialog = new `.rc` + add to `MAXPANE_RC_FILES` in
   `CMakeLists.txt` + `#include` the `.rc_mac_dlg` in your dialog `.cpp`.
   On Windows the `.rc` compiles directly via MSVC's `rc.exe` — only the
   widget syntax that overlaps SWELL's macro vocabulary is portable (see
   ADR-022).

### Known platform gaps (registered)

These are *documented stubs*, not silent ones — each is an inline
implementation (or explicit no-op) in `swell_cocoa_helpers.h` with a comment
explaining the gap and the revisit condition. The 2026-06 audit converted
the last silent stubs into either working implementations or registered gaps.

**Linux:**
- `SetWindowAlwaysOnTop` is a no-op — SWELL-generic exposes no portable
  topmost API, so the floating-mode "always on top" checkbox does nothing.
- Esc-cancel is unavailable during capture-by-click and drags — generic
  SWELL's `GetAsyncKeyState` tracks only mouse buttons + modifiers, never
  Escape. Right-click cancels instead, and `CAPTURE_CANCEL_HINT` (config.h)
  advertises the right gesture per platform.
- Dark-mode "Auto" follows GNOME/GTK only (`gsettings` `color-scheme` probe,
  cached per session); KDE and other desktops fall back to light. The manual
  Settings override always wins.
- No crosshair cursor during capture-by-click (`SetCaptureCursorActive` is
  a no-op). The hover outline and drag drop-zone frame DO exist since
  v2.2.0 via `overlay_frame.{h,cpp}` (ADR-060).
- Tooltip DPI scaling assumes 1.0 (`MaxPaneDpiScaleForDC`) — HiDPI Linux
  setups get unscaled tooltips (registered gap, ADR-068).
- Captured ReaImGui windows dragged by their background rely on the
  ~0.5 s CheckAlive snap-back — the Win32 move-veto subclass (ADR-067)
  has no SWELL-generic equivalent.

**Win32:**
- No crosshair cursor during capture-by-click (cursor is managed per
  `WM_SETCURSOR`). The hover outline and drag frame exist since v2.2.0
  (ADR-060). Esc-cancel works natively.

**macOS + Linux:**
- Maximize persistence for floating mode is Win32-only (SWELL has no
  `IsZoomed`); macOS/Linux restore size + position only (ADR-066).
- The hide-from-taskbar pref is Win32-only (`WS_EX_TOOLWINDOW`); no
  taskbar concept on macOS, no portable API on Linux (ADR-069).

---

## 16. Test infrastructure (ADR-018)

`cpp/tests/` holds Catch2 v2.13.10 single-header (vendored under
`tests/catch2/catch.hpp`) and these test TUs:

| Test TU | Covers |
|---------|--------|
| `test_split_tree.cpp` | `SplitTree::SplitLeaf` / `MergeNode` / `Recalculate` / `SaveSnapshot` / `LoadSnapshot` round-trip, paneId allocation, hit-testing. |
| `test_workspace_manager.cpp` | `WorkspaceManager::WriteTreeNodesStatic` / `ReadTreeNodesStatic` / `WritePaneTabsStatic` / `ReadPaneTabsStatic` round-trip via an in-memory `StateAccessor` stub. |
| `test_globals.cpp` | `safe_strncpy`, `clamp_i`, `clamp_f`, `ResolveActionCommand`. |
| `test_config.cpp` | ADR-052 toolbar title↔action mapping (`GetToolbarToggleAction` / `GetSearchTitleForAction`, incl. the "Toolbar 9 ≠ 41687" MIDI-toolbar pin), `ParseArbSpec`. |
| `test_action_list.cpp` | `ParseActionList` / `AppendUniqueAction` / `SerializeActionList` — truncation safety, overflow-guarded parsing, dedupe (2026-06 audit). |
| `test_state_persistence.cpp` | `ReadPaneTabsStatic` / `WritePaneTabsStatic` round-trip (`arb:` records incl. legacy + hostile inputs) and the `RppWrite → lines → RppRead` chunk path. |
| `test_stubs.cpp` | Test-only stubs for symbols pulled in by config.cpp (e.g. `IsSystemDarkMode`). |

Test target compiles with relaxed warnings (`-Wno-shadow -Wno-conversion`)
because Catch2 v2's single-header triggers many in our normal flags. The
production target keeps strict flags (`-Wall -Wextra -Wshadow -Wconversion`).

Run locally:
```bash
cd cpp/build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DMAXPANE_BUILD_TESTS=ON
cmake --build . --parallel
ctest --output-on-failure
# or direct invocation for verbose Catch2 output:
./tests/maxpane_tests
```

Currently 27 test cases (273 assertions) pass. The intent is to grow coverage
of pure-logic TUs (everything that doesn't require a live SWELL window or
REAPER process). Anything that needs `SetParent` / `EnumWindows` /
`DockWindowAddEx` belongs in the manual smoke checklist, not in
`maxpane_tests`.

CI matrix (`.github/workflows/ci.yml`, ADR-018) runs `cmake + build + ctest`
on five targets — macOS arm64, macOS x86_64, Linux x86_64, Linux aarch64, and
Windows x64 — for every push to `v2` or `main` and every PR, with the
`MAXPANE_WERROR` warning gate enabled (clang/gcc `-Werror`, MSVC `/W4 /WX`;
OFF by default locally so a stray new-toolchain warning can't brick a release
build). `fail-fast: false` so all five results land in the GitHub UI even if
one platform breaks.

---

## 17. Build & release pipeline

### Local development

```bash
git clone https://github.com/b451c/MaxPane.git
cd MaxPane
git clone https://github.com/justinfrankel/reaper-sdk.git cpp/sdk
git clone https://github.com/justinfrankel/WDL.git cpp/WDL

mkdir -p cpp/build && cd cpp/build
cmake .. -DCMAKE_BUILD_TYPE=Release    # or Debug for /tmp/maxpane_debug.log
cmake --build . --parallel

# macOS install
cp reaper_maxpane.dylib ~/Library/Application\ Support/REAPER/UserPlugins/
# Linux install
cp reaper_maxpane.so ~/.config/REAPER/UserPlugins/
# Windows install
copy Release\reaper_maxpane.dll "%APPDATA%\REAPER\UserPlugins\"
```

### Release artifacts

Built by `.github/workflows/build.yml` on push of a `v*` tag — runs `ctest`
before packaging (2026-06 audit: a tag can no longer ship binaries that never
ran the unit suite), then uploads all five binaries (`.dylib` arm64 + x86_64,
`.dll` x64, `.so` x86_64 + aarch64) to the GitHub Release. ReaPack picks
up the new version via `index.xml` once that file is updated on `main`.

CMake project version is the source of truth (`project(reaper_maxpane VERSION
2.0.0)`); bump it at the start of release prep so the binary metadata
matches the tag.

---

## 18. Glossary

| Term | Meaning |
|------|---------|
| **container** | One `MaxPaneContainer` instance. The dock-or-floating window that holds the pane grid. |
| **pane** | A `NODE_LEAF` in the SplitTree. Holds 0..N tabs. Has a stable `paneId`. |
| **tab** | One captured REAPER window inside a pane. `TabEntry` in `window_manager.h`. |
| **capture** | Reparent a REAPER window into a MaxPane pane. |
| **release** | Reparent a captured window back out + close it (or hide it, for workspace switches). |
| **known window** | One of the 15 entries in `KNOWN_WINDOWS` (config.cpp) — one-click capture, action ID baked in. |
| **arbitrary capture** | Anything not in `KNOWN_WINDOWS` — toolbars, ReaImGui scripts, third-party plugins. |
| **dock frame** | For ReaImGui scripts, the `"Title (docked)"` wrapper that REAPER creates around the inner script window. `FindReaperWindow` prefers it because it carries the rendered UI. |
| **workspace** | Named layout snapshot (`WorkspaceEntry` — tree + per-pane tab list). Stored in ExtState. |
| **instance** | One of up to 8 independent containers (0..7). Each has its own ExtState section, dock ident, RPP chunk tag. |
| **stale list** | `stale_toggle_actions` ExtState key. List of action IDs captured at session end; the next REAPER startup reads it and closes any windows REAPER restored from wnd_vis that we expect to be closed. |
| **SWELL** | WDL's macOS+Linux Win32 API shim. We treat SWELL and Win32 as one API surface via `platform.h`. |
| **Path A / B / C** | The three `SetParent` directions with distinct SWELL semantics (top→child / child→top / child→child). See §6.1. |
| **B*N* (B-fix)** | Bug fix tracked in `V2_PROGRESS.md`. e.g. B13 = "DoRelease reads toggle state AFTER SetParent(nullptr)". |
| **ADR-*N*** | Architecture Decision Record in `V2_DECISIONS.md`. Append-only log of non-trivial design choices. |

---

## 19. Reading order for new contributors

If you're new to the codebase, read in this order:

1. This document, §1–§3 (concept + module map).
2. `cpp/src/main.cpp` — see how REAPER plugs us in.
3. `cpp/src/container.h` — central class declaration; index of fields and
   methods.
4. `cpp/src/split_tree.h/cpp` — small, self-contained, easiest to reason
   about.
5. `cpp/src/window_manager.cpp` `DoCapture` + `DoRelease` — re-read §6 of
   this doc in parallel.
6. `cpp/src/state_accessor.h` + `cpp/src/workspace_manager.cpp` — see how
   the polymorphism flows.
7. Pick a feature to extend; consult its ADR in `docs/v2/V2_DECISIONS.md`.

For UI work, also read `cpp/src/container_paint.cpp` end-to-end and skim
`cpp/src/nav_bar.cpp` for paint conventions.

For cross-platform work, read `cpp/src/platform.h`, `cpp/src/swell_cocoa_
helpers.{h,mm}`, and the SWELL header `cpp/WDL/WDL/swell/swell.h`.
