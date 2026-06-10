// Persistence-format round-trip tests (repo-audit M0.1).
//
// Two gaps closed here:
//  1. ReadPaneTabsStatic / WritePaneTabsStatic — the parser of the
//     `arb:cmdstr:name` tab format that every saved workspace and project
//     lives in. A regression here silently corrupts user layouts.
//  2. RppWriteAccessor → "KEY VALUE" lines → RppReadAccessor — the exact
//     persistence path of the <MAXPANE_STATE> chunk in .rpp project files.
// Both are pure (StateAccessor seam / plain classes), so no SWELL stubs.

#include "catch2/catch.hpp"
#include "workspace_manager.h"
#include "state_accessor.h"
#include "state_limits.h"
#include "config.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace {

class MemStateAccessor : public StateAccessor {
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
private:
  static std::string MakeKey(const char* section, const char* key) {
    std::string s = section ? section : "";
    s.push_back('\x1f');
    s += (key ? key : "");
    return s;
  }
  std::map<std::string, std::string> m_store;
};

void ZeroPanes(PaneSnapshot* panes, int n) { memset(panes, 0, sizeof(PaneSnapshot) * (size_t)n); }

}  // namespace

TEST_CASE("Pane tabs round-trip: known, arb numeric, arb script, colors, pinned",
          "[persistence]")
{
  PaneSnapshot in[MAX_PANES];
  ZeroPanes(in, MAX_PANES);

  // Pane 0: known window + arbitrary with numeric command.
  in[0].tabCount = 2;
  in[0].activeTab = 1;
  safe_strncpy(in[0].tabs[0].name, "Mixer", sizeof(in[0].tabs[0].name));
  in[0].tabs[0].isArbitrary = false;
  in[0].tabs[0].colorIndex = 3;
  in[0].tabs[0].pinned = true;
  safe_strncpy(in[0].tabs[1].name, "Toolbar 9", sizeof(in[0].tabs[1].name));
  in[0].tabs[1].isArbitrary = true;
  safe_strncpy(in[0].tabs[1].actionCommand, "41936", sizeof(in[0].tabs[1].actionCommand));

  // Pane 2: script command string + a name containing a colon.
  in[2].tabCount = 1;
  in[2].activeTab = 0;
  safe_strncpy(in[2].tabs[0].name, "ReaMD: notes.md", sizeof(in[2].tabs[0].name));
  in[2].tabs[0].isArbitrary = true;
  safe_strncpy(in[2].tabs[0].actionCommand, "_RS791856ea", sizeof(in[2].tabs[0].actionCommand));
  in[2].tabs[0].colorIndex = 1;

  MemStateAccessor mem;
  // winMgr=nullptr → snapshot path (no live-pane override), same as
  // CommitSnapshotToSlot's usage.
  WorkspaceManager::WritePaneTabsStatic("S", "ws_0_", in, MAX_PANES, nullptr, mem);

  PaneSnapshot out[MAX_PANES];
  ZeroPanes(out, MAX_PANES);
  WorkspaceManager::ReadPaneTabsStatic("S", "ws_0_", out, MAX_PANES, mem);

  CHECK(out[0].tabCount == 2);
  CHECK(out[0].activeTab == 1);
  CHECK_FALSE(out[0].tabs[0].isArbitrary);
  CHECK(strcmp(out[0].tabs[0].name, "Mixer") == 0);
  CHECK(out[0].tabs[0].toggleAction == 40078);  // resolved via KNOWN_WINDOWS
  CHECK(out[0].tabs[0].colorIndex == 3);
  CHECK(out[0].tabs[0].pinned);

  CHECK(out[0].tabs[1].isArbitrary);
  CHECK(strcmp(out[0].tabs[1].name, "Toolbar 9") == 0);
  CHECK(strcmp(out[0].tabs[1].actionCommand, "41936") == 0);
  CHECK(out[0].tabs[1].toggleAction == 41936);  // numeric cmd resolves directly
  CHECK_FALSE(out[0].tabs[1].pinned);

  CHECK(out[2].tabs[0].isArbitrary);
  // Name with a colon survives (parser splits on the FIRST colon after cmd).
  CHECK(strcmp(out[2].tabs[0].name, "ReaMD: notes.md") == 0);
  CHECK(strcmp(out[2].tabs[0].actionCommand, "_RS791856ea") == 0);
  CHECK(out[2].tabs[0].colorIndex == 1);

  // Untouched pane stays empty.
  CHECK(out[1].tabCount == 0);
}

TEST_CASE("Pane tabs read: legacy and degenerate arb formats", "[persistence]")
{
  MemStateAccessor mem;
  // Legacy v1.5.x: "arb:name" with no command segment.
  mem.Set("S", "ws_0_pane_0_tab_count", "3", true);
  mem.Set("S", "ws_0_pane_0_tab_0", "arb:SneakPeak", true);
  // Cmd "0" is the legacy no-action marker — must clear to empty cmd.
  mem.Set("S", "ws_0_pane_0_tab_1", "arb:0:Old Window", true);
  // Hostile counts clamp instead of indexing out of bounds.
  mem.Set("S", "ws_0_pane_0_tab_2", "Mixer", true);
  mem.Set("S", "ws_0_pane_0_active_tab", "9999", true);
  mem.Set("S", "ws_0_pane_1_tab_count", "-5", true);
  mem.Set("S", "ws_0_pane_2_tab_count", "9999", true);

  PaneSnapshot out[MAX_PANES];
  ZeroPanes(out, MAX_PANES);
  WorkspaceManager::ReadPaneTabsStatic("S", "ws_0_", out, MAX_PANES, mem);

  CHECK(out[0].tabs[0].isArbitrary);
  CHECK(strcmp(out[0].tabs[0].name, "SneakPeak") == 0);
  CHECK(out[0].tabs[0].actionCommand[0] == '\0');
  CHECK(out[0].tabs[0].toggleAction == 0);

  CHECK(strcmp(out[0].tabs[1].name, "Old Window") == 0);
  CHECK(out[0].tabs[1].actionCommand[0] == '\0');
  CHECK(out[0].tabs[1].toggleAction == 0);

  CHECK(out[0].activeTab <= MAX_TABS_PER_PANE - 1);
  CHECK(out[1].tabCount == 0);                      // negative clamped
  CHECK(out[2].tabCount <= MAX_TABS_PER_PANE);      // oversize clamped
}

TEST_CASE("RppWrite → lines → RppRead round-trips the chunk format", "[persistence]")
{
  // Serialize a pane set into the RPP write accessor.
  PaneSnapshot in[MAX_PANES];
  ZeroPanes(in, MAX_PANES);
  in[0].tabCount = 1;
  safe_strncpy(in[0].tabs[0].name, "Routing Matrix", sizeof(in[0].tabs[0].name));

  RppWriteAccessor wr;
  WorkspaceManager::WritePaneTabsStatic("ignored", "cur_", in, MAX_PANES, nullptr, wr);
  REQUIRE(wr.GetCount() > 0);

  // Flatten to "KEY VALUE" lines exactly like project_state.cpp AddLine does.
  static char lines[RPP_MAX_LINES][RPP_MAX_LINE_LEN];
  int lineCount = 0;
  for (int i = 0; i < wr.GetCount() && lineCount < RPP_MAX_LINES; i++) {
    snprintf(lines[lineCount], RPP_MAX_LINE_LEN, "%s %s", wr.GetKey(i), wr.GetValue(i));
    lineCount++;
  }

  RppReadAccessor rd(lines, lineCount);
  PaneSnapshot out[MAX_PANES];
  ZeroPanes(out, MAX_PANES);
  WorkspaceManager::ReadPaneTabsStatic("ignored", "cur_", out, MAX_PANES, rd);

  CHECK(out[0].tabCount == 1);
  CHECK(strcmp(out[0].tabs[0].name, "Routing Matrix") == 0);
  CHECK(out[0].tabs[0].toggleAction == 40251);  // KNOWN_WINDOWS resolution
}

TEST_CASE("RppReadAccessor key matching is exact-with-space, not prefix", "[persistence]")
{
  static char lines[3][RPP_MAX_LINE_LEN];
  snprintf(lines[0], RPP_MAX_LINE_LEN, "%s", "cur_pane_0_tab_1 First");
  snprintf(lines[1], RPP_MAX_LINE_LEN, "%s", "cur_pane_0_tab_10 Second");
  snprintf(lines[2], RPP_MAX_LINE_LEN, "%s", "novalue");

  RppReadAccessor rd(lines, 3);
  const char* v1 = rd.Get("S", "cur_pane_0_tab_1");
  REQUIRE(v1 != nullptr);
  CHECK(strcmp(v1, "First") == 0);
  const char* v10 = rd.Get("S", "cur_pane_0_tab_10");
  REQUIRE(v10 != nullptr);
  CHECK(strcmp(v10, "Second") == 0);
  CHECK(rd.Get("S", "novalue") == nullptr);   // line without separator
  CHECK(rd.Get("S", "missing") == nullptr);
  CHECK(rd.Get("S", "") == nullptr);
}
