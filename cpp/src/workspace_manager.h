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
  void Save(const char* name, const SplitTree& tree, const WindowManager& winMgr);
  const WorkspaceEntry* Find(const char* name) const;
  void Delete(const char* name);
  // Returns false on bad index, empty/colliding new name, or unused slot.
  // Used by launcher card right-click menu (Sprint 2.5).
  bool Rename(int index, const char* newName);
  bool Duplicate(int sourceIndex, const char* newName);

  // List access (for menu building)
  void LoadList();
  void SaveList();
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
};
