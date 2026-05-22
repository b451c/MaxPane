// drag_dock.h — Drag-to-dock state machine + drop zone geometry (ADR-026).
//
// Workflow:
//   1. User clicks the [Drag] button on the nav bar → State.mode = ARMED.
//      MaxPaneContainer starts a timer at TIMER_DRAG_DOCK_INTERVAL.
//   2. Timer polls GetCursorPos + GetAsyncKeyState(VK_LBUTTON). When the
//      L-button transitions from up→down outside MaxPane, we record the
//      source HWND via WindowFromPoint and walk to its top-level. mode →
//      TRACKING.
//   3. During TRACKING, each timer tick recomputes the hovered pane + zone
//      and asks the container to invalidate the relevant rects. The user
//      is dragging the REAPER window normally via the OS; we never touch
//      it.
//   4. On L-button release (down→up): if cursor is inside a MaxPane pane
//      and the zone is valid → caller dispatches the drop (split/tab/
//      replace). Either way, mode → IDLE.
//   5. ESC at any point → mode → IDLE.
//
// Polling beats hooks here because (a) it avoids subclassing third-party
// HWNDs (ADR-026 Approach 2), (b) it works identically on macOS / Win32 /
// Linux SWELL builds — no platform-specific monitor APIs needed for the
// minimum viable feature. ~16 ms tick gives the preview a responsive feel
// without burning CPU.
#pragma once
#include "platform.h"

namespace DragDock {

enum Mode : int {
  IDLE     = 0,
  ARMED    = 1,   // [Drag] clicked, waiting for user to grab a REAPER window
  TRACKING = 2,   // user is mid-drag — preview rendering
};

enum DropZone : int {
  ZONE_NONE         = 0,
  ZONE_TAB_BAR      = 1,   // drop = add as new tab in hovered pane
  ZONE_BODY_CENTER  = 2,   // drop = add as new tab (forgiving)
  ZONE_SPLIT_LEFT   = 3,   // drop = vertical split, new pane on the left
  ZONE_SPLIT_RIGHT  = 4,   // drop = vertical split, new pane on the right
  ZONE_SPLIT_TOP    = 5,   // drop = horizontal split, new pane on top
  ZONE_SPLIT_BOTTOM = 6,   // drop = horizontal split, new pane on bottom
  ZONE_REPLACE      = 7,   // Shift held over body → replace active tab
};

struct State {
  Mode mode;
  bool lastBtnDown;             // previous tick's L-button state — for edge detect
  POINT mouseDownScreenPos;     // recorded at ARMED→TRACKING transition
  HWND sourceHwnd;              // window the user is dragging (top-level)
  int  sourceToggleAction;      // resolved from title at mouse-down
  char sourceTitle[256];        // window title at mouse-down

  POINT cursorScreenPos;        // latest tracked screen pos
  int   targetPaneId;           // pane under cursor, -1 if none
  DropZone zone;
  bool shiftHeld;
};

// Initialize state to IDLE — call from container constructor.
void Reset(State& s);

// Compute the drop zone for a cursor point (x, y) inside a pane rect.
// Returns ZONE_NONE if cursor is outside the rect.
DropZone ComputeZone(const RECT& paneRect, int x, int y, bool shiftHeld);

// Paint the live drop preview overlay for the active target pane. No-op
// if zone is ZONE_NONE. Drawn by the container's OnPaint after the
// regular pane grid + tab bars are rendered.
void PaintPreview(HDC hdc, const RECT& paneRect, DropZone zone, bool darkMode);

// Paint a small floating indicator (centered hint text) describing what
// the drop will do. E.g. "Add as tab" / "Split right" / "Replace".
void PaintIndicator(HDC hdc, const RECT& paneRect, DropZone zone, bool darkMode,
                    const char* sourceTitle);

// Human-readable label for the drop action — used by PaintIndicator + the
// nav bar tooltip text while armed.
const char* ZoneLabel(DropZone zone);

} // namespace DragDock
