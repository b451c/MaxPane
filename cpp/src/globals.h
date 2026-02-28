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
