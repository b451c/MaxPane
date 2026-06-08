#pragma once

#include "platform.h"

#include "reaper_plugin.h"
#include "state_limits.h"
#include "container.h"  // for MaxPaneContainer::MAX_INSTANCES

struct PendingProjectState {
  bool valid;
  bool reading;  // true while inside <MAXPANE_STATE[_N]> chunk
  char lines[RPP_MAX_LINES][RPP_MAX_LINE_LEN];
  int lineCount;
};

// Indexed by instance id. Slot 0 corresponds to legacy <MAXPANE_STATE>,
// slots 1..N-1 to <MAXPANE_STATE_1>..<MAXPANE_STATE_N-1>.
extern PendingProjectState g_pendingProjectState[MaxPaneContainer::MAX_INSTANCES];

// v2.1 (ADR-049) — true while a project-load open-poll is in flight (defined in
// main.cpp). The screenset LOAD path defers to it so a project restore wins.
extern bool g_projOpenTimerActive;

// Serialize a container's tree + pane tabs into a StateAccessor (KEY=VALUE).
// Shared by the RPP <MAXPANE_STATE> chunk path (OnSaveExtensionConfig) and the
// screenset SAVE_STATE path so the blob format has a single source of truth.
// Returns false on a corrupt tree (caller writes nothing). Defined in
// project_state.cpp.
class StateAccessor;  // fwd (state_accessor.h)
bool WriteContainerState(MaxPaneContainer& container, StateAccessor& acc);

// project_config_extension_t callbacks
bool OnProcessExtensionLine(const char* line, ProjectStateContext* ctx,
                            bool isUndo, project_config_extension_t* reg);
void OnSaveExtensionConfig(ProjectStateContext* ctx, bool isUndo,
                           project_config_extension_t* reg);
void OnBeginLoadProjectState(bool isUndo, project_config_extension_t* reg);
