// Quick Switcher modal dialog (F4, ADR-024 era).
//
// Cmd+P / Ctrl+P → modal SWELL dialog with EDIT search box + LISTBOX of
// fuzzy-filtered entries drawn from:
//   - All captured tabs across all MaxPane instances.
//   - All saved workspaces (shared list per ADR-012).
//   - All favorites (shared per ADR-009).
//
// Activate (Enter or double-click):
//   - Tab → focus the owning instance, set active tab.
//   - Workspace → load it into the focused instance.
//   - Favorite → capture into the focused pane.
//
// No default hotkey: the action `MaxPane_QuickSwitcher` is registered and
// the user binds whichever combo their platform prefers (REAPER doesn't
// translate Cmd↔Ctrl per OS).
#pragma once
#include "platform.h"

void OpenQuickSwitcher(HWND parent);
