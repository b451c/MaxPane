#pragma once

#include "platform.h"

#include <cstring>
#include <cstdlib>
#include <climits>
#include <cstdio>

// Global REAPER API function pointers (defined in globals.cpp)
extern void (*g_DockWindowAddEx)(HWND hwnd, const char* name, const char* identstr, bool allowShow);
extern void (*g_DockWindowRemove)(HWND);
extern void (*g_Main_OnCommand)(int, int);
extern const char* (*g_GetExtState)(const char*, const char*);
extern void (*g_SetExtState)(const char*, const char*, const char*, bool);
extern HWND g_reaperMainHwnd;
extern int (*g_plugin_register)(const char*, void*);
extern bool (*g_GetUserInputs)(const char*, int, const char*, char*, int);
extern int (*g_GetToggleCommandState)(int);
extern int (*g_NamedCommandLookup)(const char*);
extern const char* (*g_ReverseNamedCommandLookup)(int);

// Per-project state API
class ReaProject;  // forward declaration (defined in reaper_plugin.h)
extern ReaProject* (*g_EnumProjects)(int idx, char* projfnOut, int projfnOut_sz);
extern int (*g_GetProjExtState)(ReaProject* proj, const char* extname, const char* key,
                                 char* valOut, int valOut_sz);
extern int (*g_SetProjExtState)(ReaProject* proj, const char* extname, const char* key,
                                 const char* value);
extern void (*g_MarkProjectDirty)(ReaProject* proj);

// Safe string copy: always null-terminates, handles null src
inline void safe_strncpy(char* dst, const char* src, size_t dst_size)
{
  if (!dst || dst_size == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

// Safe integer parsing with clamping (replaces raw atoi)
inline int safe_atoi_clamped(const char* s, int minVal, int maxVal)
{
  if (!s || !s[0]) return minVal;
  char* endptr = nullptr;
  long v = strtol(s, &endptr, 10);
  if (endptr == s) return minVal;  // no digits parsed
  if (v < (long)minVal) return minVal;
  if (v > (long)maxVal) return maxVal;
  return (int)v;
}

// Safe float parsing with clamping (replaces raw atof)
inline float safe_atof_clamped(const char* s, float minVal, float maxVal)
{
  if (!s || !s[0]) return minVal;
  char* endptr = nullptr;
  double v = strtod(s, &endptr);
  if (endptr == s) return minVal;  // no digits parsed
  if (v < (double)minVal) return minVal;
  if (v > (double)maxVal) return maxVal;
  return (float)v;
}

// Resolve action command string to numeric ID (handles both "_RSxxx" and "12345")
inline int ResolveActionCommand(const char* cmd)
{
  if (!cmd || !cmd[0]) return 0;
  if (cmd[0] == '_' && g_NamedCommandLookup) {
    return g_NamedCommandLookup(cmd);
  }
  char* endptr = nullptr;
  long v = strtol(cmd, &endptr, 10);
  if (endptr == cmd || v <= 0 || v > INT_MAX) return 0;
  return (int)v;
}

// Get stable command string for an action ID (returns named ID for custom actions, numeric string for built-in)
inline const char* GetActionCommandString(int actionId, char* buf, int bufSize)
{
  if (!buf || bufSize <= 0) return "";
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
  const char* val = g_GetExtState("MaxPane_cpp", "auto_open");
  if (val && val[0] == '0') return false;
  return true;
}

inline void SetAutoOpenEnabled(bool enabled)
{
  if (g_SetExtState) {
    g_SetExtState("MaxPane_cpp", "auto_open", enabled ? "1" : "0", true);
  }
}
