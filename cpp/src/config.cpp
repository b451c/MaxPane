#include "config.h"
#include "swell_cocoa_helpers.h"
#include "globals.h"
#include <cstring>
#include <cstdio>   // snprintf (GetSearchTitleForAction)
#include <cstdlib>  // atoi (GetToolbarToggleAction)

// Cached effective dark mode — invalidated when Settings dialog changes
// the user override (see InvalidateMaxPaneDarkModeCache).
static bool g_darkModeChecked = false;
static bool g_darkModeActive = false;

bool MaxPaneIsDarkMode()
{
  if (!g_darkModeChecked) {
    // ExtState override: "dark"/"light" force; "auto" or absent falls back.
    const char* mode = g_GetExtState ? g_GetExtState(EXT_SECTION, "dark_mode") : nullptr;
    if (mode && std::strcmp(mode, "dark") == 0) {
      g_darkModeActive = true;
    } else if (mode && std::strcmp(mode, "light") == 0) {
      g_darkModeActive = false;
    } else {
      g_darkModeActive = IsSystemDarkMode();
    }
    g_darkModeChecked = true;
  }
  return g_darkModeActive;
}

// v2.5.0 (quar_edm #91 "black border color option") — pane background
// override. ExtState "pane_bg": absent / "auto" = theme-derived (the v2.4
// look); "#RRGGBB" = fixed color. Cached alongside the dark-mode flag and
// invalidated by the same call, so Settings OK refreshes both at once.
static bool g_paneBgChecked = false;
static bool g_paneBgOverride = false;
static COLORREF g_paneBgColor = 0;

bool ParsePaneBgOverride(const char* value, COLORREF* out)
{
  if (!value || value[0] != '#' || std::strlen(value) != 7) return false;
  unsigned int rgb = 0;
  for (int i = 1; i < 7; i++) {
    const char c = value[i];
    unsigned int d;
    if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
    else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
    else return false;
    rgb = (rgb << 4) | d;
  }
  if (out) *out = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
  return true;
}

static void EnsurePaneBgOverride()
{
  if (g_paneBgChecked) return;
  const char* v = g_GetExtState ? g_GetExtState(EXT_SECTION, "pane_bg") : nullptr;
  g_paneBgOverride = ParsePaneBgOverride(v, &g_paneBgColor);
  g_paneBgChecked = true;
}

void InvalidateMaxPaneDarkModeCache()
{
  g_darkModeChecked = false;
  g_paneBgChecked = false;
}

COLORREF GetPaneBgColor()
{
  EnsurePaneBgOverride();
  if (g_paneBgOverride) return g_paneBgColor;
  return MaxPaneIsDarkMode() ? RGB(51, 51, 51) : RGB(172, 172, 172);
}

COLORREF GetPaneGridLineColor()
{
  EnsurePaneBgOverride();
  if (g_paneBgOverride) {
    // Grid lines a notch off the chosen background, in the direction that
    // stays visible (lighter on dark colors, darker on light ones) — same
    // 14-step offset as the theme pair below.
    const int r = GetRValue(g_paneBgColor), g = GetGValue(g_paneBgColor),
              b = GetBValue(g_paneBgColor);
    const int lum = (r * 299 + g * 587 + b * 114) / 1000;
    const int d = (lum < 128) ? 14 : -14;
    auto clampc = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    return RGB(clampc(r + d), clampc(g + d), clampc(b + d));
  }
  return MaxPaneIsDarkMode() ? RGB(65, 65, 65) : RGB(158, 158, 158);
}

const char* const EXT_SECTION = "MaxPane_cpp";

// U13 (ADR-068, poydepzaj1616 #47) — inter-pane border (splitter) color
// presets. A cycle-button palette matching the TAB_COLORS / dark-mode-cycle
// conventions; "Default (system)" keeps the pre-U13 GetSysColor look.
const SplitterColorPreset SPLITTER_COLOR_PRESETS[] = {
  { "Default (system)", "default",  0x000000, true  },
  // v2.5.0 (quar_edm #91) — for the chrome-free "silent" look: borders
  // vanish against a black pane background / dark plugin UIs.
  { "Black",            "black",    0x000000, false },
  { "Graphite",         "graphite", 0x4A4D52, false },
  { "Slate blue",       "blue",     0x2D6BB4, false },
  { "Teal",             "teal",     0x2E8B74, false },
  { "Amber",            "amber",    0xC28E2C, false },
  { "Crimson",          "crimson",  0xB44A4A, false },
  { "Violet",           "violet",   0x7C5CB4, false },
};
const int NUM_SPLITTER_COLOR_PRESETS =
    (int)(sizeof(SPLITTER_COLOR_PRESETS) / sizeof(SPLITTER_COLOR_PRESETS[0]));

int GetSplitterColorPresetIndex()
{
  if (!g_GetExtState) return 0;
  const char* v = g_GetExtState(EXT_SECTION, "splitter_color");
  if (!v || !v[0]) return 0;
  for (int i = 0; i < NUM_SPLITTER_COLOR_PRESETS; i++) {
    if (strcmp(v, SPLITTER_COLOR_PRESETS[i].key) == 0) return i;
  }
  return 0;
}

const TabColor TAB_COLORS[] = {
  {"Default",  0,   0,   0  },  // 0: no color
  {"Red",      180, 60,  60 },  // 1
  {"Orange",   190, 120, 50 },  // 2
  {"Yellow",   180, 170, 50 },  // 3
  {"Green",    60,  150, 70 },  // 4
  {"Blue",     60,  100, 180},  // 5
  {"Purple",   130, 70,  170},  // 6
  {"Pink",     170, 70,  130},  // 7
  {"Cyan",     50,  150, 160},  // 8
};

const char* const PRESET_NAMES[] = {
    "Left + Right Split",
    "Two Columns",
    "Three Columns",
    "2x2 Grid",
    "Top + Bottom Split"
};

const int PRESET_PANE_COUNT[] = { 3, 2, 3, 4, 3 };

const WindowDef KNOWN_WINDOWS[] = {
  // Mixing & Routing
  {"Mixer",                 "Mixer",                 nullptr,          40078, 0},
  {"Track Manager",         "Track Manager",         nullptr,          40906, 2},
  {"Routing Matrix",        "Routing Matrix",        nullptr,          40251, 1},
  // Browsing & Media
  {"Media Explorer",        "Media Explorer",        nullptr,          50124, 0},
  {"FX Browser",            "Browse FX",             "Add FX",         40271, 1},
  {"Project Bay",           "Project Bay",           nullptr,          41157, 1},
  // Regions
  {"Region Manager",        "Region/Marker Manager", "Region",         40326, 1},
  {"Region Render Matrix",  "Region Render Matrix",  nullptr,          41888, 1},
  // Editing
  {"Actions",               "Actions",               nullptr,          40605, 2},
  {"Undo History",          "Undo History",           nullptr,          40072, 2},
  // Monitoring
  {"Navigator",             "Navigator",             nullptr,          40268, 2},
  {"Big Clock",             "Big Clock",             nullptr,          40378, 2},
  {"Video",                 "Video Window",          "Video",          50125, 0},
  {"Performance Meter",     "Performance Meter",     nullptr,          40240, 2},
  // Instruments
  {"Virtual MIDI Keyboard", "Virtual MIDI Keyboard", "MIDI Keyboard",  40377, 2},
};

const int NUM_KNOWN_WINDOWS = sizeof(KNOWN_WINDOWS) / sizeof(KNOWN_WINDOWS[0]);

// =========================================================================
// Title ↔ toggle-action mapping (moved from window_manager.cpp in v2.1.2 /
// ADR-052 — pure logic, no SWELL, so the unit-test target covers the table).
// =========================================================================

// Single parser for the persisted "arb:<cmdstr>:<name>" tab value (audit
// M2.7). Mirrors the historical ReadPaneTabsStatic semantics exactly:
// cmd "0" is the legacy no-action marker and clears to empty; the legacy
// "arb:<name>" form (no second colon) yields no command.
bool ParseArbSpec(const char* value, ArbSpec* out)
{
  if (!value || !out || strncmp(value, "arb:", 4) != 0) return false;
  out->cmd[0] = '\0';
  out->name[0] = '\0';
  out->action = 0;

  const char* afterArb = value + 4;
  const char* secondColon = strchr(afterArb, ':');
  if (secondColon && secondColon > afterArb) {
    size_t cmdLen = (size_t)(secondColon - afterArb);
    if (cmdLen >= sizeof(out->cmd)) cmdLen = sizeof(out->cmd) - 1;
    memcpy(out->cmd, afterArb, cmdLen);
    out->cmd[cmdLen] = '\0';
    if (strcmp(out->cmd, "0") == 0) {
      out->cmd[0] = '\0';
    } else {
      out->action = ResolveActionCommand(out->cmd);
    }
    safe_strncpy(out->name, secondColon + 1, sizeof(out->name));
  } else {
    safe_strncpy(out->name, afterArb, sizeof(out->name));
  }
  return true;
}

// Detect REAPER toggle action for toolbar windows by title.
// Returns action ID or 0 if not a toolbar.
// NOTE: deliberately returns 0 for "Main toolbar" — the main window's top
// chrome is excluded from capture (ADR-052); cleanup knows it via
// GetSearchTitleForAction(MAIN_TOOLBAR_ACTION) instead.
int GetToolbarToggleAction(const char* title)
{
  if (!title) return 0;
  if (strcmp(title, "Toolbar Docker") == 0) return TOOLBAR_DOCKER_ACTION;
  if (strncmp(title, "Toolbar ", 8) == 0) {
    int n = atoi(title + 8);
    for (int i = 0; i < NUM_TOOLBAR_ACTION_RANGES; i++) {
      const ToolbarActionRange& r = TOOLBAR_ACTION_RANGES[i];
      if (n >= r.firstToolbar && n <= r.lastToolbar)
        return r.baseAction + (n - r.firstToolbar);
    }
  }
  // MIDI toolbars (ADR-052 follow-up) — floating windows are titled "MIDI N".
  if (strcmp(title, "MIDI piano roll toolbar") == 0)
    return MIDI_PIANO_ROLL_TOOLBAR_ACTION;
  if (strncmp(title, "MIDI ", 5) == 0) {
    int n = atoi(title + 5);
    for (int i = 0; i < NUM_MIDI_TOOLBAR_ACTION_RANGES; i++) {
      const ToolbarActionRange& r = MIDI_TOOLBAR_ACTION_RANGES[i];
      if (n >= r.firstToolbar && n <= r.lastToolbar)
        return r.baseAction + (n - r.firstToolbar);
    }
  }
  return 0;
}

// Look up REAPER toggle action for any window title.
// Checks toolbars first, then KNOWN_WINDOWS by searchTitle/altSearchTitle prefix.
int LookupToggleAction(const char* title)
{
  if (!title) return 0;
  int a = GetToolbarToggleAction(title);
  if (a > 0) return a;
  for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
    if (strstr(title, KNOWN_WINDOWS[i].searchTitle) == title)
      return KNOWN_WINDOWS[i].toggleActionId;
    if (KNOWN_WINDOWS[i].altSearchTitle &&
        strstr(title, KNOWN_WINDOWS[i].altSearchTitle) == title)
      return KNOWN_WINDOWS[i].toggleActionId;
  }
  return 0;
}

bool GetSearchTitleForAction(int action, char* buf, int bufSize)
{
  if (action <= 0 || !buf || bufSize <= 0) return false;
  // Toolbars: three non-contiguous ranges → "Toolbar 1".."Toolbar 32"
  for (int i = 0; i < NUM_TOOLBAR_ACTION_RANGES; i++) {
    const ToolbarActionRange& r = TOOLBAR_ACTION_RANGES[i];
    if (action >= r.baseAction &&
        action <= r.baseAction + (r.lastToolbar - r.firstToolbar)) {
      snprintf(buf, bufSize, "Toolbar %d",
               r.firstToolbar + (action - r.baseAction));
      return true;
    }
  }
  // MIDI toolbars: three non-contiguous ranges → "MIDI 1".."MIDI 16"
  for (int i = 0; i < NUM_MIDI_TOOLBAR_ACTION_RANGES; i++) {
    const ToolbarActionRange& r = MIDI_TOOLBAR_ACTION_RANGES[i];
    if (action >= r.baseAction &&
        action <= r.baseAction + (r.lastToolbar - r.firstToolbar)) {
      snprintf(buf, bufSize, "MIDI %d",
               r.firstToolbar + (action - r.baseAction));
      return true;
    }
  }
  if (action == MIDI_PIANO_ROLL_TOOLBAR_ACTION) {
    safe_strncpy(buf, "MIDI piano roll toolbar", bufSize);
    return true;
  }
  if (action == TOOLBAR_DOCKER_ACTION) {
    safe_strncpy(buf, "Toolbar Docker", bufSize);
    return true;
  }
  // Main toolbar: capture is blocked (ADR-052) but startup ghost-cleanup
  // needs the reverse lookup so its FindReaperWindow + SW_HIDE force-hide
  // fallback can neutralize an already-floating ghost (self-heal).
  if (action == MAIN_TOOLBAR_ACTION) {
    safe_strncpy(buf, "Main toolbar", bufSize);
    return true;
  }
  // Known windows
  for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
    if (KNOWN_WINDOWS[i].toggleActionId == action) {
      safe_strncpy(buf, KNOWN_WINDOWS[i].searchTitle, bufSize);
      return true;
    }
  }
  return false;
}
