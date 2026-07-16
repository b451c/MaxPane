#pragma once
#include "config.h"  // for HWND via SWELL
#include <cstdio>    // snprintf/popen (Linux OpenUrlPlatform/IsSystemDarkMode)
#include <cstdlib>   // system (Linux OpenUrlPlatform)
#include <cstring>   // strstr (Linux IsSystemDarkMode)

// Force full Cocoa layout + display pass on an HWND and all its subviews.
// On macOS/SWELL, SetParent does NOT trigger setNeedsLayout: or display.
// Call this after reparenting to ensure child controls lay out correctly.
#ifdef __APPLE__
#include <string>
void ForceViewLayoutAndDisplay(HWND hwnd);
bool IsSystemDarkMode();
// Synchronous HTTPS GET via NSURLSession (updater). Blocks the calling
// thread up to timeoutSec; returns "" on any failure. Responses over 1 MB
// are rejected. (Audit M3.4 — was re-declared ad hoc in updater.cpp.)
std::string FetchUrlSyncMacOS(const char* url, int timeoutSec);
// Force-hide a window at the Cocoa level — orderOut: on the NSWindow plus
// setHidden:YES on the NSView. Use when SWELL's ShowWindow(SW_HIDE) doesn't
// reliably hide top-level windows after SetParent transitions on macOS.
void ForceHideWindow(HWND hwnd);

// F1a (ADR-024) — make a top-level NSWindow look like a standalone app
// window: title bar, close + miniaturize + resize controls, given title.
// Called after SetParent(m_hwnd, nullptr) recreates the NSWindow on detach.
// Safe to call on subviews (no-op when view isn't a contentView).
// U15 (ADR-069) — hideFromTaskbar is a Windows concept (WS_EX_TOOLWINDOW);
// mac/Linux implementations ignore it.
// B2 feature (v2.4.0, mb945 #78) — `owner` ties the float to REAPER's main
// window (Win32 owned-window semantics: minimize-follow, group z-order and
// activation). Windows-only: on macOS the SWELL owner list only drives a
// destroy cascade (dangerous at quit) and Linux transient-for is deferred
// pending VM verification — both ignore the parameter.
void ApplyFloatingWindowChrome(HWND hwnd, const char* title,
                               bool hideFromTaskbar = false,
                               HWND owner = nullptr);

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
// Cocoa: NSFloatingWindowLevel vs NSNormalWindowLevel. Win32: SetWindowPos
// HWND_TOPMOST. Linux: registered no-op — SWELL-generic exposes no portable
// topmost API (see the Linux block below + ARCHITECTURE.md platform gaps).
// Safe to call on subviews (no-op when view isn't a contentView).
void SetWindowAlwaysOnTop(HWND hwnd, bool onTop);

// ADR-081 addendum — Cocoa layer background for a plain NSView container
// that has no SWELL wndproc to subclass (the fx@ dark-filler fallback).
// The layer color renders beneath the view's drawn content and subviews.
// No-op on Win32/Linux (see the platform blocks below).
void SetViewBackgroundColorPlatform(HWND hwnd, int r, int g, int b);

// ADR-081 §2 telemetry — NSView class name (mac) for the [FXBG] dumps:
// identifies GL/Metal-surface plugin views (NSOpenGLView, JUCE classes)
// that go black after reparenting. Win32: GetClassNameA; Linux: stub.
void GetViewClassNamePlatform(HWND hwnd, char* buf, int bufSize);

// ADR-081 §5 — WINDOW-HOSTED capture (macOS): remote-view plug-in UIs
// (out-of-process AUs, Rosetta-bridged VSTs) break when their NSView is
// reparented, but work in their own NSWindow — so MaxPane keeps the whole
// REAPER float window alive, strips its chrome, and glues it over the pane
// as a borderless CHILD NSWindow (follows the container automatically;
// resize goes through REAPER's native float path). No-ops off-mac.
bool AttachWindowAsChildWindow(HWND containerHwnd, HWND targetHwnd);
void SetChildWindowFrame(HWND containerHwnd, HWND targetHwnd,
                         int x, int y, int w, int h);
void ShowChildWindow(HWND targetHwnd, bool show);
void DetachChildWindow(HWND targetHwnd);

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

// U9 (ADR-068) — DPI scale for owner-drawn chrome. Cocoa works in points
// (Retina scaling is transparent), so 1.0 here; the real work is in the
// Win32 block below. Linux HiDPI stays a documented gap.
inline double MaxPaneDpiScaleForDC(HDC) { return 1.0; }

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

inline void ApplyFloatingWindowChrome(HWND hwnd, const char* title,
                                      bool hideFromTaskbar = false,
                                      HWND owner = nullptr)
{
  if (!hwnd) return;
  LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  style &= ~(WS_CHILD | WS_POPUP);
  style |= WS_OVERLAPPEDWINDOW | WS_VISIBLE;
  SetWindowLongPtr(hwnd, GWL_STYLE, style);
  // B2 feature (v2.4.0, mb945 #78) — the owner ties the float to REAPER
  // main: Windows then natively handles minimize-follow + group z-order and
  // activation, like every native REAPER floating window. Keyed on the
  // PARAMETER, never a pref read inside this helper: the DoRelease
  // chromed-orphan path calls it with defaults and must stay byte-identical
  // (release path = documented regression magnet).
  SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, (LONG_PTR)owner);
  // U15 (ADR-069, mb945 #60) — WS_EX_TOOLWINDOW keeps the floating window
  // out of the taskbar (and Alt-Tab — a documented trade-off of the pref).
  LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  if (hideFromTaskbar) exStyle |= WS_EX_TOOLWINDOW;
  else                 exStyle &= ~WS_EX_TOOLWINDOW;
  // B2 — owned windows lose their taskbar button by default; WS_EX_APPWINDOW
  // restores it so tying to main doesn't silently change shell presence.
  // Untouched when owner is null (release paths stay byte-identical).
  if (owner) {
    if (hideFromTaskbar) exStyle &= ~(LONG_PTR)WS_EX_APPWINDOW;
    else                 exStyle |= WS_EX_APPWINDOW;
  }
  SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
  if (title && title[0]) SetWindowTextA(hwnd, title);
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// U9 (ADR-068, X-Raym #67) — DPI scale for owner-drawn chrome. MaxPane
// paints tooltips with pixel fonts/boxes; REAPER runs DPI-aware, so at 125%
// scaling Windows does NOT magnify the plugin's GDI output and a 12px font
// renders ~6px glyphs next to the scaled UI ("tooltips are minuscule").
// GetDpiForWindow is Win10 1607+ and REAPER still supports Win7 — resolve
// dynamically, fall back to the DC's LOGPIXELSX.
inline double MaxPaneDpiScaleForDC(HDC hdc)
{
  typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
  static GetDpiForWindowFn s_getDpiForWindow = (GetDpiForWindowFn)(void*)
      GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
  UINT dpi = 0;
  HWND wnd = hdc ? WindowFromDC(hdc) : nullptr;
  if (s_getDpiForWindow && wnd) dpi = s_getDpiForWindow(wnd);
  if (!dpi && hdc) dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
  if (!dpi) dpi = 96;
  return dpi / 96.0;
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
// ADR-081 addendum — Cocoa-only fallback; no-op here.
inline void SetViewBackgroundColorPlatform(HWND, int, int, int) {}
inline void GetViewClassNamePlatform(HWND hwnd, char* buf, int bufSize)
{
  if (!buf || bufSize <= 0) return;
  buf[0] = 0;
  if (hwnd) GetClassNameA(hwnd, buf, bufSize);
}
// ADR-081 §5 — window-hosted capture is Cocoa-only; no-ops here.
inline bool AttachWindowAsChildWindow(HWND, HWND) { return false; }
inline void SetChildWindowFrame(HWND, HWND, int, int, int, int) {}
inline void ShowChildWindow(HWND, bool) {}
inline void DetachChildWindow(HWND) {}
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
// ADR-060 — capture hover outline via the 4-strip overlay frame
// (overlay_frame.cpp; shared with the Linux branch below).
void ShowCaptureHighlight(HWND target);
void HideCaptureHighlight();
bool IsCaptureHighlightWindow(HWND hwnd);
#else
inline void ForceViewLayoutAndDisplay(HWND) {}
// Audit M2.5 — was `return false`, so dark-mode "Auto" always rendered
// light on Linux (where REAPER users overwhelmingly run dark themes).
// GNOME/GTK-desktop probe via gsettings, cached for the session (manual
// override in Settings still wins — see config.cpp). KDE and exotic
// desktops fall back to light: a registered gap, not a silent one.
inline bool IsSystemDarkMode()
{
  static int cached = -1;
  if (cached < 0) {
    cached = 0;
    FILE* p = popen(
        "gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null", "r");
    if (p) {
      char buf[64] = {};
      if (fgets(buf, sizeof(buf), p) && strstr(buf, "dark")) cached = 1;
      pclose(p);
    }
  }
  return cached == 1;
}
inline void ForceHideWindow(HWND) {}
// Audit M2.5 — was an empty stub. DoCapture strips WS_CAPTION|WS_THICKFRAME|
// WS_SYSMENU at capture (window_manager.cpp), and this is the only restore
// path — so on Linux a released/detached window came back as a borderless,
// unmovable, uncloseable GTK window. SWELL-generic decorates from the style
// bits (swell_oswindow_manage), so restoring them + SWP_FRAMECHANGED is the
// whole job. Runtime verification on a Linux VM pending.
// U15 (ADR-069) — hideFromTaskbar is a Windows concept; ignored here.
// B2 (v2.4.0) — owner (tie-to-main): Linux gets the SAME semantics as
// Win32 via SWELL-generic's owner slot (GWL_HWNDPARENT → GDK transient-for
// at os-window level; GDK defaults keep owned windows above their owner,
// mirroring the Win32 owned-group invariant). Native REAPER floats are
// transient-for main on Linux, so this restores parity with them — the
// cross-platform consistency rule (owner directive 2026-07-09). VERIFIED
// on the Linux VM 2026-07-09 (xprop WM_TRANSIENT_FOR — see the ADR-072..079
// Linux addendum). Known side effect: owned windows default OUT of the
// tasklist (gdk_owned_windows_in_tasklist=0).
inline void ApplyFloatingWindowChrome(HWND hwnd, const char* title,
                                      bool /*hideFromTaskbar*/ = false,
                                      HWND owner = nullptr)
{
  if (!hwnd) return;
  // LONG_PTR end-to-end — SWELL's GetWindowLong returns LONG_PTR, and the
  // narrowing LONG copy tripped -Wconversion (CI runs -Werror; ADR-060
  // session catch — the gate had never seen this header on Linux).
  LONG_PTR style = GetWindowLong(hwnd, GWL_STYLE);
  style &= ~(LONG_PTR)WS_CHILD;
  style |= WS_CAPTION | WS_THICKFRAME | WS_SYSMENU;
  SetWindowLong(hwnd, GWL_STYLE, style);
  SetWindowLong(hwnd, GWL_HWNDPARENT, (LONG_PTR)owner);  // B2 — see above
  if (title && title[0]) SetWindowText(hwnd, title);
  SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
               SWP_FRAMECHANGED);
}
// Audit M2.5 — was an empty stub: geometry saved on a since-disconnected
// monitor restored off-screen with no rescue. SWELL_GetViewPort is exported
// by REAPER's SWELL on Linux (same API the Win32 branch mirrors).
inline void ClampRectToVisibleScreen(RECT* rect)
{
  if (!rect) return;
  RECT vp;
  SWELL_GetViewPort(&vp, rect, true);
  int w = rect->right - rect->left;
  int h = rect->bottom - rect->top;
  int vpW = vp.right - vp.left, vpH = vp.bottom - vp.top;
  if (w > vpW) w = vpW;
  if (h > vpH) h = vpH;
  if (rect->left < vp.left) rect->left = vp.left;
  if (rect->top  < vp.top)  rect->top  = vp.top;
  if (rect->left + w > vp.right)  rect->left = vp.right - w;
  if (rect->top  + h > vp.bottom) rect->top  = vp.bottom - h;
  rect->right  = rect->left + w;
  rect->bottom = rect->top + h;
}
// Audit M1.7 — this was an empty stub while the doc comment above claims
// "xdg-open on Linux": the updater's [Open Releases] button and every
// Settings/nav-bar support link silently did nothing. All call sites pass
// string literals (no user input reaches the shell); the trailing '&'
// keeps the UI from blocking on the launcher.
inline void OpenUrlPlatform(const char* url)
{
  if (!url || !*url) return;
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", url);
  int rc = system(cmd);
  (void)rc;
}
// REGISTERED GAP (audit M2.5): SWELL-generic exposes no portable topmost
// API — the floating-mode "always on top" checkbox is a no-op on Linux.
// Revisit if SWELL grows SetWindowLevel-equivalent support.
inline void SetWindowAlwaysOnTop(HWND, bool) {}
// ADR-081 addendum — Cocoa-only fallback; no-op here.
inline void SetViewBackgroundColorPlatform(HWND, int, int, int) {}
inline void GetViewClassNamePlatform(HWND hwnd, char* buf, int bufSize)
{
  if (!buf || bufSize <= 0) return;
  buf[0] = 0;
  if (hwnd) GetClassName(hwnd, buf, bufSize);
}
// ADR-081 §5 — window-hosted capture is Cocoa-only; no-ops here.
inline bool AttachWindowAsChildWindow(HWND, HWND) { return false; }
inline void SetChildWindowFrame(HWND, HWND, int, int, int, int) {}
inline void ShowChildWindow(HWND, bool) {}
inline void DetachChildWindow(HWND) {}
inline void EnableContainerMouseTracking(HWND) {}
// U9 (ADR-068) — Linux HiDPI chrome scaling is a pre-existing documented gap
// (V2_PROGRESS "nav-bar advisory scaling"); tooltips keep 1.0 until that
// lands as a whole.
inline double MaxPaneDpiScaleForDC(HDC) { return 1.0; }
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
// ADR-060 — capture hover outline via the 4-strip overlay frame
// (overlay_frame.cpp; shared with the Win32 branch above).
void ShowCaptureHighlight(HWND target);
void HideCaptureHighlight();
bool IsCaptureHighlightWindow(HWND hwnd);
#endif
