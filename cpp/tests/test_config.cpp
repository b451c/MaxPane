// Toolbar action table unit tests (v2.1.2 / ADR-052).
//
// Pins the title ↔ toggle-action mapping in config.cpp. The "Toolbar: Open/
// close toolbar N" action IDs are NOT one contiguous range — the original
// 41679 + (N-1) arithmetic silently mapped Toolbar 9-12 onto MIDI toolbar
// actions (41687-41690) and Toolbar 13 onto "Dockers: Compact when small and
// single tab" (41691). Verified ranges (REAPER 7.62 + two independent
// action-list dumps):
//   Toolbar 1-8   = 41679-41686
//   Toolbar 9-16  = 41936-41943
//   Toolbar 17-32 = 42713-42728  (added in REAPER 7)
// Main toolbar (41651) is excluded from capture but kept in the reverse
// lookup so startup ghost-cleanup can force-hide a floating ghost.

#include "catch2/catch.hpp"
#include "config.h"

#include <cstdio>
#include <cstring>

TEST_CASE("GetToolbarToggleAction maps the three toolbar ranges", "[toolbar-actions]")
{
  // Range edges, forward.
  CHECK(GetToolbarToggleAction("Toolbar 1") == 41679);
  CHECK(GetToolbarToggleAction("Toolbar 8") == 41686);
  CHECK(GetToolbarToggleAction("Toolbar 9") == 41936);
  CHECK(GetToolbarToggleAction("Toolbar 16") == 41943);
  CHECK(GetToolbarToggleAction("Toolbar 17") == 42713);
  CHECK(GetToolbarToggleAction("Toolbar 32") == 42728);

  // Regression pin — Toolbar 9 must NOT hit MIDI toolbar 1 (the old
  // contiguous-range arithmetic returned 41687 here).
  CHECK(GetToolbarToggleAction("Toolbar 9") != 41687);

  // Out of range / not a toolbar.
  CHECK(GetToolbarToggleAction("Toolbar 0") == 0);
  CHECK(GetToolbarToggleAction("Toolbar 33") == 0);
  CHECK(GetToolbarToggleAction("Mixer") == 0);
  CHECK(GetToolbarToggleAction(nullptr) == 0);

  // Toolbar Docker is its own single ID.
  CHECK(GetToolbarToggleAction("Toolbar Docker") == TOOLBAR_DOCKER_ACTION);

  // ADR-052 — Main toolbar is deliberately NOT a capturable toolbar.
  CHECK(GetToolbarToggleAction("Main toolbar") == 0);
}

TEST_CASE("GetSearchTitleForAction round-trips every toolbar", "[toolbar-actions]")
{
  char title[64];
  char buf[64];
  for (int n = 1; n <= 32; n++) {
    snprintf(title, sizeof(title), "Toolbar %d", n);
    int action = GetToolbarToggleAction(title);
    REQUIRE(action > 0);
    REQUIRE(GetSearchTitleForAction(action, buf, sizeof(buf)));
    CHECK(strcmp(buf, title) == 0);
  }

  REQUIRE(GetSearchTitleForAction(TOOLBAR_DOCKER_ACTION, buf, sizeof(buf)));
  CHECK(strcmp(buf, "Toolbar Docker") == 0);
}

TEST_CASE("GetSearchTitleForAction knows the Main toolbar for cleanup", "[toolbar-actions]")
{
  // Capture is blocked, but startup ghost-cleanup needs 41651 → "Main
  // toolbar" so its FindReaperWindow + SW_HIDE fallback can fire.
  char buf[64];
  REQUIRE(GetSearchTitleForAction(MAIN_TOOLBAR_ACTION, buf, sizeof(buf)));
  CHECK(strcmp(buf, "Main toolbar") == 0);
}

TEST_CASE("MIDI toolbar mapping (ADR-052 follow-up)", "[toolbar-actions]")
{
  // Range edges, forward — verified live on REAPER 7.62 (ToolbarTableCheck,
  // 2026-06-10): MIDI 1-4 = 41687-41690, 5-8 = 41944-41947, 9-16 = 42745-42752.
  CHECK(GetToolbarToggleAction("MIDI 1") == 41687);
  CHECK(GetToolbarToggleAction("MIDI 4") == 41690);
  CHECK(GetToolbarToggleAction("MIDI 5") == 41944);
  CHECK(GetToolbarToggleAction("MIDI 8") == 41947);
  CHECK(GetToolbarToggleAction("MIDI 9") == 42745);
  CHECK(GetToolbarToggleAction("MIDI 16") == 42752);
  CHECK(GetToolbarToggleAction("MIDI piano roll toolbar")
          == MIDI_PIANO_ROLL_TOOLBAR_ACTION);

  // Round-trip all 16 + the piano roll toolbar.
  char title[64];
  char buf[64];
  for (int n = 1; n <= 16; n++) {
    snprintf(title, sizeof(title), "MIDI %d", n);
    int action = GetToolbarToggleAction(title);
    REQUIRE(action > 0);
    REQUIRE(GetSearchTitleForAction(action, buf, sizeof(buf)));
    CHECK(strcmp(buf, title) == 0);
  }
  REQUIRE(GetSearchTitleForAction(MIDI_PIANO_ROLL_TOOLBAR_ACTION,
                                  buf, sizeof(buf)));
  CHECK(strcmp(buf, "MIDI piano roll toolbar") == 0);

  // Out of range / not MIDI toolbars.
  CHECK(GetToolbarToggleAction("MIDI 0") == 0);
  CHECK(GetToolbarToggleAction("MIDI 17") == 0);
  CHECK(GetToolbarToggleAction("MIDI") == 0);

  // 41691-41694 (docker settings + unrelated actions between MIDI 4 and the
  // Toolbar 9 range) stay unmapped — the old contiguous arithmetic claimed
  // them for Toolbar 13-16.
  for (int action = 41691; action <= 41694; action++) {
    CHECK_FALSE(GetSearchTitleForAction(action, buf, sizeof(buf)));
  }

  // Invalid input.
  CHECK_FALSE(GetSearchTitleForAction(0, buf, sizeof(buf)));
  CHECK_FALSE(GetSearchTitleForAction(-1, buf, sizeof(buf)));
  CHECK_FALSE(GetSearchTitleForAction(41651, nullptr, 0));
}

TEST_CASE("LookupToggleAction covers toolbars and known windows", "[toolbar-actions]")
{
  CHECK(LookupToggleAction("Toolbar 10") == 41937);
  CHECK(LookupToggleAction("Mixer") == 40078);       // KNOWN_WINDOWS prefix
  CHECK(LookupToggleAction("Main toolbar") == 0);    // ADR-052 exclusion
  CHECK(LookupToggleAction(nullptr) == 0);
}

// v2.5.0 — pane background override parser (Settings "Pane background").
TEST_CASE("ParsePaneBgOverride accepts #RRGGBB only", "[pane-bg]")
{
  COLORREF c = 0;
  CHECK(ParsePaneBgOverride("#000000", &c)); CHECK(c == RGB(0, 0, 0));
  CHECK(ParsePaneBgOverride("#1A2b3C", &c));
  CHECK(GetRValue(c) == 0x1A); CHECK(GetGValue(c) == 0x2B); CHECK(GetBValue(c) == 0x3C);
  CHECK(ParsePaneBgOverride("#FFFFFF", nullptr));  // out may be null
  CHECK_FALSE(ParsePaneBgOverride("auto", &c));
  CHECK_FALSE(ParsePaneBgOverride("", &c));
  CHECK_FALSE(ParsePaneBgOverride(nullptr, &c));
  CHECK_FALSE(ParsePaneBgOverride("#12345", &c));     // too short
  CHECK_FALSE(ParsePaneBgOverride("#1234567", &c));   // too long
  CHECK_FALSE(ParsePaneBgOverride("#12G456", &c));    // bad digit
  CHECK_FALSE(ParsePaneBgOverride("123456", &c));     // no '#'
}
