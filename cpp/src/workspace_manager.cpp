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
  memset(m_pendingSave, 0, sizeof(m_pendingSave));
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

      // U14 (ADR-070) — transient (follow-mode) tabs are never persisted:
      // they are rebuilt from the live selection, and freezing them into a
      // workspace/project would pin the follow pane to one track forever.
      int persistCount = 0;
      for (int t = 0; t < ps->tabCount; t++)
        if (!ps->tabs[t].transient) persistCount++;

      snprintf(key, sizeof(key), "%spane_%d_tab_count", prefix, p);
      snprintf(buf, sizeof(buf), "%d", persistCount);
      state.Set(section, key, buf, true);

      snprintf(key, sizeof(key), "%spane_%d_active_tab", prefix, p);
      snprintf(buf, sizeof(buf), "%d",
               (ps->activeTab >= 0 && ps->activeTab < persistCount) ? ps->activeTab : 0);
      state.Set(section, key, buf, true);

      int outIdx = 0;
      for (int t = 0; t < ps->tabCount; t++) {
        const TabEntry& tab = ps->tabs[t];
        if (tab.transient) continue;
        snprintf(key, sizeof(key), "%spane_%d_tab_%d", prefix, p, outIdx);
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
        snprintf(key, sizeof(key), "%spane_%d_tab_%d_color", prefix, p, outIdx);
        snprintf(buf, sizeof(buf), "%d", tab.colorIndex);
        state.Set(section, key, buf, true);
        // Save pinned flag per tab (C2 — ADR-027)
        snprintf(key, sizeof(key), "%spane_%d_tab_%d_pinned", prefix, p, outIdx);
        state.Set(section, key, tab.pinned ? "1" : "0", true);
        outIdx++;
      }
      // Clear any leftover tabs from previous state
      for (int t = persistCount; t < MAX_TABS_PER_PANE; t++) {
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
        ArbSpec spec;
        if (ParseArbSpec(val, &spec)) {
          // Audit M2.7 — single shared parser (config.cpp); semantics
          // unchanged, pinned by the test_state_persistence round-trips.
          panes[p].tabs[t].isArbitrary = true;
          safe_strncpy(panes[p].tabs[t].actionCommand, spec.cmd,
                       sizeof(panes[p].tabs[t].actionCommand));
          panes[p].tabs[t].toggleAction = spec.action;
          safe_strncpy(panes[p].tabs[t].name, spec.name,
                       sizeof(panes[p].tabs[t].name));
          DBG("[MaxPane] ReadPaneTabs: parsed arb cmd='%s' action=%d name='%s'\n",
              spec.cmd, spec.action, spec.name);
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
                                        const WindowManager& winMgr, bool openAtSave)
{
  if (!g_SetProjExtState || !proj) return;

  ProjectStateAccessor projState(proj);

  projState.Set(m_section, "tree_version", "2", true);
  // U3 (ADR-065) — record open/closed intent. Toggle-off used to re-seed
  // tree_version unconditionally, so a closed second instance resurrected on
  // every subsequent load and the user could not make the close stick.
  projState.Set(m_section, "open_at_save", openAtSave ? "1" : "0", true);

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

// Synchronous save — used by atexit (must complete before host exits) and
// by tests. Equivalent to EnqueueSave followed by immediate queue drain,
// so we don't depend on a later timer tick.
void WorkspaceManager::Save(const char* name, const SplitTree& tree, const WindowManager& winMgr)
{
  EnqueueSave(name, tree, winMgr);
  // Drain inline. PENDING_SAVE_DEPTH+1 is a safety bound — a well-behaved
  // FlushNextPendingSave always returns true while the queue is non-empty.
  for (int safety = 0; safety < PENDING_SAVE_DEPTH + 1 && HasPendingSave(); safety++) {
    FlushNextPendingSave();
  }
}

// Feature B — Phase 1. Captures snapshot + updates m_workspaces[] in memory
// + enqueues a deferred ExtState write. The launcher card appears at the
// next repaint (Phase 1 in-mem commit); the ExtState write happens on the
// container's flush tick (Phase 2).
void WorkspaceManager::EnqueueSave(const char* name, const SplitTree& tree,
                                   const WindowManager& winMgr)
{
  if (!name || !name[0]) return;

  // Decide queue slot. Order matters for "final save wins":
  //  - 0 pending → fill slot 0
  //  - 1 pending → fill slot 1
  //  - 2 pending → overwrite slot 1 (intermediate save collapses; latest
  //    save replaces the previously-latest)
  PendingSave* dst = nullptr;
  if (m_pendingSaveCount == 0) {
    dst = &m_pendingSave[0];
    m_pendingSaveCount = 1;
  } else if (m_pendingSaveCount == 1) {
    dst = &m_pendingSave[1];
    m_pendingSaveCount = 2;
  } else {
    // Already 2 deep — collapse: overwrite slot 1 with the new save.
    dst = &m_pendingSave[1];
  }

  memset(dst, 0, sizeof(*dst));
  dst->valid = true;
  safe_strncpy(dst->name, name, sizeof(dst->name));
  tree.SaveSnapshot(dst->nodes, dst->nodeCount);
  for (int p = 0; p < MAX_PANES; p++) {
    const PaneState* ps = winMgr.GetPaneState(p);
    if (!ps) continue;
    dst->panes[p].tabCount = ps->tabCount;
    dst->panes[p].activeTab = ps->activeTab;
    for (int t = 0; t < ps->tabCount && t < MAX_TABS_PER_PANE; t++) {
      const TabEntry& tab = ps->tabs[t];
      dst->panes[p].tabs[t].isArbitrary = tab.isArbitrary;
      dst->panes[p].tabs[t].toggleAction = tab.toggleAction;
      dst->panes[p].tabs[t].colorIndex = tab.colorIndex;
      dst->panes[p].tabs[t].pinned = tab.pinned;
      if (tab.isArbitrary && tab.actionCmd[0]) {
        safe_strncpy(dst->panes[p].tabs[t].actionCommand, tab.actionCmd,
                     sizeof(dst->panes[p].tabs[t].actionCommand));
      } else if (tab.toggleAction > 0) {
        GetActionCommandString(tab.toggleAction, dst->panes[p].tabs[t].actionCommand,
                               sizeof(dst->panes[p].tabs[t].actionCommand));
      }
      if (tab.name[0]) {
        safe_strncpy(dst->panes[p].tabs[t].name, tab.name, sizeof(dst->panes[p].tabs[t].name));
      }
    }
  }

  // Phase 1 in-memory commit — the launcher's next repaint must show the
  // new/updated card immediately, even though the ExtState bytes haven't
  // hit disk yet. If FlushNextPendingSave never runs (extreme: REAPER
  // crashes before next tick) the in-mem state is lost but no harm done
  // since nothing got persisted either.
  CommitSnapshotToSlot(*dst);

  DBG("[MaxPane] WsMgr::EnqueueSave: '%s' pendingCount=%d\n", name, m_pendingSaveCount);
}

bool WorkspaceManager::FlushNextPendingSave()
{
  if (m_pendingSaveCount == 0) return false;

  // Pop slot 0.
  PendingSave& head = m_pendingSave[0];
  if (!head.valid) {
    // Defensive — count and valid bit disagree. Reset and bail.
    m_pendingSaveCount = 0;
    memset(m_pendingSave, 0, sizeof(m_pendingSave));
    return false;
  }

  // Re-commit before write in case some other path (e.g. Delete) mutated
  // the slot between Enqueue and Flush; cheap belt-and-braces, since
  // CommitSnapshotToSlot is just a memcpy + strcmp.
  CommitSnapshotToSlot(head);

  // Heavy write — hundreds of g_SetExtState calls for full tree + every
  // workspace via SaveList. This is the part Feature B moves off the
  // click frame.
  SaveList();

  DBG("[MaxPane] WsMgr::FlushNextPendingSave: '%s' written, remaining=%d\n",
      head.name, m_pendingSaveCount - 1);

  // Shift slot 1 → 0 (if any) and clear the tail.
  if (m_pendingSaveCount >= 2) {
    m_pendingSave[0] = m_pendingSave[1];
    memset(&m_pendingSave[1], 0, sizeof(m_pendingSave[1]));
    m_pendingSaveCount = 1;
  } else {
    memset(&m_pendingSave[0], 0, sizeof(m_pendingSave[0]));
    m_pendingSaveCount = 0;
  }
  return true;
}

void WorkspaceManager::EnqueueLoadList()
{
  m_pendingLoadList = true;
}

bool WorkspaceManager::FlushPendingLoadList()
{
  if (!m_pendingLoadList) return false;
  m_pendingLoadList = false;
  LoadList();
  return true;
}

void WorkspaceManager::FlushAllPending()
{
  // Drain in deterministic order: list refresh first (so any read-back logic
  // sees the freshest state), then drain the save queue. Bounded by
  // PENDING_SAVE_DEPTH so the loop can't run away.
  FlushPendingLoadList();
  for (int safety = 0; safety < PENDING_SAVE_DEPTH + 1 && HasPendingSave(); safety++) {
    FlushNextPendingSave();
  }
}

// Mutates m_workspaces[] in-memory to match `snap`. Equivalent to the
// in-mem half of the legacy Save(): find-or-claim slot by name, memset,
// copy in the snapshot fields, mark used + treeVersion=2.
void WorkspaceManager::CommitSnapshotToSlot(const PendingSave& snap)
{
  if (!snap.valid || !snap.name[0]) return;

  int slot = -1;
  for (int i = 0; i < m_count; i++) {
    if (m_workspaces[i].used && strcmp(m_workspaces[i].name, snap.name) == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    if (m_count >= MAX_WORKSPACES) return;   // hard limit reached — drop save
    slot = m_count;
    m_count++;
  }

  WorkspaceEntry& ws = m_workspaces[slot];
  memset(&ws, 0, sizeof(WorkspaceEntry));
  safe_strncpy(ws.name, snap.name, MAX_WORKSPACE_NAME);
  ws.used = true;
  ws.treeVersion = 2;
  ws.nodeCount = snap.nodeCount;
  for (int i = 0; i < snap.nodeCount && i < MAX_TREE_NODES; i++) {
    ws.nodes[i] = snap.nodes[i];
  }
  for (int p = 0; p < MAX_PANES; p++) {
    ws.panes[p] = snap.panes[p];
  }
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
      // Audit M3.3 — the shift orphans the old tail slot's ws_<N>_* keys:
      // invisible to readers (gated by ws_count) but permanent
      // reaper-extstate.ini garbage until a new workspace reused the index.
      ClearSlotPersistedKeys(m_count);
      return;
    }
  }
}

// Audit M3.3 — zero out one slot's persisted footprint (name, tree count,
// pane/tab keys). Uses the same writers as SaveList so the key set stays in
// lockstep with the format.
void WorkspaceManager::ClearSlotPersistedKeys(int slot)
{
  if (!g_SetExtState || slot < 0 || slot >= MAX_WORKSPACES) return;
  GlobalStateAccessor acc;
  char key[128];
  char prefix[32];
  snprintf(prefix, sizeof(prefix), "ws_%d_", slot);

  snprintf(key, sizeof(key), "ws_%d_name", slot);
  acc.Set(m_listSection, key, "", true);
  snprintf(key, sizeof(key), "ws_%d_tree_version", slot);
  acc.Set(m_listSection, key, "", true);
  snprintf(key, sizeof(key), "%stree_node_count", prefix);
  acc.Set(m_listSection, key, "0", true);

  static PaneSnapshot s_empty[MAX_PANES];  // static: ~50 KB, keep off the stack
  memset(s_empty, 0, sizeof(s_empty));
  WritePaneTabsStatic(m_listSection, prefix, s_empty, MAX_PANES, nullptr, acc);
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
