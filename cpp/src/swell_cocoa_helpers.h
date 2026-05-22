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
#else
inline void ForceViewLayoutAndDisplay(HWND) {}
inline bool IsSystemDarkMode() { return false; }
inline void ForceHideWindow(HWND) {}
inline void ApplyFloatingWindowChrome(HWND, const char*) {}
inline void ClampRectToVisibleScreen(RECT*) {}
inline void OpenUrlPlatform(const char*) {}
inline void SetWindowAlwaysOnTop(HWND, bool) {}
#endif
