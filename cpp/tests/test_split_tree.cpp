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
