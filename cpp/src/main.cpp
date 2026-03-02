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
#define REAPERAPI_WANT_GetCurrentProjectInLoadSave

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"
#include "globals.h"
#include "container.h"
#include "project_state.h"
#include "debug.h"
#include <memory>

static std::unique_ptr<ReDockItContainer> g_container;
static int g_cmdId = 0;
static bool g_startupComplete = false;

// atexit callback — REAPER calls this before main window destroy.
// Save list of captured toggle actions so startup can close any that REAPER
// incorrectly reopens (REAPER caches wnd_vis before atexit, so toggle here is futile).
static void onAtExit()
{
  DBG("[ReDockIt] onAtExit: called, container=%p hwnd=%p\n",
      (void*)g_container.get(), g_container ? (void*)g_container->GetHwnd() : nullptr);
  if (g_container && g_container->GetHwnd() && g_SetExtState) {
    // Save captured action IDs so startup can close orphans if needed.
    // REAPER caches wnd_vis BEFORE atexit, so these windows will reopen on restart.
    // If the session has RPP state, rppReadyTimerFunc recaptures them (no problem).
    // If not (clean session), orphan cleanup closes them.
    char buf[1024] = {};
    int pos = 0;
    const WindowManager& wm = g_container->GetWinMgr();
    for (int p = 0; p < MAX_PANES; p++) {
      const PaneState* ps = wm.GetPaneState(p);
      if (!ps) continue;
      for (int t = 0; t < ps->tabCount; t++) {
        if (ps->tabs[t].captured && ps->tabs[t].toggleAction > 0) {
          if (pos > 0 && pos < (int)sizeof(buf) - 1) buf[pos++] = ',';
          pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", ps->tabs[t].toggleAction);
        }
      }
    }
    g_SetExtState("ReDockIt_cpp", "captured_actions", buf, true);
    DBG("[ReDockIt] onAtExit: saved captured_actions='%s'\n", buf);

    g_container->SaveState();
    g_container->GetWinMgr().ReleaseAll(false);
    DBG("[ReDockIt] onAtExit: ReleaseAll(false) done\n");
  }
}

// Used by project_state.cpp to access current container for save
ReDockItContainer* GetContainer() { return g_container.get(); }

// Close windows that REAPER reopened from cached wnd_vis but that belong
// to a ReDockIt session that is NOT being loaded (clean session).
// Called as a deferred one-shot timer AFTER startup — gives rppReadyTimerFunc
// a chance to fire first and cancel this cleanup.
static bool g_orphanCleanupCancelled = false;
static void orphanCleanupTimerFunc()
{
  g_plugin_register("-timer", (void*)(void(*)())orphanCleanupTimerFunc);

  if (g_orphanCleanupCancelled) {
    DBG("[ReDockIt] orphanCleanup: cancelled (RPP state will recapture)\n");
    return;
  }

  if (!g_GetExtState || !g_Main_OnCommand || !g_GetToggleCommandState) return;

  const char* actions = g_GetExtState("ReDockIt_cpp", "captured_actions");
  if (!actions || !actions[0]) return;

  DBG("[ReDockIt] orphanCleanup: closing orphaned windows '%s'\n", actions);

  char buf[1024];
  safe_strncpy(buf, actions, sizeof(buf));
  char* p = buf;
  while (*p) {
    int actionId = atoi(p);
    if (actionId > 0) {
      int state = g_GetToggleCommandState(actionId);
      if (state == 1) {
        DBG("[ReDockIt] orphanCleanup: closing action %d\n", actionId);
        g_Main_OnCommand(actionId, 0);
      }
    }
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
  }

  if (g_SetExtState) {
    g_SetExtState("ReDockIt_cpp", "captured_actions", "", true);
  }
}

// One-shot timer: fired by ProcessExtensionLine when RPP state arrives.
// Creates the container on next main-loop tick (safe context for UI creation).
// Cancels orphan cleanup — the windows will be recaptured by the container.
static void rppReadyTimerFunc()
{
  g_plugin_register("-timer", (void*)(void(*)())rppReadyTimerFunc);

  // Cancel orphan cleanup — these windows will be recaptured, not closed
  g_orphanCleanupCancelled = true;

  // Clear the saved list since we're recapturing
  if (g_SetExtState) {
    g_SetExtState("ReDockIt_cpp", "captured_actions", "", true);
  }

  if (!g_pendingProjectState.valid) return;  // already consumed

  DBG("[ReDockIt] rppReadyTimer: RPP state available (%d lines), ensuring container\n",
      g_pendingProjectState.lineCount);

  if (!g_container) {
    g_container = std::make_unique<ReDockItContainer>();
  }
  if (!g_container->GetHwnd()) {
    g_container->Create();  // Create() → LoadState() consumes pending RPP data
  }
  // If container already has hwnd, its OnTimer will pick up the pending RPP state
}

// Called from ProcessExtensionLine (project_state.cpp) when <REDOCKIT_STATE> is parsed.
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
  // If user explicitly closed ReDockIt ([x]), was_visible="0" → don't reopen.
  // If the project has RPP state, rppReadyTimerFunc will handle it separately.
  bool wasVisible = true;
  if (g_GetExtState) {
    const char* vis = g_GetExtState("ReDockIt_cpp", "was_visible");
    if (vis && vis[0] == '0') wasVisible = false;
  }
  if (IsAutoOpenEnabled() && wasVisible) {
    if (!g_container) {
      g_container = std::make_unique<ReDockItContainer>();
    }
    if (!g_container->GetHwnd()) {
      g_container->Create();
    }
  }

  // Schedule deferred orphan cleanup.
  // This runs on the NEXT tick, giving rppReadyTimerFunc a chance to cancel it.
  // If the session has RPP state → rppReadyTimerFunc fires first → cancels cleanup.
  // If clean session (no RPP) → cleanup fires → closes windows REAPER reopened from wnd_vis.
  g_plugin_register("timer", (void*)(void(*)())orphanCleanupTimerFunc);
}

static bool hookCommandProc(int command, int flag)
{
  // Intercept Quit (File→Quit on Windows/Linux, also macOS File→Quit menu).
  // onAtExit handles the actual cleanup; this is a backup that fires earlier.
  if (command == 40004 && g_container && g_container->GetHwnd()) {
    DBG("[ReDockIt] hookCommand 40004: intercepted Quit\n");
    // onAtExit will handle saving captured_actions and release
    return false;  // let REAPER continue with quit
  }

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
