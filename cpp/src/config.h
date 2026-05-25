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
// Below this per-tab pixel width the close button is hidden and ignored by
// hit-test (otherwise name's DT_END_ELLIPSIS truncates right under the 'x').
// 90 ≈ 60 readable name + 16 close button area + 14 breathing room.
static const int TAB_CLOSE_MIN_WIDTH = 90;
static const int MAX_WORKSPACES = 32;
static const int MAX_WORKSPACE_NAME = 64;
extern const char* const EXT_SECTION;

// REAPER action IDs we care about by number (toolbars span a contiguous range;
// Toolbar Docker is a single ID). Surfaced as named constants instead of magic
// literals in window_manager.cpp::GetSearchTitleForAction.
static const int TOOLBAR_ACTION_BASE = 41679;   // Toolbar 1 (41679..41694 = Toolbar 1..16)
static const int TOOLBAR_ACTION_COUNT = 16;
static const int TOOLBAR_DOCKER_ACTION = 41084; // "View: Show docker"

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

// Pane background (captured windows have no own background — parent color shows through).
// Adapts to macOS dark mode at startup via GetPaneBgColor() / GetPaneGridLineColor().
COLORREF GetPaneBgColor();
COLORREF GetPaneGridLineColor();

// Effective dark mode — consults ExtState "dark_mode" override
// ("auto"|"dark"|"light"; absent = "auto"), then falls back to
// IsSystemDarkMode(). Cached after first call; Settings dialog calls
// InvalidateMaxPaneDarkModeCache() after the user changes the override
// so the next paint pass re-evaluates.
bool MaxPaneIsDarkMode();
void InvalidateMaxPaneDarkModeCache();
#define COLOR_PANE_BG        GetPaneBgColor()
#define COLOR_PANE_GRID_LINE GetPaneGridLineColor()

// Grid line spacing in empty panes
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
static const int TIMER_ID_LAUNCHER_TIP    = 4;
static const int LAUNCHER_TOOLTIP_DELAY_MS = 600;
// Toast bar for non-fatal feedback (Sprint 3.2). Tick interval drives the
// fade animation; duration is total visible time; fade is the trailing
// portion during which alpha ramps to 0.
static const int TIMER_ID_TOAST           = 5;
static const int TOAST_TICK_MS            = 30;
static const int TOAST_DURATION_MS        = 3000;
static const int TOAST_FADE_MS            = 500;
static const int TOAST_BAR_HEIGHT         = 24;

// ADR-026 — drag-to-dock polling timer. Active only during ARMED / TRACKING
// modes; fires every DRAG_DOCK_TICK_MS to read global cursor pos + L-button
// state. Polling avoids subclassing third-party REAPER windows (Approach 2).
static const int TIMER_ID_DRAG_DOCK       = 6;
static const int DRAG_DOCK_TICK_MS        = 16;   // ~60 Hz preview update
// Nav bar tooltip delay — reuses TIMER_ID_LAUNCHER_TIP for simplicity (the
// two states never overlap: launcher hero hides nav bar tooltips; vice
// versa). Same 600 ms delay.
static const int TIMER_ID_NAVBAR_TIP      = 7;
static const int NAVBAR_TOOLTIP_DELAY_MS  = 600;

// Feature B — async workspace save flush. One-shot timer that fires the
// deferred ExtState write off the user-click frame. Interval is short so
// the perceived "Saving…" → "Saved" transition feels snappy (sub-100 ms
// for typical workspace sizes — still way under the human-noticeable
// click-frame jank threshold of ~16 ms).
static const int TIMER_ID_WORKSPACE_FLUSH = 8;
static const int WORKSPACE_FLUSH_DELAY_MS = 30;
