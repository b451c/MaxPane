// SplitTree + NodeSnapshot unit tests (ADR-018 / T1.1).
//
// Coverage:
//   - Reset() produces a single leaf with paneId=0
//   - SplitLeaf creates branch + new leaf, paneId allocation is unique
//   - MergeNode is the inverse of SplitLeaf for a fresh tree
//   - BuildPreset shapes match expected leaf counts per LayoutPreset
//   - Snapshot roundtrip preserves structure (Save → Load → structural equality)
//   - LoadSnapshot rejects corruption (returns false + Reset state)
//
// These tests exercise SplitTree as a pure data structure — no SWELL window
// calls, no REAPER plugin entries. Only RECT (POD struct) is used from
// platform headers.

#include "catch2/catch.hpp"
#include "split_tree.h"
#include "config.h"

namespace {

// Test rendering dimensions — arbitrary positive values are fine for unit
// tests since we never read rects, only structural counts. Recalculate must
// run after structural mutations to populate the leaf list / count cache.
constexpr int TEST_W = 800;
constexpr int TEST_H = 600;

// Structural comparison of two snapshots — node-by-node field equality.
// Used to verify Save → Load roundtrip preserves the tree exactly.
bool SnapshotsEqual(const NodeSnapshot* a, int aCount,
                    const NodeSnapshot* b, int bCount)
{
  if (aCount != bCount) return false;
  for (int i = 0; i < aCount; i++) {
    if (a[i].type    != b[i].type)    return false;
    if (a[i].orient  != b[i].orient)  return false;
    if (a[i].childA  != b[i].childA)  return false;
    if (a[i].childB  != b[i].childB)  return false;
    if (a[i].paneId  != b[i].paneId)  return false;
    if (a[i].parent  != b[i].parent)  return false;
    // ratio: float comparison with small tolerance (preset values are exact .5f)
    float dr = a[i].ratio - b[i].ratio;
    if (dr > 1e-6f || dr < -1e-6f) return false;
  }
  return true;
}

}  // namespace

TEST_CASE("SplitTree::Reset produces a single leaf with paneId 0", "[split_tree]")
{
  SplitTree t;
  t.Reset();
  t.Recalculate(TEST_W, TEST_H);
  REQUIRE(t.GetLeafCount() == 1);
  REQUIRE(t.GetBranchCount() == 0);
  const int* leaves = t.GetLeafList();
  REQUIRE(leaves != nullptr);
  REQUIRE(t.GetPaneId(leaves[0]) == 0);
  REQUIRE(t.IsPaneIdUsed(0));
}

TEST_CASE("SplitTree::SplitLeaf creates a branch and a new leaf with unique paneId",
          "[split_tree]")
{
  SplitTree t;
  t.Reset();
  t.Recalculate(TEST_W, TEST_H);
  const int rootLeaf = t.GetLeafList()[0];
  REQUIRE(t.GetPaneId(rootLeaf) == 0);

  const int newLeaf = t.SplitLeaf(rootLeaf, SPLIT_VERTICAL, 0.5f);
  REQUIRE(newLeaf >= 0);
  t.Recalculate(TEST_W, TEST_H);
  REQUIRE(t.GetLeafCount() == 2);
  REQUIRE(t.GetBranchCount() == 1);

  // childA keeps original paneId; childB (newLeaf) gets a fresh one
  const int newPaneId = t.GetPaneId(newLeaf);
  REQUIRE(newPaneId != 0);
  REQUIRE(newPaneId >= 0);
  REQUIRE(newPaneId < MAX_PANES);
  REQUIRE(t.IsPaneIdUsed(0));
  REQUIRE(t.IsPaneIdUsed(newPaneId));
}

TEST_CASE("SplitTree::MergeNode is inverse of SplitLeaf on a fresh tree",
          "[split_tree]")
{
  SplitTree t;
  t.Reset();
  t.Recalculate(TEST_W, TEST_H);
  const int rootLeaf = t.GetLeafList()[0];
  const int newLeaf = t.SplitLeaf(rootLeaf, SPLIT_HORIZONTAL, 0.5f);
  REQUIRE(newLeaf >= 0);
  t.Recalculate(TEST_W, TEST_H);
  REQUIRE(t.GetLeafCount() == 2);

  const bool ok = t.MergeNode(newLeaf);
  REQUIRE(ok);
  t.Recalculate(TEST_W, TEST_H);
  REQUIRE(t.GetLeafCount() == 1);
  REQUIRE(t.GetBranchCount() == 0);
  // The surviving paneId may be 0 (sibling) — verify exactly one pane is allocated
  int usedCount = 0;
  for (int p = 0; p < MAX_PANES; p++) if (t.IsPaneIdUsed(p)) usedCount++;
  REQUIRE(usedCount == 1);
}

TEST_CASE("SplitTree::BuildPreset produces expected leaf count per preset",
          "[split_tree]")
{
  // PRESET_LEFT_RIGHT2V: 3 leaves; PRESET_TWO_COLUMNS: 2; THREE_COLUMNS: 3;
  // GRID_2X2: 4; TOP_BOTTOM2H: 3. Cross-check against PRESET_PANE_COUNT[]
  // (the legacy authoritative table in config.cpp).
  for (int i = 0; i < PRESET_COUNT; i++) {
    SplitTree t;
    t.BuildPreset((LayoutPreset)i);
    t.Recalculate(TEST_W, TEST_H);
    INFO("preset index " << i);
    REQUIRE(t.GetLeafCount() == PRESET_PANE_COUNT[i]);
    REQUIRE(t.GetLeafCount() <= MAX_LEAVES);
    REQUIRE(t.GetBranchCount() >= 1);   // any preset has at least one split
  }
}

TEST_CASE("SplitTree snapshot survives Save → Load roundtrip with structural equality",
          "[split_tree][snapshot]")
{
  SplitTree src;
  src.BuildPreset(PRESET_GRID_2X2);  // non-trivial: 4 leaves, 3 branches
  src.Recalculate(TEST_W, TEST_H);

  NodeSnapshot snapA[MAX_TREE_NODES];
  int countA = 0;
  src.SaveSnapshot(snapA, countA);
  REQUIRE(countA > 0);
  REQUIRE(countA <= MAX_TREE_NODES);
  REQUIRE(src.GetLeafCount() == 4);

  SplitTree dst;
  const bool loaded = dst.LoadSnapshot(snapA, countA);
  REQUIRE(loaded);
  dst.Recalculate(TEST_W, TEST_H);
  REQUIRE(dst.GetLeafCount() == src.GetLeafCount());
  REQUIRE(dst.GetBranchCount() == src.GetBranchCount());

  NodeSnapshot snapB[MAX_TREE_NODES];
  int countB = 0;
  dst.SaveSnapshot(snapB, countB);
  REQUIRE(SnapshotsEqual(snapA, countA, snapB, countB));
}

TEST_CASE("SplitTree::LoadSnapshot rejects corrupted node count", "[split_tree][snapshot]")
{
  SplitTree t;
  NodeSnapshot snap[MAX_TREE_NODES];
  // nodeCount > MAX_TREE_NODES is corruption — must be rejected
  const bool loaded = t.LoadSnapshot(snap, MAX_TREE_NODES + 5);
  REQUIRE_FALSE(loaded);
  // After rejection the tree should be in a usable Reset state (1 leaf)
  t.Recalculate(TEST_W, TEST_H);
  REQUIRE(t.GetLeafCount() == 1);
}

// v2.5.0 — directional neighbors (Merge Left/Right/Up/Down, LorenzoB #90).
// Geometry-based: adjacency across one splitter, overlap on the other axis,
// largest candidate wins when a neighbor is itself split.
TEST_CASE("SplitTree::NeighborPane finds adjacent panes by direction", "[split_tree][neighbor]")
{
  SplitTree t;
  // [left] | [right-top / right-bottom]
  t.BuildPreset(PRESET_LEFT_RIGHT2V);
  t.Recalculate(TEST_W, TEST_H);
  REQUIRE(t.GetLeafCount() == 3);

  // Identify panes by geometry, not by allocation order.
  int left = -1, rightTop = -1, rightBottom = -1;
  for (int i = 0; i < t.GetLeafCount(); i++) {
    const int p = t.GetPaneId(t.GetLeafList()[i]);
    const RECT& r = t.GetPaneRect(p);
    if (r.left == 0) left = p;
    else if (r.top == 0) rightTop = p;
    else rightBottom = p;
  }
  REQUIRE(left >= 0); REQUIRE(rightTop >= 0); REQUIRE(rightBottom >= 0);

  // Right of the tall left pane: two candidates touch — the LARGER wins.
  // Both right panes split 50/50, so make the top one bigger via a drag.
  const int rightBranch = t.GetNode(t.NodeForPane(rightTop)).parent;
  t.StartDrag(rightBranch);
  t.Drag(600, 400, TEST_W, TEST_H);  // move the horizontal splitter down
  t.EndDrag();
  t.Recalculate(TEST_W, TEST_H);
  CHECK(t.NeighborPane(left, SplitTree::DIR_RIGHT) == rightTop);
  CHECK(t.NeighborPane(left, SplitTree::DIR_LEFT) == -1);
  CHECK(t.NeighborPane(left, SplitTree::DIR_UP) == -1);
  CHECK(t.NeighborPane(left, SplitTree::DIR_DOWN) == -1);

  // The two right panes see each other vertically and the left pane to the west.
  CHECK(t.NeighborPane(rightTop, SplitTree::DIR_DOWN) == rightBottom);
  CHECK(t.NeighborPane(rightBottom, SplitTree::DIR_UP) == rightTop);
  CHECK(t.NeighborPane(rightTop, SplitTree::DIR_LEFT) == left);
  CHECK(t.NeighborPane(rightBottom, SplitTree::DIR_LEFT) == left);
  CHECK(t.NeighborPane(rightTop, SplitTree::DIR_RIGHT) == -1);
  CHECK(t.NeighborPane(rightTop, SplitTree::DIR_UP) == -1);

  // Bad input never crashes.
  CHECK(t.NeighborPane(-1, SplitTree::DIR_LEFT) == -1);
  CHECK(t.NeighborPane(left, 7) == -1);
  CHECK(t.NeighborPane(MAX_PANES - 1, SplitTree::DIR_LEFT) == -1);  // unused id
}

// v2.5.0 — the neighbor is GEOMETRIC, not the tree sibling: two columns each
// split horizontally with different ratios. From the top-left pane, "right"
// must pick the LARGER of the two right-column leaves, which live in the
// other subtree entirely. Also: a merged-away id reports no neighbors.
TEST_CASE("SplitTree::NeighborPane crosses subtree boundaries (largest wins)", "[split_tree][neighbor]")
{
  SplitTree t;
  t.Reset();
  t.Recalculate(TEST_W, TEST_H);
  const int root = t.GetRootIndex();
  const int rightCol = t.SplitLeaf(root, SPLIT_VERTICAL, 0.5f);   // childA = left col
  REQUIRE(rightCol >= 0);
  t.Recalculate(TEST_W, TEST_H);
  // Left column: split 50/50. Right column: 30/70 (bottom leaf is the big one).
  const int leftCol = t.GetNode(t.GetRootIndex()).childA;
  const int leftBottom = t.SplitLeaf(leftCol, SPLIT_HORIZONTAL, 0.5f);
  const int rightBottom = t.SplitLeaf(rightCol, SPLIT_HORIZONTAL, 0.3f);
  REQUIRE(leftBottom >= 0); REQUIRE(rightBottom >= 0);
  t.Recalculate(TEST_W, TEST_H);
  REQUIRE(t.GetLeafCount() == 4);

  int tl = -1, bl = -1, tr = -1, br = -1;
  for (int i = 0; i < t.GetLeafCount(); i++) {
    const int p = t.GetPaneId(t.GetLeafList()[i]);
    const RECT& r = t.GetPaneRect(p);
    const bool isLeft = (r.left == 0), isTop = (r.top == 0);
    if (isLeft && isTop) tl = p; else if (isLeft) bl = p; else if (isTop) tr = p; else br = p;
  }
  REQUIRE(tl >= 0); REQUIRE(bl >= 0); REQUIRE(tr >= 0); REQUIRE(br >= 0);

  // Top-left (0..50%) touches BOTH right leaves (0..30% and 30..100%): the
  // bottom-right one is larger → it wins, although it is not tl's sibling.
  CHECK(t.NeighborPane(tl, SplitTree::DIR_RIGHT) == br);
  // Bottom-left (50..100%) touches only the bottom-right leaf.
  CHECK(t.NeighborPane(bl, SplitTree::DIR_RIGHT) == br);
  // Top-right (0..30%) touches only the top-left leaf on its left.
  CHECK(t.NeighborPane(tr, SplitTree::DIR_LEFT) == tl);
  // Bottom-right (30..100%) touches both left leaves; the two are equal in
  // area → whichever is first is fine, but it must be one of them.
  const int nb = t.NeighborPane(br, SplitTree::DIR_LEFT);
  CHECK((nb == tl || nb == bl));

  // Merge the top-left leaf away: its id has no neighbors any more, and the
  // survivor (bottom-left, now the whole left column) sees both right leaves.
  REQUIRE(t.MergeNode(t.NodeForPane(tl)));
  t.Recalculate(TEST_W, TEST_H);
  CHECK(t.NeighborPane(tl, SplitTree::DIR_RIGHT) == -1);
  CHECK(t.NeighborPane(bl, SplitTree::DIR_RIGHT) == br);
  CHECK(t.NeighborPane(tr, SplitTree::DIR_LEFT) == bl);
}
