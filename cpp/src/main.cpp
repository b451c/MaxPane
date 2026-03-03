// MaxPane - REAPER Extension for Nested Docker Layouts

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
#define REAPERAPI_WANT_GetCurrentProjectInLoadSave

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"
#include "globals.h"
#include "container.h"
#include "project_state.h"
#include "debug.h"
#include <memory>

static std::unique_ptr<MaxPaneContainer> g_container;
static int g_cmdId = 0;
static bool g_startupComplete = false;
extern "C" bool g_atexitSaved = false;  // prevent Shutdown from overwriting atexit state

// atexit callback — REAPER calls this before main window destroy.
// Just save state. REAPER already cached wnd_vis before this callback,
// so the windows will reopen on restart and MaxPane will recapture them.
static void onAtExit()
{
  DBG("[MaxPane] onAtExit: called, container=%p hwnd=%p\n",
      (void*)g_container.get(), g_container ? (void*)g_container->GetHwnd() : nullptr);
  if (g_container && g_container->GetHwnd()) {
    g_container->SaveState();
    g_atexitSaved = true;  // prevent Shutdown's SaveState from overwriting with empty panes
    // Reparent windows back to REAPER so they're properly cleaned up during quit.
    // Don't toggle off — REAPER already cached wnd_vis, and we WANT them to reopen
    // on restart so MaxPane can recapture them.
    g_container->GetWinMgr().ReleaseAll(false);
    DBG("[MaxPane] onAtExit: ReleaseAll(false) done\n");
  }
}

// Used by project_state.cpp to access current container for save
MaxPaneContainer* GetContainer() { return g_container.get(); }

// One-shot timer: fired by ProcessExtensionLine when RPP state arrives.
// Creates the container on next main-loop tick (safe context for UI creation).
// Project RPP state always takes priority — if a project defines a MaxPane layout,
// load it regardless of was_visible (the project defines the intent).
static void rppReadyTimerFunc()
{
  g_plugin_register("-timer", (void*)(void(*)())rppReadyTimerFunc);

  if (!g_pendingProjectState.valid) return;  // already consumed

  DBG("[MaxPane] rppReadyTimer: RPP state available (%d lines), ensuring container\n",
      g_pendingProjectState.lineCount);

  if (!g_container) {
    g_container = std::make_unique<MaxPaneContainer>();
  }
  if (!g_container->GetHwnd()) {
    g_container->Create();  // Create() → LoadState() consumes pending RPP data
  }
  // If container already has hwnd, its OnTimer will pick up the pending RPP state
}

// Called from ProcessExtensionLine (project_state.cpp) when <MAXPANE_STATE> is parsed.
// Defers container creation to next main-loop tick via one-shot timer.
void OnRppStateReady()
{
  if (g_plugin_register) {
    g_plugin_register("timer", (void*)(void(*)())rppReadyTimerFunc);
  }
}

// Deferred startup timer — fires on REAPER main loop, auto-opens container if enabled
static int g_startupCounter = 0;
static void startupTimerFunc()
{
  if (++g_startupCounter < STARTUP_DELAY_TICKS) return;
  g_plugin_register("-timer", (void*)(void(*)())startupTimerFunc);
  g_startupComplete = true;

  // Only auto-open if enabled AND was visible when REAPER last closed.
  // If user explicitly closed MaxPane ([x]), was_visible="0" → don't reopen.
  // If the project has RPP state, rppReadyTimerFunc will handle it separately.
  bool wasVisible = true;
  if (g_GetExtState) {
    const char* vis = g_GetExtState("MaxPane_cpp", "was_visible");
    if (vis && vis[0] == '0') wasVisible = false;
  }
  if (IsAutoOpenEnabled() && wasVisible) {
    if (!g_container) {
      g_container = std::make_unique<MaxPaneContainer>();
    }
    if (!g_container->GetHwnd()) {
      g_container->Create();
    }
  }
}

static bool hookCommandProc(int command, int flag)
{
  // Intercept Quit (File→Quit on Windows/Linux, also macOS File→Quit menu).
  // onAtExit handles the actual cleanup; this is a backup that fires earlier.
  if (command == 40004 && g_container && g_container->GetHwnd()) {
    DBG("[MaxPane] hookCommand 40004: intercepted Quit\n");
    // onAtExit will handle saving captured_actions and release
    return false;  // let REAPER continue with quit
  }

  if (command == g_cmdId) {
    // During startup, REAPER's docker system restores docked windows by calling this hook.
    // Only restore if MaxPane was visible when REAPER last closed.
    if (!g_startupComplete && g_GetExtState) {
      const char* vis = g_GetExtState("MaxPane_cpp", "was_visible");
      if (vis && vis[0] == '0') {
        return true;  // User closed MaxPane before last exit — don't restore
      }
    }

    if (!g_container) {
      g_container = std::make_unique<MaxPaneContainer>();
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
        g_SetExtState("MaxPane_cpp", "was_visible",
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

  g_cmdId = rec->Register("command_id", (void*)"MaxPane_OpenContainer");
  if (!g_cmdId) return 0;

  static gaccel_register_t accel = {{0, 0, 0}, "MaxPane: Open Container"};
  accel.accel.cmd = static_cast<unsigned short>(g_cmdId);
  rec->Register("gaccel", &accel);
  rec->Register("hookcommand", (void*)hookCommandProc);
  rec->Register("toggleaction", (void*)toggleActionCallback);

  // Register project_config_extension_t for synchronous RPP state I/O
  static project_config_extension_t s_projConfig = {
    OnProcessExtensionLine,
    OnSaveExtensionConfig,
    OnBeginLoadProjectState,
    nullptr  // userData
  };
  rec->Register("projectconfig", &s_projConfig);

  // Register atexit — reliable shutdown on macOS (Cmd+Q bypasses hookcommand 40004)
  rec->Register("atexit", (void*)onAtExit);

  // Deferred auto-open on startup
  g_plugin_register("timer", (void*)(void(*)())startupTimerFunc);

  return 1;
}

} // extern "C"
