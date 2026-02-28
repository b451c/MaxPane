#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#ifndef SWELL_PROVIDED_BY_APP
#define SWELL_PROVIDED_BY_APP
#endif
#include "swell/swell.h"
#endif

// Global REAPER API function pointers (defined in globals.cpp)
extern void (*g_DockWindowAddEx)(HWND hwnd, const char* name, const char* identstr, bool allowShow);
extern void (*g_DockWindowRemove)(HWND);
extern void (*g_ShowConsoleMsg)(const char*);
extern void (*g_Main_OnCommand)(int, int);
extern const char* (*g_GetExtState)(const char*, const char*);
extern void (*g_SetExtState)(const char*, const char*, const char*, bool);
extern HWND g_reaperMainHwnd;
extern int (*g_plugin_register)(const char*, void*);
extern bool (*g_GetUserInputs)(const char*, int, const char*, char*, int);
extern int (*g_GetToggleCommandState)(int);
extern int (*g_NamedCommandLookup)(const char*);
extern const char* (*g_ReverseNamedCommandLookup)(int);

// Resolve action command string to numeric ID (handles both "_RSxxx" and "12345")
inline int ResolveActionCommand(const char* cmd)
{
  if (!cmd || !cmd[0]) return 0;
  if (cmd[0] == '_' && g_NamedCommandLookup) {
    return g_NamedCommandLookup(cmd);
  }
  return atoi(cmd);
}

// Get stable command string for an action ID (returns named ID for custom actions, numeric string for built-in)
inline const char* GetActionCommandString(int actionId, char* buf, int bufSize)
{
  if (actionId <= 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }
  if (g_ReverseNamedCommandLookup) {
    const char* named = g_ReverseNamedCommandLookup(actionId);
    if (named && named[0]) {
      snprintf(buf, bufSize, "%s", named);
      return buf;
    }
  }
  snprintf(buf, bufSize, "%d", actionId);
  return buf;
}

// Auto-open helpers — canonical logic in one place
// Default is ON unless explicitly set to "0"
inline bool IsAutoOpenEnabled()
{
  if (!g_GetExtState) return true;
  const char* val = g_GetExtState("ReDockIt_cpp", "auto_open");
  if (val && val[0] == '0') return false;
  return true;
}

inline void SetAutoOpenEnabled(bool enabled)
{
  if (g_SetExtState) {
    g_SetExtState("ReDockIt_cpp", "auto_open", enabled ? "1" : "0", true);
  }
}
