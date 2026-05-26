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
#else
inline void ForceViewLayoutAndDisplay(HWND) {}
inline bool IsSystemDarkMode() { return false; }
inline void ForceHideWindow(HWND) {}
inline void ApplyFloatingWindowChrome(HWND, const char*) {}
inline void ClampRectToVisibleScreen(RECT*) {}
inline void OpenUrlPlatform(const char*) {}
inline void SetWindowAlwaysOnTop(HWND, bool) {}
#endif
