#pragma once

// Layout constants
static const int MAX_PANES = 16;
static const int MAX_TREE_NODES = 31;  // full binary tree depth 4: 16 leaves + 15 branches
static const int MAX_LEAVES = 16;
static const int MAX_SPLITTERS = 3;    // kept for backward compat (old save format)
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

// Legacy preset pane counts (used for backward compat loading)
static const int PRESET_PANE_COUNT[] = { 3, 2, 3, 4, 3 };

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

// Maximum favorites
static const int MAX_FAVORITES = 32;

// =========================================================================
// UI color constants
// =========================================================================

#ifdef _WIN32
#include <windows.h>
#else
#ifndef SWELL_PROVIDED_BY_APP
#define SWELL_PROVIDED_BY_APP
#endif
#include "swell/swell.h"
#endif

// Empty pane header
static const COLORREF COLOR_EMPTY_HEADER_BG   = RGB(50, 50, 50);
static const COLORREF COLOR_EMPTY_HEADER_TEXT  = RGB(180, 180, 180);

// Tab bar
static const COLORREF COLOR_TAB_BAR_BG        = RGB(40, 40, 40);
static const COLORREF COLOR_TAB_ACTIVE_BG     = RGB(80, 80, 80);
static const COLORREF COLOR_TAB_INACTIVE_BG   = RGB(50, 50, 50);
static const COLORREF COLOR_TAB_ACTIVE_TEXT    = RGB(220, 220, 220);
static const COLORREF COLOR_TAB_INACTIVE_TEXT  = RGB(160, 160, 160);
static const COLORREF COLOR_TAB_CLOSE_TEXT     = RGB(150, 150, 150);
static const COLORREF COLOR_TAB_SEPARATOR      = RGB(30, 30, 30);

// Drag highlight
static const COLORREF COLOR_DRAG_HIGHLIGHT     = RGB(80, 140, 255);

// Splitter hover highlight
static const COLORREF COLOR_SPLITTER_HIGHLIGHT = RGB(160, 160, 165);

// =========================================================================
// UI geometry constants
// =========================================================================

static const int DRAG_THRESHOLD_PX      = 15;
static const int CLOSE_BTN_WIDTH        = 12;
static const int CLOSE_BTN_RIGHT_MARGIN = 4;
static const int CLOSE_BTN_VERT_MARGIN  = 3;
static const int TAB_TEXT_LEFT_PAD      = 4;
static const int TAB_TEXT_RIGHT_MARGIN  = 16;

// =========================================================================
// Timing constants
// =========================================================================

static const int SHUTDOWN_GRACE_TICKS   = 10;    // ~5s at 500ms timer
static const int STARTUP_DELAY_TICKS    = 15;    // ~450ms at 30ms timer
