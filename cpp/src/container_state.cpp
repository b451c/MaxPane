// container_state.cpp — State persistence for MaxPaneContainer
// (SaveState, LoadState, ApplyPaneState, workspace save/load/delete)
#include "container.h"
#include "config.h"
#include "action_list.h"
#include "globals.h"
#include "debug.h"
#include "capture_queue.h"
#include "favorites_manager.h"
#include "workspace_manager.h"
#include "project_state.h"
#include "state_accessor.h"
#include "nav_bar.h"      // Feature A — NavBar::NAV_BAR_HEIGHT (bar-strip repaints)
#include <cstring>
#include <cstdio>

// =========================================================================
// Feature A — workspace-name nav bar label
// =========================================================================

void MaxPaneContainer::PersistWorkspaceLabel()
{
  if (!g_SetExtState) return;
  g_SetExtState(m_extSection, "current_workspace_name",
                m_currentWorkspaceName[0] ? m_currentWorkspaceName : "", true);
  g_SetExtState(m_extSection, "workspace_dirty",
                m_workspaceDirty ? "1" : "0", true);
}

void MaxPaneContainer::SetCurrentWorkspace(const char* name)
{
  if (!name || !name[0]) { ClearCurrentWorkspace(); return; }
  safe_strncpy(m_currentWorkspaceName, name, sizeof(m_currentWorkspaceName));
  m_workspaceDirty = false;
  PersistWorkspaceLabel();
  if (m_hwnd) {
    // Repaint just the bar strip — InvalidateRect for the whole bar so the
    // new label appears even when nothing else changed visually.
    RECT cr; GetClientRect(m_hwnd, &cr);
    RECT bar = cr; bar.bottom = bar.top + NavBar::NAV_BAR_HEIGHT + 36;
    InvalidateRect(m_hwnd, &bar, FALSE);
  }
}

void MaxPaneContainer::ClearCurrentWorkspace()
{
  if (!m_currentWorkspaceName[0] && !m_workspaceDirty) return;
  m_currentWorkspaceName[0] = '\0';
  m_workspaceDirty = false;
  PersistWorkspaceLabel();
  if (m_hwnd) {
    RECT cr; GetClientRect(m_hwnd, &cr);
    RECT bar = cr; bar.bottom = bar.top + NavBar::NAV_BAR_HEIGHT + 36;
    InvalidateRect(m_hwnd, &bar, FALSE);
  }
}

void MaxPaneContainer::MarkWorkspaceDirty()
{
  // Dirty has no meaning without a loaded workspace — skip the write so we
  // don't repaint or persist no-ops on every capture in launcher mode.
  if (!m_currentWorkspaceName[0]) return;
  if (m_workspaceDirty) return;
  m_workspaceDirty = true;
  PersistWorkspaceLabel();
  if (m_hwnd) {
    RECT cr; GetClientRect(m_hwnd, &cr);
    RECT bar = cr; bar.bottom = bar.top + NavBar::NAV_BAR_HEIGHT;
    InvalidateRect(m_hwnd, &bar, FALSE);
  }
}

// =========================================================================
// Save / Load state (delegates to WorkspaceManager for serialization)
// =========================================================================

// F1a (ADR-024) — read floating-mode ExtState keys into in-memory state.
// All keys absent → m_floating stays false (backward compat with v1.5.x).
// Geometry defaults (100, 100, 800, 600) preserved if x/y/w/h keys missing.
void MaxPaneContainer::LoadFloatingState()
{
  if (!g_GetExtState) return;
  const char* en = g_GetExtState(m_extSection, "float_enabled");
  m_floating = (en && en[0] == '1');
  auto readInt = [&](const char* key, int defaultVal) -> int {
    const char* s = g_GetExtState(m_extSection, key);
    if (!s || !s[0]) return defaultVal;
    // Audit M3.4 — last persisted-input reader still on raw atoi. Clamp to
    // sane screen-coordinate space like every other parse site (negative is
    // legal: multi-monitor layouts extend left/up of the primary).
    return safe_atoi_clamped(s, -32000, 32000);
  };
  m_floatX = readInt("float_x", m_floatX);
  m_floatY = readInt("float_y", m_floatY);
  m_floatW = readInt("float_w", m_floatW);
  m_floatH = readInt("float_h", m_floatH);
  // C5 (ADR-027) — always-on-top flag.
  const char* aot = g_GetExtState(m_extSection, "float_always_on_top");
  m_floatAlwaysOnTop = (aot && aot[0] == '1');
  // U2/mb945 (ADR-066) — zoom state (honored on Win32 in Create).
  const char* zm = g_GetExtState(m_extSection, "float_maximized");
  m_floatMaximized = (zm && zm[0] == '1');
  // Sanity clamp on width/height — refuse pathologic values from corrupt
  // ExtState (e.g. user-edited reaper-extstate.ini). Below 200x100 we
  // wouldn't even fit a tab bar; above 8000x8000 is multi-monitor wraparound.
  if (m_floatW < 200)  m_floatW = 800;
  if (m_floatH < 100)  m_floatH = 600;
  if (m_floatW > 8000) m_floatW = 800;
  if (m_floatH > 8000) m_floatH = 600;
  DBG("[MaxPane] LoadFloatingState[%s]: floating=%d rect=(%d,%d %dx%d) maximized=%d aot=%d\n",
      m_extSection, m_floating ? 1 : 0, m_floatX, m_floatY, m_floatW, m_floatH,
      m_floatMaximized ? 1 : 0, m_floatAlwaysOnTop ? 1 : 0);
}

// F1a (ADR-024) — write floating-mode ExtState keys from in-memory state.
// Called on transition (Detach/Redock) and from Shutdown when floating
// (geometry preservation across REAPER restarts).
void MaxPaneContainer::SaveFloatingState()
{
  if (!g_SetExtState) return;
  g_SetExtState(m_extSection, "float_enabled", m_floating ? "1" : "0", true);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", m_floatX); g_SetExtState(m_extSection, "float_x", buf, true);
  snprintf(buf, sizeof(buf), "%d", m_floatY); g_SetExtState(m_extSection, "float_y", buf, true);
  snprintf(buf, sizeof(buf), "%d", m_floatW); g_SetExtState(m_extSection, "float_w", buf, true);
  snprintf(buf, sizeof(buf), "%d", m_floatH); g_SetExtState(m_extSection, "float_h", buf, true);
  g_SetExtState(m_extSection, "float_always_on_top", m_floatAlwaysOnTop ? "1" : "0", true);
  // U2/mb945 (ADR-066) — zoom state, Win32-only writer (always "0" elsewhere).
  g_SetExtState(m_extSection, "float_maximized", m_floatMaximized ? "1" : "0", true);
}

// F1a (ADR-024) — read current top-level window rect into m_float{X,Y,W,H}.
// Used before transitions and from Shutdown. No-op if not floating or
// HWND missing. Validates rect before writing — if width or height are
// pathologic (e.g. WM_SIZE fired during mid-destroy with collapsed
// geometry like 0x28 we observed in the wild after a botched smoke
// test) the in-memory state is preserved; otherwise SaveFloatingState
// would later persist a window the user could never see.
void MaxPaneContainer::CaptureFloatGeometry()
{
  if (!m_floating || !m_hwnd) return;
#ifdef _WIN32
  // U2/mb945 (ADR-066) — true-maximize persistence. A maximized window's
  // GetWindowRect is just an oversized rect; restoring it produced a large
  // but NOT maximized window, and un-maximizing had nothing to return to.
  // Persist the zoom flag separately and keep the RESTORED geometry
  // (rcNormalPosition) in m_float* so both states survive a restart.
  // SWELL has no IsZoomed — mac/Linux keep the size-only behavior.
  if (IsZoomed(m_hwnd)) {
    WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
    if (GetWindowPlacement(m_hwnd, &wp)) {
      const RECT& nr = wp.rcNormalPosition;
      int nw = nr.right - nr.left;
      int nh = nr.bottom - nr.top;
      if (nw >= 200 && nh >= 100 && nw <= 8000 && nh <= 8000) {
        m_floatX = nr.left;
        m_floatY = nr.top;
        m_floatW = nw;
        m_floatH = nh;
      }
    }
    m_floatMaximized = true;
    return;
  }
  m_floatMaximized = false;
#endif
  RECT r;
  if (!GetWindowRect(m_hwnd, &r)) return;
  int w = r.right - r.left;
  int h = r.bottom - r.top;
  if (w < 200 || h < 100 || w > 8000 || h > 8000) return;
  m_floatX = r.left;
  m_floatY = r.top;
  m_floatW = w;
  m_floatH = h;
}

// F-B (forum v2.0.6) — persist current floating geometry to ExtState now.
// The only pre-existing saver was Shutdown()'s `!g_atexitSaved && m_floating`
// block, but the two real session-end paths bypass Shutdown(): onAtExit (Cmd+Q
// / close-session, see main.cpp) and WM_DESTROY (REAPER docker close button).
// Both now call this so the last on-screen position/size survives a reopen
// instead of falling back to defaults (100,100,800,600) on the primary monitor.
// No-op when docked; CaptureFloatGeometry rejects pathologic teardown rects.
void MaxPaneContainer::PersistFloatingGeometry()
{
  if (!m_floating) return;
  CaptureFloatGeometry();
  SaveFloatingState();
}

void MaxPaneContainer::SaveState()
{
  // Don't persist the temporary solo layout — it would overwrite the real tree
  if (m_soloActive) return;

  // Per ADR-013: global `current_state_*` ExtState writes were removed
  // (LoadState no longer reads them). Per-project state is saved via
  // project_config_extension_t (REAPER calls SaveExtensionConfig when saving
  // the RPP). We additionally write to ProjExtState here for immediate
  // in-session recovery on project switch.
  if (g_EnumProjects) {
    ReaProject* proj = g_EnumProjects(-1, nullptr, 0);
    if (proj)
      // U3 (ADR-065) — m_visible is still true during Toggle→Shutdown→
      // SaveState (it flips at Shutdown's tail), so the deliberate-close
      // paths mark themselves via m_userClosing instead.
      m_wsMgr->SaveProjectState(proj, m_tree, m_winMgr,
                                m_visible && !m_userClosing);
  }
}

// Shared stale-action cleanup. Used at REAPER startup via startupTimerFunc —
// runs BEFORE any MaxPaneContainer is created, so it must not touch
// m_extSection / member state.
// Strategy:
//   state > 0  → single toggle (REAPER says open, action will close)
//   state == 0 → if window exists despite state=0 (wnd_vis quirk), double-toggle
//                to resync (open then close, properly updating wnd_vis)
//   state ==-1 → WM_CLOSE + hide when visible, defer when not (Sprint 1
//                Entry 8 — a toggle would START the script, so never toggle)
// U1 — cheap top-level ghost probe for the per-tick startup poll. Ghosts
// REAPER restores from its wnd_vis cache come back as top-level floats with
// their exact title (or the "(docked)" dock-frame variant), so two indexed
// FindWindowEx lookups catch the classic case at the same one-tick latency
// as the full FindReaperWindow walk — without the EnumWindows + recursive
// child-walk + module-enumeration cascade that cost ~10-20ms per entry per
// tick and stalled Windows launches for minutes (forum #47/#65).
static HWND FindTopLevelGhost(const char* title)
{
  if (!title || !title[0]) return nullptr;
  char dockedTitle[512];
  snprintf(dockedTitle, sizeof(dockedTitle), "%s%s", title, DOCKED_TITLE_SUFFIX);
  HWND h = FindWindowEx(nullptr, nullptr, nullptr, dockedTitle);
  if (!h) h = FindWindowEx(nullptr, nullptr, nullptr, title);
  return h;
}

void RemoveActionFromStaleLists(int action)
{
  if (action <= 0 || !g_GetExtState || !g_SetExtState) return;
  for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++) {
    char section[32];
    if (i == 0) snprintf(section, sizeof(section), "%s", EXT_SECTION);
    else        snprintf(section, sizeof(section), "%s_%d", EXT_SECTION, i);
    const char* staleStr = g_GetExtState(section, "stale_toggle_actions");
    if (!staleStr || !staleStr[0]) continue;
    int arr[ACTION_LIST_MAX];
    int cnt = ParseActionList(staleStr, arr, ACTION_LIST_MAX);
    int kept[ACTION_LIST_MAX];
    int keptCnt = 0;
    for (int j = 0; j < cnt; j++)
      if (arr[j] != action) kept[keptCnt++] = arr[j];
    if (keptCnt == cnt) continue;
    char buf[ACTION_LIST_BUF];
    SerializeActionList(kept, keptCnt, buf, sizeof(buf));
    g_SetExtState(section, "stale_toggle_actions", buf, true);
    DBG("[MaxPane] StaleList[%s]: removed %d (deliberate open), left '%s'\n",
        section, action, buf);
  }
}

// Returns true if a non-empty stale list was found.
bool ProcessStaleActionsForSection(const char* section, bool deepProbe)
{
  if (!section || !g_GetExtState || !g_SetExtState || !g_Main_OnCommand) return false;

  const char* staleStr = g_GetExtState(section, "stale_toggle_actions");
  if (!staleStr || !staleStr[0]) return false;

  DBG("[MaxPane] StaleCleanup[%s]: stale actions: %s\n", section, staleStr);

  int staleArr[ACTION_LIST_MAX];
  int staleCnt = ParseActionList(staleStr, staleArr, ACTION_LIST_MAX);

  int deferred[ACTION_LIST_MAX];
  int deferredCnt = 0;
  for (int i = 0; i < staleCnt; i++) {
    int state = g_GetToggleCommandState ? g_GetToggleCommandState(staleArr[i]) : -1;
    DBG("[MaxPane] StaleCleanup[%s]: action=%d state=%d\n", section, staleArr[i], state);
    if (state > 0) {
      g_Main_OnCommand(staleArr[i], 0);
      // Force-hide via FindReaperWindow if toggle didn't actually close it.
      char searchTitle1[256];
      if (GetSearchTitleForAction(staleArr[i], searchTitle1, sizeof(searchTitle1))) {
        HWND h1 = WindowManager::FindReaperWindow(searchTitle1);
        bool still1 = (h1 && IsWindow(h1) && IsWindowVisible(h1));
        if (still1) {
          // ADR-052 follow-up (B16 pattern) — the toggle can no-op on a
          // window REAPER restored from cached wnd_vis (main-toolbar ghost:
          // stillVis=1 at three consecutive startups in the smoke log).
          // WM_CLOSE runs REAPER's own close handler so wnd_vis actually
          // updates and the ghost stops re-spawning at the next startup.
          // SW_HIDE stays as the visual fallback, same as the state==-1
          // branch below.
          SendMessage(h1, WM_CLOSE, 0, 0);
          if (IsWindow(h1) && IsWindowVisible(h1)) ShowWindow(h1, SW_HIDE);
        }
        DBG("[MaxPane] StaleCleanup[%s]: toggled off action=%d stillVis=%d%s\n",
            section, staleArr[i], still1, still1 ? " (WM_CLOSE+hide)" : "");
      } else {
        DBG("[MaxPane] StaleCleanup[%s]: toggled off action=%d (no reverse lookup)\n",
            section, staleArr[i]);
      }
    } else if (state == -1) {
      // Sprint 1 Entry 8 — fire-and-show actions (Region Render Matrix
      // 41888 + similar) and ReaImGui scripts report state == -1 because
      // they have no on/off toggle state. The previous hard SKIP left
      // them as floating ghosts after REAPER restart. Symmetric WM_CLOSE
      // + SW_HIDE flow to the state == 0 branch below — REAPER's window
      // close handler runs and cleans up; if the window isn't currently
      // visible, defer to the next startup tick.
      char searchTitleN1[256];
      bool handledN1 = false;
      if (GetSearchTitleForAction(staleArr[i], searchTitleN1, sizeof(searchTitleN1))) {
        HWND hN1 = deepProbe ? WindowManager::FindReaperWindow(searchTitleN1)
                             : FindTopLevelGhost(searchTitleN1);
        if (hN1 && IsWindow(hN1) && IsWindowVisible(hN1)) {
          SendMessage(hN1, WM_CLOSE, 0, 0);
          if (IsWindow(hN1) && IsWindowVisible(hN1)) ShowWindow(hN1, SW_HIDE);
          DBG("[MaxPane] StaleCleanup[%s]: state=-1 action=%d WM_CLOSE/hide hwnd=%p\n",
              section, staleArr[i], (void*)hN1);
          handledN1 = true;
        }
      }
      if (!handledN1) {
        AppendUniqueAction(deferred, &deferredCnt, ACTION_LIST_MAX, staleArr[i]);
        DBG("[MaxPane] StaleCleanup[%s]: state=-1 action=%d deferred (not visible)\n",
            section, staleArr[i]);
      }
    } else {
      // state == 0 — REAPER says closed; check visually
      char searchTitle[256];
      bool handled = false;
      if (GetSearchTitleForAction(staleArr[i], searchTitle, sizeof(searchTitle))) {
        HWND h = deepProbe ? WindowManager::FindReaperWindow(searchTitle)
                           : FindTopLevelGhost(searchTitle);
        if (h && IsWindow(h) && IsWindowVisible(h)) {
          // Double toggle: open (0→1) then close (1→0) — sync wnd_vis
          g_Main_OnCommand(staleArr[i], 0);
          g_Main_OnCommand(staleArr[i], 0);
          bool stillVisible = (IsWindow(h) && IsWindowVisible(h));
          if (stillVisible) ShowWindow(h, SW_HIDE);
          DBG("[MaxPane] StaleCleanup[%s]: double-toggled action=%d hwnd=%p stillVis=%d\n",
              section, staleArr[i], (void*)h, stillVisible);
          handled = true;
        } else {
          DBG("[MaxPane] StaleCleanup[%s]: state=0 action=%d — not found/visible, deferring\n",
              section, staleArr[i]);
        }
      }
      if (!handled) {
        AppendUniqueAction(deferred, &deferredCnt, ACTION_LIST_MAX, staleArr[i]);
      }
    }
  }

  if (deferredCnt > 0) {
    char remaining[ACTION_LIST_BUF];
    SerializeActionList(deferred, deferredCnt, remaining, sizeof(remaining));
    g_SetExtState(section, "stale_toggle_actions", remaining, true);
    DBG("[MaxPane] StaleCleanup[%s]: deferred remaining: %s\n", section, remaining);
  } else {
    g_SetExtState(section, "stale_toggle_actions", "", true);
    DBG("[MaxPane] StaleCleanup[%s]: all stale actions cleaned up immediately\n", section);
  }
  return true;
}

// Shared stale-list merger. Reads existing stale_toggle_actions for `section`
// (preserves entries left by prior workspace switches), dedupes against the
// action IDs of currently-captured tabs, writes the merged list back.
// Used by MaxPaneContainer::Toggle (user-initiated close) and onAtExit (Cmd+Q).
void MergeCapturesIntoStaleListForSection(const char* section, const WindowManager& wm)
{
  if (!section || !g_GetExtState || !g_SetExtState) return;

  int merged[ACTION_LIST_MAX];
  int cnt = 0;
  auto addUnique = [&](int act) {
    AppendUniqueAction(merged, &cnt, ACTION_LIST_MAX, act);
  };

  // 1) Existing stale entries (e.g. from a prior workspace switch).
  const char* prev = g_GetExtState(section, "stale_toggle_actions");
  if (prev && prev[0]) {
    int prevArr[ACTION_LIST_MAX];
    int prevCnt = ParseActionList(prev, prevArr, ACTION_LIST_MAX);
    for (int i = 0; i < prevCnt; i++) addUnique(prevArr[i]);
  }
  // 2) Currently captured tabs across all panes.
  for (int p = 0; p < MAX_PANES; p++) {
    const PaneState* ps = wm.GetPaneState(p);
    if (!ps) continue;
    for (int t = 0; t < ps->tabCount; t++) addUnique(ps->tabs[t].toggleAction);
  }
  // 3) Sprint 1 Entry 8 — every action ID referenced by every saved workspace
  //    slot. Without this, captures released on workspace SWITCH (not still
  //    in m_panes) never landed in the stale list and survived REAPER restart
  //    as floating ghosts. Workspace data lives at EXT_SECTION (shared per
  //    ADR-012), so multi-instance setups benefit from a single pass.
  if (EXT_SECTION && g_GetExtState) {
    const char* wsCountStr = g_GetExtState(EXT_SECTION, "ws_count");
    int wsCount = wsCountStr ? safe_atoi_clamped(wsCountStr, 0, MAX_WORKSPACES) : 0;
    char key[64];
    for (int ws = 0; ws < wsCount; ws++) {
      for (int p = 0; p < MAX_PANES; p++) {
        for (int t = 0; t < MAX_TABS_PER_PANE; t++) {
          snprintf(key, sizeof(key), "ws_%d_pane_%d_tab_%d", ws, p, t);
          const char* v = g_GetExtState(EXT_SECTION, key);
          if (!v || !v[0]) continue;
          // Audit M2.7 — shared "arb:" parser (config.cpp) instead of the
          // second hand-rolled copy of the format.
          int actionId = 0;
          ArbSpec spec;
          if (ParseArbSpec(v, &spec)) actionId = spec.action;
          else                        actionId = LookupToggleAction(v);
          // ADR-052 follow-up — slots saved before the Main-toolbar capture
          // exclusion can still hold 'arb:41651:*'. Re-arming it here kept
          // the startup ghost cycle alive forever (the cleanup toggle no-ops
          // on the window, so wnd_vis re-caches open at every quit).
          if (actionId == MAIN_TOOLBAR_ACTION) continue;
          addUnique(actionId);
        }
      }
    }
  }
  // 3) Write merged list.
  char buf[ACTION_LIST_BUF];
  SerializeActionList(merged, cnt, buf, sizeof(buf));
  g_SetExtState(section, "stale_toggle_actions", buf, true);
  DBG("[MaxPane] StaleListMerge[%s]: '%s'\n", section, buf);
}

// Append a single action ID to stale_toggle_actions for `section`. Dedupes
// against existing entries. Called from close-tab paths in container.cpp
// before WindowManager wipes the tab (action ID lives only in PaneState
// until then). Without this write, any close that races with REAPER's
// wnd_vis cache (B13/B16 — Actions, FX Browser, etc.) leaks a ghost on
// the next REAPER startup that ProcessStaleActionsForSection can't clean.
void AppendActionToStaleListForSection(const char* section, int action)
{
  if (!section || action <= 0 || !g_GetExtState || !g_SetExtState) return;

  // Read existing list, dedupe.
  int existing[ACTION_LIST_MAX];
  int cnt = 0;
  const char* prev = g_GetExtState(section, "stale_toggle_actions");
  if (prev && prev[0]) cnt = ParseActionList(prev, existing, ACTION_LIST_MAX);
  if (!AppendUniqueAction(existing, &cnt, ACTION_LIST_MAX, action)) return;

  char buf[ACTION_LIST_BUF];
  SerializeActionList(existing, cnt, buf, sizeof(buf));
  g_SetExtState(section, "stale_toggle_actions", buf, true);
  DBG("[MaxPane] StaleAppend[%s]: +%d → '%s'\n", section, action, buf);
}

void MaxPaneContainer::LoadState()
{
  if (!g_GetExtState) return;

  // Per ADR-013 (workspace launcher model): only project-bound state restores
  // captures at startup. If the user opens a .rpp that carries MaxPane state
  // OR has per-project ProjExtState, we load it. Otherwise MaxPane opens
  // empty — the user explicitly loads a workspace (or captures a window) to
  // populate it. The legacy global `current_state_*` ExtState fallback and
  // the startup stale-actions cleanup are gone — those mechanisms were the
  // root of B3/B12/B15/B17/B18/B22/B26 (see ADR-013).

  NodeSnapshot snap[MAX_TREE_NODES];
  int nodeCount = 0;
  PaneSnapshot panes[MAX_PANES];
  memset(panes, 0, sizeof(panes));
  bool hasTreeFormat = false;
  bool loaded = false;

  if (g_pendingProjectState[m_instanceId].valid) {
    DBG("[MaxPane] LoadState: using pending RPP state (%d lines)\n",
        g_pendingProjectState[m_instanceId].lineCount);
    RppReadAccessor rppAcc(g_pendingProjectState[m_instanceId].lines, g_pendingProjectState[m_instanceId].lineCount);
    const char* treeVer = rppAcc.Get(m_extSection, "tree_version");
    hasTreeFormat = (treeVer && strcmp(treeVer, "2") == 0);
    if (hasTreeFormat) {
      memset(snap, 0, sizeof(snap));
      nodeCount = WorkspaceManager::ReadTreeNodesStatic(m_extSection, "", snap, rppAcc);
      if (nodeCount > 0) {
        WorkspaceManager::ReadPaneTabsStatic(m_extSection, "", panes, MAX_PANES, rppAcc);
        loaded = true;
        DBG("[MaxPane] LoadState: loaded RPP state (nodes=%d)\n", nodeCount);
      }
    }
    g_pendingProjectState[m_instanceId].valid = false;  // consumed
    m_pendingRppLoad = false;
  }

  if (!loaded && g_EnumProjects) {
    ReaProject* proj = g_EnumProjects(-1, nullptr, 0);
    DBG("[MaxPane] LoadState: project=%p, hasState=%d\n",
        proj, proj ? m_wsMgr->HasProjectState(proj) : -1);
    if (proj && m_wsMgr->HasProjectState(proj)) {
      loaded = m_wsMgr->LoadProjectState(proj, snap, nodeCount, panes, hasTreeFormat);
      DBG("[MaxPane] LoadState: loaded per-project ProjExtState (nodes=%d)\n", nodeCount);
      m_pendingRppLoad = false;
    }
  }

  if (loaded && hasTreeFormat && nodeCount >= 1) {
    if (!m_tree.LoadSnapshot(snap, nodeCount)) {
      DBG("[MaxPane] LoadState: corrupt tree in project state, resetting to empty\n");
      m_tree.Reset();
      memset(panes, 0, sizeof(panes));
      // Audit M1.2 — this used to be Release-silent: the user opened a
      // project and found MaxPane empty with no explanation and no log.
      ShowToast("MaxPane layout reset — saved state in this project couldn't be read.");
    }
  } else {
    // No RPP / no project state → empty launcher (ADR-013)
    m_tree.Reset();
    memset(panes, 0, sizeof(panes));
    DBG("[MaxPane] LoadState: empty launcher (no RPP, no project state)\n");
  }

  ApplyPaneState(panes, MAX_PANES, true);
}

// F-39 — swap an already-open container's contents to the current project's
// saved state. projStateOpenTimer Creates a CLOSED container (which loads state
// via Create→LoadState); but when MaxPane is already open and a project with
// saved captures loads, we must drop the current captures and reload, or the
// new project's windows come back floating outside MaxPane. Mirrors the
// OnTimer deferred-RPP reload but routes through LoadState so it covers both
// the chunk and ProjExtState restore paths.
void MaxPaneContainer::ReloadProjectState()
{
  if (!m_hwnd) return;
  DBG("[MaxPane] ReloadProjectState: swapping contents to newly-loaded project\n");
  m_captureQueue->CancelAll();
  m_winMgr.ReleaseAll(false);   // detach + hide current captures (no toggle)
  LoadState();                  // tree + ApplyPaneState for the current project
  RECT rc;
  GetClientRect(m_hwnd, &rc);
  m_tree.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
  m_winMgr.RepositionAll(m_tree);
  InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MaxPaneContainer::ApplyPaneState(const PaneSnapshot* panes, int maxPanes, bool deferActions)
{
  bool needsCaptureTimer = false;

  for (int i = 0; i < maxPanes && i < MAX_PANES; i++) {
    if (!m_tree.IsPaneIdUsed(i)) continue;
    if (panes[i].tabCount == 0) continue;

    bool paneQueued = false;  // F-40 — any capture in this pane routed async
    for (int t = 0; t < panes[i].tabCount && t < MAX_TABS_PER_PANE; t++) {
      const char* winName = panes[i].tabs[t].name;
      if (!winName[0]) continue;

      if (panes[i].tabs[t].isArbitrary) {
        int arbAction = panes[i].tabs[t].toggleAction;
        const char* arbCmd = panes[i].tabs[t].actionCommand;
        // If saved state has no action, check favorites for a matching entry
        if ((!arbCmd || !arbCmd[0]) && m_favMgr) {
          int favIdx = m_favMgr->FindByName(winName);
          if (favIdx >= 0) {
            const FavoriteEntry& fav = m_favMgr->Get(favIdx);
            if (fav.actionCommand[0] && strcmp(fav.actionCommand, "0") != 0) {
              arbAction = fav.toggleAction;
              arbCmd = fav.actionCommand;
              DBG("[MaxPane] ApplyPaneState: enriched '%s' from favorites: action=%d cmd='%s'\n",
                  winName, arbAction, arbCmd);
            }
          }
        }
        // Dynamic-title windows (e.g. MIDI Editor): search by prefix
        const char* dynPrefix = GetDynamicTitlePrefix(winName);
        const char* searchName = dynPrefix ? dynPrefix : winName;
        HWND h = WindowManager::FindReaperWindow(searchName, m_hwnd);
        if (h) {
          m_winMgr.CaptureArbitraryWindow(i, h, winName, m_hwnd, arbAction, arbCmd);
        } else {
          m_captureQueue->EnqueueArbitrary(i, searchName, arbAction, arbCmd, deferActions);
          needsCaptureTimer = true;
          paneQueued = true;
        }
      } else {
        for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
          if (strcmp(KNOWN_WINDOWS[j].name, winName) == 0) {
            HWND h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].searchTitle, m_hwnd);
            if (!h && KNOWN_WINDOWS[j].altSearchTitle) {
              h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].altSearchTitle, m_hwnd);
            }
            if (!h && g_Main_OnCommand) {
              m_captureQueue->EnqueueKnown(i, j, deferActions);
              needsCaptureTimer = true;
              paneQueued = true;
            } else if (h) {
              m_winMgr.CaptureByIndex(i, j, m_hwnd);
            }
            break;
          }
        }
      }
    }

    if (paneQueued) {
      // F-40 — at least one tab here is being captured asynchronously, so the
      // live order is still filling and will land in capture-completion order,
      // not the saved order. Stash the saved order and defer color/pinned/
      // active to FinalizeRestoreOrder() (applying them now would target the
      // wrong, still-filling positions).
      RestoreOrderPane& ro = m_restoreOrder[i];
      ro.tabCount = panes[i].tabCount;
      ro.activeTab = panes[i].activeTab;
      for (int t = 0; t < panes[i].tabCount && t < MAX_TABS_PER_PANE; t++) {
        safe_strncpy(ro.tabs[t].name, panes[i].tabs[t].name, sizeof(ro.tabs[t].name));
        safe_strncpy(ro.tabs[t].actionCmd, panes[i].tabs[t].actionCommand,
                     sizeof(ro.tabs[t].actionCmd));
        ro.tabs[t].colorIndex = panes[i].tabs[t].colorIndex;
        ro.tabs[t].pinned = panes[i].tabs[t].pinned;
      }
      m_paneAwaitingReorder[i] = true;
    } else {
      // All tabs captured synchronously, in snapshot order — apply now.
      // Restore tab colors + pinned state from snapshot (C2 — ADR-027).
      // Pinned must be re-applied via SetTabPinned (not raw flag write) so
      // the tab gets sorted to its proper position.
      int loadedTabs = m_winMgr.GetTabCount(i);
      for (int t = 0; t < loadedTabs && t < panes[i].tabCount; t++) {
        int ci = panes[i].tabs[t].colorIndex;
        if (ci > 0 && ci < TAB_COLOR_COUNT) {
          m_winMgr.SetTabColor(i, t, ci);
        }
      }
      for (int snapT = 0; snapT < panes[i].tabCount; snapT++) {
        if (!panes[i].tabs[snapT].pinned) continue;
        const char* snapName = panes[i].tabs[snapT].name;
        if (!snapName[0]) continue;
        // Find matching loaded tab by name and pin it.
        for (int liveT = 0; liveT < m_winMgr.GetTabCount(i); liveT++) {
          const TabEntry* live = m_winMgr.GetTab(i, liveT);
          if (live && strcmp(live->name, snapName) == 0) {
            m_winMgr.SetTabPinned(i, liveT, true);
            break;
          }
        }
      }

      // Set active tab
      if (panes[i].activeTab >= 0 && panes[i].activeTab < m_winMgr.GetTabCount(i)) {
        m_winMgr.SetActiveTab(i, panes[i].activeTab);
      }
    }
  }

  if (needsCaptureTimer) {
    StartCaptureTimer();
  }
}

// F-40 — match a live tab to a saved restore slot. Prefer the stable action
// command / FX identity (unique per FX / known window); fall back to display
// name. Mirrors the round-trip keys used by Write/ReadPaneTabsStatic.
static bool TabMatchesSaved(const TabEntry& live, const RestoreOrderSlot& saved)
{
  if (saved.actionCmd[0] && live.actionCmd[0])
    return strcmp(saved.actionCmd, live.actionCmd) == 0;
  return strcmp(saved.name, live.name) == 0;
}

void MaxPaneContainer::FinalizeRestoreOrder()
{
  for (int p = 0; p < MAX_PANES; p++) {
    if (!m_paneAwaitingReorder[p]) continue;
    m_paneAwaitingReorder[p] = false;

    const RestoreOrderPane& want = m_restoreOrder[p];
    if (m_winMgr.GetTabCount(p) <= 0) continue;

    // 1. Apply pinned flags by identity. SetTabPinned re-partitions, but step 2
    //    reorders to the exact saved sequence afterward, so the transient
    //    partition order here is irrelevant.
    for (int s = 0; s < want.tabCount && s < MAX_TABS_PER_PANE; s++) {
      if (!want.tabs[s].pinned) continue;
      for (int j = 0; j < m_winMgr.GetTabCount(p); j++) {
        const TabEntry* t = m_winMgr.GetTab(p, j);
        if (t && TabMatchesSaved(*t, want.tabs[s])) {
          m_winMgr.SetTabPinned(p, j, true);
          break;
        }
      }
    }

    // 2. Reorder live tabs to the saved sequence: selection-place each saved
    //    slot from its current position, recording its final live index. Saved
    //    tabs that never materialized (e.g. an FX that failed to resolve) map
    //    to -1; unmatched live tabs keep their relative order at the end. Once
    //    a slot is placed at `target`, later placements only touch indices
    //    > target, so the recorded index stays valid.
    int slotToLive[MAX_TABS_PER_PANE];
    for (int s = 0; s < MAX_TABS_PER_PANE; s++) slotToLive[s] = -1;
    int target = 0;
    for (int s = 0; s < want.tabCount && s < MAX_TABS_PER_PANE &&
                    target < m_winMgr.GetTabCount(p); s++) {
      int found = -1;
      for (int j = target; j < m_winMgr.GetTabCount(p); j++) {
        const TabEntry* t = m_winMgr.GetTab(p, j);
        if (t && TabMatchesSaved(*t, want.tabs[s])) { found = j; break; }
      }
      if (found < 0) continue;
      if (found != target) m_winMgr.ReorderTab(p, found, target);
      slotToLive[s] = target;
      target++;
    }

    // 3. Re-derive color + active from each saved slot via its final live index
    //    (identity-stable — correct even when some saved tabs never appeared,
    //    which would shift a position-based mapping onto the wrong tabs).
    for (int s = 0; s < want.tabCount && s < MAX_TABS_PER_PANE; s++) {
      if (slotToLive[s] < 0) continue;
      int ci = want.tabs[s].colorIndex;
      if (ci > 0 && ci < TAB_COLOR_COUNT) m_winMgr.SetTabColor(p, slotToLive[s], ci);
    }
    if (want.activeTab >= 0 && want.activeTab < MAX_TABS_PER_PANE &&
        slotToLive[want.activeTab] >= 0) {
      m_winMgr.SetActiveTab(p, slotToLive[want.activeTab]);
    }

    InvalidateRect(m_hwnd, nullptr, FALSE);  // repaint tab bar in corrected order
  }
}

// =========================================================================
// Workspace management (delegates to WorkspaceManager)
// =========================================================================

// Feature B — async workspace save with progress toast.
// Phase 1 (this function, on GUI thread): snapshot the live tree + tab state
// into m_wsMgr's pending buffer, commit to m_workspaces[] in memory (so the
// launcher card appears at next repaint), record metadata for the eventual
// completion toast, arm the one-shot deferred-flush timer.
// Phase 2 (OnWorkspaceFlushTick, off the click frame): pops one queue entry,
// writes ExtState (the hundreds-of-Set freeze that used to hang the click),
// re-arms if the queue still has depth, then transitions the toast from
// sticky "Saving '<X>'…" to timed "Saved '<X>'" / "Replaced '<X>'".
void MaxPaneContainer::SaveWorkspace(const char* name)
{
  if (!name || !name[0]) return;
  // Audit M1.2 — the 32-slot cap used to be enforced silently deep in
  // CommitSnapshotToSlot while the completion toast still said "Saved '<X>'".
  // Refuse up front with an honest toast instead.
  if (!m_wsMgr->Find(name) && m_wsMgr->GetCount() >= MAX_WORKSPACES) {
    char msg[192];
    snprintf(msg, sizeof(msg),
             "Workspace list is full (%d) — delete one to save '%s'.",
             MAX_WORKSPACES, name);
    ShowToast(msg);
    return;
  }
  // Feature B — async save: determine "Saved" vs "Replaced" wording NOW
  // while pre-save state is still visible; EnqueueSave commits to the slot;
  // OnWorkspaceFlushTick fires the completion toast off the click frame.
  m_pendingWorkspaceWasReplace = (m_wsMgr->Find(name) != nullptr);
  safe_strncpy(m_pendingWorkspaceName, name, sizeof(m_pendingWorkspaceName));

  m_wsMgr->EnqueueSave(name, m_tree, m_winMgr);
  ArmWorkspaceFlushTimer();

  // Feature A — saving establishes (or refreshes) the active workspace and
  // clears the dirty marker. Update the in-mem label immediately so the nav-
  // bar tracks the user's intent; the deferred flush persists the data and
  // the toast confirms completion.
  SetCurrentWorkspace(name);

  DBG("[MaxPane] SaveWorkspace: enqueued '%s' (wasReplace=%d)\n",
      name, m_pendingWorkspaceWasReplace ? 1 : 0);
}

void MaxPaneContainer::LoadWorkspace(const char* name)
{
  if (!name || !name[0]) return;

  // Exit solo before loading workspace
  if (m_soloActive) ToggleSolo(m_soloPaneId);

  const WorkspaceEntry* ws = m_wsMgr->Find(name);
  if (!ws) return;

  DBG("[MaxPane] === LoadWorkspace '%s' BEGIN ===\n", name);

#ifdef MAXPANE_DEBUG
  // --- Dump current state ---
  DBG("[MaxPane] LW: Current captured tabs:\n");
  for (int i = 0; i < MAX_PANES; i++) {
    const PaneState* ps = m_winMgr.GetPaneState(i);
    if (!ps || ps->tabCount == 0) continue;
    for (int t = 0; t < ps->tabCount; t++) {
      int lookupAct = LookupToggleAction(ps->tabs[t].name);
      DBG("[MaxPane] LW:   pane %d tab %d: name='%s' toggleAction=%d lookup=%d captured=%d hwnd=%p isArb=%d\n",
          i, t, ps->tabs[t].name, ps->tabs[t].toggleAction, lookupAct,
          ps->tabs[t].captured, (void*)ps->tabs[t].hwnd, ps->tabs[t].isArbitrary);
    }
  }
#endif

  // --- Dump target workspace ---
  DBG("[MaxPane] LW: Target workspace '%s' tabs:\n", name);
  for (int p = 0; p < MAX_PANES; p++) {
    if (ws->panes[p].tabCount == 0) continue;
    for (int t = 0; t < ws->panes[p].tabCount; t++) {
      DBG("[MaxPane] LW:   pane %d tab %d: name='%s' toggleAction=%d isArb=%d cmd='%s'\n",
          p, t, ws->panes[p].tabs[t].name, ws->panes[p].tabs[t].toggleAction,
          ws->panes[p].tabs[t].isArbitrary, ws->panes[p].tabs[t].actionCommand);
    }
  }

  // --- Stale action tracking ---
  // Build set of toggle-action IDs in the NEW workspace.
  int newActions[MAX_PANES * MAX_TABS_PER_PANE];
  int newActionCount = 0;
  for (int p = 0; p < MAX_PANES; p++) {
    for (int t = 0; t < ws->panes[p].tabCount; t++) {
      int act = ws->panes[p].tabs[t].toggleAction;
      // Also resolve from name for old workspace data
      if (act <= 0) act = LookupToggleAction(ws->panes[p].tabs[t].name);
      if (act > 0 && newActionCount < MAX_PANES * MAX_TABS_PER_PANE) {
        newActions[newActionCount++] = act;
        DBG("[MaxPane] LW: newAction[%d] = %d (from '%s')\n", newActionCount - 1, act, ws->panes[p].tabs[t].name);
      }
    }
  }

  // Read existing stale list from ext state (accumulated from prior switches).
  int staleActions[ACTION_LIST_MAX];
  int staleCount = 0;
  if (g_GetExtState) {
    const char* s = g_GetExtState(m_extSection, "stale_toggle_actions");
    if (s && s[0]) {
      DBG("[MaxPane] LW: existing stale ext state: '%s'\n", s);
      staleCount = ParseActionList(s, staleActions, ACTION_LIST_MAX);
    }
  }

  // Add currently captured windows NOT in the new workspace to stale list.
  for (int i = 0; i < MAX_PANES; i++) {
    const PaneState* ps = m_winMgr.GetPaneState(i);
    if (!ps) continue;
    for (int t = 0; t < ps->tabCount; t++) {
      int act = ps->tabs[t].toggleAction;
      // Resolve toggle action from window title if missing (old workspace data)
      if (act <= 0) act = LookupToggleAction(ps->tabs[t].name);
      if (act <= 0) {
        DBG("[MaxPane] LW: SKIP pane %d tab %d '%s' — no toggle action (even after lookup)\n",
            i, t, ps->tabs[t].name);
        continue;
      }
      bool inNew = false;
      for (int n = 0; n < newActionCount; n++) {
        if (newActions[n] == act) { inNew = true; break; }
      }
      if (inNew) {
        DBG("[MaxPane] LW: SHARED pane %d tab %d '%s' action=%d — in new workspace\n",
            i, t, ps->tabs[t].name, act);
        continue;
      }
      if (AppendUniqueAction(staleActions, &staleCount, ACTION_LIST_MAX, act)) {
        DBG("[MaxPane] LW: STALE pane %d tab %d '%s' action=%d — added to stale list\n",
            i, t, ps->tabs[t].name, act);
      }
    }
  }

  // Remove from stale list any actions that are in the new workspace
  // (they'll be recaptured — no longer stale).
  {
    int kept = 0;
    for (int s = 0; s < staleCount; s++) {
      bool inNew = false;
      for (int n = 0; n < newActionCount; n++) {
        if (newActions[n] == staleActions[s]) { inNew = true; break; }
      }
      if (!inNew) staleActions[kept++] = staleActions[s];
    }
    staleCount = kept;
  }

  DBG("[MaxPane] LW: Final stale list (%d actions):", staleCount);
  for (int s = 0; s < staleCount; s++) DBG(" %d", staleActions[s]);
  DBG("\n");

  // Persist stale list — cleaned up at next startup (OnTimer deferred cleanup).
  // We do NOT toggle stale windows during the switch because SetParent(nullptr)
  // gives REAPER a different NSWindow that it can't track — Main_OnCommand
  // creates/closes DIFFERENT windows, leaving our HWNDs floating.
  // Instead, at next startup REAPER properly opens stale windows from wnd_vis,
  // and our deferred cleanup toggles them off with reliable toggle state.
  if (g_SetExtState) {
    char buf[ACTION_LIST_BUF];
    SerializeActionList(staleActions, staleCount, buf, sizeof(buf));
    g_SetExtState(m_extSection, "stale_toggle_actions", buf, true);
    DBG("[MaxPane] LW: saved stale list to ExtState: '%s'\n", buf);
  }

  // --- Release and reload ---
  // Release ALL without toggle — just hide + reparent. Stale windows remain
  // hidden; they'll be toggled off properly at next startup.
  m_winMgr.ReleaseAll(false);

  // B7: best-effort synchronous stale cleanup. After ReleaseAll(false) the
  // captured windows are reparented to REAPER main and hidden, but REAPER's
  // toggle state may still report them as open. Force-toggle the ones still
  // marked open so wnd_vis in reaper.ini gets the right value before any
  // force-quit between now and the next OnTimer firing. LoadState's deferred
  // cleanup at next startup remains the safety net for anything we miss
  // (notably state=0 wnd_vis quirks and windows REAPER opens later).
  if (g_Main_OnCommand && g_GetToggleCommandState) {
    [[maybe_unused]] int handled = 0;
    for (int s = 0; s < staleCount; s++) {
      int act = staleActions[s];
      int state = g_GetToggleCommandState(act);
      if (state != 1) continue;  // state=0: already off; state=-1: script, skip
      g_Main_OnCommand(act, 0);
      handled++;
      // Verify-hide: some windows (e.g. Actions) don't honor toggle-off
      // reliably mid-session — force-hide the HWND so user sees no ghost.
      char searchTitle[256];
      if (GetSearchTitleForAction(act, searchTitle, sizeof(searchTitle))) {
        HWND h = WindowManager::FindReaperWindow(searchTitle);
        if (h && IsWindow(h) && IsWindowVisible(h)) ShowWindow(h, SW_HIDE);
      }
      DBG("[MaxPane] LW: sync toggle off action=%d (was state=1)\n", act);
    }
    DBG("[MaxPane] LW: sync cleanup handled=%d/%d (rest deferred to next startup)\n",
        handled, staleCount);
  }

  if (ws->treeVersion == 2) {
    m_tree.LoadSnapshot(ws->nodes, ws->nodeCount);
  } else {
    int lp = ws->layoutPreset;
    if (lp < 0 || lp >= PRESET_COUNT) lp = 0;
    m_tree.BuildPreset((LayoutPreset)lp);
  }

  {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_tree.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
  }

  ApplyPaneState(ws->panes, MAX_PANES, false);
  m_winMgr.RepositionAll(m_tree);

  // Feature A — establish the active workspace label on success (after all
  // mutations that would otherwise call MarkWorkspaceDirty during release/
  // recapture have completed). Setting the name also clears m_workspaceDirty
  // and persists to ExtState so a subsequent Toggle/Create cycle restores
  // the label.
  SetCurrentWorkspace(name);

  InvalidateRect(m_hwnd, nullptr, FALSE);
  SaveState();
}

void MaxPaneContainer::DeleteWorkspace(const char* name)
{
  // Feature A — if the workspace being deleted is the active one, the nav
  // bar label loses its meaning; clear it so the bar drops back to "no
  // workspace loaded" state.
  if (name && name[0] && m_currentWorkspaceName[0] &&
      strcmp(name, m_currentWorkspaceName) == 0) {
    ClearCurrentWorkspace();
  }
  m_wsMgr->Delete(name);
}

// =========================================================================
// C1 (ADR-027) — Reopen last closed tab (ring buffer)
// =========================================================================

void MaxPaneContainer::RecordClosedTab(int paneId, int tabIndex)
{
  const TabEntry* t = m_winMgr.GetTab(paneId, tabIndex);
  if (!t || !t->captured) return;
  ClosedTabSnapshot& slot = m_recentClosed[m_recentClosedHead];
  safe_strncpy(slot.name,        t->name,        sizeof(slot.name));
  safe_strncpy(slot.searchTitle, t->searchTitle, sizeof(slot.searchTitle));
  safe_strncpy(slot.actionCmd,   t->actionCmd,   sizeof(slot.actionCmd));
  slot.toggleAction = t->toggleAction;
  slot.sourcePaneId = paneId;
  slot.isArbitrary  = t->isArbitrary;
  m_recentClosedHead = (m_recentClosedHead + 1) % RECENT_CLOSED_CAP;
  if (m_recentClosedCount < RECENT_CLOSED_CAP) m_recentClosedCount++;
}

bool MaxPaneContainer::ReopenLastClosedTab()
{
  if (m_recentClosedCount == 0) return false;
  // Pop newest: head points at next-write, so newest is at (head - 1) mod N.
  int idx = (m_recentClosedHead - 1 + RECENT_CLOSED_CAP) % RECENT_CLOSED_CAP;
  ClosedTabSnapshot snap = m_recentClosed[idx];
  // Wipe the popped slot so accidental re-press doesn't reopen the same tab
  // twice in a row from the buffer.
  m_recentClosed[idx] = {};
  m_recentClosedHead = idx;
  m_recentClosedCount--;

  // Target pane: prefer the source pane if it still exists; else focused;
  // else first used pane.
  int paneId = -1;
  if (snap.sourcePaneId >= 0 && m_tree.IsPaneIdUsed(snap.sourcePaneId)) {
    paneId = snap.sourcePaneId;
  } else if (m_focusedPaneId >= 0 && m_tree.IsPaneIdUsed(m_focusedPaneId)) {
    paneId = m_focusedPaneId;
  } else {
    for (int p = 0; p < MAX_PANES; p++) {
      if (m_tree.IsPaneIdUsed(p)) { paneId = p; break; }
    }
  }
  if (paneId < 0) return false;

  DBG("[MaxPane] ReopenLastClosedTab: name='%s' search='%s' action=%d cmd='%s' arb=%d pane=%d\n",
      snap.name, snap.searchTitle, snap.toggleAction, snap.actionCmd,
      snap.isArbitrary ? 1 : 0, paneId);

  // Replicate the ActivateFavorite-style find-or-queue dance: if the window
  // is already visible, capture directly; otherwise enqueue (which will fire
  // the toggle action when available and retry FindReaperWindow until it
  // appears).
  HWND found = WindowManager::FindReaperWindow(snap.searchTitle, m_hwnd);
  if (found) {
    char foundTitle[512];
    GetWindowText(found, foundTitle, sizeof(foundTitle));
    bool isDockFrame = (strstr(foundTitle, DOCKED_TITLE_SUFFIX) != nullptr);
    if (snap.isArbitrary && !isDockFrame) {
      m_captureQueue->EnqueueArbitrary(paneId, snap.searchTitle,
                                        snap.toggleAction, snap.actionCmd);
      StartCaptureTimer();
    } else {
      m_winMgr.CaptureArbitraryWindow(paneId, found, snap.name, m_hwnd,
                                       snap.toggleAction, snap.actionCmd);
      RefreshLayout();
      SaveState();
    }
    return true;
  }

  // Not visible — queue with action fire.
  m_captureQueue->EnqueueArbitrary(paneId, snap.searchTitle,
                                    snap.toggleAction, snap.actionCmd);
  StartCaptureTimer();
  return true;
}

// =========================================================================
// C4 (ADR-027) — Workspace pickup
// =========================================================================

void MaxPaneContainer::OpenWorkspacePickup()
{
  if (!g_GetUserInputs || !m_wsMgr) return;
  char buf[32] = "";
  char prompt[96];
  snprintf(prompt, sizeof(prompt), "Workspace # (1-%d):,extrawidth=80", MAX_WORKSPACES);
  if (!g_GetUserInputs("MaxPane: Workspace pickup", 1, prompt, buf, sizeof(buf))) {
    return;  // user cancelled
  }
  int slotOneBased = atoi(buf);
  if (slotOneBased < 1 || slotOneBased > MAX_WORKSPACES) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Invalid workspace #: %s", buf);
    ShowToast(msg);
    return;
  }
  int idx = slotOneBased - 1;
  if (idx >= m_wsMgr->GetCount()) {
    char msg[64];
    snprintf(msg, sizeof(msg), "No workspace at slot #%d", slotOneBased);
    ShowToast(msg);
    return;
  }
  const WorkspaceEntry& ws = m_wsMgr->Get(idx);
  if (!ws.used) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Slot #%d is empty", slotOneBased);
    ShowToast(msg);
    return;
  }
  LoadWorkspace(ws.name);
  char msg[128];
  snprintf(msg, sizeof(msg), "Loaded #%d: %s", slotOneBased, ws.name);
  ShowToast(msg);
}

// =========================================================================
// ActivateFavorite (F6 — capture-by-slot)
// =========================================================================

void MaxPaneContainer::ActivateFavorite(int favIdx, int paneId)
{
  if (!m_favMgr || favIdx < 0 || favIdx >= m_favMgr->GetCount()) return;
  if (paneId < 0) paneId = m_focusedPaneId;
  if (paneId < 0 || !m_tree.IsPaneIdUsed(paneId)) {
    // Fall back to first used pane (covers freshly-opened container).
    paneId = -1;
    for (int p = 0; p < MAX_PANES; p++) {
      if (m_tree.IsPaneIdUsed(p)) { paneId = p; break; }
    }
    if (paneId < 0) return;
  }

  const FavoriteEntry& fav = m_favMgr->Get(favIdx);
  DBG("[MaxPane] ActivateFavorite[%d]: '%s' search='%s' action=%d cmd='%s' isKnown=%d pane=%d\n",
      favIdx, fav.name, fav.searchTitle, fav.toggleAction, fav.actionCommand,
      fav.isKnown, paneId);

  HWND found = WindowManager::FindReaperWindow(fav.searchTitle, m_hwnd);

  if (found && fav.isKnown) {
    for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
      if (strcmp(KNOWN_WINDOWS[j].name, fav.name) == 0) {
        m_winMgr.CaptureByIndex(paneId, j, m_hwnd);
        break;
      }
    }
    RefreshLayout();
    SaveState();
    return;
  }

  if (found && !fav.isKnown) {
    char foundTitle[512];
    GetWindowText(found, foundTitle, sizeof(foundTitle));
    bool isDockFrame = (strstr(foundTitle, DOCKED_TITLE_SUFFIX) != nullptr);
    if (isDockFrame) {
      m_winMgr.CaptureArbitraryWindow(paneId, found, fav.name, m_hwnd,
                                       fav.toggleAction, fav.actionCommand);
      RefreshLayout();
      SaveState();
    } else {
      m_captureQueue->EnqueueArbitrary(paneId, fav.searchTitle,
                                        fav.toggleAction, fav.actionCommand);
      StartCaptureTimer();
    }
    return;
  }

  // Window not visible yet — queue it (firing toggle action if available).
  if (fav.isKnown) {
    for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
      if (strcmp(KNOWN_WINDOWS[j].name, fav.name) == 0) {
        if (fav.toggleAction > 0) m_captureQueue->EnqueueKnown(paneId, j);
        else m_captureQueue->EnqueueArbitrary(paneId, fav.searchTitle, 0, fav.actionCommand);
        break;
      }
    }
  } else {
    m_captureQueue->EnqueueArbitrary(paneId, fav.searchTitle,
                                      fav.toggleAction, fav.actionCommand);
  }
  StartCaptureTimer();
}
