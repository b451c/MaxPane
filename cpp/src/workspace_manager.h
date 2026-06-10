#pragma once
#include "split_tree.h"
#include "window_manager.h"
#include "state_accessor.h"

// Per-pane tab snapshot for workspace persistence
struct PaneSnapshot {
  int tabCount;
  int activeTab;
  struct TabSnapshot {
    bool isArbitrary;
    char name[256];
    int toggleAction;
    char actionCommand[128];  // stable command string
    int colorIndex;
    bool pinned;  // C2 (ADR-027)
  } tabs[MAX_TABS_PER_PANE];
};

// Workspace preset — stores tree snapshots
struct WorkspaceEntry {
  char name[MAX_WORKSPACE_NAME];
  bool used;
  // Tree structure snapshot
  int treeVersion;  // 2 = tree format
  NodeSnapshot nodes[MAX_TREE_NODES];
  int nodeCount;
  // Legacy fields (for backward compat loading)
  int layoutPreset;
  float ratios[MAX_SPLITTERS];
  int paneCount;
  // Per-pane tab data
  PaneSnapshot panes[MAX_PANES];
};

class WorkspaceManager {
public:
  WorkspaceManager();

  // ExtState section to read/write. Defaults to legacy EXT_SECTION;
  // MaxPaneContainer overrides per-instance after construction (F2).
  void SetSection(const char* section) { m_section = section; }

  // Per-project state persistence (ProjExtState, stored in RPP)
  void SaveProjectState(ReaProject* proj, const SplitTree& tree, const WindowManager& winMgr);
  bool LoadProjectState(ReaProject* proj, NodeSnapshot* outSnap, int& outNodeCount,
                        PaneSnapshot outPanes[MAX_PANES], bool& outHasTreeFormat) const;
  bool HasProjectState(ReaProject* proj) const;

  // Named workspace CRUD
  // Save() is the synchronous path — used by atexit (where we must flush
  // before the host process exits) and tests. UI paths prefer the async
  // pair EnqueueSave + FlushNextPendingSave (Feature B, ADR-018-era spec):
  // EnqueueSave snapshots the live tree/tabs into an in-memory buffer and
  // also commits to m_workspaces[] so the launcher repaints with the new
  // card immediately; the heavy ExtState write happens later on a deferred
  // tick to keep the click frame snappy.
  void Save(const char* name, const SplitTree& tree, const WindowManager& winMgr);
  // Feature B — Phase 1. Captures snapshot, updates m_workspaces[] in memory
  // (so the new/renamed card is visible at the next repaint), enqueues the
  // pending ExtState flush. Queue is 2-deep — intermediate saves collapse:
  // a third Enqueue while two are already pending overwrites slot B (the
  // most recent), preserving "final save wins" with bounded memory.
  void EnqueueSave(const char* name, const SplitTree& tree, const WindowManager& winMgr);
  // Feature B — Phase 2. Pops the oldest pending save, writes ExtState.
  // Returns true if any work was done (so caller can repeat next tick to
  // drain queue depth 2). The container drives this from a one-shot timer
  // armed by EnqueueSave.
  bool FlushNextPendingSave();
  // Feature B — true if at least one save is queued. Container uses this to
  // know whether to re-arm the deferred flush timer.
  bool HasPendingSave() const { return m_pendingSaveCount > 0; }
  // Feature B — atexit drain. Calls FlushNextPendingSave + FlushPendingLoadList
  // in a loop until nothing remains. Called from MaxPaneContainer::Shutdown
  // when invoked from atexit (we must not leave the workspace list partially
  // written across a REAPER restart).
  void FlushAllPending();
  // Feature B — defer LoadList same as Save (spec point 6). The launcher
  // doesn't currently re-read after Save (Save mutates m_workspaces in
  // memory so the next repaint is up to date), but external sync paths
  // (Settings dialog rename of a workspace by another instance, etc.) may
  // still want a non-blocking refresh.
  void EnqueueLoadList();
  bool FlushPendingLoadList();   // true if a refresh was pending and consumed
  bool HasPendingLoadList() const { return m_pendingLoadList; }
  const WorkspaceEntry* Find(const char* name) const;
  void Delete(const char* name);
  // Returns false on bad index, empty/colliding new name, or unused slot.
  // Used by launcher card right-click menu (Sprint 2.5).
  bool Rename(int index, const char* newName);
  bool Duplicate(int sourceIndex, const char* newName);

  // List access (for menu building)
  void LoadList();
  void SaveList();
  // Audit M3.3 — zero one slot's persisted ws_<N>_* footprint (Delete()
  // shift-orphans the tail slot's keys otherwise).
  void ClearSlotPersistedKeys(int slot);
  int GetCount() const { return m_count; }
  const WorkspaceEntry& Get(int index) const;

  // Shared serialization helpers (DRY — used by state, workspace, and RPP I/O).
  // section: ExtState section name (e.g. "MaxPane_cpp" or "MaxPane_cpp_3").
  // For RPP accessors the section is virtual; pass the instance's ExtSection()
  // for consistency.
  static void WriteTreeNodesStatic(const char* section, const char* prefix,
                                   const NodeSnapshot* snap, int count,
                                   StateAccessor& state);
  static int  ReadTreeNodesStatic(const char* section, const char* prefix,
                                  NodeSnapshot* snap, StateAccessor& state);
  static void WritePaneTabsStatic(const char* section, const char* prefix,
                                  const PaneSnapshot* panes, int maxPanes,
                                  const WindowManager* winMgr, StateAccessor& state);
  static void ReadPaneTabsStatic(const char* section, const char* prefix,
                                 PaneSnapshot* panes, int maxPanes,
                                 StateAccessor& state);

private:
  WorkspaceEntry m_workspaces[MAX_WORKSPACES];
  int m_count;
  const char* m_section;       // per-instance state (current tree/tabs, project chunk)
  const char* m_listSection;   // ADR-012: workspace LIST shared across instances (always EXT_SECTION)

  // Feature B — async-save pending buffer. 2-deep queue, FIFO. Each slot
  // holds the full snapshot captured at EnqueueSave time so the live tree
  // can mutate freely between Enqueue and Flush. Slot 0 flushes first;
  // slot 1 is the "next" slot. When both are full and a third Enqueue
  // arrives, slot 1 is overwritten (final save wins, middle collapses).
  struct PendingSave {
    bool valid;
    char name[MAX_WORKSPACE_NAME];
    NodeSnapshot nodes[MAX_TREE_NODES];
    int nodeCount;
    PaneSnapshot panes[MAX_PANES];
  };
  static constexpr int PENDING_SAVE_DEPTH = 2;
  PendingSave m_pendingSave[PENDING_SAVE_DEPTH];
  int m_pendingSaveCount = 0;
  bool m_pendingLoadList = false;

  // Helper — commits a captured PendingSave into m_workspaces[] (mutates
  // in-memory slot only, no ExtState I/O). Used by both EnqueueSave (so
  // launcher repaint sees the new card immediately) and by FlushNextPendingSave
  // (so the slot remains correct even if some other path mutated
  // m_workspaces between Enqueue and Flush).
  void CommitSnapshotToSlot(const PendingSave& snap);
};
