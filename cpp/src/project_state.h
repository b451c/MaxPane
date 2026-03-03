#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#ifndef SWELL_PROVIDED_BY_APP
#define SWELL_PROVIDED_BY_APP
#endif
#include "swell/swell.h"
#endif

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
