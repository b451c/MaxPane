#include "workspace_manager.h"
#include "config.h"
#include "globals.h"
#include "debug.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

WorkspaceManager::WorkspaceManager()
  : m_count(0)
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

void WorkspaceManager::WriteTreeNodes(const char* prefix, const NodeSnapshot* snap, int count,
                                      StateAccessor& state)
{
  char buf[256];
  char key[128];

  snprintf(key, sizeof(key), "%stree_node_count", prefix);
  snprintf(buf, sizeof(buf), "%d", count);
  state.Set(EXT_SECTION, key, buf, true);

  for (int i = 0; i < count; i++) {
    snprintf(key, sizeof(key), "%stn_%d_type", prefix, i);
    snprintf(buf, sizeof(buf), "%d", (int)snap[i].type);
    state.Set(EXT_SECTION, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_orient", prefix, i);
    snprintf(buf, sizeof(buf), "%d", (int)snap[i].orient);
    state.Set(EXT_SECTION, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_ratio", prefix, i);
    snprintf(buf, sizeof(buf), "%.4f", snap[i].ratio);
    state.Set(EXT_SECTION, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_childA", prefix, i);
    snprintf(buf, sizeof(buf), "%d", snap[i].childA);
    state.Set(EXT_SECTION, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_childB", prefix, i);
    snprintf(buf, sizeof(buf), "%d", snap[i].childB);
    state.Set(EXT_SECTION, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_paneId", prefix, i);
    snprintf(buf, sizeof(buf), "%d", snap[i].paneId);
    state.Set(EXT_SECTION, key, buf, true);

    snprintf(key, sizeof(key), "%stn_%d_parent", prefix, i);
    snprintf(buf, sizeof(buf), "%d", snap[i].parent);
    state.Set(EXT_SECTION, key, buf, true);
  }

  // Clear any stale nodes beyond current count
  for (int i = count; i < MAX_TREE_NODES; i++) {
    snprintf(key, sizeof(key), "%stn_%d_type", prefix, i);
    const char* val = state.Get(EXT_SECTION, key);
    if (!val) break;  // no more stale data
    state.Set(EXT_SECTION, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_orient", prefix, i);
    state.Set(EXT_SECTION, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_ratio", prefix, i);
    state.Set(EXT_SECTION, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_childA", prefix, i);
    state.Set(EXT_SECTION, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_childB", prefix, i);
    state.Set(EXT_SECTION, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_paneId", prefix, i);
    state.Set(EXT_SECTION, key, "", true);
    snprintf(key, sizeof(key), "%stn_%d_parent", prefix, i);
    state.Set(EXT_SECTION, key, "", true);
  }
}

int WorkspaceManager::ReadTreeNodes(const char* prefix, NodeSnapshot* snap,
                                    StateAccessor& state)
{
  char key[128];
  snprintf(key, sizeof(key), "%stree_node_count", prefix);
  const char* ncStr = state.Get(EXT_SECTION, key);
  int count = ncStr ? atoi(ncStr) : 0;
  if (count < 0) count = 0;
  if (count > MAX_TREE_NODES) count = MAX_TREE_NODES;

  for (int i = 0; i < count; i++) {
    const char* val;

    snprintf(key, sizeof(key), "%stn_%d_type", prefix, i);
    val = state.Get(EXT_SECTION, key);
    snap[i].type = val ? (SplitNodeType)atoi(val) : NODE_EMPTY;

    snprintf(key, sizeof(key), "%stn_%d_orient", prefix, i);
    val = state.Get(EXT_SECTION, key);
    snap[i].orient = val ? (SplitterOrientation)atoi(val) : SPLIT_VERTICAL;

    snprintf(key, sizeof(key), "%stn_%d_ratio", prefix, i);
    val = state.Get(EXT_SECTION, key);
    snap[i].ratio = val ? (float)atof(val) : 0.5f;

    snprintf(key, sizeof(key), "%stn_%d_childA", prefix, i);
    val = state.Get(EXT_SECTION, key);
    snap[i].childA = val ? atoi(val) : -1;

    snprintf(key, sizeof(key), "%stn_%d_childB", prefix, i);
    val = state.Get(EXT_SECTION, key);
    snap[i].childB = val ? atoi(val) : -1;

    snprintf(key, sizeof(key), "%stn_%d_paneId", prefix, i);
    val = state.Get(EXT_SECTION, key);
    snap[i].paneId = val ? atoi(val) : -1;

    snprintf(key, sizeof(key), "%stn_%d_parent", prefix, i);
    val = state.Get(EXT_SECTION, key);
    snap[i].parent = val ? atoi(val) : -1;
  }

  return count;
}

void WorkspaceManager::WritePaneTabs(const char* prefix, const PaneSnapshot* panes,
                                     int maxPanes, const WindowManager* winMgr,
                                     StateAccessor& state)
{
  char buf[256];
  char key[128];

  for (int p = 0; p < maxPanes && p < MAX_PANES; p++) {
    if (winMgr) {
      // Saving from live state — read from WindowManager
      const PaneState* ps = winMgr->GetPaneState(p);
      if (!ps) continue;

      snprintf(key, sizeof(key), "%spane_%d_tab_count", prefix, p);
      snprintf(buf, sizeof(buf), "%d", ps->tabCount);
      state.Set(EXT_SECTION, key, buf, true);

      snprintf(key, sizeof(key), "%spane_%d_active_tab", prefix, p);
      snprintf(buf, sizeof(buf), "%d", ps->activeTab);
      state.Set(EXT_SECTION, key, buf, true);

      for (int t = 0; t < ps->tabCount; t++) {
        snprintf(key, sizeof(key), "%spane_%d_tab_%d", prefix, p, t);
        const TabEntry& tab = ps->tabs[t];
        if (tab.captured && tab.name) {
          if (tab.isArbitrary) {
            // Get stable action command string
            char cmdStr[128] = "0";
            if (tab.arbitraryActionCmd[0]) {
              safe_strncpy(cmdStr, tab.arbitraryActionCmd, sizeof(cmdStr));
            } else if (tab.toggleAction > 0) {
              GetActionCommandString(tab.toggleAction, cmdStr, sizeof(cmdStr));
            }
            char val[512];
            snprintf(val, sizeof(val), "arb:%s:%s", cmdStr, tab.name);
            state.Set(EXT_SECTION, key, val, true);
          } else {
            state.Set(EXT_SECTION, key, tab.name, true);
          }
        } else {
          state.Set(EXT_SECTION, key, "", true);
        }
        // Save tab color
        snprintf(key, sizeof(key), "%spane_%d_tab_%d_color", prefix, p, t);
        snprintf(buf, sizeof(buf), "%d", tab.colorIndex);
        state.Set(EXT_SECTION, key, buf, true);
      }
      // Clear any leftover tabs from previous state
      for (int t = ps->tabCount; t < MAX_TABS_PER_PANE; t++) {
        snprintf(key, sizeof(key), "%spane_%d_tab_%d", prefix, p, t);
        state.Set(EXT_SECTION, key, "", true);
        snprintf(key, sizeof(key), "%spane_%d_tab_%d_color", prefix, p, t);
        state.Set(EXT_SECTION, key, "", true);
      }
    } else {
      // Saving from snapshot data
      snprintf(key, sizeof(key), "%spane_%d_tab_count", prefix, p);
      snprintf(buf, sizeof(buf), "%d", panes[p].tabCount);
      state.Set(EXT_SECTION, key, buf, true);

      snprintf(key, sizeof(key), "%spane_%d_active_tab", prefix, p);
      snprintf(buf, sizeof(buf), "%d", panes[p].activeTab);
      state.Set(EXT_SECTION, key, buf, true);

      for (int t = 0; t < panes[p].tabCount && t < MAX_TABS_PER_PANE; t++) {
        snprintf(key, sizeof(key), "%spane_%d_tab_%d", prefix, p, t);
        if (panes[p].tabs[t].isArbitrary) {
          const char* cmdStr = panes[p].tabs[t].actionCommand[0]
            ? panes[p].tabs[t].actionCommand : "0";
          char val[512];
          snprintf(val, sizeof(val), "arb:%s:%s", cmdStr, panes[p].tabs[t].name);
          state.Set(EXT_SECTION, key, val, true);
        } else {
          state.Set(EXT_SECTION, key, panes[p].tabs[t].name, true);
        }
        // Save tab color from snapshot
        snprintf(key, sizeof(key), "%spane_%d_tab_%d_color", prefix, p, t);
        snprintf(buf, sizeof(buf), "%d", panes[p].tabs[t].colorIndex);
        state.Set(EXT_SECTION, key, buf, true);
      }
    }
  }
}

void WorkspaceManager::ReadPaneTabs(const char* prefix, PaneSnapshot* panes, int maxPanes,
                                    StateAccessor& state)
{
  char key[128];

  for (int p = 0; p < maxPanes && p < MAX_PANES; p++) {
    snprintf(key, sizeof(key), "%spane_%d_tab_count", prefix, p);
    const char* val = state.Get(EXT_SECTION, key);
    panes[p].tabCount = val ? atoi(val) : 0;

    snprintf(key, sizeof(key), "%spane_%d_active_tab", prefix, p);
    val = state.Get(EXT_SECTION, key);
    panes[p].activeTab = val ? atoi(val) : 0;

    for (int t = 0; t < panes[p].tabCount && t < MAX_TABS_PER_PANE; t++) {
      snprintf(key, sizeof(key), "%spane_%d_tab_%d", prefix, p, t);
      val = state.Get(EXT_SECTION, key);
      DBG("[ReDockIt] ReadPaneTabs: %s = '%s'\n", key, val ? val : "(null)");
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
            strncpy(panes[p].tabs[t].actionCommand, afterArb, cmdLen);
            panes[p].tabs[t].actionCommand[cmdLen] = '\0';
            // Treat "0" as no action (legacy/empty marker)
            if (strcmp(panes[p].tabs[t].actionCommand, "0") == 0) {
              panes[p].tabs[t].actionCommand[0] = '\0';
              panes[p].tabs[t].toggleAction = 0;
            } else {
              panes[p].tabs[t].toggleAction = ResolveActionCommand(panes[p].tabs[t].actionCommand);
            }
            // Name after second colon
            safe_strncpy(panes[p].tabs[t].name, secondColon + 1, sizeof(panes[p].tabs[t].name));
            DBG("[ReDockIt] ReadPaneTabs: parsed arb cmd='%s' action=%d name='%s'\n",
                panes[p].tabs[t].actionCommand, panes[p].tabs[t].toggleAction, secondColon + 1);
          } else {
            // Legacy format — no action command
            panes[p].tabs[t].toggleAction = 0;
            panes[p].tabs[t].actionCommand[0] = '\0';
            safe_strncpy(panes[p].tabs[t].name, afterArb, sizeof(panes[p].tabs[t].name));
            DBG("[ReDockIt] ReadPaneTabs: parsed arb LEGACY name='%s'\n", afterArb);
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
      val = state.Get(EXT_SECTION, key);
      panes[p].tabs[t].colorIndex = val ? atoi(val) : 0;
    }
  }
}

// =========================================================================
// Current state persistence
// =========================================================================

void WorkspaceManager::SaveCurrentState(const SplitTree& tree, const WindowManager& winMgr)
{
  if (!g_SetExtState) return;

  GlobalStateAccessor globalState;

  // Save tree version marker
  globalState.Set(EXT_SECTION, "tree_version", "2", true);

  // Save tree nodes
  NodeSnapshot snap[MAX_TREE_NODES];
  int nodeCount = 0;
  tree.SaveSnapshot(snap, nodeCount);

  // Validate snapshot before saving — branches must have distinct children
  bool corrupt = false;
  for (int i = 0; i < nodeCount; i++) {
    if (snap[i].type == NODE_BRANCH && snap[i].childA == snap[i].childB) {
      DBG("[ReDockIt] SaveCurrentState: WARNING — node %d has childA==childB==%d (corrupt tree)\n",
          i, snap[i].childA);
      corrupt = true;
    }
  }
  if (corrupt) {
    DBG("[ReDockIt] SaveCurrentState: corrupt tree detected, skipping save\n");
    return;
  }
  WriteTreeNodes("", snap, nodeCount, globalState);

  // Save pane tab assignments for all used paneIds
  WritePaneTabs("", nullptr, MAX_PANES, &winMgr, globalState);
}

bool WorkspaceManager::LoadCurrentState(NodeSnapshot* outSnap, int& outNodeCount,
                                        PaneSnapshot outPanes[MAX_PANES],
                                        bool& outHasTreeFormat) const
{
  if (!g_GetExtState) return false;

  GlobalStateAccessor globalState;

  // Check tree version
  const char* treeVer = globalState.Get(EXT_SECTION, "tree_version");
  outHasTreeFormat = (treeVer && strcmp(treeVer, "2") == 0);

  if (outHasTreeFormat) {
    memset(outSnap, 0, sizeof(NodeSnapshot) * MAX_TREE_NODES);
    outNodeCount = ReadTreeNodes("", outSnap, globalState);
    if (outNodeCount < 1) return false;
  } else {
    // Legacy format: caller must handle preset loading
    outNodeCount = 0;
  }

  // Read pane tabs (format is the same regardless of tree version)
  memset(outPanes, 0, sizeof(PaneSnapshot) * MAX_PANES);
  ReadPaneTabs("", outPanes, MAX_PANES, globalState);

  return true;
}

// =========================================================================
// Per-project state persistence
// =========================================================================

void WorkspaceManager::SaveProjectState(ReaProject* proj, const SplitTree& tree,
                                        const WindowManager& winMgr)
{
  if (!g_SetProjExtState || !proj) return;

  ProjectStateAccessor projState(proj);

  projState.Set(EXT_SECTION, "tree_version", "2", true);

  NodeSnapshot snap[MAX_TREE_NODES];
  int nodeCount = 0;
  tree.SaveSnapshot(snap, nodeCount);

  bool corrupt = false;
  for (int i = 0; i < nodeCount; i++) {
    if (snap[i].type == NODE_BRANCH && snap[i].childA == snap[i].childB) {
      DBG("[ReDockIt] SaveProjectState: WARNING — node %d has childA==childB==%d\n",
          i, snap[i].childA);
      corrupt = true;
    }
  }
  if (corrupt) {
    DBG("[ReDockIt] SaveProjectState: corrupt tree detected, skipping save\n");
    return;
  }
  WriteTreeNodes("", snap, nodeCount, projState);
  WritePaneTabs("", nullptr, MAX_PANES, &winMgr, projState);

  DBG("[ReDockIt] SaveProjectState: saved %d nodes to proj=%p\n", nodeCount, proj);

  // Mark project dirty so REAPER saves ProjExtState to RPP
  if (g_MarkProjectDirty) {
    g_MarkProjectDirty(proj);
    DBG("[ReDockIt] SaveProjectState: MarkProjectDirty called on proj=%p\n", proj);
  } else {
    DBG("[ReDockIt] SaveProjectState: WARNING — MarkProjectDirty is NULL!\n");
  }
}

bool WorkspaceManager::LoadProjectState(ReaProject* proj, NodeSnapshot* outSnap,
                                        int& outNodeCount, PaneSnapshot outPanes[MAX_PANES],
                                        bool& outHasTreeFormat) const
{
  if (!g_GetProjExtState || !proj) return false;

  ProjectStateAccessor projState(proj);

  const char* treeVer = projState.Get(EXT_SECTION, "tree_version");
  outHasTreeFormat = (treeVer && strcmp(treeVer, "2") == 0);

  if (outHasTreeFormat) {
    memset(outSnap, 0, sizeof(NodeSnapshot) * MAX_TREE_NODES);
    outNodeCount = ReadTreeNodes("", outSnap, projState);
    if (outNodeCount < 1) return false;
  } else {
    outNodeCount = 0;
    return false;  // per-project state is always tree format
  }

  memset(outPanes, 0, sizeof(PaneSnapshot) * MAX_PANES);
  ReadPaneTabs("", outPanes, MAX_PANES, projState);

  return true;
}

bool WorkspaceManager::HasProjectState(ReaProject* proj) const
{
  if (!g_GetProjExtState || !proj) return false;

  // Try both our section name and uppercase variant
  char rawBuf[64] = {};
  int rawRet = g_GetProjExtState(proj, EXT_SECTION, "tree_version", rawBuf, sizeof(rawBuf));

  // Also try uppercase section + key in case REAPER needs exact RPP case after reload
  char rawBuf2[64] = {};
  int rawRet2 = g_GetProjExtState(proj, "REDOCKIT_CPP", "TREE_VERSION", rawBuf2, sizeof(rawBuf2));

  DBG("[ReDockIt] HasProjectState: proj=%p mixed='%s'(ret=%d) upper='%s'(ret=%d)\n",
      proj, rawBuf, rawRet, rawBuf2, rawRet2);

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
      if (tab.isArbitrary && tab.arbitraryActionCmd[0]) {
        safe_strncpy(ws.panes[p].tabs[t].actionCommand, tab.arbitraryActionCmd,
                     sizeof(ws.panes[p].tabs[t].actionCommand));
      } else if (tab.toggleAction > 0) {
        GetActionCommandString(tab.toggleAction, ws.panes[p].tabs[t].actionCommand,
                               sizeof(ws.panes[p].tabs[t].actionCommand));
      }
      if (tab.name) {
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

// =========================================================================
// Workspace list persistence
// =========================================================================

void WorkspaceManager::LoadList()
{
  m_count = 0;
  memset(m_workspaces, 0, sizeof(m_workspaces));

  if (!g_GetExtState) return;

  GlobalStateAccessor globalState;

  const char* countStr = g_GetExtState(EXT_SECTION, "ws_count");
  if (!countStr || !countStr[0]) return;

  int count = atoi(countStr);
  if (count < 0) count = 0;
  if (count > MAX_WORKSPACES) count = MAX_WORKSPACES;

  char key[128];

  for (int w = 0; w < count; w++) {
    snprintf(key, sizeof(key), "ws_%d_name", w);
    const char* name = g_GetExtState(EXT_SECTION, key);
    if (!name || !name[0]) continue;

    WorkspaceEntry& ws = m_workspaces[m_count];
    memset(&ws, 0, sizeof(WorkspaceEntry));
    safe_strncpy(ws.name, name, MAX_WORKSPACE_NAME);
    ws.used = true;

    // Check if workspace uses tree format
    snprintf(key, sizeof(key), "ws_%d_tree_version", w);
    const char* tvStr = g_GetExtState(EXT_SECTION, key);
    ws.treeVersion = (tvStr && tvStr[0]) ? atoi(tvStr) : 0;

    if (ws.treeVersion == 2) {
      // Load tree snapshot using shared helper
      char prefix[32];
      snprintf(prefix, sizeof(prefix), "ws_%d_", w);
      ws.nodeCount = ReadTreeNodes(prefix, ws.nodes, globalState);
    } else {
      // Legacy format
      snprintf(key, sizeof(key), "ws_%d_preset", w);
      const char* val = g_GetExtState(EXT_SECTION, key);
      ws.layoutPreset = (val && val[0]) ? atoi(val) : 0;

      for (int r = 0; r < MAX_SPLITTERS; r++) {
        snprintf(key, sizeof(key), "ws_%d_ratio_%d", w, r);
        val = g_GetExtState(EXT_SECTION, key);
        ws.ratios[r] = (val && val[0]) ? (float)atof(val) : 0.5f;
      }

      snprintf(key, sizeof(key), "ws_%d_pane_count", w);
      val = g_GetExtState(EXT_SECTION, key);
      ws.paneCount = (val && val[0]) ? atoi(val) : 0;
    }

    // Load pane tab data using shared helper
    int maxPanes = (ws.treeVersion == 2) ? MAX_PANES : (ws.paneCount > 0 ? ws.paneCount : 4);
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "ws_%d_", w);
    ReadPaneTabs(prefix, ws.panes, maxPanes, globalState);

    m_count++;
  }
}

void WorkspaceManager::SaveList()
{
  if (!g_SetExtState) return;

  GlobalStateAccessor globalState;

  char buf[256];
  char key[128];

  snprintf(buf, sizeof(buf), "%d", m_count);
  g_SetExtState(EXT_SECTION, "ws_count", buf, true);

  for (int w = 0; w < m_count; w++) {
    WorkspaceEntry& ws = m_workspaces[w];

    snprintf(key, sizeof(key), "ws_%d_name", w);
    g_SetExtState(EXT_SECTION, key, ws.name, true);

    snprintf(key, sizeof(key), "ws_%d_tree_version", w);
    snprintf(buf, sizeof(buf), "%d", ws.treeVersion);
    g_SetExtState(EXT_SECTION, key, buf, true);

    if (ws.treeVersion == 2) {
      char prefix[32];
      snprintf(prefix, sizeof(prefix), "ws_%d_", w);
      WriteTreeNodes(prefix, ws.nodes, ws.nodeCount, globalState);
    } else {
      // Legacy format
      snprintf(key, sizeof(key), "ws_%d_preset", w);
      snprintf(buf, sizeof(buf), "%d", ws.layoutPreset);
      g_SetExtState(EXT_SECTION, key, buf, true);

      for (int r = 0; r < MAX_SPLITTERS; r++) {
        snprintf(key, sizeof(key), "ws_%d_ratio_%d", w, r);
        snprintf(buf, sizeof(buf), "%.4f", ws.ratios[r]);
        g_SetExtState(EXT_SECTION, key, buf, true);
      }

      snprintf(key, sizeof(key), "ws_%d_pane_count", w);
      snprintf(buf, sizeof(buf), "%d", ws.paneCount);
      g_SetExtState(EXT_SECTION, key, buf, true);
    }

    // Save pane tab data using shared helper
    int maxPanes = (ws.treeVersion == 2) ? MAX_PANES : (ws.paneCount > 0 ? ws.paneCount : 4);
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "ws_%d_", w);
    WritePaneTabs(prefix, ws.panes, maxPanes, nullptr, globalState);
  }
}
