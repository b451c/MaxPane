// platform.h — Central platform abstraction for MaxPane
#pragma once

#ifdef _WIN32
  // Windows: native Win32 API
  #include <windows.h>
#else
  // macOS / Linux: Win32 API via SWELL
  #ifndef WDL_NO_DEFINE_MINMAX
    #define WDL_NO_DEFINE_MINMAX
  #endif
  #ifndef SWELL_PROVIDED_BY_APP
    #define SWELL_PROVIDED_BY_APP
  #endif
  #include "swell/swell.h"

  // Bridge SWELL (GWL_USERDATA/SetWindowLong) to Win64 names (GWLP_USERDATA/SetWindowLongPtr)
  #ifndef GWLP_USERDATA
    #define GWLP_USERDATA GWL_USERDATA
  #endif
  #ifndef GWLP_WNDPROC
    #define GWLP_WNDPROC GWL_WNDPROC
  #endif
  // B2 (v2.4.0) — owner slot (F8 tie-to-main; Linux transient-for).
  #ifndef GWLP_HWNDPARENT
    #define GWLP_HWNDPARENT GWL_HWNDPARENT
  #endif
  #ifndef SetWindowLongPtr
    #define SetWindowLongPtr SetWindowLong
  #endif
  #ifndef GetWindowLongPtr
    #define GetWindowLongPtr GetWindowLong
  #endif
#endif

// Portable dialog creation helper
#ifdef _WIN32
inline HWND CreateMaxPaneDialog(HWND parent, DLGPROC dlgProc, LPARAM param) {
  #pragma pack(push, 4)
  struct { DLGTEMPLATE tmpl; WORD menu; WORD wndClass; WORD title; } dlg = {};
  #pragma pack(pop)
  // Sprint 1 Entry 3 — WS_CLIPCHILDREN | WS_CLIPSIBLINGS prevents the container
  // paint from blitting over captured child rects during resize (Mixer columns
  // smearing across the container). WS_CLIPSIBLINGS future-proofs side-by-side
  // captured panes. Direct2D children still smear under default copy-bits
  // behaviour — Entry 19 handles that via SWP_NOCOPYBITS on arbitrary captures.
  dlg.tmpl.style = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | DS_CONTROL;
  dlg.tmpl.cx = 400;
  dlg.tmpl.cy = 300;
  return CreateDialogIndirectParam(GetModuleHandle(nullptr), &dlg.tmpl, parent, dlgProc, param);
}
#else
inline HWND CreateMaxPaneDialog(HWND parent, DLGPROC dlgProc, LPARAM param) {
  return SWELL_CreateDialog(nullptr, nullptr, parent, dlgProc, param);
}
#endif

// Cross-platform UTF-8 text rendering helper.
// macOS / Linux SWELL DrawText accepts UTF-8 byte sequences natively and
// renders multi-byte glyphs (⌂ ⊕ ≡ ⚙ ♥ • ▼ ★ …) correctly. Windows native
// DrawText is DrawTextA (ANSI / CP-1252) — multi-byte UTF-8 input gets
// reinterpreted as one-byte-per-char mojibake (e.g. "⌂" → "âŒ‚"). Route
// every paint-time text through this wrapper so all platforms render the
// same UTF-8 string the same way. For ASCII-only inputs both branches are
// effectively no-ops; the wrapper exists for the multi-byte path.
#include <cstdlib>
#include <cstring>
inline int DrawTextUtf8(HDC hdc, const char* text, int len,
                        LPRECT rect, UINT format)
{
#ifdef _WIN32
  if (!text) return 0;
  const int byteLen = (len < 0) ? (int)strlen(text) : len;
  if (byteLen <= 0) return DrawTextA(hdc, text, byteLen, rect, format);
  int wlen = MultiByteToWideChar(CP_UTF8, 0, text, byteLen, nullptr, 0);
  if (wlen <= 0) return DrawTextA(hdc, text, byteLen, rect, format);
  // Stack buffer for the typical (short) case — tab names, button labels,
  // tooltips. 256 wchar_t covers ~256 BMP code points before falling back
  // to malloc.
  if (wlen <= 256) {
    wchar_t wbuf[256];
    MultiByteToWideChar(CP_UTF8, 0, text, byteLen, wbuf, wlen);
    return DrawTextW(hdc, wbuf, wlen, rect, format);
  }
  wchar_t* wbuf = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
  if (!wbuf) return DrawTextA(hdc, text, byteLen, rect, format);
  MultiByteToWideChar(CP_UTF8, 0, text, byteLen, wbuf, wlen);
  int r = DrawTextW(hdc, wbuf, wlen, rect, format);
  free(wbuf);
  return r;
#else
  return DrawText(hdc, text, len, rect, format);
#endif
}
