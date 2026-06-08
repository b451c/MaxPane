#pragma once
#include "config.h"  // for HWND via SWELL

// Force full Cocoa layout + display pass on an HWND and all its subviews.
// On macOS/SWELL, SetParent does NOT trigger setNeedsLayout: or display.
// Call this after reparenting to ensure child controls lay out correctly.
#ifdef __APPLE__
void ForceViewLayoutAndDisplay(HWND hwnd);
bool IsSystemDarkMode();
// Force-hide a window at the Cocoa level — orderOut: on the NSWindow plus
// setHidden:YES on the NSView. Use when SWELL's ShowWindow(SW_HIDE) doesn't
// reliably hide top-level windows after SetParent transitions on macOS.
void ForceHideWindow(HWND hwnd);

// F1a (ADR-024) — make a top-level NSWindow look like a standalone app
// window: title bar, close + miniaturize + resize controls, given title.
// Called after SetParent(m_hwnd, nullptr) recreates the NSWindow on detach.
// Safe to call on subviews (no-op when view isn't a contentView).
void ApplyFloatingWindowChrome(HWND hwnd, const char* title);

// F1a (ADR-024) — clamp a window rect to the visible frame of the screen
// it mostly occupies (NSScreen with the largest intersection with rect).
// Falls back to primary screen visible frame if rect is fully off-screen
// (e.g. saved monitor disconnected). Mutates rect in place; clamps both
// position (so origin is on-screen) and size (so width/height fit screen).
void ClampRectToVisibleScreen(RECT* rect);

// ADR-026 — open a URL in the user's default browser. Cocoa NSWorkspace
// on macOS; Win32 ShellExecute on Windows; xdg-open on Linux. Used by the
// nav bar's Support button.
void OpenUrlPlatform(const char* url);

// C5 (ADR-027) — keep a top-level floating NSWindow above other apps.
// Cocoa: NSFloatingWindowLevel vs NSNormalWindowLevel. Win32 will land in
// Sprint 1 via SetWindowPos HWND_TOPMOST; Linux via gtk_window_set_keep_above.
// Safe to call on subviews (no-op when view isn't a contentView).
void SetWindowAlwaysOnTop(HWND hwnd, bool onTop);

// v2.0.2 cross-platform icon unify — blit a BGRA pre-composed bitmap into a
// SWELL HDC at the given destination rect, scaling if needed. macOS Cocoa
// SWELL lacks the Linux StretchBltFromMem + SelectObject(HBITMAP) niceties;
// this helper goes Cocoa-native via the CGContextRef inside HDC__.
// `bgra` must point to `sw * sh * 4` bytes laid out per PackBGRA in
// nav_icons.cpp (B, G, R, A in ascending memory order). Alpha is treated as
// opaque-padding (kCGImageAlphaNoneSkipFirst) — the runtime already composed
// the icon against its backdrop.
void BlitBGRABitmapMacOS(HDC hdc, const void* bgra, int sw, int sh,
                        int dx, int dy, int dw, int dh);

// F-H2 (forum v2.0.6) — add an always-active NSTrackingArea to the container
// view so mouseMoved: (tab-bar hover) keeps firing even when a captured FX/AU
// plugin tab becomes the key window. Cocoa only delivers mouseMoved to the key
// window otherwise; macOS-specific (Win32/GTK deliver move events regardless).
void EnableContainerMouseTracking(HWND hwnd);

// ADR-048 (forum v2.0.6) — capture-by-click safety. Reparenting a window
// while an app-modal dialog is up (e.g. REAPER's "Save changes?" on quit)
// freezes REAPER's modal run loop: the captured dialog goes dead and REAPER
// is bricked (a user had to force-quit). [NSApp modalWindow] is non-nil for
// the whole modal session (SWELL itself uses this as its authoritative modal
// check), so the capture poll refuses to grab anything while it is set.
bool IsAppModalActive();
// Per-window backstop to IsAppModalActive(): reject the specific resolved
// target if it is the modal window itself, a sheet, or a sheet's parent.
// Returns true (safe) for ordinary REAPER/plugin/floating windows.
bool IsWindowSafeToCapture(HWND hwnd);

// ADR-048 — true when `candidate` lives inside REAPER's MAIN NSWindow (core
// arrange/ruler/TCP, or a window docked in the main docker) vs a separate
// window. The capture allow-list uses this (GetParent is unreliable on macOS
// SWELL — floating windows report g_reaperMainHwnd as their owner).
bool IsEmbeddedInMainWindow(HWND candidate);

// ADR-048 — capture-by-click cursor feedback. While armed to pick a window,
// show a crosshair so it's obvious capture mode is live. A naive [NSCursor push]
// reverts because AppKit's per-window cursor rects re-assert on every
// mouse-moved (the reason the earlier crosshair attempt was abandoned);
// disabling cursor rects across the app's windows for the brief armed window
// stops the re-assertion. Reversed on disarm. Idempotent — safe to call
// repeatedly. macOS only (Win32/GTK manage the cursor per WM_SETCURSOR).
void SetCaptureCursorActive(bool on);
// Re-assert the capture crosshair from the poll (covers windows that don't
// emit mouseMoved + the still-mouse case). No-op unless armed.
void RefreshCaptureCursor();

// ADR-048 — capture-by-click cancel key. SWELL's GetAsyncKeyState(VK_ESCAPE)
// always returns 0 on macOS (swell-kb.mm maps only mouse + modifier keys), so
// the capture poll can't see Escape that way. Query the Escape key (virtual
// keycode 53) directly via CoreGraphics, focus-independent.
bool IsCaptureCancelKeyDown();

// ADR-048 — capture hover-highlight overlay. ShowCaptureHighlight outlines the
// window under the crosshair (pass `nullptr` to hide); HideCaptureHighlight is
// called before the committing WindowFromPoint so the click hits the real
// target; IsCaptureHighlightWindow lets the poll skip ticks where the cursor
// sits on the overlay (it's click-through, so it must be excluded from resolve).
void ShowCaptureHighlight(HWND target);
void HideCaptureHighlight();
bool IsCaptureHighlightWindow(HWND hwnd);
#elif defined(_WIN32)
// Sprint 1 Entry 6 — native Win32 implementations. The helpers were
// previously {} stubs on every non-Apple platform; F1a Detach-to-Floating
// and Settings dark-mode override therefore no-op'd on Windows.
#include <shellapi.h>

inline void ForceViewLayoutAndDisplay(HWND hwnd)
{
  if (!hwnd) return;
  InvalidateRect(hwnd, nullptr, TRUE);
  UpdateWindow(hwnd);
}

inline bool IsSystemDarkMode()
{
  // HKCU\...\Themes\Personalize\AppsUseLightTheme: 0 = dark, 1 = light.
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &key) != ERROR_SUCCESS) return false;
  DWORD value = 1, size = sizeof(value), type = REG_DWORD;
  LONG s = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr,
                            &type, (BYTE*)&value, &size);
  RegCloseKey(key);
  return (s == ERROR_SUCCESS && value == 0);
}

inline void ForceHideWindow(HWND hwnd)
{
  if (hwnd) ShowWindow(hwnd, SW_HIDE);
}

inline void ApplyFloatingWindowChrome(HWND hwnd, const char* title)
{
  if (!hwnd) return;
  LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  style &= ~(WS_CHILD | WS_POPUP);
  style |= WS_OVERLAPPEDWINDOW | WS_VISIBLE;
  SetWindowLongPtr(hwnd, GWL_STYLE, style);
  if (title && title[0]) SetWindowTextA(hwnd, title);
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

inline void ClampRectToVisibleScreen(RECT* rect)
{
  if (!rect) return;
  HMONITOR mon = MonitorFromRect(rect, MONITOR_DEFAULTTONEAREST);
  if (!mon) return;
  MONITORINFO mi = { sizeof(mi) };
  if (!GetMonitorInfoW(mon, &mi)) return;
  const RECT wa = mi.rcWork;
  int w = rect->right - rect->left, h = rect->bottom - rect->top;
  const int waW = wa.right - wa.left, waH = wa.bottom - wa.top;
  if (w > waW) w = waW;
  if (h > waH) h = waH;
  if (rect->left < wa.left) rect->left = wa.left;
  if (rect->top  < wa.top)  rect->top  = wa.top;
  if (rect->left + w > wa.right)  rect->left = wa.right  - w;
  if (rect->top  + h > wa.bottom) rect->top  = wa.bottom - h;
  rect->right  = rect->left + w;
  rect->bottom = rect->top  + h;
}

inline void OpenUrlPlatform(const char* url)
{
  if (url && url[0]) {
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
  }
}

inline void SetWindowAlwaysOnTop(HWND hwnd, bool onTop)
{
  if (!hwnd) return;
  SetWindowPos(hwnd, onTop ? HWND_TOPMOST : HWND_NOTOPMOST,
               0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
// Win32 delivers WM_MOUSEMOVE to the window under the cursor regardless of
// focus, so the macOS key-window hover gap doesn't exist here.
inline void EnableContainerMouseTracking(HWND) {}

// ADR-048 — capture-by-click safety (see __APPLE__ block). A native Win32
// modal disables its owner; REAPER's main window is disabled for the whole
// modal session, so GW_ENABLEDPOPUP / IsWindowEnabled is the modal marker.
inline bool IsAppModalActive()
{
  extern HWND g_reaperMainHwnd;
  return g_reaperMainHwnd && !IsWindowEnabled(g_reaperMainHwnd);
}
inline bool IsWindowSafeToCapture(HWND hwnd)
{
  if (!hwnd) return false;
  if (IsAppModalActive()) return false;
  // A modal dialog disables its owner; refuse a window whose owner is disabled.
  HWND owner = GetWindow(hwnd, GW_OWNER);
  if (owner && !IsWindowEnabled(owner)) return false;
  return true;
}
// Win32 cursor is managed per WM_SETCURSOR; no app-wide override yet.
inline void SetCaptureCursorActive(bool) {}
inline void RefreshCaptureCursor() {}
// Win32 GetAsyncKeyState(VK_ESCAPE) works natively.
inline bool IsCaptureCancelKeyDown() { return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0; }
// Hover-highlight overlay not yet implemented on Win32 (would need a layered window).
inline void ShowCaptureHighlight(HWND) {}
inline void HideCaptureHighlight() {}
inline bool IsCaptureHighlightWindow(HWND) { return false; }
#else
inline void ForceViewLayoutAndDisplay(HWND) {}
inline bool IsSystemDarkMode() { return false; }
inline void ForceHideWindow(HWND) {}
inline void ApplyFloatingWindowChrome(HWND, const char*) {}
inline void ClampRectToVisibleScreen(RECT*) {}
inline void OpenUrlPlatform(const char*) {}
inline void SetWindowAlwaysOnTop(HWND, bool) {}
inline void EnableContainerMouseTracking(HWND) {}
// ADR-048 — capture-by-click safety. Generic SWELL_DialogBox disables all
// other top-level windows during a modal (swell-dlg-generic.cpp), so the
// disabled REAPER main window is a reliable modal marker on Linux/GTK.
inline bool IsAppModalActive()
{
  extern HWND g_reaperMainHwnd;
  return g_reaperMainHwnd && !IsWindowEnabled(g_reaperMainHwnd);
}
inline bool IsWindowSafeToCapture(HWND hwnd)
{
  if (!hwnd) return false;
  return !IsAppModalActive();
}
inline void SetCaptureCursorActive(bool) {}
inline void RefreshCaptureCursor() {}
inline bool IsCaptureCancelKeyDown() { return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0; }
inline void ShowCaptureHighlight(HWND) {}
inline void HideCaptureHighlight() {}
inline bool IsCaptureHighlightWindow(HWND) { return false; }
#endif
