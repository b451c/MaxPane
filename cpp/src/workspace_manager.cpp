#include "workspace_manager.h"
#include "config.h"
#include "globals.h"
#include "debug.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

WorkspaceManager::WorkspaceManager()
  : m_count(0)
  , m_section(EXT_SECTION)
  , m_listSection(EXT_SECTION)
{
  memset(m_workspaces, 0, sizeof(m_workspaces));
}

const WorkspaceEntry& WorkspaceManager::Get(int index) const
{
  static WorkspaceEntry empty = {};
  if (index < 0 || index >= m_count) return empty;
  return m_workspaces[index];
}

const WorkspaceEntry* WorkspaceManager::Find(const char* name) const
{
  if (!name || !name[0]) return nullptr;
  for (int i = 0; i < m_count; i++) {
    if (m_workspaces[i].used && strcmp(m_workspaces[i].name, name) == 0) {
      return &m_workspaces[i];
    }
  }
  return nullptr;
}

// =========================================================================
// Shared serialization helpers
// =========================================================================

void WorkspaceManager::WriteTreeNodesStatic(const char* section, const char* prefix,
                                      const NodeSnapshot* snap, int count, StateAccessor& state)
{
  char buf[256];
  char key[128];

  snprintf(key, sizeof(key), "%stree_node_count", prefix);
  snprintf(buf, sizeof(buf), "%d", count);
  state.Set(section, key, buf, true);

  for (int i = 0; i < count; i++) {
    snprintf(key, sizeof(key), "%stn_%d_type", prefix, i);
    snprintf(buf, sizeof(buf), "%d", (int)snap[i].type);
    state.Set(section, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_orient", prefix, i);
    snprintf(buf, sizeof(buf), "%d", (int)snap[i].orient);
    state.Set(section, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_ratio", prefix, i);
    snprintf(buf, sizeof(buf), "%.4f", snap[i].ratio);
    state.Set(section, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_childA", prefix, i);
    snprintf(buf, sizeof(buf), "%d", snap[i].childA);
    state.Set(section, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_childB", prefix, i);
    snprintf(buf, sizeof(buf), "%d", snap[i].childB);
    state.Set(section, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_paneId", prefix, i);
    snprintf(buf, sizeof(buf), "%d", snap[i].paneId);
    state.Set(section, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_parent", prefix, i);
    snprintf(buf, sizeof(buf), "%d", snap[i].parent);
    state.Set(section, key, buf, true);
  }

  // Clear any stale nodes beyond current count
  for (int i = count; i < MAX_TREE_NODES; i++) {
    snprintf(key, sizeof(key), "%stn_%d_type", prefix, i);
    const char* val = state.Get(section, key);
    if (!val) break;  // no more stale data
    state.Set(section, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_orient", prefix, i);
    state.Set(section, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_ratio", prefix, i);
    state.Set(section, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_childA", prefix, i);
    state.Set(section, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_childB", prefix, i);
    state.Set(section, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_paneId", prefix, i);
    state.Set(section, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_parent", prefix, i);
    state.Set(section, key, "", true);
  }
}

int WorkspaceManager::ReadTreeNodesStatic(const char* section, const char* prefix,
                                    NodeSnapshot* snap, StateAccessor& state)
{
  char key[128];
  snprintf(key, sizeof(key), "%stree_node_count", prefix);
  const char* ncStr = state.Get(section, key);
  int count = safe_atoi_clamped(ncStr, 0, MAX_TREE_NODES);

  for (int i = 0; i < count; i++) {
    const char* val;

    snprintf(key, sizeof(key), "%stn_%d_type", prefix, i);
    val = state.Get(section, key);
    snap[i].type = (SplitNodeType)safe_atoi_clamped(val, 0, 2);

    snprintf(key, sizeof(key), "%stn_%d_orient", prefix, i);
    val = state.Get(section, key);
    snap[i].orient = (SplitterOrientation)safe_atoi_clamped(val, 0, 1);

    snprintf(key, sizeof(key), "%stn_%d_ratio", prefix, i);
    val = state.Get(section, key);
    snap[i].ratio = safe_atof_clamped(val, 0.05f, 0.95f);

    snprintf(key, sizeof(key), "%stn_%d_childA", prefix, i);
    val = state.Get(section, key);
    snap[i].childA = safe_atoi_clamped(val, -1, MAX_TREE_NODES - 1);

    snprintf(key, sizeof(key), "%stn_%d_childB", prefix, i);
    val = state.Get(section, key);
    snap[i].childB = safe_atoi_clamped(val, -1, MAX_TREE_NODES - 1);

    snprintf(key, sizeof(key), "%stn_%d_paneId", prefix, i);
    val = state.Get(section, key);
    snap[i].paneId = safe_atoi_clamped(val, -1, MAX_PANES - 1);

    snprintf(key, sizeof(key), "%stn_%d_parent", prefix, i);
    val = state.Get(section, key);
    snap[i].parent = safe_atoi_clamped(val, -1, MAX_TREE_NODES - 1);
  }

  return count;
}

void WorkspaceManager::WritePaneTabsStatic(const char* section, const char* prefix,
                                     const PaneSnapshot* panes, int maxPanes,
                                     const WindowManager* winMgr, StateAccessor& state)
{
  char buf[256];
  char key[128];

  // B21: pre-clear all pane keys so that overwriting a slot with a smaller
  // layout (e.g. former 7-pane workspace → new 2-pane workspace) doesn't
  // leave behind ws_N_pane_3..7 keys that ReadPaneTabsStatic would later
  // resurrect as phantom panes. Valid panes are re-written below; anything
  // beyond maxPanes (or with ps==nullptr in live path) stays cleared.
  for (int p = 0; p < MAX_PANES; p++) {
    snprintf(key, sizeof(key), "%spane_%d_tab_count", prefix, p);
    state.Set(section, key, "0", true);
    snprintf(key, sizeof(key), "%spane_%d_active_tab", prefix, p);
    state.Set(section, key, "0", true);
    for (int t = 0; t < MAX_TABS_PER_PANE; t++) {
      snprintf(key, sizeof(key), "%spane_%d_tab_%d", prefix, p, t);
      state.Set(section, key, "", true);
      snprintf(key, sizeof(key), "%spane_%d_tab_%d_color", prefix, p, t);
      state.Set(section, key, "", true);
    }
  }

  for (int p = 0; p < maxPanes && p < MAX_PANES; p++) {
    if (winMgr) {
      // Saving from live state — read from WindowManager
      const PaneState* ps = winMgr->GetPaneState(p);
      if (!ps) continue;

      snprintf(key, sizeof(key), "%spane_%d_tab_count", prefix, p);
      snprintf(buf, sizeof(buf), "%d", ps->tabCount);
      state.Set(section, key, buf, true);

      snprintf(key, sizeof(key), "%spane_%d_active_tab", prefix, p);
      snprintf(buf, sizeof(buf), "%d", ps->activeTab);
      state.Set(section, key, buf, true);

      for (int t = 0; t < ps->tabCount; t++) {
        snprintf(key, sizeof(key), "%spane_%d_tab_%d", prefix, p, t);
        const TabEntry& tab = ps->tabs[t];
        if (tab.name[0]) {
          if (tab.isArbitrary) {
            // Get stable action command string
            char cmdStr[128] = "0";
            if (tab.actionCmd[0]) {
              safe_strncpy(cmdStr, tab.actionCmd, sizeof(cmdStr));
            } else if (tab.toggleAction > 0) {
              GetActionCommandString(tab.toggleAction, cmdStr, sizeof(cmdStr));
            }
            char val[512];
            snprintf(val, sizeof(val), "arb:%s:%s", cmdStr, tab.name);
            state.Set(section, key, val, true);
          } else {
            state.Set(section, key, tab.name, true);
          }
        } else {
          state.Set(section, key, "", true);
        }
        // Save tab color
        snprintf(key, sizeof(key), "%spane_%d_tab_%d_color", prefix, p, t);
        snprintf(buf, sizeof(buf), "%d", tab.colorIndex);
        state.Set(section, key, buf, true);
      }
      // Save pinned flag per tab (C2 — ADR-027)
      for (int t = 0; t < ps->tabCount; t++) {
        snprintf(key, sizeof(key), "%spane_%d_tab_%d_pinned", prefix, p, t);
        state.Set(section, key, ps->tabs[t].pinned ? "1" : "0", true);
      }
      // Clear any leftover tabs from previous state
      for (int t = ps->tabCount; t < MAX_TABS_PER_PANE; t++) {
        snprintf(key, sizeof(key), "%spane_%d_tab_%d", prefix, p, t);
        state.Set(section, key, "", true);
        snprintf(key, sizeof(key), "%spane_%d_tab_%d_color", prefix, p, t);
        state.Set(section, key, "", true);
        snprintf(key, sizeof(key), "%spane_%d_tab_%d_pinned", prefix, p, t);
        state.Set(section, key, "", true);
      }
    } else {
      // Saving from snapshot data
      snprintf(key, sizeof(key), "%spane_%d_tab_count", prefix, p);
      snprintf(buf, sizeof(buf), "%d", panes[p].tabCount);
      state.Set(section, key, buf, true);

      snprintf(key, sizeof(key), "%spane_%d_active_tab", prefix, p);
      snprintf(buf, sizeof(buf), "%d", panes[p].activeTab);
      state.Set(section, key, buf, true);

      for (int t = 0; t < panes[p].tabCount && t < MAX_TABS_PER_PANE; t++) {
        snprintf(key, sizeof(key), "%spane_%d_tab_%d", prefix, p, t);
        if (panes[p].tabs[t].isArbitrary) {
          const char* cmdStr = panes[p].tabs[t].actionCommand[0]
            ? panes[p].tabs[t].actionCommand : "0";
          char val[512];
          snprintf(val, sizeof(val), "arb:%s:%s", cmdStr, panes[p].tabs[t].name);
          state.Set(section, key, val, true);
        } else {
          state.Set(section, key, panes[p].tabs[t].name, true);
        }
        // Save tab color from snapshot
        snprintf(key, sizeof(key), "%spane_%d_tab_%d_color", prefix, p, t);
        snprintf(buf, sizeof(buf), "%d", panes[p].tabs[t].colorIndex);
        state.Set(section, key, buf, true);
        // Save pinned flag (C2 — ADR-027)
        snprintf(key, sizeof(key), "%spane_%d_tab_%d_pinned", prefix, p, t);
        state.Set(section, key, panes[p].tabs[t].pinned ? "1" : "0", true);
      }
    }
  }
}

void WorkspaceManager::ReadPaneTabsStatic(const char* section, const char* prefix,
                                    PaneSnapshot* panes, int maxPanes, StateAccessor& state)
{
  char key[128];

  for (int p = 0; p < maxPanes && p < MAX_PANES; p++) {
    snprintf(key, sizeof(key), "%spane_%d_tab_count", prefix, p);
    const char* val = state.Get(section, key);
    panes[p].tabCount = safe_atoi_clamped(val, 0, MAX_TABS_PER_PANE);

    snprintf(key, sizeof(key), "%spane_%d_active_tab", prefix, p);
    val = state.Get(section, key);
    panes[p].activeTab = safe_atoi_clamped(val, 0, MAX_TABS_PER_PANE - 1);

    for (int t = 0; t < panes[p].tabCount && t < MAX_TABS_PER_PANE; t++) {
      snprintf(key, sizeof(key), "%spane_%d_tab_%d", prefix, p, t);
      val = state.Get(section, key);
      DBG("[MaxPane] ReadPaneTabs: %s = '%s'\n", key, val ? val : "(null)");
      if (val) {
        if (strncmp(val, "arb:", 4) == 0) {
          panes[p].tabs[t].isArbitrary = true;
          // Format: "arb:cmdstr:name" (cmdstr = "_RSxxx" or "12345" or "0")
          // Legacy: "arb:name" (no command)
          const char* afterArb = val + 4;
          const char* secondColon = strchr(afterArb, ':');
          if (secondColon && secondColon > afterArb) {
            // Extract command string
            int cmdLen = (int)(secondColon - afterArb);
            if (cmdLen >= (int)sizeof(panes[p].tabs[t].actionCommand))
              cmdLen = (int)sizeof(panes[p].tabs[t].actionCommand) - 1;
            char cmdBuf[128];
            safe_strncpy(cmdBuf, afterArb, sizeof(cmdBuf));
            cmdBuf[cmdLen < (int)sizeof(cmdBuf) - 1 ? cmdLen : (int)sizeof(cmdBuf) - 1] = '\0';
            safe_strncpy(panes[p].tabs[t].actionCommand, cmdBuf, sizeof(panes[p].tabs[t].actionCommand));
            // Treat "0" as no action (legacy/empty marker)
            if (strcmp(panes[p].tabs[t].actionCommand, "0") == 0) {
              panes[p].tabs[t].actionCommand[0] = '\0';
              panes[p].tabs[t].toggleAction = 0;
            } else {
              panes[p].tabs[t].toggleAction = ResolveActionCommand(panes[p].tabs[t].actionCommand);
            }
            // Name after second colon
            safe_strncpy(panes[p].tabs[t].name, secondColon + 1, sizeof(panes[p].tabs[t].name));
            DBG("[MaxPane] ReadPaneTabs: parsed arb cmd='%s' action=%d name='%s'\n",
                panes[p].tabs[t].actionCommand, panes[p].tabs[t].toggleAction, secondColon + 1);
          } else {
            // Legacy format — no action command
            panes[p].tabs[t].toggleAction = 0;
            panes[p].tabs[t].actionCommand[0] = '\0';
            safe_strncpy(panes[p].tabs[t].name, afterArb, sizeof(panes[p].tabs[t].name));
            DBG("[MaxPane] ReadPaneTabs: parsed arb LEGACY name='%s'\n", afterArb);
          }
        } else {
          panes[p].tabs[t].isArbitrary = false;
          safe_strncpy(panes[p].tabs[t].name, val, sizeof(panes[p].tabs[t].name));
          for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
            if (strcmp(KNOWN_WINDOWS[j].name, val) == 0) {
              panes[p].tabs[t].toggleAction = KNOWN_WINDOWS[j].toggleActionId;
              break;
            }
          }
        }
      }
      // Read tab color
      snprintf(key, sizeof(key), "%spane_%d_tab_%d_color", prefix, p, t);
      val = state.Get(section, key);
      panes[p].tabs[t].colorIndex = safe_atoi_clamped(val, 0, TAB_COLOR_COUNT - 1);
      // Read pinned flag (C2 — ADR-027). Missing key → false (backward compat).
      snprintf(key, sizeof(key), "%spane_%d_tab_%d_pinned", prefix, p, t);
      val = state.Get(section, key);
      panes[p].tabs[t].pinned = (val && val[0] == '1');
    }
  }
}

// =========================================================================
// Per-project state persistence
// =========================================================================

void WorkspaceManager::SaveProjectState(ReaProject* proj, const SplitTree& tree,
                                        const WindowManager& winMgr)
{
  if (!g_SetProjExtState || !proj) return;

  ProjectStateAccessor projState(proj);

  projState.Set(m_section, "tree_version", "2", true);

  NodeSnapshot snap[MAX_TREE_NODES];
  int nodeCount = 0;
  tree.SaveSnapshot(snap, nodeCount);

  bool corrupt = false;
  for (int i = 0; i < nodeCount; i++) {
    if (snap[i].type == NODE_BRANCH && snap[i].childA == snap[i].childB) {
      DBG("[MaxPane] SaveProjectState: WARNING — node %d has childA==childB==%d\n",
          i, snap[i].childA);
      corrupt = true;
    }
  }
  if (corrupt) {
    DBG("[MaxPane] SaveProjectState: corrupt tree detected, skipping save\n");
    return;
  }
  WriteTreeNodesStatic(m_section, "", snap, nodeCount, projState);
  WritePaneTabsStatic(m_section, "", nullptr, MAX_PANES, &winMgr, projState);

  DBG("[MaxPane] SaveProjectState: saved %d nodes to proj=%p\n", nodeCount, proj);

  // Mark project dirty so REAPER saves ProjExtState to RPP
  if (g_MarkProjectDirty) {
    g_MarkProjectDirty(proj);
    DBG("[MaxPane] SaveProjectState: MarkProjectDirty called on proj=%p\n", proj);
  } else {
    DBG("[MaxPane] SaveProjectState: WARNING — MarkProjectDirty is NULL!\n");
  }
}

bool WorkspaceManager::LoadProjectState(ReaProject* proj, NodeSnapshot* outSnap,
                                        int& outNodeCount, PaneSnapshot outPanes[MAX_PANES],
                                        bool& outHasTreeFormat) const
{
  if (!g_GetProjExtState || !proj) return false;

  ProjectStateAccessor projState(proj);

  const char* treeVer = projState.Get(m_section, "tree_version");
  outHasTreeFormat = (treeVer && strcmp(treeVer, "2") == 0);

  if (outHasTreeFormat) {
    memset(outSnap, 0, sizeof(NodeSnapshot) * MAX_TREE_NODES);
    outNodeCount = ReadTreeNodesStatic(m_section, "", outSnap, projState);
    if (outNodeCount < 1) return false;
  } else {
    outNodeCount = 0;
    return false;  // per-project state is always tree format
  }

  memset(outPanes, 0, sizeof(PaneSnapshot) * MAX_PANES);
  ReadPaneTabsStatic(m_section, "", outPanes, MAX_PANES, projState);

  return true;
}

bool WorkspaceManager::HasProjectState(ReaProject* proj) const
{
  if (!g_GetProjExtState || !proj) return false;

  // Try both our section name and uppercase variant
  char rawBuf[64] = {};
  g_GetProjExtState(proj, m_section, "tree_version", rawBuf, sizeof(rawBuf));

  // Also try uppercase section + key in case REAPER needs exact RPP case after reload
  char rawBuf2[64] = {};
  g_GetProjExtState(proj, "MAXPANE_CPP", "TREE_VERSION", rawBuf2, sizeof(rawBuf2));

  DBG("[MaxPane] HasProjectState: proj=%p mixed='%s' upper='%s'\n",
      (void*)proj, rawBuf, rawBuf2);

  return (rawBuf[0] != '\0' || rawBuf2[0] != '\0');
}

// =========================================================================
// Named workspace CRUD
// =========================================================================

void WorkspaceManager::Save(const char* name, const SplitTree& tree, const WindowManager& winMgr)
{
  if (!name || !name[0]) return;

  int slot = -1;
  for (int i = 0; i < m_count; i++) {
    if (m_workspaces[i].used && strcmp(m_workspaces[i].name, name) == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    if (m_count >= MAX_WORKSPACES) return;
    slot = m_count;
    m_count++;
  }

  WorkspaceEntry& ws = m_workspaces[slot];
  memset(&ws, 0, sizeof(WorkspaceEntry));
  safe_strncpy(ws.name, name, MAX_WORKSPACE_NAME);
  ws.used = true;
  ws.treeVersion = 2;

  // Save tree snapshot
  tree.SaveSnapshot(ws.nodes, ws.nodeCount);

  // Save pane tab data
  for (int p = 0; p < MAX_PANES; p++) {
    const PaneState* ps = winMgr.GetPaneState(p);
    if (!ps) continue;
    ws.panes[p].tabCount = ps->tabCount;
    ws.panes[p].activeTab = ps->activeTab;
    for (int t = 0; t < ps->tabCount && t < MAX_TABS_PER_PANE; t++) {
      const TabEntry& tab = ps->tabs[t];
      ws.panes[p].tabs[t].isArbitrary = tab.isArbitrary;
      ws.panes[p].tabs[t].toggleAction = tab.toggleAction;
      ws.panes[p].tabs[t].colorIndex = tab.colorIndex;
      ws.panes[p].tabs[t].pinned = tab.pinned;
      if (tab.isArbitrary && tab.actionCmd[0]) {
        safe_strncpy(ws.panes[p].tabs[t].actionCommand, tab.actionCmd,
                     sizeof(ws.panes[p].tabs[t].actionCommand));
      } else if (tab.toggleAction > 0) {
        GetActionCommandString(tab.toggleAction, ws.panes[p].tabs[t].actionCommand,
                               sizeof(ws.panes[p].tabs[t].actionCommand));
      }
      if (tab.name[0]) {
        safe_strncpy(ws.panes[p].tabs[t].name, tab.name, sizeof(ws.panes[p].tabs[t].name));
      }
    }
  }

  SaveList();
}

void WorkspaceManager::Delete(const char* name)
{
  if (!name || !name[0]) return;

  for (int i = 0; i < m_count; i++) {
    if (m_workspaces[i].used && strcmp(m_workspaces[i].name, name) == 0) {
      for (int j = i; j < m_count - 1; j++) {
        m_workspaces[j] = m_workspaces[j + 1];
      }
      m_count--;
      memset(&m_workspaces[m_count], 0, sizeof(WorkspaceEntry));
      SaveList();
      return;
    }
  }
}

bool WorkspaceManager::Rename(int index, const char* newName)
{
  if (index < 0 || index >= m_count) return false;
  if (!newName || !newName[0]) return false;
  if (!m_workspaces[index].used) return false;
  // Reject collision (case-sensitive — matches Find/Delete behavior).
  if (Find(newName)) return false;
  safe_strncpy(m_workspaces[index].name, newName, MAX_WORKSPACE_NAME);
  SaveList();
  return true;
}

bool WorkspaceManager::Duplicate(int sourceIndex, const char* newName)
{
  if (sourceIndex < 0 || sourceIndex >= m_count) return false;
  if (!newName || !newName[0]) return false;
  if (!m_workspaces[sourceIndex].used) return false;
  if (Find(newName)) return false;
  if (m_count >= MAX_WORKSPACES) return false;

  m_workspaces[m_count] = m_workspaces[sourceIndex];  // value copy — preserves snapshot
  safe_strncpy(m_workspaces[m_count].name, newName, MAX_WORKSPACE_NAME);
  m_count++;
  SaveList();
  return true;
}

// =========================================================================
// Workspace list persistence
// =========================================================================

void WorkspaceManager::LoadList()
{
  m_count = 0;
  memset(m_workspaces, 0, sizeof(m_workspaces));

  if (!g_GetExtState) return;

  GlobalStateAccessor globalState;

  // B25/ADR-012: workspace list lives in m_listSection (shared across instances).
  const char* countStr = globalState.Get(m_listSection, "ws_count");
  if (!countStr) return;

  int count = safe_atoi_clamped(countStr, 0, MAX_WORKSPACES);

  char key[128];

  for (int w = 0; w < count; w++) {
    snprintf(key, sizeof(key), "ws_%d_name", w);
    const char* name = globalState.Get(m_listSection, key);
    if (!name) continue;

    WorkspaceEntry& ws = m_workspaces[m_count];
    memset(&ws, 0, sizeof(WorkspaceEntry));
    safe_strncpy(ws.name, name, MAX_WORKSPACE_NAME);
    ws.used = true;

    // Check if workspace uses tree format
    snprintf(key, sizeof(key), "ws_%d_tree_version", w);
    const char* tvStr = globalState.Get(m_listSection, key);
    ws.treeVersion = safe_atoi_clamped(tvStr, 0, 2);

    if (ws.treeVersion == 2) {
      // Load tree snapshot using shared helper
      char prefix[32];
      snprintf(prefix, sizeof(prefix), "ws_%d_", w);
      ws.nodeCount = ReadTreeNodesStatic(m_listSection, prefix, ws.nodes, globalState);
    } else {
      // Legacy format
      snprintf(key, sizeof(key), "ws_%d_preset", w);
      const char* val = globalState.Get(m_listSection, key);
      ws.layoutPreset = safe_atoi_clamped(val, 0, PRESET_COUNT - 1);

      for (int r = 0; r < MAX_SPLITTERS; r++) {
        snprintf(key, sizeof(key), "ws_%d_ratio_%d", w, r);
        val = globalState.Get(m_listSection, key);
        ws.ratios[r] = safe_atof_clamped(val, 0.05f, 0.95f);
      }

      snprintf(key, sizeof(key), "ws_%d_pane_count", w);
      val = globalState.Get(m_listSection, key);
      ws.paneCount = safe_atoi_clamped(val, 0, MAX_PANES);
    }

    // Load pane tab data using shared helper
    int maxPanes = (ws.treeVersion == 2) ? MAX_PANES : (ws.paneCount > 0 ? ws.paneCount : 4);
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "ws_%d_", w);
    ReadPaneTabsStatic(m_listSection, prefix, ws.panes, maxPanes, globalState);

    m_count++;
  }
}

void WorkspaceManager::SaveList()
{
  if (!g_SetExtState) return;

  GlobalStateAccessor globalState;

  char buf[256];
  char key[128];

  // B25/ADR-012: workspace list lives in m_listSection (shared across instances).
  snprintf(buf, sizeof(buf), "%d", m_count);
  globalState.Set(m_listSection, "ws_count", buf, true);

  for (int w = 0; w < m_count; w++) {
    WorkspaceEntry& ws = m_workspaces[w];

    snprintf(key, sizeof(key), "ws_%d_name", w);
    globalState.Set(m_listSection, key, ws.name, true);

    snprintf(key, sizeof(key), "ws_%d_tree_version", w);
    snprintf(buf, sizeof(buf), "%d", ws.treeVersion);
    globalState.Set(m_listSection, key, buf, true);

    if (ws.treeVersion == 2) {
      char prefix[32];
      snprintf(prefix, sizeof(prefix), "ws_%d_", w);
      WriteTreeNodesStatic(m_listSection, prefix, ws.nodes, ws.nodeCount, globalState);
    } else {
      // Legacy format
      snprintf(key, sizeof(key), "ws_%d_preset", w);
      snprintf(buf, sizeof(buf), "%d", ws.layoutPreset);
      globalState.Set(m_listSection, key, buf, true);

      for (int r = 0; r < MAX_SPLITTERS; r++) {
        snprintf(key, sizeof(key), "ws_%d_ratio_%d", w, r);
        snprintf(buf, sizeof(buf), "%.4f", ws.ratios[r]);
        globalState.Set(m_listSection, key, buf, true);
      }

      snprintf(key, sizeof(key), "ws_%d_pane_count", w);
      snprintf(buf, sizeof(buf), "%d", ws.paneCount);
      globalState.Set(m_listSection, key, buf, true);
    }

    // Save pane tab data using shared helper
    int maxPanes = (ws.treeVersion == 2) ? MAX_PANES : (ws.paneCount > 0 ? ws.paneCount : 4);
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "ws_%d_", w);
    WritePaneTabsStatic(m_listSection, prefix, ws.panes, maxPanes, nullptr, globalState);
  }
}
