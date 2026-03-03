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

  // Clean up stale windows from a previous workspace switch.
  // During workspace switching we don't toggle off unused windows (to preserve
  // HWNDs and avoid rendering breakage).  Instead we record their action IDs
  // and close them here at startup — REAPER has reopened them from wnd_vis,
  // and this toggle properly updates wnd_vis so they stay closed.
  if (g_SetExtState && g_Main_OnCommand) {
    const char* staleStr = g_GetExtState(EXT_SECTION, "stale_toggle_actions");
    if (staleStr && staleStr[0]) {
      DBG("[MaxPane] LoadState: cleaning stale actions: %s\n", staleStr);
      const char* cur = staleStr;
      while (*cur) {
        int a = 0;
        while (*cur >= '0' && *cur <= '9') { a = a * 10 + (*cur - '0'); cur++; }
        if (a > 0) {
          int state = g_GetToggleCommandState ? g_GetToggleCommandState(a) : -1;
          if (state != 0) {
            DBG("[MaxPane] LoadState: closing stale action=%d (state=%d)\n", a, state);
            g_Main_OnCommand(a, 0);
          }
        }
        if (*cur == ',') cur++;
        else break;
      }
      g_SetExtState(EXT_SECTION, "stale_toggle_actions", "", true);
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
    int p = (presetStr && presetStr[0]) ? atoi(presetStr) : 0;
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
        HWND h = WindowManager::FindReaperWindow(winName, m_hwnd);
        if (h) {
          m_winMgr.CaptureArbitraryWindow(i, h, winName, m_hwnd, arbAction, arbCmd);
        } else {
          m_captureQueue->EnqueueArbitrary(i, winName, arbAction, arbCmd, deferActions);
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

  const WorkspaceEntry* ws = m_wsMgr->Find(name);
  if (!ws) return;

  // --- Stale action tracking ---
  // Build set of toggle-action IDs in the NEW workspace.
  int newActions[MAX_PANES * MAX_TABS_PER_PANE];
  int newActionCount = 0;
  for (int p = 0; p < MAX_PANES; p++) {
    for (int t = 0; t < ws->panes[p].tabCount; t++) {
      if (ws->panes[p].tabs[t].toggleAction > 0 &&
          newActionCount < MAX_PANES * MAX_TABS_PER_PANE) {
        newActions[newActionCount++] = ws->panes[p].tabs[t].toggleAction;
      }
    }
  }

  // Read existing stale list from ext state (accumulated from prior switches).
  int staleActions[256];
  int staleCount = 0;
  if (g_GetExtState) {
    const char* s = g_GetExtState(EXT_SECTION, "stale_toggle_actions");
    if (s && s[0]) {
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
      if (act <= 0) continue;
      bool inNew = false;
      for (int n = 0; n < newActionCount; n++) {
        if (newActions[n] == act) { inNew = true; break; }
      }
      if (inNew) continue;
      bool already = false;
      for (int s = 0; s < staleCount; s++) {
        if (staleActions[s] == act) { already = true; break; }
      }
      if (!already && staleCount < 256) {
        staleActions[staleCount++] = act;
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

  // Persist updated stale list — will be cleaned up at next startup.
  if (g_SetExtState) {
    char buf[2048] = {};
    int len = 0;
    for (int s = 0; s < staleCount; s++) {
      int n = snprintf(buf + len, sizeof(buf) - (size_t)len,
                       "%s%d", len > 0 ? "," : "", staleActions[s]);
      if (n > 0) len += n;
    }
    g_SetExtState(EXT_SECTION, "stale_toggle_actions", buf, true);
    DBG("[MaxPane] LoadWorkspace: stale list (%d actions): %s\n", staleCount, buf);
  }

  // --- Release and reload ---
  // Release ALL without toggle — preserves every HWND.
  // Shared windows keep the same handle, avoiding re-creation which breaks
  // internal rendering for complex windows like Routing Matrix.
  // Stale windows are NOT toggled off here — that happens at next startup
  // (see LoadState) where REAPER's wnd_vis is properly updated.
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

  InvalidateRect(m_hwnd, nullptr, TRUE);
  SaveState();
}

void MaxPaneContainer::DeleteWorkspace(const char* name)
{
  m_wsMgr->Delete(name);
}
