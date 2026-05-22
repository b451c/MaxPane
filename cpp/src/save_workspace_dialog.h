// save_workspace_dialog.h — public API for the Save Workspace modal dialog
// (Sprint 3.3 / ADR-020). The dialog presents a name input, a clickable
// listbox of existing workspaces (selecting one populates the name field),
// and a dynamic status label indicating whether OK will create a new
// workspace or replace an existing one.
#pragma once
#include "platform.h"

class WorkspaceManager;

// Opens the dialog modally rooted at `parent`. On user confirm (OK), writes
// the chosen name into `outName` (clamped to `outNameSize`) and returns
// true. Returns false on Cancel / Close / empty input. Caller passes
// `outName` to WorkspaceManager::Save which handles new vs replace
// internally (matching by name).
bool OpenSaveWorkspaceDialog(HWND parent, const WorkspaceManager& wsMgr,
                             char* outName, int outNameSize);
