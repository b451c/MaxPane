// macOS only — compiled via CMakeLists.txt (APPLE target_sources)
#import <Cocoa/Cocoa.h>
#include "swell_cocoa_helpers.h"
#include "debug.h"

bool IsSystemDarkMode()
{
  if (@available(macOS 10.14, *)) {
    NSAppearanceName appearanceName = [[NSApp effectiveAppearance] bestMatchFromAppearancesWithNames:
      @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
    return [appearanceName isEqualToString:NSAppearanceNameDarkAqua];
  }
  return false;
}

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

void ForceHideWindow(HWND hwnd)
{
  if (!hwnd) return;
  NSView* view = (NSView*)hwnd;
  if (![view isKindOfClass:[NSView class]]) return;

  // Hide the NSView itself — safe on all states.
  [view setHidden:YES];

  // If the view OWNS its NSWindow (it is the contentView, i.e. top-level after
  // SetParent(nullptr)), orderOut: removes that NSWindow from screen ordering.
  // For a subview embedded in someone else's NSWindow (REAPER main, MaxPane
  // container), [view window] returns the *host* NSWindow — calling orderOut:
  // on that hides the host entirely. setHidden:YES above already handles the
  // subview case; only orderOut when this view is the contentView.
  NSWindow* window = [view window];
  if (window && [window contentView] == view) {
    [window orderOut:nil];
  }
}

void ApplyFloatingWindowChrome(HWND hwnd, const char* title)
{
  if (!hwnd) return;
  NSView* view = (NSView*)hwnd;
  if (![view isKindOfClass:[NSView class]]) return;
  NSWindow* window = [view window];
  // Only apply chrome when the view owns its NSWindow (top-level state after
  // SetParent(nullptr)). Embedded subviews don't get standalone chrome.
  if (!window || [window contentView] != view) return;

  NSUInteger mask = NSWindowStyleMaskTitled
                  | NSWindowStyleMaskClosable
                  | NSWindowStyleMaskMiniaturizable
                  | NSWindowStyleMaskResizable;
  [window setStyleMask:mask];
  if (title && title[0]) {
    [window setTitle:[NSString stringWithUTF8String:title]];
  }
  // Make sure the window can receive key events for keyboard nav.
  [window setAcceptsMouseMovedEvents:YES];
}

void OpenUrlPlatform(const char* url)
{
  if (!url || !url[0]) return;
  NSString* s = [NSString stringWithUTF8String:url];
  NSURL* u = [NSURL URLWithString:s];
  if (!u) return;
  [[NSWorkspace sharedWorkspace] openURL:u];
}

void SetWindowAlwaysOnTop(HWND hwnd, bool onTop)
{
  if (!hwnd) return;
  NSView* view = (NSView*)hwnd;
  if (![view isKindOfClass:[NSView class]]) return;
  NSWindow* window = [view window];
  // Only act when the view owns its NSWindow — embedded subviews can't have
  // independent window level. (Same guard as ApplyFloatingWindowChrome.)
  if (!window || [window contentView] != view) return;
  [window setLevel:onTop ? NSFloatingWindowLevel : NSNormalWindowLevel];
}

void ClampRectToVisibleScreen(RECT* rect)
{
  if (!rect) return;
  // Convert RECT (top-left origin, y-down) to NSRect (bottom-left origin, y-up).
  // We only need rect bounds for intersection — keep both representations.
  NSArray<NSScreen*>* screens = [NSScreen screens];
  if ([screens count] == 0) return;

  // Find the screen with the largest intersection with rect (in screen coords).
  // Cocoa global screen coords have origin at primary screen's bottom-left;
  // Win32 coords have origin at primary screen's top-left. Translate before
  // intersecting.
  NSScreen* primary = [screens firstObject];
  CGFloat primaryHeight = [primary frame].size.height;

  // rect in Cocoa coords (bottom-left origin):
  CGFloat rx = (CGFloat)rect->left;
  CGFloat rw = (CGFloat)(rect->right - rect->left);
  CGFloat rh = (CGFloat)(rect->bottom - rect->top);
  CGFloat ry = primaryHeight - (CGFloat)rect->bottom;
  NSRect rectCocoa = NSMakeRect(rx, ry, rw, rh);

  NSScreen* best = primary;
  CGFloat bestArea = 0;
  for (NSScreen* s in screens) {
    NSRect inter = NSIntersectionRect([s visibleFrame], rectCocoa);
    CGFloat area = inter.size.width * inter.size.height;
    if (area > bestArea) {
      bestArea = area;
      best = s;
    }
  }

  NSRect vf = [best visibleFrame];
  // Clamp size first — don't exceed screen dimensions.
  if (rw > vf.size.width)  rw = vf.size.width;
  if (rh > vf.size.height) rh = vf.size.height;
  // Clamp position so window stays fully inside visibleFrame.
  if (rx < vf.origin.x) rx = vf.origin.x;
  if (ry < vf.origin.y) ry = vf.origin.y;
  if (rx + rw > vf.origin.x + vf.size.width)  rx = vf.origin.x + vf.size.width  - rw;
  if (ry + rh > vf.origin.y + vf.size.height) ry = vf.origin.y + vf.size.height - rh;

  // Back to Win32 coords.
  rect->left   = (int)rx;
  rect->top    = (int)(primaryHeight - (ry + rh));
  rect->right  = rect->left + (int)rw;
  rect->bottom = rect->top  + (int)rh;
}

#define SWELL_INTERNAL_HDC_ACCESS  // signal intent to include swell internals
#include "swell-internal.h"

void BlitBGRABitmapMacOS(HDC hdc, const void* bgra, int sw, int sh,
                         int dx, int dy, int dw, int dh)
{
  HDC__* h = (HDC__*)hdc;
  if (!h || !h->ctx || !bgra || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;

  CGDataProviderRef provider = CGDataProviderCreateWithData(
      NULL, bgra, (size_t)sw * sh * 4, NULL);
  if (!provider) return;

  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGImageRef img = CGImageCreate(
      sw, sh, 8, 32, sw * 4, cs,
      // BGRA bytes in memory + Little-endian byte order → CGImage reads
      // them as the native pixel layout we want; alpha is "skip first" so
      // the runtime-composed opaque output renders correctly.
      kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little,
      provider, NULL, false, kCGRenderingIntentDefault);
  CGDataProviderRelease(provider);
  CGColorSpaceRelease(cs);
  if (!img) return;

  // SWELL Cocoa HDC contexts have a top-down CTM established at creation
  // (see swell-gdi.mm SWELL_CreateMemContext); CGContextDrawImage uses
  // bottom-up native Cocoa coords. Locally flip and restore so the icon
  // appears at (dx, dy) with the right orientation.
  CGContextSaveGState(h->ctx);
  CGContextTranslateCTM(h->ctx, dx, dy + dh);
  CGContextScaleCTM(h->ctx, 1.0, -1.0);
  CGContextDrawImage(h->ctx, CGRectMake(0, 0, dw, dh), img);
  CGContextRestoreGState(h->ctx);

  CGImageRelease(img);
}
