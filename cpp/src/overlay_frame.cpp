// overlay_frame.cpp — 4-strip border overlay, Win32 + SWELL-generic (ADR-060).
//
// Also hosts the Win32/Linux implementations of the capture hover-outline
// helpers declared in swell_cocoa_helpers.h (ShowCaptureHighlight & co.) —
// they are thin wrappers over the frame. macOS keeps its Cocoa overlay; on
// __APPLE__ this TU compiles to nothing.
#include "overlay_frame.h"

#if !defined(__APPLE__)

#include "globals.h"
#include "swell_cocoa_helpers.h"
#include "debug.h"
#include <cstring>

namespace {

HWND s_strip[4] = {};          // top, bottom, left, right
RECT s_lastRect = {};
COLORREF s_color = 0;
bool s_visible = false;

// Strip thickness — matches the 3px border of the macOS Cocoa overlay.
constexpr int kThick = 3;
// Capture hover accent — matches macOS systemBlue.
constexpr COLORREF kCaptureAccent = RGB(0, 122, 255);

#ifdef _WIN32

LRESULT CALLBACK StripWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      HBRUSH b = CreateSolidBrush(s_color);
      FillRect(dc, &ps.rcPaint, b);
      DeleteObject(b);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_MOUSEACTIVATE:
      // Pure chrome — never takes the mouse or focus (belt-and-suspenders
      // to WS_EX_NOACTIVATE | WS_EX_TRANSPARENT).
      return MA_NOACTIVATE;
  }
  return DefWindowProc(h, msg, wp, lp);
}

bool EnsureCreated()
{
  if (s_strip[0]) return true;
  static bool s_classRegistered = false;
  if (!s_classRegistered) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = StripWndProc;
    wc.hInstance = g_hInstance;
    wc.lpszClassName = "MaxPaneOverlayStrip";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassA(&wc)) return false;
    s_classRegistered = true;
  }
  for (int i = 0; i < 4; i++) {
    // WS_EX_TRANSPARENT: WindowFromPoint resolves through the strip to the
    // window underneath, so the frame can never become the capture target.
    // WS_EX_NOACTIVATE + WS_EX_TOOLWINDOW: no focus steal, no taskbar entry.
    s_strip[i] = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        "MaxPaneOverlayStrip", "", WS_POPUP,
        0, 0, kThick, kThick, nullptr, nullptr, g_hInstance, nullptr);
    if (!s_strip[i]) {
      OverlayFrame::Destroy();
      return false;
    }
  }
  return true;
}

void PlaceStrip(HWND h, const RECT& r, bool colorChanged)
{
  SetWindowPos(h, HWND_TOPMOST, r.left, r.top,
               r.right - r.left, r.bottom - r.top,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  if (colorChanged) InvalidateRect(h, nullptr, TRUE);
}

#else  // SWELL-generic (Linux)

INT_PTR CALLBACK StripDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  (void)wp; (void)lp;
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps;
    if (BeginPaint(h, &ps)) {
      RECT r;
      GetClientRect(h, &r);
      HBRUSH b = CreateSolidBrush(s_color);
      FillRect(ps.hdc, &r, b);
      DeleteObject(b);
      EndPaint(h, &ps);
    }
    return 1;
  }
  return 0;
}

bool EnsureCreated()
{
  if (s_strip[0]) return true;
  for (int i = 0; i < 4; i++) {
    HWND h = SWELL_CreateDialog(nullptr, nullptr, nullptr, StripDlgProc, 0);
    if (!h) {
      OverlayFrame::Destroy();
      return false;
    }
    // Style EXACTLY WS_CHILD on a parentless top-level: SWELL-GDK maps that
    // to gdk_window_set_override_redirect(true) — the same path its own
    // menus use (swell-generic-gdk.cpp:644-653). The window is then fully
    // outside the WM: no decorations, no focus, no GNOME '"" is ready'
    // notification, and moves/raises apply instantly EVEN DURING a WM
    // interactive move grab (the user dragging a REAPER window — Linux-VM
    // live debug 2026-06-10: WM-managed strips froze mid-drag because the
    // WM defers configure requests of managed windows while grabbing).
    SetWindowLong(h, GWL_STYLE, WS_CHILD);
    s_strip[i] = h;
  }
  return true;
}

void PlaceStrip(HWND h, const RECT& r, bool colorChanged)
{
  SetWindowPos(h, nullptr, r.left, r.top,
               r.right - r.left, r.bottom - r.top,
               SWP_NOZORDER | SWP_NOACTIVATE);
  ShowWindow(h, SW_SHOWNA);
  // Raise above REAPER's windows — SWELL-generic has no TOPMOST, so
  // re-assert top-of-stack on every placement (cheap; placements only
  // happen on rect/color transitions).
  SetWindowPos(h, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  if (colorChanged) InvalidateRect(h, nullptr, TRUE);
}

#endif

} // namespace (anon)

namespace OverlayFrame {

void ShowRect(const RECT& screenRect, COLORREF color)
{
  if (!EnsureCreated()) return;
  if (s_visible && color == s_color &&
      memcmp(&screenRect, &s_lastRect, sizeof(RECT)) == 0) {
    return;
  }
  const bool colorChanged = (color != s_color);
  s_color = color;
  s_lastRect = screenRect;
  // Transitions only (the same-rect early-return above keeps this quiet).
  DBG("[MaxPane] OverlayFrame: place (%ld,%ld)-(%ld,%ld) color=%06x vis=%d\n",
      (long)screenRect.left, (long)screenRect.top,
      (long)screenRect.right, (long)screenRect.bottom,
      (unsigned)color, s_visible ? 1 : 0);

  // Strips sit just INSIDE the rect edges (same look as the macOS border
  // overlay). Side strips fit between top and bottom so corners don't
  // double-paint. Degenerate rects clamp to empty side strips.
  const RECT& rc = screenRect;
  int innerTop = rc.top + kThick;
  int innerBot = rc.bottom - kThick;
  if (innerBot < innerTop) innerBot = innerTop;
  const RECT strips[4] = {
    { rc.left, rc.top, rc.right, rc.top + kThick },       // top
    { rc.left, rc.bottom - kThick, rc.right, rc.bottom }, // bottom
    { rc.left, innerTop, rc.left + kThick, innerBot },    // left
    { rc.right - kThick, innerTop, rc.right, innerBot },  // right
  };
#ifdef _WIN32
  // Batch the 4 strips into one atomic reposition — sequential SetWindowPos
  // calls repaint strip-by-strip and read as flicker during a native window
  // drag (Win-VM live debug).
  HDWP dwp = BeginDeferWindowPos(4);
  for (int i = 0; i < 4; i++) {
    if (strips[i].right <= strips[i].left ||
        strips[i].bottom <= strips[i].top) {
      ShowWindow(s_strip[i], SW_HIDE);
      continue;
    }
    if (dwp) {
      dwp = DeferWindowPos(dwp, s_strip[i], HWND_TOPMOST,
                           strips[i].left, strips[i].top,
                           strips[i].right - strips[i].left,
                           strips[i].bottom - strips[i].top,
                           SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
      PlaceStrip(s_strip[i], strips[i], colorChanged);
    }
  }
  if (dwp) EndDeferWindowPos(dwp);
  if (colorChanged) {
    for (int i = 0; i < 4; i++) {
      if (s_strip[i]) InvalidateRect(s_strip[i], nullptr, TRUE);
    }
  }
#else
  for (int i = 0; i < 4; i++) {
    if (strips[i].right <= strips[i].left ||
        strips[i].bottom <= strips[i].top) {
      ShowWindow(s_strip[i], SW_HIDE);
      continue;
    }
    PlaceStrip(s_strip[i], strips[i], colorChanged);
  }
#endif
  s_visible = true;
}

void Hide()
{
  if (!s_visible) return;
  for (int i = 0; i < 4; i++) {
    if (s_strip[i]) ShowWindow(s_strip[i], SW_HIDE);
  }
  s_visible = false;
  DBG("[MaxPane] OverlayFrame: hide\n");
}

bool IsFrameWindow(HWND hwnd)
{
  if (!hwnd || !s_strip[0]) return false;
  for (int i = 0; i < 4; i++) {
    if (hwnd == s_strip[i]) return true;
  }
  return false;
}

void Destroy()
{
  for (int i = 0; i < 4; i++) {
    if (s_strip[i]) DestroyWindow(s_strip[i]);
    s_strip[i] = nullptr;
  }
  s_visible = false;
}

} // namespace OverlayFrame

// ---------------------------------------------------------------------------
// Win32/Linux capture hover-outline helpers (declared in swell_cocoa_helpers.h,
// Cocoa implementation in swell_cocoa_helpers.mm). Thin wrappers over the
// frame so the container's ADR-048 call sites stay platform-uniform.
// ---------------------------------------------------------------------------

void ShowCaptureHighlight(HWND target)
{
  if (!target) {
    OverlayFrame::Hide();
    return;
  }
  RECT r = {};
  GetWindowRect(target, &r);
  if (r.right <= r.left || r.bottom <= r.top) {
    OverlayFrame::Hide();
    return;
  }
  OverlayFrame::ShowRect(r, kCaptureAccent);
}

void HideCaptureHighlight()
{
  OverlayFrame::Hide();
}

bool IsCaptureHighlightWindow(HWND hwnd)
{
  return OverlayFrame::IsFrameWindow(hwnd);
}

#endif // !defined(__APPLE__)
