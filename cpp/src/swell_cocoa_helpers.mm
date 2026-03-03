#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "swell_cocoa_helpers.h"
#include "debug.h"

void ForceViewLayoutAndDisplay(HWND hwnd)
{
  if (!hwnd) return;

  // On SWELL, HWND is an NSView*.
  NSView* view = (NSView*)hwnd;
  if (![view isKindOfClass:[NSView class]]) return;

  // Force the view subtree to re-layout — SWELL's SetParent does not
  // call setNeedsLayout:, so child controls may have stale frames.
  [view setNeedsLayout:YES];
  [view layoutSubtreeIfNeeded];

  // Force immediate display of the view and all subviews.
  // SWELL's InvalidateRect only marks the target view, not children.
  [view setNeedsDisplay:YES];
  [view displayIfNeeded];

  // Also force layout + display on every direct subview — some REAPER
  // windows (Routing Matrix) have child controls that need their own
  // layout pass after the parent's frame changes.
  for (NSView* child in [view subviews]) {
    [child setNeedsLayout:YES];
    [child layoutSubtreeIfNeeded];
    [child setNeedsDisplay:YES];
    [child displayIfNeeded];
  }
}

#else
// Windows / Linux stub
#include "swell_cocoa_helpers.h"
void ForceViewLayoutAndDisplay(HWND) {}
#endif
