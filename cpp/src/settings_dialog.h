// settings_dialog.h — public API for the MaxPane Settings modal dialog
// (Sprint 3.1 / ADR-019). 4 tabs (General / Appearance / Hotkeys / Advanced)
// share a single SWELL Tab Control; DlgProc swaps visible control sets on
// tab change. All settings persist in ExtState (section "MaxPane_cpp"):
//   - auto_open       (existing, "0"/"1")
//   - dark_mode       ("auto" | "dark" | "light"; absent = "auto")
#pragma once
#include "platform.h"

// Opens the dialog modally rooted at `parent`. Blocks until OK/Cancel.
void OpenSettingsDialog(HWND parent);
