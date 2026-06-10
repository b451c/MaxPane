// overlay_frame.h — 4-thin-strip border overlay for Win32 + Linux (ADR-060).
//
// Visual feedback that the container's own DC cannot provide: the capture
// hover outline targets windows OUTSIDE the container, and the drag-to-dock
// zone preview over an OCCUPIED pane is covered by the captured child
// window (drag_dock.cpp PaintPreview paints under it). macOS solves both
// with a transparent click-through NSWindow (swell_cocoa_helpers.mm);
// SWELL-generic has no per-pixel alpha or click-through windows, so the
// platform-neutral shape is four thin solid strips along the rect edges —
// nothing covers the interior, so nothing needs to be transparent.
//
// The strips must stay out of every hit-test / enum path:
//  - the capture poll skips ticks where the cursor sits on the frame
//    (IsCaptureHighlightWindow → IsFrameWindow),
//  - the committing click hides the frame BEFORE WindowFromPoint,
//  - TitleBandEnumProc skips frame windows explicitly (the SWELL-generic
//    whole-rect+48px band test would otherwise catch the bottom strip for
//    cursor points in the target's lower 48px),
//  - FindWindowEnumProc's 50×50 min-size guard rejects the 3px strips.
#pragma once
#include "platform.h"

namespace OverlayFrame {

#if defined(__APPLE__)
// macOS keeps the Cocoa overlay (swell_cocoa_helpers.mm) for the capture
// outline and the painted preview for drag zones. Inline no-ops keep the
// container call sites #ifdef-free.
inline void ShowRect(const RECT&, COLORREF) {}
inline void Hide() {}
inline bool IsFrameWindow(HWND) { return false; }
inline void Destroy() {}
#else
// Position the 4 strips just inside screenRect's edges and show them
// without activating. Safe to call per poll tick: repositions/repaints
// only when the rect or color changed.
void ShowRect(const RECT& screenRect, COLORREF color);
void Hide();
bool IsFrameWindow(HWND hwnd);
// Destroy the strip windows — called at plugin unload so no window with a
// WndProc inside this module outlives it.
void Destroy();
#endif

} // namespace OverlayFrame
