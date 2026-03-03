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
extern const char* const EXT_SECTION;

// Tab color palette (index 0 = no color)
static const int TAB_COLOR_COUNT = 9;
struct TabColor {
  const char* name;
  unsigned char r, g, b;
};
extern const TabColor TAB_COLORS[];

// Layout presets
enum LayoutPreset {
    PRESET_LEFT_RIGHT2V = 0,  // [left] | [right-top / right-bottom]
    PRESET_TWO_COLUMNS,       // [left] | [right]
    PRESET_THREE_COLUMNS,     // [left] | [mid] | [right]
    PRESET_GRID_2X2,          // [TL] [TR] / [BL] [BR]
    PRESET_TOP_BOTTOM2H,      // [top] / [bottom-left | bottom-right]
    PRESET_COUNT
};

extern const char* const PRESET_NAMES[];

// Legacy preset pane counts (used for backward compat loading)
extern const int PRESET_PANE_COUNT[];

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

extern const WindowDef KNOWN_WINDOWS[];
extern const int NUM_KNOWN_WINDOWS;

// Maximum favorites
static const int MAX_FAVORITES = 32;

// =========================================================================
// UI color constants
// =========================================================================

#include "platform.h"

// Pane background (captured windows have no own background — parent color shows through)
static const COLORREF COLOR_PANE_BG           = RGB(172, 172, 172);

// Subtle vertical lines drawn in empty panes (disappear when a window is captured)
static const COLORREF COLOR_PANE_GRID_LINE    = RGB(158, 158, 158);
static const int      PANE_GRID_SPACING       = 24;

// Empty pane header
static const COLORREF COLOR_EMPTY_HEADER_BG   = RGB(50, 50, 50);
static const COLORREF COLOR_EMPTY_HEADER_TEXT  = RGB(180, 180, 180);

// Tab bar
static const COLORREF COLOR_TAB_BAR_BG        = RGB(40, 40, 40);
static const COLORREF COLOR_TAB_ACTIVE_BG     = RGB(80, 80, 80);
static const COLORREF COLOR_TAB_INACTIVE_BG   = RGB(50, 50, 50);
static const int TAB_HOVER_LIGHTEN            = 25;  // added to each RGB channel on hover
static const COLORREF COLOR_TAB_ACTIVE_TEXT    = RGB(220, 220, 220);
static const COLORREF COLOR_TAB_INACTIVE_TEXT  = RGB(160, 160, 160);
static const COLORREF COLOR_TAB_CLOSE_TEXT     = RGB(150, 150, 150);
static const COLORREF COLOR_TAB_SEPARATOR      = RGB(30, 30, 30);

// Drag highlight
static const COLORREF COLOR_DRAG_HIGHLIGHT     = RGB(80, 140, 255);

// Splitter hover highlight
static const COLORREF COLOR_SPLITTER_HIGHLIGHT = RGB(255, 255, 255);
static const int SPLITTER_HIGHLIGHT_INSET = 1;

// =========================================================================
// UI geometry constants
// =========================================================================

static const int DRAG_THRESHOLD_PX      = 15;
static const int CLOSE_BTN_WIDTH        = 12;
static const int CLOSE_BTN_RIGHT_MARGIN = 4;
static const int CLOSE_BTN_VERT_MARGIN  = 3;
static const int TAB_TEXT_LEFT_PAD      = 4;
static const int TAB_TEXT_RIGHT_MARGIN  = 16;

// Pane menu button (▼) in tab bar
static const int PANE_MENU_BTN_WIDTH   = 16;
static const int PANE_MENU_BTN_MARGIN  = 2;


// =========================================================================
// Timing constants
// =========================================================================

static const int STARTUP_DELAY_TICKS    = 15;    // ~450ms at 30ms timer

// Container timer IDs and intervals
static const int TIMER_ID_CHECK           = 1;
static const int TIMER_INTERVAL           = 500;   // ms — CheckAlive + RPP poll
static const int TIMER_ID_CAPTURE         = 2;
static const int TIMER_CAPTURE_INTERVAL   = 50;    // ms — CaptureQueue tick
static const int TIMER_ID_HOVER           = 3;
