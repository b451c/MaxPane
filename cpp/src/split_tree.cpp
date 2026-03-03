#include "split_tree.h"
#include "debug.h"
#include <cstring>

static inline int clamp_i(int val, int lo, int hi) { return val < lo ? lo : (val > hi ? hi : val); }
static inline float clamp_f(float val, float lo, float hi) { return val < lo ? lo : (val > hi ? hi : val); }

// =========================================================================
// Constructor / Reset
// =========================================================================

SplitTree::SplitTree()
  : m_root(0)
  , m_nodeCount(0)
  , m_leafCount(0)
  , m_branchCount(0)
  , m_dragNode(-1)
  , m_containerW(0)
  , m_containerH(0)
{
  memset(m_nodes, 0, sizeof(m_nodes));
  memset(m_leafList, 0, sizeof(m_leafList));
  memset(m_branchList, 0, sizeof(m_branchList));
  memset(m_paneIdUsed, 0, sizeof(m_paneIdUsed));
  memset(m_paneRects, 0, sizeof(m_paneRects));
  Reset();
}

void SplitTree::Reset()
{
  memset(m_nodes, 0, sizeof(m_nodes));
  memset(m_paneIdUsed, 0, sizeof(m_paneIdUsed));
  memset(m_paneRects, 0, sizeof(m_paneRects));
  m_nodeCount = 0;
  m_leafCount = 0;
  m_branchCount = 0;
  m_dragNode = -1;

  // Create single root leaf with paneId 0
  int root = AllocNode();
  m_root = root;
  m_nodes[root].type = NODE_LEAF;
  m_nodes[root].paneId = 0;
  m_nodes[root].parent = -1;
  m_nodes[root].childA = -1;
  m_nodes[root].childB = -1;
  m_paneIdUsed[0] = true;
}

// =========================================================================
// Node allocation
// =========================================================================

int SplitTree::AllocNode()
{
  // Find first empty slot
  for (int i = 0; i < MAX_TREE_NODES; i++) {
    if (m_nodes[i].type == NODE_EMPTY) {
      memset(&m_nodes[i], 0, sizeof(SplitNode));
      m_nodes[i].type = NODE_LEAF;  // mark as allocated (caller will set final type)
      m_nodes[i].parent = -1;
      m_nodes[i].childA = -1;
      m_nodes[i].childB = -1;
      m_nodes[i].paneId = -1;
      m_nodes[i].ratio = 0.5f;
      if (i >= m_nodeCount) m_nodeCount = i + 1;
      return i;
    }
  }
  return -1;  // pool exhausted
}

void SplitTree::FreeNode(int idx)
{
  if (idx < 0 || idx >= MAX_TREE_NODES) return;
  m_nodes[idx].type = NODE_EMPTY;
}

// =========================================================================
// PaneId allocation (bitmap-based, returns lowest available)
// =========================================================================

int SplitTree::AllocPaneId()
{
  for (int i = 0; i < MAX_PANES; i++) {
    if (!m_paneIdUsed[i]) {
      m_paneIdUsed[i] = true;
      return i;
    }
  }
  return -1;
}

void SplitTree::FreePaneId(int id)
{
  if (id >= 0 && id < MAX_PANES) {
    m_paneIdUsed[id] = false;
  }
}

// =========================================================================
// Split / Merge
// =========================================================================

int SplitTree::SplitLeaf(int nodeIndex, SplitterOrientation orient, float ratio)
{
  if (nodeIndex < 0 || nodeIndex >= MAX_TREE_NODES) return -1;
  if (m_nodes[nodeIndex].type != NODE_LEAF) return -1;
  if (m_leafCount >= MAX_LEAVES) return -1;

  int childAIdx = AllocNode();
  if (childAIdx < 0) return -1;
  int childBIdx = AllocNode();
  if (childBIdx < 0) { FreeNode(childAIdx); return -1; }

  int newPaneId = AllocPaneId();
  if (newPaneId < 0) { FreeNode(childAIdx); FreeNode(childBIdx); return -1; }

  int origPaneId = m_nodes[nodeIndex].paneId;

  // ChildA keeps original paneId
  m_nodes[childAIdx].type = NODE_LEAF;
  m_nodes[childAIdx].paneId = origPaneId;
  m_nodes[childAIdx].parent = nodeIndex;
  m_nodes[childAIdx].childA = -1;
  m_nodes[childAIdx].childB = -1;

  // ChildB gets new paneId
  m_nodes[childBIdx].type = NODE_LEAF;
  m_nodes[childBIdx].paneId = newPaneId;
  m_nodes[childBIdx].parent = nodeIndex;
  m_nodes[childBIdx].childA = -1;
  m_nodes[childBIdx].childB = -1;

  // Convert the original leaf into a branch
  m_nodes[nodeIndex].type = NODE_BRANCH;
  m_nodes[nodeIndex].orient = orient;
  m_nodes[nodeIndex].ratio = ratio;
  m_nodes[nodeIndex].childA = childAIdx;
  m_nodes[nodeIndex].childB = childBIdx;
  m_nodes[nodeIndex].paneId = -1;

  // Recalculate if we have dimensions
  if (m_containerW > 0 && m_containerH > 0) {
    Recalculate(m_containerW, m_containerH);
  }

  return childBIdx;
}

bool SplitTree::MergeNode(int leafNodeIndex)
{
  if (leafNodeIndex < 0 || leafNodeIndex >= MAX_TREE_NODES) return false;
  if (m_nodes[leafNodeIndex].type != NODE_LEAF) return false;

  int parentIdx = m_nodes[leafNodeIndex].parent;
  if (parentIdx < 0) return false;  // root can't be merged
  if (m_nodes[parentIdx].type != NODE_BRANCH) return false;

  // Find the sibling
  int siblingIdx = GetSibling(leafNodeIndex);
  if (siblingIdx < 0) return false;

  // Free the leaf's paneId
  int leafPaneId = m_nodes[leafNodeIndex].paneId;
  FreePaneId(leafPaneId);

  // If sibling is a leaf, parent becomes a leaf with sibling's paneId
  if (m_nodes[siblingIdx].type == NODE_LEAF) {
    int siblingPaneId = m_nodes[siblingIdx].paneId;

    // Convert parent back to leaf
    m_nodes[parentIdx].type = NODE_LEAF;
    m_nodes[parentIdx].paneId = siblingPaneId;
    m_nodes[parentIdx].childA = -1;
    m_nodes[parentIdx].childB = -1;

    FreeNode(leafNodeIndex);
    FreeNode(siblingIdx);
  } else {
    // Sibling is a branch — promote sibling's children into parent
    m_nodes[parentIdx].orient = m_nodes[siblingIdx].orient;
    m_nodes[parentIdx].ratio = m_nodes[siblingIdx].ratio;
    m_nodes[parentIdx].childA = m_nodes[siblingIdx].childA;
    m_nodes[parentIdx].childB = m_nodes[siblingIdx].childB;

    // Reparent children
    if (m_nodes[parentIdx].childA >= 0)
      m_nodes[m_nodes[parentIdx].childA].parent = parentIdx;
    if (m_nodes[parentIdx].childB >= 0)
      m_nodes[m_nodes[parentIdx].childB].parent = parentIdx;

    FreeNode(leafNodeIndex);
    FreeNode(siblingIdx);
  }

  if (m_containerW > 0 && m_containerH > 0) {
    Recalculate(m_containerW, m_containerH);
  }

  return true;
}

int SplitTree::GetSibling(int nodeIndex) const
{
  if (nodeIndex < 0 || nodeIndex >= MAX_TREE_NODES) return -1;
  int parentIdx = m_nodes[nodeIndex].parent;
  if (parentIdx < 0) return -1;
  if (m_nodes[parentIdx].childA == nodeIndex) return m_nodes[parentIdx].childB;
  if (m_nodes[parentIdx].childB == nodeIndex) return m_nodes[parentIdx].childA;
  return -1;
}

bool SplitTree::CanMerge(int leafNodeIndex) const
{
  if (leafNodeIndex < 0 || leafNodeIndex >= MAX_TREE_NODES) return false;
  if (m_nodes[leafNodeIndex].type != NODE_LEAF) return false;
  return m_nodes[leafNodeIndex].parent >= 0;
}

int SplitTree::NodeForPane(int paneId) const
{
  for (int i = 0; i < m_leafCount; i++) {
    if (m_nodes[m_leafList[i]].paneId == paneId) return m_leafList[i];
  }
  return -1;
}

// =========================================================================
// Rect splitting helpers
// =========================================================================

void SplitTree::SplitRectV(const RECT& parent, float ratio,
                            RECT& outLeft, RECT& outSplitter, RECT& outRight)
{
  int w = parent.right - parent.left;
  int splitX = parent.left + (int)(w * ratio);
  splitX = clamp_i(splitX, parent.left + MIN_PANE_SIZE, parent.right - MIN_PANE_SIZE - SPLITTER_WIDTH);

  outLeft = parent;
  outLeft.right = splitX;

  outSplitter = parent;
  outSplitter.left = splitX;
  outSplitter.right = splitX + SPLITTER_WIDTH;

  outRight = parent;
  outRight.left = splitX + SPLITTER_WIDTH;
}

void SplitTree::SplitRectH(const RECT& parent, float ratio,
                            RECT& outTop, RECT& outSplitter, RECT& outBottom)
{
  int h = parent.bottom - parent.top;
  int splitY = parent.top + (int)(h * ratio);
  splitY = clamp_i(splitY, parent.top + MIN_PANE_SIZE, parent.bottom - MIN_PANE_SIZE - SPLITTER_WIDTH);

  outTop = parent;
  outTop.bottom = splitY;

  outSplitter = parent;
  outSplitter.top = splitY;
  outSplitter.bottom = splitY + SPLITTER_WIDTH;

  outBottom = parent;
  outBottom.top = splitY + SPLITTER_WIDTH;
}

// =========================================================================
// Recalculate — recursive DFS computing RECTs
// =========================================================================

void SplitTree::Recalculate(int w, int h)
{
  m_containerW = w;
  m_containerH = h;
  if (w < 1 || h < 1) return;

  // Rebuild leaf/branch lists
  m_leafCount = 0;
  m_branchCount = 0;
  memset(m_paneRects, 0, sizeof(m_paneRects));

  RECT full = {0, 0, w, h};
  RecalcNode(m_root, full);
}

void SplitTree::RecalcNode(int idx, const RECT& area, int depth)
{
  if (idx < 0 || idx >= MAX_TREE_NODES) return;
  if (depth > MAX_TREE_NODES) return;  // cycle protection
  SplitNode& node = m_nodes[idx];
  node.rect = area;

  if (node.type == NODE_LEAF) {
    if (m_leafCount < MAX_LEAVES) {
      m_leafList[m_leafCount++] = idx;
    }
    if (node.paneId >= 0 && node.paneId < MAX_PANES) {
      m_paneRects[node.paneId] = area;
    }
  } else if (node.type == NODE_BRANCH) {
    if (m_branchCount < MAX_TREE_NODES) {
      m_branchList[m_branchCount++] = idx;
    }

    RECT partA, splitter, partB;
    if (node.orient == SPLIT_VERTICAL) {
      SplitRectV(area, node.ratio, partA, splitter, partB);
    } else {
      SplitRectH(area, node.ratio, partA, splitter, partB);
    }
    node.splitterRect = splitter;

    RecalcNode(node.childA, partA, depth + 1);
    RecalcNode(node.childB, partB, depth + 1);
  }
}

// =========================================================================
// Hit testing
// =========================================================================

int SplitTree::HitTestSplitter(int x, int y) const
{
  const int GRAB_MARGIN = 5;
  for (int i = 0; i < m_branchCount; i++) {
    int nodeIdx = m_branchList[i];
    const SplitNode& n = m_nodes[nodeIdx];
    const RECT& r = n.splitterRect;
    if (n.orient == SPLIT_VERTICAL) {
      if (x >= r.left - GRAB_MARGIN && x < r.right + GRAB_MARGIN &&
          y >= r.top && y < r.bottom)
        return nodeIdx;
    } else {
      if (x >= r.left && x < r.right &&
          y >= r.top - GRAB_MARGIN && y < r.bottom + GRAB_MARGIN)
        return nodeIdx;
    }
  }
  return -1;
}

int SplitTree::LeafAtPoint(int x, int y) const
{
  for (int i = 0; i < m_leafCount; i++) {
    int nodeIdx = m_leafList[i];
    const RECT& r = m_nodes[nodeIdx].rect;
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
      return nodeIdx;
  }
  return -1;
}

// =========================================================================
// Splitter dragging
// =========================================================================

void SplitTree::StartDrag(int branchNodeIndex)
{
  m_dragNode = branchNodeIndex;
}

void SplitTree::Drag(int x, int y, int containerW, int containerH)
{
  if (containerW < 1 || containerH < 1) return;
  if (m_dragNode < 0 || m_dragNode >= MAX_TREE_NODES) return;
  if (m_nodes[m_dragNode].type != NODE_BRANCH) return;

  SplitNode& node = m_nodes[m_dragNode];
  const RECT& ref = node.rect;  // the branch's own rect is the reference

  if (node.orient == SPLIT_VERTICAL) {
    int refW = ref.right - ref.left;
    if (refW < 1) return;
    float ratio = (float)(x - ref.left) / (float)refW;
    float minR = (float)MIN_PANE_SIZE / (float)refW;
    float maxR = 1.0f - (float)(MIN_PANE_SIZE + SPLITTER_WIDTH) / (float)refW;
    node.ratio = clamp_f(ratio, minR, maxR);
  } else {
    int refH = ref.bottom - ref.top;
    if (refH < 1) return;
    float ratio = (float)(y - ref.top) / (float)refH;
    float minR = (float)MIN_PANE_SIZE / (float)refH;
    float maxR = 1.0f - (float)(MIN_PANE_SIZE + SPLITTER_WIDTH) / (float)refH;
    node.ratio = clamp_f(ratio, minR, maxR);
  }

  Recalculate(containerW, containerH);
}

void SplitTree::EndDrag()
{
  m_dragNode = -1;
}

// =========================================================================
// Preset construction
// =========================================================================

void SplitTree::BuildPreset(LayoutPreset preset)
{
  Reset();

  switch (preset) {
    case PRESET_TWO_COLUMNS: {
      // root V → pane0, pane1
      SplitLeaf(m_root, SPLIT_VERTICAL, 0.5f);
      break;
    }

    case PRESET_LEFT_RIGHT2V: {
      // root V → pane0, right; right H → pane1, pane2
      int rightLeaf = SplitLeaf(m_root, SPLIT_VERTICAL, 0.5f);
      if (rightLeaf >= 0) {
        SplitLeaf(rightLeaf, SPLIT_HORIZONTAL, 0.5f);
      }
      break;
    }

    case PRESET_THREE_COLUMNS: {
      // root V(.333) → pane0, right; right V(.5) → pane1, pane2
      int rightLeaf = SplitLeaf(m_root, SPLIT_VERTICAL, 0.333f);
      if (rightLeaf >= 0) {
        SplitLeaf(rightLeaf, SPLIT_VERTICAL, 0.5f);
      }
      break;
    }

    case PRESET_GRID_2X2: {
      // root H → top, bottom; top V → pane0, pane1; bottom V → pane2, pane3
      int bottomLeaf = SplitLeaf(m_root, SPLIT_HORIZONTAL, 0.5f);
      // Now root is branch, childA is the top leaf (pane0), childB is bottomLeaf (new pane)
      int topLeaf = m_nodes[m_root].childA;
      SplitLeaf(topLeaf, SPLIT_VERTICAL, 0.5f);
      if (bottomLeaf >= 0) {
        SplitLeaf(bottomLeaf, SPLIT_VERTICAL, 0.5f);
      }
      break;
    }

    case PRESET_TOP_BOTTOM2H: {
      // root H → pane0, bottom; bottom V → pane1, pane2
      int bottomLeaf = SplitLeaf(m_root, SPLIT_HORIZONTAL, 0.5f);
      if (bottomLeaf >= 0) {
        SplitLeaf(bottomLeaf, SPLIT_VERTICAL, 0.5f);
      }
      break;
    }

    default:
      break;
  }

  if (m_containerW > 0 && m_containerH > 0) {
    Recalculate(m_containerW, m_containerH);
  }
}

// =========================================================================
// Snapshot support (for workspaces and save/load)
// =========================================================================

void SplitTree::SaveSnapshot(NodeSnapshot* out, int& nodeCount) const
{
  nodeCount = 0;
  for (int i = 0; i < MAX_TREE_NODES; i++) {
    if (m_nodes[i].type != NODE_EMPTY) {
      if (nodeCount >= MAX_TREE_NODES) break;
    }
    out[i].type = m_nodes[i].type;
    out[i].orient = m_nodes[i].orient;
    out[i].ratio = m_nodes[i].ratio;
    out[i].childA = m_nodes[i].childA;
    out[i].childB = m_nodes[i].childB;
    out[i].paneId = m_nodes[i].paneId;
    out[i].parent = m_nodes[i].parent;
    if (m_nodes[i].type != NODE_EMPTY) nodeCount = i + 1;
  }
}

bool SplitTree::LoadSnapshot(const NodeSnapshot* in, int nodeCount)
{
  // Root must exist
  if (nodeCount < 1 || in[0].type == NODE_EMPTY) {
    DBG("[MaxPane] LoadSnapshot: empty or rootless snapshot (nodeCount=%d)\n", nodeCount);
    Reset();
    return false;
  }

  // Validate snapshot: branches must have valid, distinct children
  for (int i = 0; i < nodeCount && i < MAX_TREE_NODES; i++) {
    if (in[i].type == NODE_BRANCH) {
      if (in[i].childA == in[i].childB) {
        DBG("[MaxPane] LoadSnapshot: corrupt node %d (childA==childB==%d)\n", i, in[i].childA);
        Reset();
        return false;
      }
      if (in[i].childA < 0 || in[i].childA >= MAX_TREE_NODES ||
          in[i].childB < 0 || in[i].childB >= MAX_TREE_NODES) {
        DBG("[MaxPane] LoadSnapshot: corrupt node %d (child out of range)\n", i);
        Reset();
        return false;
      }
      if (in[i].childA == i || in[i].childB == i) {
        DBG("[MaxPane] LoadSnapshot: corrupt node %d (self-reference)\n", i);
        Reset();
        return false;
      }
    }
  }

  // Tree validation: indegree check + DFS reachability
  {
    int indegree[MAX_TREE_NODES] = {};
    for (int i = 0; i < nodeCount && i < MAX_TREE_NODES; i++) {
      if (in[i].type == NODE_BRANCH) {
        int cA = in[i].childA, cB = in[i].childB;
        if (cA >= 0 && cA < nodeCount) indegree[cA]++;
        if (cB >= 0 && cB < nodeCount) indegree[cB]++;
      }
    }
    for (int i = 0; i < nodeCount && i < MAX_TREE_NODES; i++) {
      if (in[i].type == NODE_EMPTY) continue;
      if (i == 0 && indegree[i] != 0) {
        DBG("[MaxPane] LoadSnapshot: root has indegree %d (expect 0)\n", indegree[i]);
        Reset(); return false;
      }
      if (i != 0 && indegree[i] != 1) {
        DBG("[MaxPane] LoadSnapshot: node %d has indegree %d (expect 1)\n", i, indegree[i]);
        Reset(); return false;
      }
    }

    // DFS reachability from root
    bool visited[MAX_TREE_NODES] = {};
    int stack[MAX_TREE_NODES];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
      int n = stack[--sp];
      if (n < 0 || n >= nodeCount) continue;
      if (visited[n]) {
        DBG("[MaxPane] LoadSnapshot: node %d visited twice (corrupt)\n", n);
        Reset(); return false;
      }
      visited[n] = true;
      if (in[n].type == NODE_BRANCH) {
        if (sp + 2 > MAX_TREE_NODES) {
          DBG("[MaxPane] LoadSnapshot: DFS stack overflow (corrupt tree)\n");
          Reset(); return false;
        }
        stack[sp++] = in[n].childA;
        stack[sp++] = in[n].childB;
      }
    }
    for (int i = 0; i < nodeCount && i < MAX_TREE_NODES; i++) {
      if (in[i].type != NODE_EMPTY && !visited[i]) {
        DBG("[MaxPane] LoadSnapshot: node %d unreachable from root\n", i);
        Reset(); return false;
      }
    }
  }

  memset(m_nodes, 0, sizeof(m_nodes));
  memset(m_paneIdUsed, 0, sizeof(m_paneIdUsed));
  m_nodeCount = 0;
  m_leafCount = 0;
  m_branchCount = 0;
  m_dragNode = -1;
  m_root = 0;

  for (int i = 0; i < nodeCount && i < MAX_TREE_NODES; i++) {
    m_nodes[i].type = in[i].type;
    m_nodes[i].orient = in[i].orient;
    m_nodes[i].ratio = (in[i].type == NODE_BRANCH && in[i].ratio < 0.05f) ? 0.1f :
                       (in[i].type == NODE_BRANCH && in[i].ratio > 0.95f) ? 0.9f : in[i].ratio;
    m_nodes[i].childA = in[i].childA;
    m_nodes[i].childB = in[i].childB;
    m_nodes[i].paneId = in[i].paneId;
    m_nodes[i].parent = in[i].parent;
    if (in[i].type != NODE_EMPTY && i >= m_nodeCount) m_nodeCount = i + 1;

    if (in[i].type == NODE_LEAF && in[i].paneId >= 0 && in[i].paneId < MAX_PANES) {
      m_paneIdUsed[in[i].paneId] = true;
    }
  }

  if (m_containerW > 0 && m_containerH > 0) {
    Recalculate(m_containerW, m_containerH);
  }
  return true;
}
