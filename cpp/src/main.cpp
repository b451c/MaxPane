// ReDockIt - REAPER Extension for Nested Docker Layouts

#ifdef _WIN32
#include <windows.h>
#else
#ifndef SWELL_PROVIDED_BY_APP
#define SWELL_PROVIDED_BY_APP
#endif
#include "swell/swell.h"
#endif

#define REAPERAPI_IMPLEMENT
#define REAPERAPI_MINIMAL

#define REAPERAPI_WANT_DockWindowAddEx
#define REAPERAPI_WANT_DockWindowRemove
#define REAPERAPI_WANT_DockWindowRefresh
#define REAPERAPI_WANT_ShowConsoleMsg
#define REAPERAPI_WANT_Main_OnCommand
#define REAPERAPI_WANT_GetExtState
#define REAPERAPI_WANT_SetExtState
#define REAPERAPI_WANT_plugin_register
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_GetUserInputs

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"
#include "globals.h"
#include "container.h"

static ReDockItContainer* g_container = nullptr;
static int g_cmdId = 0;

// Deferred startup timer — fires on REAPER main loop, auto-opens container if enabled
static int g_startupCounter = 0;
static void startupTimerFunc()
{
  if (++g_startupCounter < 15) return; // ~450ms delay
  g_plugin_register("-timer", (void*)(void(*)())startupTimerFunc);

  if (IsAutoOpenEnabled()) {
    if (!g_container) {
      g_container = new ReDockItContainer();
    }
    if (!g_container->GetHwnd()) {
      g_container->Create();
    }
  }
}

static bool hookCommandProc(int command, int flag)
{
  if (command == g_cmdId) {
    if (!g_container) {
      g_container = new ReDockItContainer();
    }
    if (!g_container->GetHwnd()) {
      g_container->Create();
    } else {
      g_container->Toggle();
    }
    return true;
  }
  return false;
}

static int toggleActionCallback(int command)
{
  if (command == g_cmdId) {
    if (g_container && g_container->IsVisible()) return 1;
    return 0;
  }
  return -1;
}

extern "C" {

REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(
  HINSTANCE hInstance, reaper_plugin_info_t* rec)
{
  if (!rec) {
    if (g_container) {
      g_container->Shutdown();
      delete g_container;
      g_container = nullptr;
    }
    return 0;
  }

  if (rec->caller_version < 0x20E) {
    return 0;
  }

  REAPERAPI_LoadAPI(rec->GetFunc);

  g_reaperMainHwnd = rec->hwnd_main;
  g_plugin_register = rec->Register;

  g_DockWindowAddEx = DockWindowAddEx;
  g_DockWindowRemove = DockWindowRemove;
  g_ShowConsoleMsg = ShowConsoleMsg;
  g_Main_OnCommand = Main_OnCommand;
  g_GetExtState = GetExtState;
  g_SetExtState = SetExtState;
  g_GetUserInputs = GetUserInputs;

  g_cmdId = rec->Register("command_id", (void*)"ReDockIt_OpenContainer");
  if (!g_cmdId) return 0;

  static gaccel_register_t accel = {{0, 0, 0}, "ReDockIt: Open Container"};
  accel.accel.cmd = g_cmdId;
  rec->Register("gaccel", &accel);
  rec->Register("hookcommand", (void*)hookCommandProc);
  rec->Register("toggleaction", (void*)toggleActionCallback);

  // Deferred auto-open on startup
  g_plugin_register("timer", (void*)(void(*)())startupTimerFunc);

  return 1;
}

} // extern "C"
