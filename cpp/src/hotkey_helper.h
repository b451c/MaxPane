#pragma once
#include "platform.h"

// v2.0.4 #2 — hotkey binding helpers (ADR-038).
//
// These wrap REAPER's native DoActionShortcutDialog so right-click "Bind
// hotkey" entries throughout MaxPane can open a keystroke-capture modal
// scoped to ONE specific command ID — bypassing the 200k-action hunt that
// the legacy "open full Actions dialog" callsite imposed on users.
//
// REAPER handles capture, conflict detection, "already bound to X — replace?"
// dialog, and reaper-kb.ini write. We just pass the cmdID through.
//
// Implementation lives in main.cpp where the slot tables (g_cmdWsSlot[],
// g_cmdFavSlot[]) are file-local statics.

// Open REAPER's shortcut-edit dialog for an arbitrary command ID. If the cmd
// already has a shortcut, opens to edit the first one; otherwise adds a new
// shortcut. Falls back to the full Actions dialog (40605) if the SDK
// pointers aren't available (very old REAPER, defensive).
void MaxPane_OpenHotkeyDialogForCmd(HWND parent, int cmdID);

// Convenience wrapper that resolves the workspace slot index (0..MAX_WORKSPACES-1)
// to its registered command ID via the file-static g_cmdWsSlot table.
void MaxPane_OpenHotkeyDialogForWsSlot(HWND parent, int slotIdx);

// Convenience wrapper for favorite slots (future use: favorite slot context
// menu doesn't expose a "Bind hotkey" entry in v2.0.4 #2 cut, but the helper
// is wired so when that menu lands it can call this directly).
void MaxPane_OpenHotkeyDialogForFavSlot(HWND parent, int slotIdx);
