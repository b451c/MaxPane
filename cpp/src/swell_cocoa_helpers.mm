// macOS only — compiled via CMakeLists.txt (APPLE target_sources)
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>   // CALayer (capture hover-highlight overlay)
#include "swell_cocoa_helpers.h"
#include "debug.h"

bool IsSystemDarkMode()
{
  // REAPER ships with NSRequiresAquaSystemAppearance=YES, which forces the
  // application's [NSApp effectiveAppearance] to Aqua (light) regardless of the
  // macOS system setting. Reading effectiveAppearance therefore always reported
  // "light" and broke "Auto (follow system)" under a dark system. Read the
  // OS-wide appearance directly instead — the same value that
  // `defaults read -g AppleInterfaceStyle` returns ("Dark" when dark, nil when
  // light). Available since 10.10; nil on older systems → light, which is
  // correct (no dark mode pre-10.10).
  NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
  return style != nil && [style caseInsensitiveCompare:@"Dark"] == NSOrderedSame;
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

void ApplyFloatingWindowChrome(HWND hwnd, const char* title, bool /*hideFromTaskbar*/)
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

void EnableContainerMouseTracking(HWND hwnd)
{
  if (!hwnd) return;
  NSView* view = (NSView*)hwnd;
  if (![view isKindOfClass:[NSView class]]) return;

  // Don't add a second always-active moved-tracking area if one is already
  // present (Create can run multiple times across a container's lifetime).
  for (NSTrackingArea* ta in [view trackingAreas]) {
    NSTrackingAreaOptions o = [ta options];
    if ((o & NSTrackingMouseMoved) && (o & NSTrackingActiveAlways)) return;
  }

  // F-H2 (forum v2.0.6) — Cocoa delivers mouseMoved: only to the key window.
  // When a captured FX/AU plugin tab becomes key, the MaxPane container's
  // NSView stops receiving mouseMoved → the tab-bar hover highlight freezes
  // (REAPER-native captures like Routing Matrix don't steal key the same way,
  // so hover worked there). An always-active tracking area delivers mouseMoved
  // to this view regardless of key-window status; SWELL's own mouseMoved: then
  // posts WM_MOUSEMOVE → OnMouseMove keeps hover live. NSTrackingInVisibleRect
  // auto-tracks the view's bounds, so no manual update on resize is needed.
  NSTrackingAreaOptions opts = NSTrackingMouseMoved
                             | NSTrackingActiveAlways
                             | NSTrackingInVisibleRect;
  NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                      options:opts
                                                        owner:view
                                                     userInfo:nil];
  [view addTrackingArea:area];
  [area release];  // view retains it; balance our alloc (project is MRC).
}

void OpenUrlPlatform(const char* url)
{
  if (!url || !url[0]) return;
  NSString* s = [NSString stringWithUTF8String:url];
  NSURL* u = [NSURL URLWithString:s];
  if (!u) return;
  [[NSWorkspace sharedWorkspace] openURL:u];
}

bool IsAppModalActive()
{
  // True while an app-modal dialog runs. REAPER's "Save changes?" box (and
  // SWELL file/alert panels) run via NSRunAlertPanel / [NSApp runModalForWindow:]
  // (swell-miscdlg.mm, swell-dlg.mm), both of which set [NSApp modalWindow];
  // SWELL itself trusts this exact signal as its modal check (swell-dlg.mm
  // EndDialog). Capturing a window during the session reparents the modal's
  // NSWindow and freezes the modal run loop → REAPER bricked (forum v2.0.6:
  // owner force-quit). The capture poll refuses to grab anything while set.
  return [NSApp modalWindow] != nil;
}

bool IsWindowSafeToCapture(HWND hwnd)
{
  if (!hwnd) return false;
  // Belt-and-suspenders backstop to IsAppModalActive(): reject the specific
  // resolved target if it is the modal window, a sheet, or a sheet's parent —
  // none are legitimate reparent targets. Ordinary REAPER/plugin/floating
  // windows (NSWindow, not the modal panel) pass.
  NSView* view = (NSView*)hwnd;
  if (![view isKindOfClass:[NSView class]]) return true;  // unknown — defer to other guards
  NSWindow* win = [view window];
  if (!win) return true;
  if ([NSApp modalWindow] == win) return false;
  if ([win isSheet] || [win sheetParent] != nil) return false;
  return true;
}

static bool s_captureCursorActive = false;
static id   s_captureMoveMonitor = nil;

bool IsEmbeddedInMainWindow(HWND candidate)
{
  // True when `candidate` lives inside REAPER's MAIN NSWindow (arrange / ruler /
  // TCP / a window docked in the main docker) vs a SEPARATE window (floating
  // FX/ReaImGui/dockers). NOTE: GetParent is NOT a usable discriminator here —
  // on macOS SWELL a floating window's GetParent returns g_reaperMainHwnd as
  // its OWNER (MEMORY: "SetParent(nullptr) → parent = REAPER main"), so every
  // window looks like a child of main. NSWindow identity is the real signal.
  extern HWND g_reaperMainHwnd;
  if (!candidate || !g_reaperMainHwnd) return false;
  NSView* cand = (NSView*)candidate;
  NSView* main = (NSView*)g_reaperMainHwnd;
  if (![cand isKindOfClass:[NSView class]] ||
      ![main isKindOfClass:[NSView class]]) return false;
  NSWindow* mainWin = [main window];
  return mainWin != nil && [cand window] == mainWin;
}

void SetCaptureCursorActive(bool on)
{
  if (on == s_captureCursorActive) return;  // idempotent — balance the pair
  s_captureCursorActive = on;
  if (on) {
    // Stop AppKit re-asserting per-window cursor rects on every mouse-moved
    // (the reason a plain [NSCursor push] flickered back to the arrow), then
    // set the crosshair. REAPER + MaxPane share one process, so iterating
    // [NSApp windows] also covers REAPER's native windows.
    for (NSWindow* w in [NSApp windows]) [w disableCursorRects];
    [[NSCursor crosshairCursor] set];
    // Re-assert the crosshair after every mouse-moved across the app. A window
    // asserts its OWN cursor while handling the move, so setting synchronously
    // here loses the race (the window's set runs after ours). dispatch_async
    // defers our set to the next runloop hop, AFTER the window finishes the
    // event — so the crosshair wins (Apple DTS pattern, forum/thread/708211).
    // Return the event (never nil) so SWELL hover/tracking keeps running.
    s_captureMoveMonitor = [[NSEvent addLocalMonitorForEventsMatchingMask:
        (NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged)
        handler:^NSEvent*(NSEvent* ev) {
          dispatch_async(dispatch_get_main_queue(), ^{
            if (s_captureCursorActive) [[NSCursor crosshairCursor] set];
          });
          return ev;
        }] retain];
  } else {
    if (s_captureMoveMonitor) {
      [NSEvent removeMonitor:s_captureMoveMonitor];
      [s_captureMoveMonitor release];
      s_captureMoveMonitor = nil;
    }
    for (NSWindow* w in [NSApp windows]) [w enableCursorRects];
    [[NSCursor arrowCursor] set];
  }
}

void RefreshCaptureCursor()
{
  // Belt-and-suspenders to the move-monitor: re-assert the crosshair from the
  // capture poll (~20 Hz). Covers windows that don't generate mouseMoved (so
  // the monitor never fires over them) and the still-mouse case. No-op unless
  // armed. dispatch_async so it lands after any in-flight event handling.
  if (!s_captureCursorActive) return;
  dispatch_async(dispatch_get_main_queue(), ^{
    if (s_captureCursorActive) [[NSCursor crosshairCursor] set];
  });
}

// ADR-048 — capture hover-highlight overlay. A single borderless, transparent,
// CLICK-THROUGH window (ignoresMouseEvents:YES) drawn over the window under the
// crosshair so the user sees exactly what a click will grab. It must never
// participate in hit-testing: the capture poll hides it before WindowFromPoint
// on the committing click, and skips ticks where the cursor sits on it (see
// IsCaptureHighlightWindow), so the proven poll+WindowFromPoint path is intact.
static NSWindow* s_highlightWin = nil;

static void EnsureHighlightWindow()
{
  if (s_highlightWin) return;
  NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 16, 16)
                                            styleMask:NSWindowStyleMaskBorderless
                                              backing:NSBackingStoreBuffered
                                                defer:YES];
  [w setOpaque:NO];
  [w setBackgroundColor:[NSColor clearColor]];
  [w setIgnoresMouseEvents:YES];     // click-through — never steals the capture click
  [w setHasShadow:NO];
  [w setLevel:NSPopUpMenuWindowLevel];  // above REAPER's windows
  [w setCollectionBehavior:(NSWindowCollectionBehaviorCanJoinAllSpaces |
                            NSWindowCollectionBehaviorStationary |
                            NSWindowCollectionBehaviorIgnoresCycle |
                            NSWindowCollectionBehaviorFullScreenAuxiliary)];
  NSView* cv = [w contentView];
  [cv setWantsLayer:YES];
  CALayer* layer = [cv layer];
  [layer setBorderWidth:3.0];
  [layer setBorderColor:[[NSColor systemBlueColor] CGColor]];
  [layer setBackgroundColor:[[[NSColor systemBlueColor]
                               colorWithAlphaComponent:0.12] CGColor]];
  [layer setCornerRadius:2.0];
  s_highlightWin = w;  // retained by alloc; singleton kept for app lifetime
}

void ShowCaptureHighlight(HWND target)
{
  if (!target) { if (s_highlightWin) [s_highlightWin orderOut:nil]; return; }
  NSView* v = (NSView*)target;
  if (![v isKindOfClass:[NSView class]]) {
    if (s_highlightWin) [s_highlightWin orderOut:nil];
    return;
  }
  NSWindow* tw = [v window];
  if (!tw) { if (s_highlightWin) [s_highlightWin orderOut:nil]; return; }
  // View bounds → window coords → screen coords. Works for a floating window's
  // contentView and for a docked subview (outlines just the docked region).
  NSRect inWin = [v convertRect:[v bounds] toView:nil];
  NSRect screenRect = [tw convertRectToScreen:inWin];
  EnsureHighlightWindow();
  [s_highlightWin setFrame:screenRect display:YES];
  [s_highlightWin orderFrontRegardless];  // never becomes key (borderless)
}

void HideCaptureHighlight()
{
  if (s_highlightWin) [s_highlightWin orderOut:nil];
}

bool IsCaptureHighlightWindow(HWND hwnd)
{
  if (!hwnd || !s_highlightWin) return false;
  NSView* v = (NSView*)hwnd;
  if (![v isKindOfClass:[NSView class]]) return false;
  return [v window] == s_highlightWin;
}

bool IsCaptureCancelKeyDown()
{
  // Escape = virtual keycode 53 (kVK_Escape). CoreGraphics combined session
  // state is focus-independent — works while the user hovers any REAPER window.
  // SWELL's GetAsyncKeyState(VK_ESCAPE) always returns 0 here (swell-kb.mm).
  return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                               (CGKeyCode)53);
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
  // appears at (dx, dy) with the right orientation. Bumping interpolation
  // to High gives parity with the Win32 HALFTONE path — same "smooth"
  // downscale on all three platforms.
  CGContextSaveGState(h->ctx);
  CGContextSetInterpolationQuality(h->ctx, kCGInterpolationHigh);
  CGContextTranslateCTM(h->ctx, dx, dy + dh);
  CGContextScaleCTM(h->ctx, 1.0, -1.0);
  CGContextDrawImage(h->ctx, CGRectMake(0, 0, dw, dh), img);
  CGContextRestoreGState(h->ctx);

  CGImageRelease(img);
}

#include <string>

// v2.0.3 update check — synchronous HTTPS GET via NSURLSession.
// Spins a local run loop on the calling thread (which is the main UI
// thread when invoked from Settings → "Check for updates"), and waits
// until the completionHandler fills in `out` or `timeoutSec` elapses.
// Suitable for user-explicit checks that briefly block the UI.
//
// Note: NSURLSession completionHandler runs on a delegate queue — we
// signal a semaphore so the calling thread can return synchronously.
std::string FetchUrlSyncMacOS(const char* url, int timeoutSec)
{
  if (!url || !*url) return std::string();

  __block std::string result;
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);

  NSString* nsurl = [NSString stringWithUTF8String:url];
  NSURL* parsed = [NSURL URLWithString:nsurl];
  if (!parsed) { dispatch_release(sem); return std::string(); }

  NSURLSessionConfiguration* cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
  cfg.timeoutIntervalForRequest = (NSTimeInterval)timeoutSec;
  cfg.timeoutIntervalForResource = (NSTimeInterval)timeoutSec;

  NSURLSession* session = [NSURLSession sessionWithConfiguration:cfg];
  NSURLSessionDataTask* task = [session dataTaskWithURL:parsed
                                  completionHandler:^(NSData* data,
                                                       NSURLResponse* /*resp*/,
                                                       NSError* err) {
    // 1 MB cap (audit M1.5) — the manifest is a few KB; an oversized
    // response (CDN compromise / captive portal) must not balloon memory.
    if (!err && data && data.length > 0 && data.length <= 1024 * 1024) {
      result.assign((const char*)data.bytes, data.length);
    }
    dispatch_semaphore_signal(sem);
  }];
  [task resume];

  dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW,
      (int64_t)timeoutSec * NSEC_PER_SEC + (int64_t)NSEC_PER_SEC);
  dispatch_semaphore_wait(sem, deadline);
  dispatch_release(sem);
  // Sessions leak until invalidated (Apple docs); tasks in flight (timeout
  // path) finish or cancel on their own, then the session is released.
  [session finishTasksAndInvalidate];
  return result;
}
