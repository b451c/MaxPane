#pragma once
#include "config.h"
#include <cassert>

enum SplitNodeType : unsigned char { NODE_EMPTY = 0, NODE_LEAF, NODE_BRANCH };

struct SplitNode {
  SplitNodeType type;
  SplitterOrientation orient;  // branch only
  float ratio;                 // branch only (0..1)
  int childA, childB;          // branch only (node indices, -1 if none)
  int paneId;                  // leaf only (maps to WindowManager pane)
  RECT rect;                   // computed by Recalculate
  RECT splitterRect;           // computed, branch only
  int parent;                  // -1 for root
};

struct NodeSnapshot {
  SplitNodeType type;
  SplitterOrientation orient;
  float ratio;
  int childA, childB;
  int paneId;
  int parent;
};

class SplitTree {
public:
  SplitTree();

  void Reset();  // single leaf (paneId=0)
  void BuildPreset(LayoutPreset preset);

  // Split a leaf into a branch with two children
  // childA keeps the original paneId, childB gets a new paneId
  // Returns the new leaf's node index, or -1 on failure
  int SplitLeaf(int nodeIndex, SplitterOrientation orient, float ratio = 0.5f);

  // Merge a leaf: collapse it and its sibling back into the parent
  // The sibling's paneId is kept, the merged leaf's paneId is freed
  // Returns true on success
  bool MergeNode(int leafNodeIndex);

  // Recalculate all rects from container size
  void Recalculate(int w, int h);

  // Hit testing
  int HitTestSplitter(int x, int y) const;
  int LeafAtPoint(int x, int y) const;  // returns node index of leaf

  // Splitter dragging
  void StartDrag(int branchNodeIndex);
  void Drag(int x, int y, int containerW, int containerH);
  void EndDrag();
  void ResetRatio(int branchNodeIndex);
  bool IsDragging() const { return m_dragNode >= 0; }
  int DraggingNode() const { return m_dragNode; }

  // Accessors
  int GetRootIndex() const { return m_root; }
  const SplitNode& GetNode(int idx) const {
    assert(idx >= 0 && idx < MAX_TREE_NODES);
    return m_nodes[idx];
  }
  int GetPaneId(int nodeIdx) const {
    assert(nodeIdx >= 0 && nodeIdx < MAX_TREE_NODES);
    return m_nodes[nodeIdx].paneId;
  }
  const RECT& GetPaneRect(int paneId) const {
    assert(paneId >= 0 && paneId < MAX_PANES);
    return m_paneRects[paneId];
  }
  SplitterOrientation GetSplitterOrientation(int nodeIdx) const { return m_nodes[nodeIdx].orient; }

  // Leaf/branch lists (rebuilt each Recalculate)
  int GetLeafCount() const { return m_leafCount; }
  int GetBranchCount() const { return m_branchCount; }
  const int* GetLeafList() const { return m_leafList; }
  const int* GetBranchList() const { return m_branchList; }

  // PaneId allocation
  int AllocPaneId();
  void FreePaneId(int id);
  bool IsPaneIdUsed(int id) const { return id >= 0 && id < MAX_PANES && m_paneIdUsed[id]; }

  // Snapshot support for workspaces
  void SaveSnapshot(NodeSnapshot* out, int& nodeCount) const;
  // Returns true if snapshot was loaded, false if corruption was detected (tree was Reset)
  bool LoadSnapshot(const NodeSnapshot* in, int nodeCount);

  // Find the node index that holds a given paneId
  int NodeForPane(int paneId) const;

  // Check if a leaf can be merged (has a parent)
  bool CanMerge(int leafNodeIndex) const;

  // Get the sibling node index of a given node
  int GetSibling(int nodeIndex) const;

private:
  SplitNode m_nodes[MAX_TREE_NODES];
  int m_root;
  int m_nodeCount;  // next free slot (high water mark)

  int m_leafList[MAX_LEAVES];
  int m_leafCount;
  int m_branchList[MAX_TREE_NODES];
  int m_branchCount;

  bool m_paneIdUsed[MAX_PANES];
  RECT m_paneRects[MAX_PANES];

  int m_dragNode;  // branch node index being dragged, -1 if none
  int m_containerW, m_containerH;

  int AllocNode();
  void FreeNode(int idx);
  void RecalcNode(int idx, const RECT& area, int depth = 0);

  static void SplitRectV(const RECT& parent, float ratio,
                          RECT& outLeft, RECT& outSplitter, RECT& outRight);
  static void SplitRectH(const RECT& parent, float ratio,
                          RECT& outTop, RECT& outSplitter, RECT& outBottom);
};
