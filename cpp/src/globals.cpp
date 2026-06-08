#include "globals.h"

// Global REAPER API function pointers
void (*g_DockWindowAddEx)(HWND, const char*, const char*, bool) = nullptr;
void (*g_DockWindowRemove)(HWND) = nullptr;
// v2.1 — screenset integration (ADR-049)
void (*g_screenset_registerNew)(char*, void*, void*) = nullptr;
void (*g_screenset_unregister)(char*) = nullptr;
int  (*g_DockIsChildOfDock)(HWND, bool*) = nullptr;
void (*g_Main_OnCommand)(int, int) = nullptr;
const char* (*g_GetExtState)(const char*, const char*) = nullptr;
void (*g_SetExtState)(const char*, const char*, const char*, bool) = nullptr;
HWND g_reaperMainHwnd = nullptr;
int (*g_plugin_register)(const char*, void*) = nullptr;
bool (*g_GetUserInputs)(const char*, int, const char*, char*, int) = nullptr;
int (*g_GetToggleCommandState)(int) = nullptr;
int (*g_NamedCommandLookup)(const char*) = nullptr;
const char* (*g_ReverseNamedCommandLookup)(int) = nullptr;
const char* (*g_kbd_getTextFromCmd)(int, void*) = nullptr;
HINSTANCE g_hInstance = nullptr;
ReaProject* (*g_EnumProjects)(int, char*, int) = nullptr;
int (*g_GetProjExtState)(ReaProject*, const char*, const char*, char*, int) = nullptr;
int (*g_SetProjExtState)(ReaProject*, const char*, const char*, const char*) = nullptr;
void (*g_MarkProjectDirty)(ReaProject*) = nullptr;

// v2.0.4 #1 — FX capture SDK pointers (ADR-037)
int (*g_CountTracks)(ReaProject*) = nullptr;
int (*g_CountMediaItems)(ReaProject*) = nullptr;
MediaTrack* (*g_GetMasterTrack)(ReaProject*) = nullptr;
MediaItem* (*g_GetMediaItem)(ReaProject*, int) = nullptr;
int (*g_GetMediaItemNumTakes)(MediaItem*) = nullptr;
MediaItem_Take* (*g_GetMediaItemTake)(MediaItem*, int) = nullptr;
bool (*g_GetSetMediaItemTakeInfo_String)(MediaItem_Take*, const char*, char*, bool) = nullptr;
bool (*g_GetSetMediaTrackInfo_String)(MediaTrack*, const char*, char*, bool) = nullptr;
MediaTrack* (*g_GetTrack)(ReaProject*, int) = nullptr;
void (*g_guidToString)(const GUID*, char*) = nullptr;
void (*g_stringToGuid)(const char*, GUID*) = nullptr;
int (*g_TakeFX_GetCount)(MediaItem_Take*) = nullptr;
HWND (*g_TakeFX_GetFloatingWindow)(MediaItem_Take*, int) = nullptr;
GUID* (*g_TakeFX_GetFXGUID)(MediaItem_Take*, int) = nullptr;
bool (*g_TakeFX_GetFXName)(MediaItem_Take*, int, char*, int) = nullptr;
void (*g_TakeFX_Show)(MediaItem_Take*, int, int) = nullptr;
int (*g_TrackFX_GetCount)(MediaTrack*) = nullptr;
HWND (*g_TrackFX_GetFloatingWindow)(MediaTrack*, int) = nullptr;
GUID* (*g_TrackFX_GetFXGUID)(MediaTrack*, int) = nullptr;
bool (*g_TrackFX_GetFXName)(MediaTrack*, int, char*, int) = nullptr;
void (*g_TrackFX_Show)(MediaTrack*, int, int) = nullptr;

// v2.0.4 #2 — hotkey binding (ADR-038)
bool (*g_DoActionShortcutDialog)(HWND, void*, int, int) = nullptr;
int (*g_CountActionShortcuts)(void*, int) = nullptr;
bool (*g_GetActionShortcutDesc)(void*, int, int, char*, int) = nullptr;
bool (*g_DeleteActionShortcut)(void*, int, int) = nullptr;
void* (*g_SectionFromUniqueID)(int) = nullptr;
