#include "globals.h"

// Global REAPER API function pointers
void (*g_DockWindowAddEx)(HWND, const char*, const char*, bool) = nullptr;
void (*g_DockWindowRemove)(HWND) = nullptr;
void (*g_ShowConsoleMsg)(const char*) = nullptr;
void (*g_Main_OnCommand)(int, int) = nullptr;
const char* (*g_GetExtState)(const char*, const char*) = nullptr;
void (*g_SetExtState)(const char*, const char*, const char*, bool) = nullptr;
HWND g_reaperMainHwnd = nullptr;
int (*g_plugin_register)(const char*, void*) = nullptr;
bool (*g_GetUserInputs)(const char*, int, const char*, char*, int) = nullptr;
int (*g_GetToggleCommandState)(int) = nullptr;
int (*g_NamedCommandLookup)(const char*) = nullptr;
const char* (*g_ReverseNamedCommandLookup)(int) = nullptr;
