// MaxPane - REAPER Extension for Nested Docker Layouts

#include "platform.h"

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
// Sprint 1 Entry 15 — kbd_getTextFromCmd (REAPER 6.71+) for action display
// names used by DiscoverActionForWindow.
#define REAPERAPI_WANT_kbd_getTextFromCmd

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"
#include "globals.h"
#include "container.h"
#include "instance_manager.h"
#include "workspace_manager.h"
#include "project_state.h"
#include "quick_switcher.h"
#include "debug.h"
#include <cstdio>

// Action command IDs. g_cmdOpenContainer[0] is the legacy "MaxPane_OpenContainer"
// (no suffix); g_cmdOpenContainer[N>0] is "MaxPane_OpenContainer_(N+1)".
static int g_cmdOpenContainer[MaxPaneContainer::MAX_INSTANCES] = {0};
static int g_cmdNextTab = 0;
static int g_cmdPrevTab = 0;
static int g_cmdQuickSwitcher = 0;
static int g_cmdReopenTab = 0;       // C1 (ADR-027)
static int g_cmdWsPickup  = 0;       // C4 (ADR-027)
static int g_cmdNextPane = 0;
static int g_cmdPrevPane = 0;
static int g_cmdSoloToggle = 0;
// F6 — slot actions. User binds these in REAPER prefs; we map cmd → slot idx.
static int g_cmdWsSlot[MAX_WORKSPACES] = {0};
static int g_cmdFavSlot[MAX_FAVORITES] = {0};
static bool g_startupComplete = false;
extern "C" bool g_atexitSaved = false;  // prevent Shutdown from overwriting atexit state

// Map a command ID to instance index, or -1 if it's not an Open action.
static int OpenCommandToInstance(int command)
{
  for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++) {
    if (command == g_cmdOpenContainer[i]) return i;
  }
  return -1;
}

static int WsSlotFromCommand(int command)
{
  for (int i = 0; i < MAX_WORKSPACES; i++) {
    if (command == g_cmdWsSlot[i]) return i;
  }
  return -1;
}

static int FavSlotFromCommand(int command)
{
  for (int i = 0; i < MAX_FAVORITES; i++) {
    if (command == g_cmdFavSlot[i]) return i;
  }
  return -1;
}

// Resolve "where should this slot action act" — focused instance with a live
// container, else inst 0 (created if needed). Used by both ws and fav slots.
static MaxPaneContainer* ResolveSlotTargetInstance()
{
  MaxPaneContainer* c = InstanceManager::Get().GetFocused();
  if (c && c->GetHwnd()) return c;
  c = InstanceManager::Get().GetOrCreate(0);
  if (!c) return nullptr;
  if (!c->GetHwnd()) c->Create();
  return c;
}

// atexit callback — REAPER calls this before main window destroy.
// Save state for every live instance, write the stale-action list so startup
// can close ghost windows (REAPER's wnd_vis cache restores them as floating
// regardless of where they were docked), then release captured children.
static void onAtExit()
{
  InstanceManager::Get().ForEach([](int id, MaxPaneContainer& c) {
    DBG("[MaxPane] onAtExit: instance %d hwnd=%p\n", id, (void*)c.GetHwnd());
    if (c.GetHwnd()) {
      c.SaveState();
      MergeCapturesIntoStaleListForSection(c.ExtSection(), c.GetWinMgr());
      c.GetWinMgr().ReleaseAll(false);
    }
  });
  g_atexitSaved = true;
  DBG("[MaxPane] onAtExit: done\n");
}

// Used by project_state.cpp to access the primary container for save/load.
// Instance 0 is the "main" MaxPane for v1.5.x backward compatibility.
MaxPaneContainer* GetContainer() { return InstanceManager::Get().GetExisting(0); }

// One-shot timer: fired by ProcessExtensionLine when RPP state arrives for
// any instance. Each instance with pending RPP data gets its container
// created on next main-loop tick (safe context for UI). Project RPP state
// always takes priority — if a project defines a MaxPane layout, load it
// regardless of was_visible.
static void rppReadyTimerFunc()
{
  g_plugin_register("-timer", (void*)(void(*)())rppReadyTimerFunc);

  for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++) {
    if (!g_pendingProjectState[i].valid) continue;
    DBG("[MaxPane] rppReadyTimer: inst %d has RPP state (%d lines), ensuring container\n",
        i, g_pendingProjectState[i].lineCount);
    MaxPaneContainer* c = InstanceManager::Get().GetOrCreate(i);
    if (c && !c->GetHwnd()) {
      c->Create();  // Create() → LoadState() consumes this instance's pending RPP data
    }
    // If container already has hwnd, its OnTimer will pick up the pending RPP state
  }
}

// Called from ProcessExtensionLine (project_state.cpp) when <MAXPANE_STATE> is parsed.
void OnRppStateReady()
{
  if (g_plugin_register) {
    g_plugin_register("timer", (void*)(void(*)())rppReadyTimerFunc);
  }
}

// Deferred startup timer — fires on REAPER main loop.
//
// Per ADR-013 (workspace launcher model): MaxPane opens empty at startup. No
// captures are restored. BUT we still must close ghost windows REAPER opens
// from its own wnd_vis cache — those are windows we had captured last session
// that REAPER decided to re-show because their visibility was frozen as `1`
// in reaper.ini *before* our atexit could change it (MEMORY.md
// [[shutdown-restart]]). Closing them at startup is NOT auto-restore (no
// recapture), it's just preventing the floating-ghost UX.
//
// Polling strategy: run stale-action cleanup on EVERY tick from tick 1, not
// just once after STARTUP_DELAY_TICKS. REAPER opens the docker-restored
// windows somewhere between T+100ms and T+300ms after plugins load; firing
// cleanup at T+30ms (first tick) catches them as soon as state==1 becomes
// observable, instead of letting them sit floating for ~450ms. Cleanup is
// idempotent: when the stale list is empty (or all entries are state==0 and
// not yet visible), the call is a cheap no-op + write-back.
//
// Shell auto-open still happens on the original delay so REAPER has time to
// finish docker layout before we try to dock MaxPane into it. After a
// generous polling window (~2 seconds total), we stop polling to avoid
// burning ticks for the rest of the session.
static int g_startupCounter = 0;
static bool g_shellOpenAttempted = false;
static constexpr int STARTUP_POLL_TICKS = 60;  // ~1.8s of polling past delay

static void startupTimerFunc()
{
  g_startupCounter++;
  if (!g_GetExtState) return;

  // STEP 1 — Aggressive ghost cleanup. Runs every tick so the window of
  // visible floaters is minimized to one tick (~30ms) instead of ~450ms.
  ProcessStaleActionsForSection("MaxPane_cpp");
  for (int i = 1; i < MaxPaneContainer::MAX_INSTANCES; i++) {
    char section[32];
    snprintf(section, sizeof(section), "MaxPane_cpp_%d", i);
    ProcessStaleActionsForSection(section);
  }

  // STEP 2 — Inst 0 shell auto-open (one-shot, after settle delay).
  if (!g_shellOpenAttempted && g_startupCounter >= STARTUP_DELAY_TICKS) {
    g_shellOpenAttempted = true;
    g_startupComplete = true;

    bool wasVisible = true;
    const char* vis = g_GetExtState("MaxPane_cpp", "was_visible");
    if (vis && vis[0] == '0') wasVisible = false;
    if (IsAutoOpenEnabled() && wasVisible) {
      MaxPaneContainer* c = InstanceManager::Get().GetOrCreate(0);
      if (c && !c->GetHwnd()) c->Create();
    }
  }

  // STEP 3 — Stop polling after the polling window expires.
  if (g_startupCounter > STARTUP_DELAY_TICKS + STARTUP_POLL_TICKS) {
    g_plugin_register("-timer", (void*)(void(*)())startupTimerFunc);
  }
}

static bool hookCommandProc(int command, int /*flag*/)
{
  // Intercept Quit (File→Quit on Windows/Linux, also macOS File→Quit menu).
  // onAtExit handles the actual cleanup; this is a backup that fires earlier.
  if (command == 40004) {
    bool anyOpen = false;
    InstanceManager::Get().ForEach([&](int /*id*/, MaxPaneContainer& c) {
      if (c.GetHwnd()) anyOpen = true;
    });
    if (anyOpen) {
      DBG("[MaxPane] hookCommand 40004: intercepted Quit\n");
      return false;  // let REAPER continue with quit
    }
  }

  // Open / toggle commands for each instance.
  int instId = OpenCommandToInstance(command);
  if (instId >= 0) {
    // During startup, REAPER's docker system restores docked windows by calling
    // this hook. For instance 0 only, suppress restore if was_visible=0 (user
    // explicitly closed before last exit).
    if (instId == 0 && !g_startupComplete && g_GetExtState) {
      const char* vis = g_GetExtState("MaxPane_cpp", "was_visible");
      if (vis && vis[0] == '0') {
        return true;
      }
    }

    MaxPaneContainer* c = InstanceManager::Get().GetOrCreate(instId);
    if (!c) return false;
    if (!c->GetHwnd()) c->Create();
    else                c->Toggle();
    return true;
  }

  // F6 — workspace slot. Slot N (1-indexed in name) = workspace at index N-1.
  // Routes to focused instance; opens inst 0 if no instance is live.
  {
    int wsSlot = WsSlotFromCommand(command);
    if (wsSlot >= 0) {
      MaxPaneContainer* c = ResolveSlotTargetInstance();
      if (!c) return false;
      const WorkspaceManager& wsMgr = c->GetWsMgr();
      if (wsSlot < wsMgr.GetCount()) {
        const WorkspaceEntry& ws = wsMgr.Get(wsSlot);
        if (ws.used) c->LoadWorkspace(ws.name);
      }
      return true;
    }
  }

  // F6 — favorite slot. Slot N = favorite at index N-1; captures into focused
  // pane of the focused instance (or inst 0's first pane if none).
  {
    int favSlot = FavSlotFromCommand(command);
    if (favSlot >= 0) {
      MaxPaneContainer* c = ResolveSlotTargetInstance();
      if (!c) return false;
      c->ActivateFavorite(favSlot);
      return true;
    }
  }

  // F4 Quick Switcher — available regardless of container state. If no
  // MaxPane instance is open, the index will be empty (no tabs) but
  // workspaces/favorites still appear via the shared store; activating
  // any of those creates instance 0 implicitly through LoadWorkspace /
  // ActivateFavorite paths.
  if (command == g_cmdQuickSwitcher && g_cmdQuickSwitcher) {
    OpenQuickSwitcher(g_reaperMainHwnd);
    return true;
  }

  // C1 (ADR-027) — Reopen last closed tab. Routes to focused instance.
  // Silent no-op if no instance is live or the buffer is empty: user binding
  // a hotkey for a stack that hasn't accumulated yet shouldn't see an error.
  if (command == g_cmdReopenTab && g_cmdReopenTab) {
    MaxPaneContainer* c = ResolveSlotTargetInstance();
    if (c && c->HasRecentlyClosedTab()) c->ReopenLastClosedTab();
    return true;
  }

  // C4 (ADR-027) — Workspace pickup (single hotkey → prompts for slot).
  if (command == g_cmdWsPickup && g_cmdWsPickup) {
    MaxPaneContainer* c = ResolveSlotTargetInstance();
    if (c) c->OpenWorkspacePickup();
    return true;
  }

  // Navigation actions — only affect instance 0 for now (focused-instance
  // routing is a follow-up; see F6 in V2_SCOPE).
  MaxPaneContainer* c0 = InstanceManager::Get().GetExisting(0);
  if (c0 && c0->IsVisible()) {
    if (command == g_cmdNextTab)    { c0->NextTab();    return true; }
    if (command == g_cmdPrevTab)    { c0->PrevTab();    return true; }
    if (command == g_cmdNextPane)   { c0->NextPane();   return true; }
    if (command == g_cmdPrevPane)   { c0->PrevPane();   return true; }
    if (command == g_cmdSoloToggle) { c0->SoloToggleFocused(); return true; }
  }

  return false;
}

static int toggleActionCallback(int command)
{
  int instId = OpenCommandToInstance(command);
  if (instId >= 0) {
    MaxPaneContainer* c = InstanceManager::Get().GetExisting(instId);
    return (c && c->IsVisible()) ? 1 : 0;
  }
  return -1;
}

// F6 — register N workspace-slot + M favorite-slot actions. The slots are
// position-based: WsSlot_K = workspace at index K-1 in WorkspaceManager's
// (shared) list; FavSlot_K = favorite at index K-1. User binds these in
// REAPER's keybindings preference; we dispatch them in hookCommandProc.
// Static storage so REAPER's pointers stay valid for the plugin's lifetime.
static void RegisterSlotActions(reaper_plugin_info_t* rec)
{
  static char wsCmd[MAX_WORKSPACES][32];
  static char wsDesc[MAX_WORKSPACES][48];
  static gaccel_register_t wsAccel[MAX_WORKSPACES];
  for (int i = 0; i < MAX_WORKSPACES; i++) {
    snprintf(wsCmd[i],  sizeof(wsCmd[i]),  "MaxPane_WsSlot_%d",      i + 1);
    snprintf(wsDesc[i], sizeof(wsDesc[i]), "MaxPane: Load Workspace Slot %d", i + 1);
    g_cmdWsSlot[i] = rec->Register("command_id", (void*)wsCmd[i]);
    if (g_cmdWsSlot[i]) {
      wsAccel[i].accel = {0, 0, 0};
      wsAccel[i].accel.cmd = (unsigned short)g_cmdWsSlot[i];
      wsAccel[i].desc = wsDesc[i];
      rec->Register("gaccel", &wsAccel[i]);
    }
  }

  static char favCmd[MAX_FAVORITES][32];
  static char favDesc[MAX_FAVORITES][48];
  static gaccel_register_t favAccel[MAX_FAVORITES];
  for (int i = 0; i < MAX_FAVORITES; i++) {
    snprintf(favCmd[i],  sizeof(favCmd[i]),  "MaxPane_FavSlot_%d",     i + 1);
    snprintf(favDesc[i], sizeof(favDesc[i]), "MaxPane: Capture Favorite Slot %d", i + 1);
    g_cmdFavSlot[i] = rec->Register("command_id", (void*)favCmd[i]);
    if (g_cmdFavSlot[i]) {
      favAccel[i].accel = {0, 0, 0};
      favAccel[i].accel.cmd = (unsigned short)g_cmdFavSlot[i];
      favAccel[i].desc = favDesc[i];
      rec->Register("gaccel", &favAccel[i]);
    }
  }
}

// Register one Open-Container action per instance. Stored gaccel_register_t
// structs must live for the lifetime of the plugin — keep them as a static
// array per instance to satisfy REAPER's pointer requirements.
static void RegisterOpenActions(reaper_plugin_info_t* rec)
{
  // Instance 0 — legacy action name (no suffix) for backward compat with
  // existing user keybinds from v1.5.x.
  g_cmdOpenContainer[0] = rec->Register("command_id", (void*)"MaxPane_OpenContainer");
  static gaccel_register_t accel0 = {{0, 0, 0}, "MaxPane: Open Container"};
  if (g_cmdOpenContainer[0]) {
    accel0.accel.cmd = static_cast<unsigned short>(g_cmdOpenContainer[0]);
    rec->Register("gaccel", &accel0);
  }

  // Instances 1..N-1 — suffixed actions. The user-facing number is N+1 so
  // instance 1 shows as "Container 2", matching the docker tab title.
  static char cmdName[MaxPaneContainer::MAX_INSTANCES][32];
  static char accelDesc[MaxPaneContainer::MAX_INSTANCES][48];
  static gaccel_register_t accelN[MaxPaneContainer::MAX_INSTANCES];
  for (int i = 1; i < MaxPaneContainer::MAX_INSTANCES; i++) {
    snprintf(cmdName[i],   sizeof(cmdName[i]),   "MaxPane_OpenContainer_%d", i + 1);
    snprintf(accelDesc[i], sizeof(accelDesc[i]), "MaxPane: Open Container %d", i + 1);
    g_cmdOpenContainer[i] = rec->Register("command_id", (void*)cmdName[i]);
    if (g_cmdOpenContainer[i]) {
      accelN[i].accel = {0, 0, 0};
      accelN[i].accel.cmd = static_cast<unsigned short>(g_cmdOpenContainer[i]);
      accelN[i].desc = accelDesc[i];
      rec->Register("gaccel", &accelN[i]);
    }
  }
}

extern "C" {

REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(
  HINSTANCE hInstance, reaper_plugin_info_t* rec)
{
  // Sprint 1 Entry 5 — capture plugin HINSTANCE before any branch so
  // dialogs can use it as the resource module (Windows native rc.exe path).
  g_hInstance = hInstance;

  if (!rec) {
    // Save visibility state for each live instance before shutdown — if open,
    // mark for restore on next start.
    if (g_SetExtState) {
      InstanceManager::Get().ForEach([](int /*id*/, MaxPaneContainer& c) {
        // Sprint 1 Entry 10 — WasIntendedVisible() reads the m_visible
        // intent flag instead of IsWindowVisible(). On Win32 quit the
        // docker parent is hidden BEFORE plugin unload, so IsWindowVisible
        // walks the parent chain and reports false even when MaxPane was
        // still open — was_visible persisted as "0" and the next start
        // didn't auto-open. SWELL doesn't reproduce the race but the
        // intent semantics are conceptually correct everywhere.
        g_SetExtState(c.ExtSection(), "was_visible",
                      c.WasIntendedVisible() ? "1" : "0", true);
      });
    }
    InstanceManager::Get().DestroyAll();
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
  // Sprint 1 Entry 15 — cast through (const char* (*)(int, void*)) because
  // globals.h hides KbdSectionInfo behind void* to keep the header free of
  // SDK type dependencies.
  g_kbd_getTextFromCmd = (const char* (*)(int, void*))kbd_getTextFromCmd;
  g_EnumProjects = EnumProjects;
  g_GetProjExtState = GetProjExtState;
  g_SetProjExtState = SetProjExtState;
  g_MarkProjectDirty = MarkProjectDirty;

  RegisterOpenActions(rec);
  if (!g_cmdOpenContainer[0]) return 0;
  RegisterSlotActions(rec);

  g_cmdNextTab    = rec->Register("command_id", (void*)"MaxPane_NextTab");
  g_cmdPrevTab    = rec->Register("command_id", (void*)"MaxPane_PrevTab");
  g_cmdNextPane   = rec->Register("command_id", (void*)"MaxPane_NextPane");
  g_cmdPrevPane   = rec->Register("command_id", (void*)"MaxPane_PrevPane");
  g_cmdSoloToggle = rec->Register("command_id", (void*)"MaxPane_SoloToggle");
  g_cmdQuickSwitcher = rec->Register("command_id", (void*)"MaxPane_QuickSwitcher");
  g_cmdReopenTab     = rec->Register("command_id", (void*)"MaxPane_ReopenClosedTab");
  g_cmdWsPickup      = rec->Register("command_id", (void*)"MaxPane_WorkspacePickup");

  static gaccel_register_t accelNextTab = {{0, 0, 0}, "MaxPane: Next Tab"};
  accelNextTab.accel.cmd = static_cast<unsigned short>(g_cmdNextTab);
  rec->Register("gaccel", &accelNextTab);

  static gaccel_register_t accelPrevTab = {{0, 0, 0}, "MaxPane: Previous Tab"};
  accelPrevTab.accel.cmd = static_cast<unsigned short>(g_cmdPrevTab);
  rec->Register("gaccel", &accelPrevTab);

  static gaccel_register_t accelNextPane = {{0, 0, 0}, "MaxPane: Next Pane"};
  accelNextPane.accel.cmd = static_cast<unsigned short>(g_cmdNextPane);
  rec->Register("gaccel", &accelNextPane);

  static gaccel_register_t accelPrevPane = {{0, 0, 0}, "MaxPane: Previous Pane"};
  accelPrevPane.accel.cmd = static_cast<unsigned short>(g_cmdPrevPane);
  rec->Register("gaccel", &accelPrevPane);

  static gaccel_register_t accelSolo = {{0, 0, 0}, "MaxPane: Solo Toggle"};
  accelSolo.accel.cmd = static_cast<unsigned short>(g_cmdSoloToggle);
  rec->Register("gaccel", &accelSolo);

  // F4 — no default accel; user binds Cmd+P (macOS) / Ctrl+P (Win/Linux)
  // themselves via REAPER's Actions list (no per-platform translation).
  static gaccel_register_t accelQS = {{0, 0, 0}, "MaxPane: Quick Switcher"};
  if (g_cmdQuickSwitcher) {
    accelQS.accel.cmd = static_cast<unsigned short>(g_cmdQuickSwitcher);
    rec->Register("gaccel", &accelQS);
  }

  // C1 (ADR-027) — no default accel; user binds Cmd+Shift+T themselves.
  static gaccel_register_t accelReopen = {{0, 0, 0}, "MaxPane: Reopen last closed tab"};
  if (g_cmdReopenTab) {
    accelReopen.accel.cmd = static_cast<unsigned short>(g_cmdReopenTab);
    rec->Register("gaccel", &accelReopen);
  }

  // C4 (ADR-027) — workspace pickup (single hotkey → prompt for slot).
  static gaccel_register_t accelWsPickup = {{0, 0, 0}, "MaxPane: Workspace pickup"};
  if (g_cmdWsPickup) {
    accelWsPickup.accel.cmd = static_cast<unsigned short>(g_cmdWsPickup);
    rec->Register("gaccel", &accelWsPickup);
  }

  rec->Register("hookcommand", (void*)hookCommandProc);
  rec->Register("toggleaction", (void*)toggleActionCallback);

  // Accelerator hook — route key events targeting any MaxPane container HWND
  // (or a captured descendant) through REAPER's main accel table so user-
  // bound MaxPane actions (Cmd+Shift+T → ReopenClosedTab, Cmd+P → Quick
  // Switcher, workspace/favorite slots, etc.) fire even when MaxPane or one
  // of its captured panes has keyboard focus. Without this, native REAPER
  // accel translation skips plugin windows and the bindings appear "dead"
  // until the user clicks the REAPER arrange window.
  //
  // Return -666 = "force to main window's accel table (except ESC)" — our
  // pane-internal ESC handling (capture-cancel, drag-cancel) keeps working
  // because ESC is excluded by SDK design.
  static accelerator_register_t s_accelReg = {
    [](MSG* msg, accelerator_register_t*) -> int {
      if (!msg) return 0;
      if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN) return 0;
      HWND walk = msg->hwnd;
      while (walk) {
        bool isOurs = false;
        InstanceManager::Get().ForEach([&](int /*id*/, MaxPaneContainer& c) {
          if (c.GetHwnd() == walk) isOurs = true;
        });
        if (isOurs) return -666;
        walk = GetParent(walk);
      }
      return 0;
    },
    true,
    nullptr
  };
  rec->Register("accelerator", &s_accelReg);

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
