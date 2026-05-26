# Contributing to MaxPane

Thanks for your interest in contributing to MaxPane! For an architectural
overview of the codebase — module map, layering, window-reparenting flow,
state-persistence model — read [ARCHITECTURE.md](ARCHITECTURE.md) first.
This document covers the practical workflow: how to build, test, and submit
a change.

## Getting Started

### Prerequisites

- **C++17 compiler** — Clang on macOS, GCC on Linux, MSVC 2022 on Windows
- **CMake** 3.15+
- **Perl** — required on macOS/Linux for the SWELL resgen step (ADR-023).
  Preinstalled on macOS (`/usr/bin/perl`) and every major Linux distro;
  also available on every GitHub Actions runner image. Windows builds do
  not need Perl — MSVC's `rc.exe` consumes the `.rc` files directly.
- **REAPER** 7.0+ for smoke testing

### Building

```bash
# Clone external dependencies (gitignored — not vendored)
git clone https://github.com/justinfrankel/reaper-sdk.git cpp/sdk
git clone https://github.com/justinfrankel/WDL.git cpp/WDL

# Configure + build
mkdir -p cpp/build && cd cpp/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --parallel

# Install (macOS)
cp reaper_maxpane.dylib ~/Library/Application\ Support/REAPER/UserPlugins/
# Install (Linux)
cp reaper_maxpane.so ~/.config/REAPER/UserPlugins/
# Install (Windows)
copy Release\reaper_maxpane.dll "%APPDATA%\REAPER\UserPlugins\"
```

Debug builds log to `/tmp/maxpane_debug.log` via the `DBG(...)` macro
(`cpp/src/debug.h`). Release builds compile it out to a no-op.

### Running tests

Unit tests live under `cpp/tests/` (ADR-018). They're built by default;
opt out with `-DMAXPANE_BUILD_TESTS=OFF` if you need to.

```bash
cd cpp/build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DMAXPANE_BUILD_TESTS=ON
cmake --build . --parallel

# Run the suite via CTest
ctest --output-on-failure
# Or invoke the test binary directly for verbose Catch2 output
./tests/maxpane_tests
```

Tests target pure-logic translation units (`SplitTree`, `WorkspaceManager`
static helpers, `safe_strncpy`, `ResolveActionCommand`) — no SWELL window
calls, no live REAPER process. Anything that needs `SetParent` or
`EnumWindows` belongs on the manual smoke checklist, not in `maxpane_tests`.

## Code Style

- **C++17.** No exceptions, no RTTI.
- **Cross-platform first.** Every change must build on all four CI targets
  (macOS arm64, macOS x86_64, Ubuntu, Windows). See ADR-017 in
  `docs/v2/V2_DECISIONS.md` — multiplatform parity is a v2.0 hard
  requirement, not a "nice to have". The CI matrix enforces this.
- **No direct Cocoa, GTK, or native Win32 calls in `cpp/src/*.cpp`.** Cocoa
  lives in `cpp/src/swell_cocoa_helpers.{h,mm}` behind a flat C++ interface.
  Win32-vs-SWELL differences live in `cpp/src/platform.h`. Read both before
  reaching for an `#ifdef`.
- **`safe_strncpy` not `strncpy`** — guarantees null termination.
  Defined in `globals.h`.
- **`clamp_i` / `clamp_f` not `std::min` / `std::max`** — SWELL defines
  `min` / `max` macros that collide with the STL.
- **Named constants in `config.h`** — no magic numbers.
- **Strict warnings.** Production target compiles with
  `-Wall -Wextra -Wshadow -Wconversion` and **zero warnings**. CI rejects
  anything that introduces one.
- **No emojis in code or commits** unless an owner asks for them.
- **Comments explain *why*, not *what*.** Well-named identifiers cover the
  what. Reserve comments for hidden constraints, subtle invariants,
  workarounds for a specific bug, or behavior that would surprise a reader.

### Adding a new SWELL dialog

`.rc` files in `cpp/resources/` are converted to `.rc_mac_dlg` /
`.rc_mac_menu` by `swell_resgen.pl` on macOS/Linux builds, and consumed
directly by MSVC `rc.exe` on Windows (ADR-023). To add a new dialog:

1. Write the `.rc` using SWELL's macro vocabulary
   (`DIALOG`, `CHECKBOX`, `PUSHBUTTON`, `LISTBOX`, `LTEXT`, `EDITTEXT`,
   `GROUPBOX`, `COMBOBOX`, `ICON`). Avoid `CONTROL` with a custom class
   string — the argument order differs between SWELL and Win32 `rc.exe`
   (ADR-022 explains the pitfall).
2. Add the file to `MAXPANE_RC_FILES` in `cpp/CMakeLists.txt` (non-Windows
   branch) **and** to the Windows branch's `target_sources` block.
3. In your dialog `.cpp`, `#include` the generated `.rc_mac_dlg` at TU
   scope (non-Windows only) — copy the pattern from
   `settings_dialog.cpp` or `save_workspace_dialog.cpp`.

## Pull Request Process

1. Fork the repository and create a feature branch
   (`git checkout -b feature/my-feature`).
2. Make your changes. Build locally and confirm:
   - Zero warnings on `-Wall -Wextra -Wshadow -Wconversion`.
   - `ctest` is green.
   - The extension loads in a real REAPER session and the change behaves
     as advertised.
3. Push your branch. CI fires automatically on push — verify the
   5-platform matrix (macOS arm64 + macOS x86_64 + Win64 + Linux x86_64
   + Linux aarch64) is green before opening a PR.
4. Open a Pull Request against `main`. In the description, link to any
   open issues, describe what the change does and why, and list the
   manual smoke steps you ran.
5. If your change touches an architectural decision (storage format,
   per-instance vs shared state, close-mechanism, etc.) — propose an ADR
   in the PR description rather than committing one alongside your code
   changes. ADRs in `docs/v2/V2_DECISIONS.md` are append-only and live in
   a gitignored directory locally; the owner adds them after PR
   discussion settles.

### What gets a green light fast

- A bug fix with a clear repro, a minimal diff, and a manual-test note.
- A feature change that already has an ADR or a `+1` from the owner on a
  forum thread / GitHub issue.
- A cross-platform improvement (Win32 / Linux fix that doesn't regress
  macOS).

### What needs more discussion before code

- Storage format changes (ExtState keys, RPP chunk schema). Backward
  compatibility with v1.5.x data is a hard requirement — see ADR-003.
- New architectural patterns (a new `StateAccessor` subclass, a new
  module under `cpp/src/`, a new external dependency).
- Anything that breaks the cross-platform parity invariants in
  ADR-017.

## Reporting Bugs

Use the [Bug Report](https://github.com/b451c/MaxPane/issues/new?template=bug_report.md)
issue template. Include:

- REAPER version and OS (please specify macOS arm64 vs x86_64; Windows 10
  vs 11; Linux distro + version).
- Steps to reproduce.
- Expected vs actual behavior.
- Debug log output from `/tmp/maxpane_debug.log` if you can produce a Debug
  build. If not, a screen recording is the next best thing.

## Feature Requests

Use the
[Feature Request](https://github.com/b451c/MaxPane/issues/new?template=feature_request.md)
issue template. Describe the use case and how it fits into the existing
workflow. A clear "I'm trying to do X, currently I have to do Y, this
change would let me do Z" beats a feature-list-item phrasing.

## Code of Conduct

By contributing, you agree to abide by the project's
[Code of Conduct](CODE_OF_CONDUCT.md).
