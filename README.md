# MaxPane

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Latest Release](https://img.shields.io/github/v/release/b451c/MaxPane)](https://github.com/b451c/MaxPane/releases/latest)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey.svg)](#platforms)
[![CI](https://github.com/b451c/MaxPane/actions/workflows/ci.yml/badge.svg)](https://github.com/b451c/MaxPane/actions/workflows/ci.yml)

**Nested docker layouts for REAPER.** A native C++ extension that captures any
REAPER window — Mixer, Media Explorer, FX Browser, toolbars, ReaImGui scripts,
AU / VST / JSFX plugin UIs — into a tiling workspace with resizable splits,
tabbed panes and one-click workspace recall. No scripting, no dependencies.

![MaxPane](docs/images/Hero.png)

Latest: **v2.5.0** — see [CHANGELOG.md](CHANGELOG.md) for release history.

## What it does

- **Split and tab** — split panes to any depth (up to 16 per container),
  drag splitters, drop several windows on one pane as tabs, drag tabs
  between panes. Solo a pane, apply one of 5 layout presets, or switch to
  **layout edit mode** and rearrange panes as cards without releasing a
  single window.
- **Capture anything** — drag a REAPER window from outside and drop it on a
  pane, or arm click-to-capture and click the window. Known windows are one
  click away in the menu; toolbars, third-party ReaImGui scripts and plugin
  UIs work too. Capture a whole track's FX chain in one go, or lock a pane
  to FX slot N of the selected track.
- **Workspaces** — 32 saved layouts with mini previews in the launcher, one
  hotkey each (or one "Workspace pickup" action for all), 32 favorites for
  single windows. Layout is also saved in the project file and round-trips
  through REAPER's Window sets.
- **Up to 8 containers** — each with its own layout, captured windows and
  floating geometry; workspaces and favorites are shared.
- **Floating or docked** — detach a container into its own window
  (multi-monitor position remembered, always-on-top toggle) or keep it in
  REAPER's docker.
- **Everything is an action** — open, split, merge in any direction, next
  tab / pane, solo, layout edit, Quick Switcher (fuzzy search across tabs,
  workspaces and favorites), Settings. Bindings fire even while a captured
  window has focus.
- **Looks the way you want** — navigation bar (collapsible), auto-hiding tab
  bars, clean mode, pane border and background colors, light / dark / auto.
- **Debug log for bug reports** — a Settings switch makes the public build
  write the same log the developer reads; "Show log file..." opens it.

The complete feature list, with every option and platform note, is in
[docs/FEATURES.md](docs/FEATURES.md).

## Screenshots

| Workspace launcher | Settings |
|:---:|:---:|
| ![Workspace launcher](docs/images/MaxPane-v2_launcher.png) | ![Settings dialog](docs/images/MaxPane-v2_settings.png) |

## Installation

### ReaPack (recommended)

1. In REAPER: **Extensions → ReaPack → Import repositories…**
2. Paste this URL:
   ```
   https://raw.githubusercontent.com/b451c/MaxPane/main/index.xml
   ```
3. **Extensions → ReaPack → Browse packages**, search for **MaxPane**,
   right-click → **Install**, restart REAPER.

ReaPack notifies you of updates automatically.

### Manual install

Download the binary for your platform from the
[Releases](../../releases) page and copy it into REAPER's `UserPlugins`
folder, then restart REAPER:

| Platform | File | Folder |
|---|---|---|
| macOS Apple Silicon | `reaper_maxpane-arm64.dylib` | `~/Library/Application Support/REAPER/UserPlugins/` |
| macOS Intel | `reaper_maxpane-x86_64.dylib` | same as above |
| Windows x64 | `reaper_maxpane-x64.dll` | `%APPDATA%\REAPER\UserPlugins\` |
| Linux x86_64 / aarch64 | `reaper_maxpane-x86_64.so` / `reaper_maxpane-aarch64.so` | `~/.config/REAPER/UserPlugins/` |

## Quick start

1. Run the action **MaxPane: Open Container** (Actions list; bind a key if
   you like).
2. Click the **drag** button on the navigation bar and drag any REAPER
   window into a pane — or right-click a pane and pick a window from the
   menu.
3. Split panes from the pane menu (or by dropping a window on a pane edge),
   drop more windows on a pane to get tabs.
4. Click **Save** on the navigation bar and name the workspace. It shows up
   as a card in the launcher; **Home** overlays the launcher on top of any
   layout.
5. Bind hotkeys in REAPER's Actions dialog: `MaxPane: Workspace Slot N`,
   or `MaxPane: Workspace pickup` for all slots with one key.

## Platforms

Requires REAPER 7.0 or newer.

| Platform | Status | Notes |
|---|---|---|
| macOS arm64 + x86_64 | Primary | Every release runs an automated regression suite plus live sessions with real-world plugin suites. |
| Windows x64 | Supported | CI-built; Windows-specific changes are verified live on Windows 11. |
| Linux x86_64 + aarch64 | Supported | CI-built and unit-tested on Ubuntu; runtime reports welcome. |

## Known limitations

- **Plugin tabs restore only in a project that contains those tracks and
  FX** — plugin identities are track-bound by design. Open the project
  first, then load the workspace.
- **A plugin GUI larger than its pane crops at the pane edge on macOS**
  (REAPER has no FX-window scrollbars there) — use the pane menu's
  "Fit Pane to Window"; Windows shows REAPER's native scrollbars.
- **Windows / Linux: a ReaImGui window docked in REAPER's docker must be
  undocked before capture.** Pulling it out of the docker would crash
  REAPER, so MaxPane refuses with a message; drag the script's tab out of
  the docker once, capture it floating, save the workspace.
- **Linux: GL-rendered ReaImGui windows need ReaImGui's "Disable hardware
  acceleration" preference** to be captured (restart the script after
  enabling it).
- **macOS: ReaImGui scripts that enforce a minimum window size** (e.g. TK
  Patchbay) can resize REAPER's main window when captured into a smaller
  pane — keep the pane large enough or dock them natively.
- **A docked MaxPane on a second monitor may come back on the main monitor
  after restart** — REAPER manages docked-window positions; floating mode
  remembers its monitor.

The full list, including the smaller platform-specific ones, is in
[docs/FEATURES.md](docs/FEATURES.md#known-limitations-complete-list).

## Building from source

```bash
git clone https://github.com/b451c/MaxPane.git
cd MaxPane
git clone https://github.com/justinfrankel/reaper-sdk.git cpp/sdk
git clone https://github.com/justinfrankel/WDL.git cpp/WDL
mkdir -p cpp/build && cd cpp/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

Copy the resulting `reaper_maxpane.{dylib,so,dll}` into the `UserPlugins`
folder above. Workflow and conventions: [CONTRIBUTING.md](CONTRIBUTING.md);
design and module map: [ARCHITECTURE.md](ARCHITECTURE.md). CI runs the full
5-platform matrix on every PR.

## Support development

MaxPane is MIT-licensed and free. If it saves you time:
[Ko-fi](https://ko-fi.com/quickmd) ·
[Buy Me a Coffee](https://buymeacoffee.com/bsroczynskh) ·
[PayPal](https://paypal.me/b451c)

## License and links

[MIT](LICENSE) — Copyright (c) 2025–2026 b451c

- Forum thread — https://forum.cockos.com/showthread.php?t=307267
- Bug reports and requests — https://github.com/b451c/MaxPane/issues
  (with v2.5.0, please attach the debug log: Settings → Write debug log →
  Show log file...)
- REAPER — https://www.reaper.fm · ReaPack — https://reapack.com
- REAPER SDK — https://github.com/justinfrankel/reaper-sdk ·
  WDL / SWELL — https://github.com/justinfrankel/WDL

Made by [falami.studio](https://falami.studio/lab/maxpane/) — audio
production & engineering studio.
