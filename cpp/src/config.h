#pragma once

// Layout constants
static const int MAX_PANES = 4;
static const int MAX_SPLITTERS = 3;
static const int SPLITTER_WIDTH = 5;
static const int MIN_PANE_SIZE = 50;
static const int PANE_HEADER_HEIGHT = 18;
static const int MAX_TABS_PER_PANE = 8;
static const int TAB_BAR_HEIGHT = 20;
static const int TAB_MIN_WIDTH = 60;
static const int TAB_MAX_WIDTH = 150;
static const int MAX_WORKSPACES = 10;
static const int MAX_WORKSPACE_NAME = 64;
static const char* EXT_SECTION = "ReDockIt_cpp";

// Tab color palette (index 0 = no color)
static const int TAB_COLOR_COUNT = 9;
struct TabColor {
  const char* name;
  unsigned char r, g, b;
};
static const TabColor TAB_COLORS[] = {
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

// Layout presets
enum LayoutPreset {
    PRESET_LEFT_RIGHT2V = 0,  // [left] | [right-top / right-bottom]
    PRESET_TWO_COLUMNS,       // [left] | [right]
    PRESET_THREE_COLUMNS,     // [left] | [mid] | [right]
    PRESET_GRID_2X2,          // [TL] [TR] / [BL] [BR]
    PRESET_TOP_BOTTOM2H,      // [top] / [bottom-left | bottom-right]
    PRESET_COUNT
};

static const char* PRESET_NAMES[] = {
    "Left + Right Split",
    "Two Columns",
    "Three Columns",
    "2x2 Grid",
    "Top + Bottom Split"
};

static const int PRESET_PANE_COUNT[] = { 3, 2, 3, 4, 3 };
static const int PRESET_SPLITTER_COUNT[] = { 2, 1, 2, 2, 2 };

// Splitter orientation
enum SplitterOrientation { SPLIT_VERTICAL, SPLIT_HORIZONTAL };

// Known REAPER windows
struct WindowDef {
  const char* name;
  const char* searchTitle;
  const char* altSearchTitle;
  int toggleActionId;
  int defaultPane;
};

static const WindowDef KNOWN_WINDOWS[] = {
  {"Media Explorer",    "Media Explorer",        nullptr,          50124, 0},
  {"FX Browser",        "Browse FX",             "Add FX",          40271, 1},
  {"Actions",           "Actions",               nullptr,          40605, 2},
  {"Mixer",             "Mixer",                 nullptr,          40078, 0},
  {"Region Manager",    "Region/Marker Manager", "Region",         40326, 1},
  {"Routing Matrix",    "Routing Matrix",        "Matrix",         40251, 1},
  {"Track Manager",     "Track Manager",         nullptr,          40906, 2},
  {"Big Clock",         "Big Clock",             nullptr,          40378, 2},
  {"Navigator",         "Navigator",             nullptr,          40268, 2},
  {"Undo History",      "Undo History",          nullptr,          40072, 2},
  {"Project Bay",       "Project Bay",           nullptr,          41157, 1},
  {"Video",             "Video Window",          "Video",          50125, 0},
  {"Virtual MIDI Keyboard", "Virtual MIDI Keyboard", "MIDI Keyboard", 40377, 2},
  {"Performance Meter", "Performance Meter",     nullptr,          40240, 2},
};

static const int NUM_KNOWN_WINDOWS = sizeof(KNOWN_WINDOWS) / sizeof(KNOWN_WINDOWS[0]);
