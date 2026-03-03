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
#include <cstring>
#include <cstdio>

// =========================================================================
// Save / Load state (delegates to WorkspaceManager for serialization)
// =========================================================================

void MaxPaneContainer::SaveState()
{
  // Don't persist the temporary solo layout — it would overwrite the real tree
  if (m_soloActive) return;

  m_wsMgr->SaveCurrentState(m_tree, m_winMgr);  // global (default + backward compat)
  // Per-project state is now saved via project_config_extension_t (SaveExtensionConfig)
  // which REAPER calls automatically when saving the RPP file.
  // We still save to ProjExtState for immediate in-session state (e.g., project switch).
  if (g_EnumProjects) {
    ReaProject* proj = g_EnumProjects(-1, nullptr, 0);
    if (proj)
      m_wsMgr->SaveProjectState(proj, m_tree, m_winMgr);
  }
}

void MaxPaneContainer::LoadState()
{
  if (!g_GetExtState) return;

  // Stale window cleanup: try immediately for windows already open, defer
  // the rest.  LoadState may run early (via hookCommandProc) before REAPER
  // has opened all windows from wnd_vis.
  if (g_GetExtState && g_SetExtState && g_Main_OnCommand) {
    const char* staleStr = g_GetExtState(EXT_SECTION, "stale_toggle_actions");
    if (staleStr && staleStr[0]) {
      DBG("[MaxPane] LoadState: stale actions: %s\n", staleStr);
      // Parse into array for both immediate and deferred use
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
      // Immediate pass: close windows that REAPER has already opened.
      //  state > 0  → single toggle (REAPER tracks it as open, toggle closes)
      //  state == 0 → window may exist but REAPER lost track (wnd_vis quirk
      //               for toolbars etc).  Double-toggle: first opens (0→1),
      //               second closes (1→0), properly updating wnd_vis.
      //  state ==-1 → unknown / script action (e.g. ReaImGui).  DO NOT toggle
      //               — that would START the script instead of closing a window.
      bool anyRemaining = false;
      char remaining[2048] = {};
      int rLen = 0;
      for (int i = 0; i < staleCnt; i++) {
        int state = g_GetToggleCommandState ? g_GetToggleCommandState(staleArr[i]) : -1;
        DBG("[MaxPane] LoadState: stale action=%d state=%d\n", staleArr[i], state);
        if (state > 0) {
          g_Main_OnCommand(staleArr[i], 0);
          // Verify: if window is still visible after toggle, force-hide it.
          // Some windows (e.g. Actions) may not close reliably via toggle at startup.
          char searchTitle1[256];
          if (GetSearchTitleForAction(staleArr[i], searchTitle1, sizeof(searchTitle1))) {
            HWND h1 = WindowManager::FindReaperWindow(searchTitle1);
            bool still1 = (h1 && IsWindow(h1) && IsWindowVisible(h1));
            if (still1) ShowWindow(h1, SW_HIDE);
            DBG("[MaxPane] LoadState: toggled off action=%d (state=%d) stillVis=%d\n", staleArr[i], state, still1);
          } else {
            DBG("[MaxPane] LoadState: toggled off action=%d (state=%d) no-reverse-lookup\n", staleArr[i], state);
          }
        } else if (state == -1) {
          // Script/unknown — skip toggle entirely (would start script)
          DBG("[MaxPane] LoadState: SKIP state=-1 action=%d (script/unknown, no toggle)\n", staleArr[i]);
        } else {
          // state=0: check if window actually exists despite state=0
          char searchTitle[256];
          bool handled = false;
          if (GetSearchTitleForAction(staleArr[i], searchTitle, sizeof(searchTitle))) {
            HWND h = WindowManager::FindReaperWindow(searchTitle);
            if (h && IsWindow(h) && IsWindowVisible(h)) {
              // Double toggle: open (0→1) then close (1→0) — updates wnd_vis
              g_Main_OnCommand(staleArr[i], 0);
              int midState = g_GetToggleCommandState ? g_GetToggleCommandState(staleArr[i]) : -1;
              g_Main_OnCommand(staleArr[i], 0);
              int endState = g_GetToggleCommandState ? g_GetToggleCommandState(staleArr[i]) : -1;
              // Safety: if REAPER used a different HWND, hide the original too
              bool stillVisible = (IsWindow(h) && IsWindowVisible(h));
              if (stillVisible) ShowWindow(h, SW_HIDE);
              DBG("[MaxPane] LoadState: double-toggled state=0 action=%d — '%s' hwnd=%p mid=%d end=%d stillVis=%d\n",
                  staleArr[i], searchTitle, (void*)h, midState, endState, stillVisible);
              handled = true;
            } else {
              DBG("[MaxPane] LoadState: state=0 action=%d — window '%s' not found/visible, deferring\n",
                  staleArr[i], searchTitle);
            }
          }
          if (!handled) {
            // Defer — window doesn't exist yet, REAPER may open it later
            int n = snprintf(remaining + rLen, sizeof(remaining) - (size_t)rLen,
                             "%s%d", rLen > 0 ? "," : "", staleArr[i]);
            if (n > 0) rLen += n;
            anyRemaining = true;
          }
        }
      }
      if (anyRemaining) {
        g_SetExtState(EXT_SECTION, "stale_toggle_actions", remaining, true);
        m_staleCleanupCountdown = 2;  // retry in ~1 second
        DBG("[MaxPane] LoadState: deferred remaining: %s\n", remaining);
      } else {
        g_SetExtState(EXT_SECTION, "stale_toggle_actions", "", true);
        DBG("[MaxPane] LoadState: all stale actions cleaned up immediately\n");
      }
    }
  }

  NodeSnapshot snap[MAX_TREE_NODES];
  int nodeCount = 0;
  PaneSnapshot panes[MAX_PANES];
  bool hasTreeFormat = false;

  // Try pending RPP state (from project_config_extension_t), then ProjExtState, then global
  bool loadedProject = false;
  if (g_pendingProjectState.valid) {
    DBG("[MaxPane] LoadState: using pending RPP state (%d lines)\n",
        g_pendingProjectState.lineCount);
    RppReadAccessor rppAcc(g_pendingProjectState.lines, g_pendingProjectState.lineCount);
    const char* treeVer = rppAcc.Get(EXT_SECTION, "tree_version");
    hasTreeFormat = (treeVer && strcmp(treeVer, "2") == 0);
    if (hasTreeFormat) {
      memset(snap, 0, sizeof(snap));
      nodeCount = WorkspaceManager::ReadTreeNodesStatic("", snap, rppAcc);
      if (nodeCount > 0) {
        memset(panes, 0, sizeof(panes));
        WorkspaceManager::ReadPaneTabsStatic("", panes, MAX_PANES, rppAcc);
        loadedProject = true;
        DBG("[MaxPane] LoadState: loaded RPP state (nodes=%d)\n", nodeCount);
      }
    }
    g_pendingProjectState.valid = false;  // consumed
    m_pendingRppLoad = false;
  }

  if (!loadedProject && g_EnumProjects) {
    ReaProject* proj = g_EnumProjects(-1, nullptr, 0);
    DBG("[MaxPane] LoadState: project=%p, hasState=%d\n",
        proj, proj ? m_wsMgr->HasProjectState(proj) : -1);
    if (proj && m_wsMgr->HasProjectState(proj)) {
      loadedProject = m_wsMgr->LoadProjectState(proj, snap, nodeCount, panes, hasTreeFormat);
      DBG("[MaxPane] LoadState: loaded per-project ProjExtState (nodes=%d)\n", nodeCount);
      m_pendingRppLoad = false;
    }
  }

  bool loaded = loadedProject;
  if (!loaded) {
    loaded = m_wsMgr->LoadCurrentState(snap, nodeCount, panes, hasTreeFormat);
    DBG("[MaxPane] LoadState: loaded global state (nodes=%d)\n", nodeCount);
    // Restore panes from global state — recapture the same windows as last session.
    // If RPP data arrives later, OnTimer will override with project-specific state.
    m_pendingRppLoad = true;
    DBG("[MaxPane] LoadState: will recapture from global state, RPP may override later\n");
  }

  if (hasTreeFormat) {
    if (nodeCount < 1) {
      m_tree.Reset();
    } else {
      if (!m_tree.LoadSnapshot(snap, nodeCount)) {
        DBG("[MaxPane] LoadState: corrupt tree detected, saving clean reset state\n");
        SaveState();
      }
    }
  } else {
    const char* presetStr = g_GetExtState(EXT_SECTION, "layout_preset");
    int p = (presetStr && presetStr[0]) ? safe_atoi_clamped(presetStr, 0, PRESET_COUNT - 1) : 0;
    if (p < 0 || p >= PRESET_COUNT) p = 0;
    m_tree.BuildPreset((LayoutPreset)p);
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

    // Restore tab colors from snapshot
    int loadedTabs = m_winMgr.GetTabCount(i);
    for (int t = 0; t < loadedTabs && t < panes[i].tabCount; t++) {
      int ci = panes[i].tabs[t].colorIndex;
      if (ci > 0 && ci < TAB_COLOR_COUNT) {
        m_winMgr.SetTabColor(i, t, ci);
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

void MaxPaneContainer::SaveWorkspace(const char* name)
{
  m_wsMgr->Save(name, m_tree, m_winMgr);
}

void MaxPaneContainer::LoadWorkspace(const char* name)
{
  if (!name || !name[0]) return;

  // Exit solo before loading workspace
  if (m_soloActive) ToggleSolo(m_soloPaneId);

  const WorkspaceEntry* ws = m_wsMgr->Find(name);
  if (!ws) return;

  DBG("[MaxPane] === LoadWorkspace '%s' BEGIN ===\n", name);

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
    const char* s = g_GetExtState(EXT_SECTION, "stale_toggle_actions");
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
    g_SetExtState(EXT_SECTION, "stale_toggle_actions", buf, true);
    DBG("[MaxPane] LW: saved stale list to ExtState: '%s'\n", buf);
  }

  // --- Release and reload ---
  // Release ALL without toggle — just hide + reparent. Stale windows remain
  // hidden; they'll be toggled off properly at next startup.
  m_winMgr.ReleaseAll(false);

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

  InvalidateRect(m_hwnd, nullptr, FALSE);
  SaveState();
}

void MaxPaneContainer::DeleteWorkspace(const char* name)
{
  m_wsMgr->Delete(name);
}
