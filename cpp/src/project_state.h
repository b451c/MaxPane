#pragma once

#include "platform.h"

#include "reaper_plugin.h"
#include "state_limits.h"

struct PendingProjectState {
  bool valid;
  bool reading;  // true while inside <MAXPANE_STATE> chunk
  char lines[RPP_MAX_LINES][RPP_MAX_LINE_LEN];
  int lineCount;
};

extern PendingProjectState g_pendingProjectState;

// project_config_extension_t callbacks
bool OnProcessExtensionLine(const char* line, ProjectStateContext* ctx,
                            bool isUndo, project_config_extension_t* reg);
void OnSaveExtensionConfig(ProjectStateContext* ctx, bool isUndo,
                           project_config_extension_t* reg);
void OnBeginLoadProjectState(bool isUndo, project_config_extension_t* reg);
