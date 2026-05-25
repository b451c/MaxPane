// container_state.cpp — State persistence for MaxPaneContainer
// (SaveState, LoadState, ApplyPaneState, workspace save/load/delete)
#include "container.h"
#include "config.h"
#include "globals.h"
#include "debug.h"
#include "capture_queue.h"
#include "favorites_manager.h"
#include "workspace_manager.h"
#include "project_state.h"
#include "state_accessor.h"
#include "nav_bar.h"      // Feature A — Set/ResetActiveWorkspaceLabel
#include <cstring>
#include <cstdio>

// =========================================================================
// Feature A — workspace-name nav bar label
// =========================================================================

void MaxPaneContainer::RefreshNavBarLabelCache()
{
  if (m_currentWorkspaceName[0]) {
    NavBar::SetActiveWorkspaceLabel(m_currentWorkspaceName, m_workspaceDirty);
  } else {
    NavBar::ResetActiveWorkspaceLabel();
  }
}

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
  RefreshNavBarLabelCache();
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
  RefreshNavBarLabelCache();
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
  RefreshNavBarLabelCache();
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
    int v = atoi(s);
    return v;
  };
  m_floatX = readInt("float_x", m_floatX);
  m_floatY = readInt("float_y", m_floatY);
  m_floatW = readInt("float_w", m_floatW);
  m_floatH = readInt("float_h", m_floatH);
  // C5 (ADR-027) — always-on-top flag.
  const char* aot = g_GetExtState(m_extSection, "float_always_on_top");
  m_floatAlwaysOnTop = (aot && aot[0] == '1');
  // Sanity clamp on width/height — refuse pathologic values from corrupt
  // ExtState (e.g. user-edited reaper-extstate.ini). Below 200x100 we
  // wouldn't even fit a tab bar; above 8000x8000 is multi-monitor wraparound.
  if (m_floatW < 200)  m_floatW = 800;
  if (m_floatH < 100)  m_floatH = 600;
  if (m_floatW > 8000) m_floatW = 800;
  if (m_floatH > 8000) m_floatH = 600;
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
      m_wsMgr->SaveProjectState(proj, m_tree, m_winMgr);
  }
}

// Shared stale-action cleanup. Used at REAPER startup via startupTimerFunc —
// runs BEFORE any MaxPaneContainer is created, so it must not touch
// m_extSection / member state.
// Strategy:
//   state > 0  → single toggle (REAPER says open, action will close)
//   state == 0 → if window exists despite state=0 (wnd_vis quirk), double-toggle
//                to resync (open then close, properly updating wnd_vis)
//   state ==-1 → skip entirely (script / ReaImGui — toggle would START it)
// Returns true if a non-empty stale list was found.
bool ProcessStaleActionsForSection(const char* section)
{
  if (!section || !g_GetExtState || !g_SetExtState || !g_Main_OnCommand) return false;

  const char* staleStr = g_GetExtState(section, "stale_toggle_actions");
  if (!staleStr || !staleStr[0]) return false;

  DBG("[MaxPane] StaleCleanup[%s]: stale actions: %s\n", section, staleStr);

  // Parse comma-separated action IDs
  int staleArr[256];
  int staleCnt = 0;
  {
    const char* cur = staleStr;
    while (*cur && staleCnt < 256) {
      int a = 0;
      while (*cur >= '0' && *cur <= '9') { a = a * 10 + (*cur - '0'); cur++; }
      if (a > 0) staleArr[staleCnt++] = a;
      if (*cur == ',') cur++;
      else break;
    }
  }

  bool anyRemaining = false;
  char remaining[2048] = {};
  int rLen = 0;
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
        if (still1) ShowWindow(h1, SW_HIDE);
        DBG("[MaxPane] StaleCleanup[%s]: toggled off action=%d stillVis=%d\n",
            section, staleArr[i], still1);
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
        HWND hN1 = WindowManager::FindReaperWindow(searchTitleN1);
        if (hN1 && IsWindow(hN1) && IsWindowVisible(hN1)) {
          SendMessage(hN1, WM_CLOSE, 0, 0);
          if (IsWindow(hN1) && IsWindowVisible(hN1)) ShowWindow(hN1, SW_HIDE);
          DBG("[MaxPane] StaleCleanup[%s]: state=-1 action=%d WM_CLOSE/hide hwnd=%p\n",
              section, staleArr[i], (void*)hN1);
          handledN1 = true;
        }
      }
      if (!handledN1) {
        int n = snprintf(remaining + rLen, sizeof(remaining) - (size_t)rLen,
                         "%s%d", rLen > 0 ? "," : "", staleArr[i]);
        if (n > 0) rLen += n;
        anyRemaining = true;
        DBG("[MaxPane] StaleCleanup[%s]: state=-1 action=%d deferred (not visible)\n",
            section, staleArr[i]);
      }
    } else {
      // state == 0 — REAPER says closed; check visually
      char searchTitle[256];
      bool handled = false;
      if (GetSearchTitleForAction(staleArr[i], searchTitle, sizeof(searchTitle))) {
        HWND h = WindowManager::FindReaperWindow(searchTitle);
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
        int n = snprintf(remaining + rLen, sizeof(remaining) - (size_t)rLen,
                         "%s%d", rLen > 0 ? "," : "", staleArr[i]);
        if (n > 0) rLen += n;
        anyRemaining = true;
      }
    }
  }

  if (anyRemaining) {
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

  int merged[512];
  int cnt = 0;
  auto addUnique = [&](int act) {
    if (act <= 0 || cnt >= 512) return;
    for (int i = 0; i < cnt; i++) if (merged[i] == act) return;
    merged[cnt++] = act;
  };

  // 1) Existing stale entries (e.g. from a prior workspace switch).
  const char* prev = g_GetExtState(section, "stale_toggle_actions");
  if (prev && prev[0]) {
    const char* cur = prev;
    while (*cur) {
      int a = 0;
      while (*cur >= '0' && *cur <= '9') { a = a * 10 + (*cur - '0'); cur++; }
      if (a > 0) addUnique(a);
      if (*cur == ',') cur++; else break;
    }
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
          int actionId = 0;
          if (strncmp(v, "arb:", 4) == 0) {
            // "arb:<cmdstr>:<name>" — cmdstr is "_RSxxx" or numeric or "0".
            const char* p2 = strchr(v + 4, ':');
            if (p2 && p2 > v + 4) {
              char cmd[128] = {};
              size_t cmdLen = (size_t)(p2 - (v + 4));
              if (cmdLen >= sizeof(cmd)) cmdLen = sizeof(cmd) - 1;
              memcpy(cmd, v + 4, cmdLen);
              actionId = ResolveActionCommand(cmd);
            }
          } else {
            actionId = LookupToggleAction(v);
          }
          addUnique(actionId);
        }
      }
    }
  }
  // 3) Write merged list.
  char buf[4096] = {};
  int len = 0;
  for (int i = 0; i < cnt; i++) {
    int n = snprintf(buf + len, sizeof(buf) - (size_t)len,
                     "%s%d", len > 0 ? "," : "", merged[i]);
    if (n > 0) len += n;
  }
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
  int existing[512];
  int cnt = 0;
  const char* prev = g_GetExtState(section, "stale_toggle_actions");
  if (prev && prev[0]) {
    const char* cur = prev;
    while (*cur && cnt < 512) {
      int a = 0;
      while (*cur >= '0' && *cur <= '9') { a = a * 10 + (*cur - '0'); cur++; }
      if (a > 0) {
        if (a == action) return;  // already present
        existing[cnt++] = a;
      }
      if (*cur == ',') cur++; else break;
    }
  }
  if (cnt >= 512) return;
  existing[cnt++] = action;

  char buf[4096] = {};
  int len = 0;
  for (int i = 0; i < cnt; i++) {
    int n = snprintf(buf + len, sizeof(buf) - (size_t)len,
                     "%s%d", len > 0 ? "," : "", existing[i]);
    if (n > 0) len += n;
  }
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
    }
  } else {
    // No RPP / no project state → empty launcher (ADR-013)
    m_tree.Reset();
    memset(panes, 0, sizeof(panes));
    DBG("[MaxPane] LoadState: empty launcher (no RPP, no project state)\n");
  }

  ApplyPaneState(panes, MAX_PANES, true);
}

void MaxPaneContainer::ApplyPaneState(const PaneSnapshot* panes, int maxPanes, bool deferActions)
{
  bool needsCaptureTimer = false;

  for (int i = 0; i < maxPanes && i < MAX_PANES; i++) {
    if (!m_tree.IsPaneIdUsed(i)) continue;
    if (panes[i].tabCount == 0) continue;

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
            } else if (h) {
              m_winMgr.CaptureByIndex(i, j, m_hwnd);
            }
            break;
          }
        }
      }
    }

    // Restore tab colors + pinned state from snapshot (C2 — ADR-027).
    // Pinned must be re-applied via SetTabPinned (not raw flag write) so
    // the tab gets sorted to its proper position — but capture order may
    // not match the saved snapshot 1:1, so pin-by-name across the loaded
    // tabs and let the sort handle layout.
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

  if (needsCaptureTimer) {
    StartCaptureTimer();
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
  int staleActions[256];
  int staleCount = 0;
  if (g_GetExtState) {
    const char* s = g_GetExtState(m_extSection, "stale_toggle_actions");
    if (s && s[0]) {
      DBG("[MaxPane] LW: existing stale ext state: '%s'\n", s);
      const char* cur = s;
      while (*cur && staleCount < 256) {
        int a = 0;
        while (*cur >= '0' && *cur <= '9') { a = a * 10 + (*cur - '0'); cur++; }
        if (a > 0) staleActions[staleCount++] = a;
        if (*cur == ',') cur++;
        else break;
      }
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
      bool already = false;
      for (int s = 0; s < staleCount; s++) {
        if (staleActions[s] == act) { already = true; break; }
      }
      if (!already && staleCount < 256) {
        staleActions[staleCount++] = act;
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
    char buf[2048] = {};
    int len = 0;
    for (int s = 0; s < staleCount; s++) {
      int n = snprintf(buf + len, sizeof(buf) - (size_t)len,
                       "%s%d", len > 0 ? "," : "", staleActions[s]);
      if (n > 0) len += n;
    }
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
    bool isDockFrame = (strstr(foundTitle, "(docked)") != nullptr);
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
    bool isDockFrame = (strstr(foundTitle, "(docked)") != nullptr);
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
