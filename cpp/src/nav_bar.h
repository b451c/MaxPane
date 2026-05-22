// nav_bar.h — Persistent navigation toolbar at the top of every MaxPane
// container (ADR-026).
//
// Surfaces the quick actions that used to be buried in the right-click menu
// or available only via REAPER actions:
//   [Home] | [Drag] [Switch] [Save] [Load▾] |        [Settings] [Support]
//
// One bar per container instance. Reserved height NAV_BAR_HEIGHT subtracted
// from the client area available to the pane grid. Dark/light palette via
// MaxPaneIsDarkMode(). Toggleable via Settings → "Show navigation bar"
// (ExtState key "show_nav_bar" per-instance, default ON).
#pragma once
#include "platform.h"

namespace NavBar {

enum ButtonId : int {
  BTN_NONE     = -1,
  BTN_HOME     = 0,
  BTN_DRAG     = 1,
  BTN_SWITCH   = 2,
  BTN_SAVE     = 3,
  BTN_LOAD     = 4,
  BTN_SETTINGS = 5,
  BTN_SUPPORT  = 6,
  BUTTON_COUNT = 7,
};

constexpr int NAV_BAR_HEIGHT  = 30;
constexpr int BUTTON_SIZE     = 26;    // all buttons square + identical width
constexpr int MIN_VISIBLE_W   = 320;   // below this container width, bar hides

struct Layout {
  bool visible;
  RECT barRect;                        // full nav bar strip
  RECT buttons[BUTTON_COUNT];          // empty rect = not laid out
  RECT divider1;                       // between Home and capture group
  RECT divider2;                       // between capture group and utility group
};

struct State {
  int hoverButton;     // BTN_NONE or button id under cursor
  bool dragModeArmed;  // true when drag-to-dock is waiting/tracking
  bool homeActive;     // true when Home overlay is showing
};

// Plan layout for the given client rect. Bar is laid out at the very top.
// If client is too narrow, Layout::visible is false and the caller paints
// nothing (and reserves zero rows in the pane grid).
Layout Compute(const RECT& containerRect);

// Render the bar. Tooltip is painted separately via PaintTooltip below so
// repeated hovers can flicker-free invalidate just the tooltip area.
void Paint(HDC hdc, const Layout& lay, const State& state, bool darkMode);

// Render a tooltip beneath the hovered button. Caller passes the same
// hoverButton it gave to Paint. Tooltip is positioned just below the bar.
void PaintTooltip(HDC hdc, const Layout& lay, int buttonId, bool darkMode);

// Returns ButtonId under (x, y) in client coords, or BTN_NONE.
int HitTest(const Layout& lay, int x, int y);

// Tooltip text for the given button. Used for accessibility + dev reference.
const char* ButtonTooltip(int buttonId, bool dragArmed);

} // namespace NavBar
