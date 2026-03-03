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
  dlg.tmpl.style = WS_CHILD | DS_CONTROL;
  dlg.tmpl.cx = 400;
  dlg.tmpl.cy = 300;
  return CreateDialogIndirectParam(GetModuleHandle(nullptr), &dlg.tmpl, parent, dlgProc, param);
}
#else
inline HWND CreateMaxPaneDialog(HWND parent, DLGPROC dlgProc, LPARAM param) {
  return SWELL_CreateDialog(nullptr, nullptr, parent, dlgProc, param);
}
#endif
