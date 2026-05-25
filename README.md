# MaxPane

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Latest Release](https://img.shields.io/github/v/release/b451c/MaxPane)](https://github.com/b451c/MaxPane/releases/latest)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-lightgrey.svg)](#requirements)
[![CI](https://github.com/b451c/MaxPane/actions/workflows/ci.yml/badge.svg)](https://github.com/b451c/MaxPane/actions/workflows/ci.yml)

**Nested docker layouts for REAPER** — a native C++ extension that turns any
REAPER window into a tile inside a multi-pane workspace. Up to 8 independent
containers, drag-to-dock from outside, 32 saved workspaces, hotkeys for
everything, Quick Switcher (Cmd+P-style), floating mode, and a launcher that
remembers your layouts.

![MaxPane v2.0](docs/images/MaxPane-v2_launcher.png)

> Capture any REAPER window — Media Explorer, FX Browser, Mixer, Actions,
> toolbars, or third-party ReaImGui scripts — into a tiling container with
> resizable splits, tabbed panes, and one-click workspace recall.

---

## What's new in 2.0

- **Drag-to-dock** — grab any REAPER window from outside MaxPane and drop
  it on a pane. Live preview shows the four split zones, the tab-bar
  zone, and the body-center forgiving zone. Shift+drop replaces the
  active tab.
- **Up to 8 MaxPane containers** in one REAPER session — independent
  layouts, captured windows, and project state. Open via
  `MaxPane: Open Container`, `MaxPane: Open Container 2`, … through 8.
- **Floating mode** — detach the whole container into a top-level
  window with native chrome. Multi-monitor positions remembered.
  Always-on-top toggle for keyboard-monitor setups.
- **Quick Switcher** — bind one hotkey. Type a few characters. Fuzzy-
  match across every open tab (across every instance), every saved
  workspace, every favorite. Enter activates.
- **32 workspace slots + 32 favorite slots** — bind hotkeys to any of
  them via REAPER's Actions dialog. Or use the single "Workspace
  pickup" action to reach all 32 from one binding.
- **Persistent navigation bar** at the top of every container.
  Home, Drag-to-dock, Quick Switch, Save workspace, Load▾, Settings,
  Support — actions that used to be buried in the right-click menu
  surfaced as a clean toolbar.
- **Workspace launcher** — when a container is empty, you see a card
  grid of saved workspaces with mini layout previews. One click loads.
  Right-click for Rename / Duplicate / Delete / Bind Hotkey.
- **C-series UX wins** — Reopen last closed tab (Cmd+Shift+T style),
  pinned tabs (sticky, sort to left, exempt from bulk close), tab
  Close-Others / Close-to-Right / Close-All, always-on-top floating.
- **Settings dialog** — auto-open, show nav bar, dark-mode override,
  default workspace, support links, all in one place.
- **Toast bar + custom Save dialog** — non-fatal feedback ("Workspace
  saved as 'Edit Mode'") via a 3-second toast strip; rename / duplicate
  collisions surface clearly instead of failing silently.
- **Reliability** — close-leak bugs that haunted v1.5.x are gone. The
  close mechanism uses `WM_CLOSE` as primary, action-toggle as fallback,
  startup ghost cleanup as safety net (see
  [ARCHITECTURE.md](ARCHITECTURE.md) for the full story).

Full per-bug detail in [CHANGELOG.md](CHANGELOG.md).

---

## Features

### Layout
- **Flexible split layouts** — split panes horizontally or vertically to any
  depth (up to 16 panes). Drag splitter bars to resize on the fly. Double-
  click any splitter to reset to 50/50.
- **Tabbed panes** — multiple windows per pane with a tab bar. Drag tabs
  between panes, reorder within a pane. Each tab bar has a **▼ menu** for
  pane operations.
- **Solo / maximize** — temporarily expand any pane to fill the entire
  container; full tree restored on exit.
- **5 layout presets** — Two Columns, Left + Right Split, Three Columns,
  2x2 Grid, Top + Bottom Split.
- **Pinned tabs** — sticky tabs sorted to the left of each pane, exempt
  from "Close Others" / "Close All".
- **Tab colors** — color-code tabs with 8 palette colors.

### Capture
- **15 known REAPER windows** — one-click capture for Mixer, Track
  Manager, Routing Matrix, Media Explorer, FX Browser, Project Bay,
  Region Manager, Region Render Matrix, Actions, Undo History,
  Navigator, Big Clock, Video, Performance Meter, Virtual MIDI
  Keyboard.
- **Arbitrary window capture** — grab any visible REAPER window
  (toolbars, third-party ReaImGui scripts like ReaBeat / ReaMD /
  reamix.me) via the "Open Windows" submenu, click-to-capture mode, or
  drag-to-dock.
- **Favorites** — pin frequently-used windows for instant access from any
  container's menu.

### Workspaces
- **32 saved workspace slots** — tree layout + captured windows snapshot.
  One click in the launcher or one hotkey binding to recall.
- **Per-project state** — layout is saved inside each `.RPP` project file
  via REAPER's `project_config_extension_t`, so different projects can
  have different MaxPane configurations and they load with the project.
- **Custom Save dialog** — name input + clickable list of existing
  workspaces + dynamic status label so you know whether you're creating
  new or replacing.

### Multi-instance
- **Up to 8 independent containers** per session. Each instance has its
  own layout, captured windows, floating geometry, and per-project
  state. Workspaces and favorites are shared across instances.

### Navigation + hotkeys
- **Quick Switcher** — fuzzy-match across open tabs / workspaces /
  favorites. Bind your own hotkey.
- **`MaxPane: Workspace pickup`** — single hotkey, prompt for slot
  number, load. One binding reaches all 32 slots.
- **`MaxPane: Reopen last closed tab`** — 16-entry per-container ring
  buffer; session-scoped.
- **Tab + pane keyboard nav** — Next/Prev Tab, Next/Prev Pane, Solo
  Toggle, all bindable in REAPER's Actions dialog.
- **Accelerator hook** — MaxPane action bindings fire even when MaxPane
  (or a captured pane) has focus — v1.x required REAPER's main window
  to have focus first.

### Platform
- **macOS arm64 + x86_64** — stable, fully functional.
- **Windows x64** — stable as of v2.0.1 (closes [#8](https://github.com/b451c/MaxPane/issues/8) Windows mouse input).
- Linux build pending smoke testing on the open blocker
  [#9](https://github.com/b451c/MaxPane/issues/9) (Linux FX Browser close).
- **Zero scripting / no dependencies** — pure C++ extension using REAPER
  SDK + WDL/SWELL. No `js_ReaScriptAPI`, no ReaImGui, no Lua.

---

## Screenshots

| Launcher hero | Settings dialog |
|:---:|:---:|
| ![Launcher hero](docs/images/MaxPane-v2_launcher.png) | ![Settings dialog](docs/images/MaxPane-v2_settings.png) |

| Create grid layout | Assign windows to panes | Recall workspace |
|:---:|:---:|:---:|
| ![Create grid](docs/images/create-grid.gif) | ![Assign windows](docs/images/assign-windows.gif) | ![Recall workspace](docs/images/workspace-recall.gif) |

---

## Installation

### ReaPack (recommended)

1. In REAPER, go to **Extensions → ReaPack → Import repositories…**
2. Paste this URL:
   ```
   https://raw.githubusercontent.com/b451c/MaxPane/main/index.xml
   ```
3. Go to **Extensions → ReaPack → Browse packages**, search for **MaxPane**.
4. Right-click → **Install**, then restart REAPER.

ReaPack will notify you of future updates automatically.

### Manual install

1. Download the macOS binary from the [Releases](../../releases) page
   (`reaper_maxpane-arm64.dylib` for Apple Silicon,
   `reaper_maxpane-x86_64.dylib` for Intel).
2. Copy it to `~/Library/Application Support/REAPER/UserPlugins/`.
3. Restart REAPER.
4. Open via **Actions → MaxPane: Open Container**, or assign a keyboard
   shortcut.

---

## Quick start

1. **Open MaxPane** — run the action `MaxPane: Open Container` from
   REAPER's Actions menu.
2. **First time?** The launcher card grid is empty. Click any **▼ menu**
   button on a pane tab bar, or right-click any pane, to capture your
   first window. Or click `[Drag]` on the nav bar and drag a REAPER
   window from outside into the pane.
3. **Split panes** via the pane menu (Split Left/Right or Split
   Top/Bottom) or via drag-to-dock with an edge zone.
4. **Tabbed windows** — drop multiple windows on the same pane; click
   tabs to switch, drag tabs between panes to rearrange.
5. **Save a workspace** — click `[Save]` on the nav bar (or right-click
   → Save Workspace…). Name it; it shows up in the launcher next time.
6. **Recall a workspace** — when a container is empty, click its card.
   Or click `[Home]` to overlay the picker on top of your current layout
   without disturbing it.
7. **Bind a hotkey** — in REAPER's Actions dialog, search for
   `MaxPane: Workspace Slot 1` (etc.) or
   `MaxPane: Workspace pickup` (single hotkey for all 32 slots).

---

## Building from source

See [CONTRIBUTING.md](CONTRIBUTING.md) for the full workflow. Quick path:

```bash
git clone https://github.com/b451c/MaxPane.git
cd MaxPane
git clone https://github.com/justinfrankel/reaper-sdk.git cpp/sdk
git clone https://github.com/justinfrankel/WDL.git cpp/WDL

mkdir -p cpp/build && cd cpp/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

Then copy the resulting `reaper_maxpane.{dylib,so,dll}` to your REAPER
UserPlugins directory (paths above).

Architecture, module map, the three SetParent paths, the
close-mechanism deep-dive, and the v2.0 feature surface — see
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Requirements

- **REAPER** 7.0+ (tested on 7.62 and 7.73)
- **macOS** arm64 (Apple Silicon) and x86_64 (Intel) — **stable**
- **Windows** x64 — **stable** as of v2.0.1

The Linux build will be published once it passes smoke testing on the
open blocker [#9](https://github.com/b451c/MaxPane/issues/9) (FX Browser
close crash).

---

## Contributing

Contributions welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) and
[ARCHITECTURE.md](ARCHITECTURE.md) first. CI runs the full 5-platform
matrix on every PR — your change has to be green there before review.

---

## Support development

MaxPane is MIT-licensed and free. If it saves you time, please support
its development:

- [Ko-fi](https://ko-fi.com/quickmd)
- [Buy Me a Coffee](https://buymeacoffee.com/bsroczynskh)
- [PayPal](https://paypal.me/b451c)

---

## License

[MIT](LICENSE) — Copyright (c) 2025–2026 b451c

## Links

- **Forum thread** — https://forum.cockos.com/showthread.php?t=307267
- **REAPER** — https://www.reaper.fm
- **ReaPack** — https://reapack.com
- **REAPER SDK** — https://github.com/justinfrankel/reaper-sdk
- **WDL / SWELL** — https://github.com/justinfrankel/WDL
