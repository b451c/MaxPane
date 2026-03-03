# Contributing to MaxPane

Thanks for your interest in contributing to MaxPane!

## Getting Started

### Prerequisites

- C++17 compiler (Clang on macOS, GCC on Linux, MSVC on Windows)
- CMake 3.15+
- REAPER 7.0+ for testing

### Building

```bash
# Clone dependencies
git clone https://github.com/justinfrankel/reaper-sdk.git cpp/sdk
git clone https://github.com/justinfrankel/WDL.git cpp/WDL

# Build
cd cpp/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make

# Install
cp reaper_maxpane.dylib ~/Library/Application\ Support/REAPER/UserPlugins/
```

Debug builds log to `/tmp/maxpane_debug.log`.

## Code Style

- C++17, no exceptions, no RTTI
- SWELL API for cross-platform windowing (no direct Win32 or Cocoa calls in core code)
- Use `safe_strncpy` instead of `strncpy`
- Use `clamp_i`/`clamp_f` instead of `std::min`/`std::max` (SWELL macro conflicts)
- Named constants in `config.h` — no magic numbers
- Compile with zero warnings (`-Wall -Wextra -Wshadow -Wconversion`)

## Pull Request Process

1. Fork the repository and create a feature branch (`git checkout -b feature/my-feature`)
2. Make your changes and ensure the build compiles with zero warnings
3. Test in REAPER — verify the extension loads, basic capture/release/split works
4. Commit with a descriptive message
5. Push and open a Pull Request against `main`

## Reporting Bugs

Use the [Bug Report](https://github.com/b451c/MaxPane/issues/new?template=bug_report.md) issue template. Include:

- REAPER version and OS
- Steps to reproduce
- Expected vs actual behavior
- Debug log output if available (`/tmp/maxpane_debug.log` from Debug builds)

## Feature Requests

Use the [Feature Request](https://github.com/b451c/MaxPane/issues/new?template=feature_request.md) issue template. Describe the use case and how it fits into the existing workflow.
