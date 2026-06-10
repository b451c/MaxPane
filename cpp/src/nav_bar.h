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
  BTN_NONE           = -1,
  BTN_HOME           = 0,
  BTN_DRAG           = 1,
  BTN_SWITCH         = 2,
  BTN_SAVE           = 3,
  BTN_LOAD           = 4,
  BTN_SETTINGS       = 5,
  BTN_SUPPORT        = 6,
  // Collapse chevron at the far right (user request, 2026-06-10): collapses
  // the bar to a NAV_BAR_COLLAPSED_HEIGHT sliver; in collapsed mode the
  // whole sliver is this button (click anywhere to expand). The sliver must
  // never shrink to zero — captured child views paint ABOVE the container
  // view on macOS (ADR-026 lore), so a floating re-expand affordance over
  // the panes is impossible; the sliver is the only reachable click target.
  BTN_COLLAPSE       = 7,
  BUTTON_COUNT       = 8,
  // Pseudo-button: center-aligned workspace-name label between the two
  // groups. Hit-tested so the standard tooltip delay mechanism reuses the
  // m_navTooltipBtn path to display the full name when truncated.
  BTN_WORKSPACE_NAME = 8,
};

constexpr int NAV_BAR_HEIGHT           = 30;
constexpr int NAV_BAR_COLLAPSED_HEIGHT = 10;  // chevron sliver (collapsed mode)
constexpr int BUTTON_SIZE     = 26;    // all buttons square + identical width
constexpr int COLLAPSE_BTN_W  = 14;    // slim chevron button (expanded mode)
constexpr int MIN_VISIBLE_W   = 320;   // below this container width, bar hides

struct Layout {
  bool visible;
  bool collapsed;                      // sliver mode — barRect IS the sliver
  RECT barRect;                        // full nav bar strip
  RECT buttons[BUTTON_COUNT];          // empty rect = not laid out
  RECT divider1;                       // between Home and capture group
  RECT divider2;                       // between capture group and utility group
  // Feature A — workspace-name label centered between left+right groups.
  // Empty rect when no workspace is loaded; truncated to fit available
  // space between divider2.right and BTN_SETTINGS.left.
  RECT workspaceNameRect;
  bool workspaceNameTruncated;         // true if rendered with DT_END_ELLIPSIS
};

struct State {
  int hoverButton;     // BTN_NONE or button id under cursor
  bool collapsed;      // bar collapsed to the chevron sliver
  bool dragModeArmed;  // true when drag-to-dock is waiting/tracking
  bool homeActive;     // true when Home overlay is showing
  // Feature A — name of the currently-loaded workspace and dirty flag.
  // workspaceName == nullptr / "" → no label rendered, no space reserved.
  // workspaceDirty == true → trailing "•" (U+2022) appended to the label.
  const char* workspaceName;
  bool workspaceDirty;
};

// Plan layout for the given client rect. Bar is laid out at the very top.
// If client is too narrow, Layout::visible is false and the caller paints
// nothing (and reserves zero rows in the pane grid). If state.workspaceName
// is set, the workspace-name rect is filled too (and Layout::workspaceNameTruncated
// is set when the text wouldn't fit).
Layout Compute(const RECT& containerRect, const State& state, HDC measureHdc = nullptr);

// Render the bar. Tooltip is painted separately via PaintTooltip below so
// repeated hovers can flicker-free invalidate just the tooltip area.
void Paint(HDC hdc, const Layout& lay, const State& state, bool darkMode);

// Render a tooltip beneath the hovered button. Caller passes the same
// hoverButton it gave to Paint. Tooltip is positioned just below the bar.
// state is consulted for the workspace name when buttonId ==
// BTN_WORKSPACE_NAME so the tooltip reflects the live (possibly long) name.
void PaintTooltip(HDC hdc, const Layout& lay, int buttonId, const State& state,
                  bool darkMode);

// Returns ButtonId under (x, y) in client coords, or BTN_NONE. Includes
// BTN_WORKSPACE_NAME when (x, y) hits Layout::workspaceNameRect.
int HitTest(const Layout& lay, int x, int y);

// Tooltip text for the given button. Used for accessibility + dev reference.
// For BTN_WORKSPACE_NAME the caller must use State::workspaceName directly.
const char* ButtonTooltip(int buttonId, bool dragArmed);

} // namespace NavBar
