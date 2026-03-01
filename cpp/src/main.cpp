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
#define REAPERAPI_WANT_GetToggleCommandState
#define REAPERAPI_WANT_NamedCommandLookup
#define REAPERAPI_WANT_ReverseNamedCommandLookup
#define REAPERAPI_WANT_EnumProjects
#define REAPERAPI_WANT_GetProjExtState
#define REAPERAPI_WANT_SetProjExtState
#define REAPERAPI_WANT_MarkProjectDirty

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"
#include "globals.h"
#include "container.h"
#include <memory>

static std::unique_ptr<ReDockItContainer> g_container;
static int g_cmdId = 0;
static bool g_startupComplete = false;

// Deferred startup timer — fires on REAPER main loop, auto-opens container if enabled
static int g_startupCounter = 0;
static void startupTimerFunc()
{
  if (++g_startupCounter < STARTUP_DELAY_TICKS) return;
  g_plugin_register("-timer", (void*)(void(*)())startupTimerFunc);
  g_startupComplete = true;

  if (IsAutoOpenEnabled()) {
    if (!g_container) {
      g_container = std::make_unique<ReDockItContainer>();
    }
    if (!g_container->GetHwnd()) {
      g_container->Create();
    }
  }
}

static bool hookCommandProc(int command, int flag)
{
  if (command == g_cmdId) {
    // During startup, REAPER's docker system restores docked windows by calling this hook.
    // Only restore if ReDockIT was visible when REAPER last closed.
    if (!g_startupComplete && g_GetExtState) {
      const char* vis = g_GetExtState("ReDockIt_cpp", "was_visible");
      if (vis && vis[0] == '0') {
        return true;  // User closed ReDockIT before last exit — don't restore
      }
    }

    if (!g_container) {
      g_container = std::make_unique<ReDockItContainer>();
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
      // Save visibility state before shutdown — if open, mark for restore on next start
      if (g_SetExtState) {
        g_SetExtState("ReDockIt_cpp", "was_visible",
                       g_container->IsVisible() ? "1" : "0", true);
      }
      g_container->Shutdown();
      g_container.reset();
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
  g_GetToggleCommandState = GetToggleCommandState;
  g_NamedCommandLookup = NamedCommandLookup;
  g_ReverseNamedCommandLookup = ReverseNamedCommandLookup;
  g_EnumProjects = EnumProjects;
  g_GetProjExtState = GetProjExtState;
  g_SetProjExtState = SetProjExtState;
  g_MarkProjectDirty = MarkProjectDirty;

  g_cmdId = rec->Register("command_id", (void*)"ReDockIt_OpenContainer");
  if (!g_cmdId) return 0;

  static gaccel_register_t accel = {{0, 0, 0}, "ReDockIt: Open Container"};
  accel.accel.cmd = static_cast<unsigned short>(g_cmdId);
  rec->Register("gaccel", &accel);
  rec->Register("hookcommand", (void*)hookCommandProc);
  rec->Register("toggleaction", (void*)toggleActionCallback);

  // Deferred auto-open on startup
  g_plugin_register("timer", (void*)(void(*)())startupTimerFunc);

  return 1;
}

} // extern "C"
