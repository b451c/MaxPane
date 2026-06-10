#pragma once

#include "platform.h"

#include <cstring>
#include <cstdlib>
#include <climits>
#include <cstdio>

// Global REAPER API function pointers (defined in globals.cpp)
extern void (*g_DockWindowAddEx)(HWND hwnd, const char* name, const char* identstr, bool allowShow);
extern void (*g_DockWindowRemove)(HWND);
// v2.1 — screenset integration (ADR-049). Callback typed as void* to keep this
// header free of the SDK screensetNewCallbackFunc typedef (cast at assignment,
// same pattern as the hotkey-dialog pointers below).
extern void (*g_screenset_registerNew)(char* id, void* callbackFunc, void* param);
extern void (*g_screenset_unregister)(char* id);
extern int  (*g_DockIsChildOfDock)(HWND hwnd, bool* isFloatingDockerOut);
extern void (*g_Main_OnCommand)(int, int);
extern const char* (*g_GetExtState)(const char*, const char*);
extern void (*g_SetExtState)(const char*, const char*, const char*, bool);
extern HWND g_reaperMainHwnd;
extern int (*g_plugin_register)(const char*, void*);
extern bool (*g_GetUserInputs)(const char*, int, const char*, char*, int);
extern int (*g_GetToggleCommandState)(int);
extern int (*g_NamedCommandLookup)(const char*);
extern const char* (*g_ReverseNamedCommandLookup)(int);
// Sprint 1 Entry 15 — kbd_getTextFromCmd returns the human-readable action
// label for a command ID (used by DiscoverActionForWindow to title-token-
// score candidate actions against the captured window's title). Section
// parameter typed as void* to avoid pulling KbdSectionInfo into globals.h;
// pass nullptr (main section) at call sites.
extern const char* (*g_kbd_getTextFromCmd)(int cmd, void* section);

// Plugin DLL/dylib HINSTANCE captured in ReaperPluginEntry (Sprint 1 Entry 5).
// On Windows DialogBoxParam needs the plugin's own HINSTANCE to find dialog
// resources — passing nullptr resolves to the host EXE (REAPER) which doesn't
// have our IDD_*. SWELL macOS/Linux ignores it; safe to use cross-platform.
extern HINSTANCE g_hInstance;

// Per-project state API
class ReaProject;  // forward declaration (defined in reaper_plugin.h)
extern ReaProject* (*g_EnumProjects)(int idx, char* projfnOut, int projfnOut_sz);
extern int (*g_GetProjExtState)(ReaProject* proj, const char* extname, const char* key,
                                 char* valOut, int valOut_sz);
extern int (*g_SetProjExtState)(ReaProject* proj, const char* extname, const char* key,
                                 const char* value);
extern void (*g_MarkProjectDirty)(ReaProject* proj);

// v2.0.4 #1 — AU/VST plugin window save/restore (ADR-037).
// Forward declarations to keep this header free of reaper_plugin.h.
// MediaTrack/MediaItem/MediaItem_Take are class in reaper_plugin.h.
// GUID is a typedef in SWELL (already pulled via platform.h) — no forward decl.
class MediaTrack;
class MediaItem;
class MediaItem_Take;
extern int (*g_CountTracks)(ReaProject* proj);
extern int (*g_CountMediaItems)(ReaProject* proj);
extern MediaTrack* (*g_GetMasterTrack)(ReaProject* proj);
extern MediaItem* (*g_GetMediaItem)(ReaProject* proj, int itemidx);
extern int (*g_GetMediaItemNumTakes)(MediaItem* item);
extern MediaItem_Take* (*g_GetMediaItemTake)(MediaItem* item, int tk);
extern bool (*g_GetSetMediaItemTakeInfo_String)(MediaItem_Take* tk, const char* parmname,
                                                 char* stringNeedBig, bool setNewValue);
extern bool (*g_GetSetMediaTrackInfo_String)(MediaTrack* tr, const char* parmname,
                                              char* stringNeedBig, bool setNewValue);
extern MediaTrack* (*g_GetTrack)(ReaProject* proj, int trackidx);
extern void (*g_guidToString)(const GUID* g, char* destNeed64);
extern void (*g_stringToGuid)(const char* str, GUID* g);
extern int (*g_TakeFX_GetCount)(MediaItem_Take* take);
extern HWND (*g_TakeFX_GetFloatingWindow)(MediaItem_Take* take, int index);
extern GUID* (*g_TakeFX_GetFXGUID)(MediaItem_Take* take, int fx);
extern bool (*g_TakeFX_GetFXName)(MediaItem_Take* take, int fx, char* bufOut, int bufOut_sz);
extern void (*g_TakeFX_Show)(MediaItem_Take* take, int index, int showFlag);
extern int (*g_TrackFX_GetCount)(MediaTrack* track);
extern HWND (*g_TrackFX_GetFloatingWindow)(MediaTrack* track, int index);
extern GUID* (*g_TrackFX_GetFXGUID)(MediaTrack* track, int fx);
extern bool (*g_TrackFX_GetFXName)(MediaTrack* track, int fx, char* bufOut, int bufOut_sz);
extern void (*g_TrackFX_Show)(MediaTrack* track, int index, int showFlag);

// v2.0.4 #2 — hotkey binding (ADR-038). KbdSectionInfo is typed as void*
// here (same pattern as g_kbd_getTextFromCmd above) to keep globals.h free
// of SDK type pulls — call sites cast through main.cpp's REAPERAPI surface.
extern bool (*g_DoActionShortcutDialog)(HWND hwnd, void* section,
                                         int cmdID, int shortcutidx);
extern int (*g_CountActionShortcuts)(void* section, int cmdID);
extern bool (*g_GetActionShortcutDesc)(void* section, int cmdID,
                                        int shortcutidx, char* descOut, int descOut_sz);
extern bool (*g_DeleteActionShortcut)(void* section, int cmdID,
                                       int shortcutidx);
extern void* (*g_SectionFromUniqueID)(int uniqueID);

// Safe string copy: always null-terminates, handles null src
inline void safe_strncpy(char* dst, const char* src, size_t dst_size)
{
  if (!dst || dst_size == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  // strlen+memcpy instead of strncpy: truncation is this helper's contract,
  // but GCC's -Wstringop-truncation flags every inlined strncpy call site
  // for it under the CI -Werror gate (ADR-060 session catch).
  size_t len = strlen(src);
  if (len > dst_size - 1) len = dst_size - 1;
  memcpy(dst, src, len);
  dst[len] = '\0';
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

// Resolve action command string to numeric ID (handles both "_RSxxx" and "12345").
// Sprint 1 Entry 15 — backward-compat: pre-fix saves wrote named cmds without
// the leading "_" (REAPER's ReverseNamedCommandLookup returns the name without
// it, but NamedCommandLookup requires it). When the numeric path fails for a
// non-empty cmd, retry with one prepended so old workspaces still resolve.
inline int ResolveActionCommand(const char* cmd)
{
  if (!cmd || !cmd[0]) return 0;
  if (cmd[0] == '_' && g_NamedCommandLookup) {
    return g_NamedCommandLookup(cmd);
  }
  char* endptr = nullptr;
  long v = strtol(cmd, &endptr, 10);
  if (endptr != cmd && v > 0 && v <= INT_MAX) return (int)v;

  if (g_NamedCommandLookup) {
    char prefixed[256];
    snprintf(prefixed, sizeof(prefixed), "_%s", cmd);
    int id = g_NamedCommandLookup(prefixed);
    if (id > 0) return id;
  }
  return 0;
}

// Get stable command string for an action ID (returns named ID for custom actions, numeric string for built-in)
inline const char* GetActionCommandString(int actionId, char* buf, int bufSize)
{
  if (!buf || bufSize <= 0) return "";
  if (actionId <= 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }
  if (g_ReverseNamedCommandLookup) {
    const char* named = g_ReverseNamedCommandLookup(actionId);
    if (named && named[0]) {
      // Sprint 1 Entry 15 — round-trip safety: prepend "_" so the saved
      // string passes back through NamedCommandLookup on load (the SDK
      // contract: ReverseNamedCommandLookup returns the name without it).
      if (named[0] == '_') snprintf(buf, bufSize, "%s", named);
      else                 snprintf(buf, bufSize, "_%s", named);
      return buf;
    }
  }
  snprintf(buf, bufSize, "%d", actionId);
  return buf;
}

// Audit M3.4 — the global ExtState section name. Canonical definition in
// config.cpp; declared here too because globals.h deliberately doesn't pull
// in config.h (a typo'd hand-typed literal silently forks the namespace).
extern const char* const EXT_SECTION;

// Auto-open helpers — canonical logic in one place
// Default is ON unless explicitly set to "0"
inline bool IsAutoOpenEnabled()
{
  if (!g_GetExtState) return true;
  const char* val = g_GetExtState(EXT_SECTION, "auto_open");
  if (val && val[0] == '0') return false;
  return true;
}

inline void SetAutoOpenEnabled(bool enabled)
{
  if (g_SetExtState) {
    g_SetExtState(EXT_SECTION, "auto_open", enabled ? "1" : "0", true);
  }
}

// v2.0.4 #3 — async update check on startup (ADR-039).
// Default ON; only literal "0" turns it off.
inline bool IsAutoUpdateEnabled()
{
  if (!g_GetExtState) return true;
  const char* val = g_GetExtState(EXT_SECTION, "auto_update_check");
  if (val && val[0] == '0') return false;
  return true;
}

inline void SetAutoUpdateEnabled(bool enabled)
{
  if (g_SetExtState) {
    g_SetExtState(EXT_SECTION, "auto_update_check", enabled ? "1" : "0", true);
  }
}
