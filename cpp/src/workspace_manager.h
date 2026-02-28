#pragma once
#include "split_tree.h"
#include "window_manager.h"

// Per-pane tab snapshot for workspace persistence
struct PaneSnapshot {
  int tabCount;
  int activeTab;
  struct TabSnapshot {
    bool isArbitrary;
    char name[256];
    int toggleAction;
    char actionCommand[128];  // stable command string
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

  // Named workspace CRUD
  void Save(const char* name, const SplitTree& tree, const WindowManager& winMgr);
  const WorkspaceEntry* Find(const char* name) const;
  void Delete(const char* name);

  // List access (for menu building)
  void LoadList();
  void SaveList();
  int GetCount() const { return m_count; }
  const WorkspaceEntry& Get(int index) const;

private:
  WorkspaceEntry m_workspaces[MAX_WORKSPACES];
  int m_count;

  // Shared serialization helpers (DRY — used by both state + workspace)
  static void WriteTreeNodes(const char* prefix, const NodeSnapshot* snap, int count);
  static int  ReadTreeNodes(const char* prefix, NodeSnapshot* snap);
  static void WritePaneTabs(const char* prefix, const PaneSnapshot* panes,
                            int maxPanes, const WindowManager* winMgr);
  static void ReadPaneTabs(const char* prefix, PaneSnapshot* panes, int maxPanes);
};
