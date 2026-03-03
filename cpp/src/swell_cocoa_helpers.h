#pragma once
#include "config.h"  // for HWND via SWELL

// Force full Cocoa layout + display pass on an HWND and all its subviews.
// On macOS/SWELL, SetParent does NOT trigger setNeedsLayout: or display.
// Call this after reparenting to ensure child controls lay out correctly.
#ifdef __APPLE__
void ForceViewLayoutAndDisplay(HWND hwnd);
#else
inline void ForceViewLayoutAndDisplay(HWND) {}
#endif
