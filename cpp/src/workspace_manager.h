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

  // Current state persistence (ExtState)
  void SaveCurrentState(const SplitTree& tree, const WindowManager& winMgr);
  bool LoadCurrentState(NodeSnapshot* outSnap, int& outNodeCount,
                        PaneSnapshot outPanes[MAX_PANES],
                        bool& outHasTreeFormat) const;

  // Per-project state persistence (ProjExtState, stored in RPP)
  void SaveProjectState(ReaProject* proj, const SplitTree& tree, const WindowManager& winMgr);
  bool LoadProjectState(ReaProject* proj, NodeSnapshot* outSnap, int& outNodeCount,
                        PaneSnapshot outPanes[MAX_PANES], bool& outHasTreeFormat) const;
  bool HasProjectState(ReaProject* proj) const;

  // Named workspace CRUD
  void Save(const char* name, const SplitTree& tree, const WindowManager& winMgr);
  const WorkspaceEntry* Find(const char* name) const;
  void Delete(const char* name);

  // List access (for menu building)
  void LoadList();
  void SaveList();
  int GetCount() const { return m_count; }
  const WorkspaceEntry& Get(int index) const;

  // Shared serialization helpers (DRY — used by state, workspace, and RPP I/O)
  static void WriteTreeNodesStatic(const char* prefix, const NodeSnapshot* snap, int count,
                                   StateAccessor& state);
  static int  ReadTreeNodesStatic(const char* prefix, NodeSnapshot* snap,
                                  StateAccessor& state);
  static void WritePaneTabsStatic(const char* prefix, const PaneSnapshot* panes,
                                  int maxPanes, const WindowManager* winMgr,
                                  StateAccessor& state);
  static void ReadPaneTabsStatic(const char* prefix, PaneSnapshot* panes, int maxPanes,
                                 StateAccessor& state);

private:
  WorkspaceEntry m_workspaces[MAX_WORKSPACES];
  int m_count;
};
