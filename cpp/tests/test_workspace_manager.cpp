// WorkspaceManager static helpers — unit tests (ADR-018 / T1.1).
//
// Coverage: the StateAccessor-driven serialization layer (the pure-data half
// of WorkspaceManager). Tests roundtrip a SplitTree snapshot through
// WriteTreeNodesStatic → MockStateAccessor → ReadTreeNodesStatic and verify
// structural integrity. Prefix isolation guards against B21-style collisions
// (slot reuse pollution).
//
// Full Save() / LoadList() paths use global ExtState directly and are out of
// scope for unit-level tests — they would require function-pointer mocks of
// the REAPER API. The StateAccessor abstraction is the unit boundary.

#include "catch2/catch.hpp"
#include "workspace_manager.h"
#include "split_tree.h"
#include "state_accessor.h"
#include "config.h"

#include <map>
#include <string>

namespace {

// In-memory StateAccessor for tests. Keys are namespaced by section so the
// same key under different sections doesn't collide.
class MockStateAccessor : public StateAccessor {
public:
  void Set(const char* section, const char* key, const char* value, bool) override {
    const std::string fullKey = MakeKey(section, key);
    if (!value || !value[0]) m_store.erase(fullKey);
    else m_store[fullKey] = value;
  }
  const char* Get(const char* section, const char* key) override {
    auto it = m_store.find(MakeKey(section, key));
    if (it == m_store.end()) return nullptr;
    return it->second.c_str();
  }
  size_t Size() const { return m_store.size(); }
private:
  static std::string MakeKey(const char* section, const char* key) {
    std::string s = section ? section : "";
    s.push_back('\x1f');  // unit separator — won't appear in real section/key strings
    s += (key ? key : "");
    return s;
  }
  std::map<std::string, std::string> m_store;
};

// Looser tolerance than test_split_tree.cpp's in-memory copy: ExtState
// serializes ratio with %.4f, so worst-case rounding is ~5e-5. Use 5e-4 for
// comfortable safety margin including conversion + clamp ([0.05, 0.95]).
constexpr float kRatioTolExtState = 5e-4f;

bool TreeNodesEquivalent(const NodeSnapshot* a, int aCount,
                         const NodeSnapshot* b, int bCount)
{
  if (aCount != bCount) return false;
  for (int i = 0; i < aCount; i++) {
    if (a[i].type   != b[i].type)   return false;
    if (a[i].orient != b[i].orient) return false;
    if (a[i].childA != b[i].childA) return false;
    if (a[i].childB != b[i].childB) return false;
    if (a[i].paneId != b[i].paneId) return false;
    if (a[i].parent != b[i].parent) return false;
    const float dr = a[i].ratio - b[i].ratio;
    if (dr > kRatioTolExtState || dr < -kRatioTolExtState) return false;
  }
  return true;
}

}  // namespace

TEST_CASE("WorkspaceManager tree-nodes roundtrip preserves a 4-leaf grid layout",
          "[workspace_manager][serialization]")
{
  SplitTree src;
  src.BuildPreset(PRESET_GRID_2X2);
  src.Recalculate(800, 600);
  REQUIRE(src.GetLeafCount() == 4);

  NodeSnapshot srcSnap[MAX_TREE_NODES];
  int srcCount = 0;
  src.SaveSnapshot(srcSnap, srcCount);
  REQUIRE(srcCount > 0);

  MockStateAccessor state;
  WorkspaceManager::WriteTreeNodesStatic("MaxPane_cpp_test", "", srcSnap, srcCount, state);
  REQUIRE(state.Size() > 0);

  NodeSnapshot dstSnap[MAX_TREE_NODES] = {};
  const int dstCount =
    WorkspaceManager::ReadTreeNodesStatic("MaxPane_cpp_test", "", dstSnap, state);
  REQUIRE(dstCount == srcCount);
  REQUIRE(TreeNodesEquivalent(srcSnap, srcCount, dstSnap, dstCount));

  // Loaded snapshot must rebuild a structurally identical tree.
  SplitTree dst;
  REQUIRE(dst.LoadSnapshot(dstSnap, dstCount));
  dst.Recalculate(800, 600);
  REQUIRE(dst.GetLeafCount() == src.GetLeafCount());
  REQUIRE(dst.GetBranchCount() == src.GetBranchCount());
}

TEST_CASE("WorkspaceManager tree-nodes: empty section reads as count 0",
          "[workspace_manager][serialization]")
{
  MockStateAccessor state;
  NodeSnapshot snap[MAX_TREE_NODES] = {};
  const int count = WorkspaceManager::ReadTreeNodesStatic("empty", "", snap, state);
  REQUIRE(count == 0);
}

TEST_CASE("WorkspaceManager tree-nodes: prefix isolation prevents slot collision",
          "[workspace_manager][serialization]")
{
  // Two workspaces under the same section, different prefixes — must not
  // bleed into each other (guards B21-style slot-reuse pollution).
  SplitTree a; a.BuildPreset(PRESET_TWO_COLUMNS); a.Recalculate(800, 600);
  SplitTree b; b.BuildPreset(PRESET_GRID_2X2);    b.Recalculate(800, 600);

  NodeSnapshot snapA[MAX_TREE_NODES]; int countA = 0;
  NodeSnapshot snapB[MAX_TREE_NODES]; int countB = 0;
  a.SaveSnapshot(snapA, countA);
  b.SaveSnapshot(snapB, countB);
  REQUIRE(countA != countB);  // sanity

  MockStateAccessor state;
  WorkspaceManager::WriteTreeNodesStatic("section", "ws_4_", snapA, countA, state);
  WorkspaceManager::WriteTreeNodesStatic("section", "ws_7_", snapB, countB, state);

  NodeSnapshot readA[MAX_TREE_NODES] = {};
  NodeSnapshot readB[MAX_TREE_NODES] = {};
  const int readACount =
    WorkspaceManager::ReadTreeNodesStatic("section", "ws_4_", readA, state);
  const int readBCount =
    WorkspaceManager::ReadTreeNodesStatic("section", "ws_7_", readB, state);
  REQUIRE(readACount == countA);
  REQUIRE(readBCount == countB);
  REQUIRE(TreeNodesEquivalent(snapA, countA, readA, readACount));
  REQUIRE(TreeNodesEquivalent(snapB, countB, readB, readBCount));
}
