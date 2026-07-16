// Control ID defines for the Settings dialog (Sprint 3.1 / ADR-019).
// IDs are namespaced by tab so the DlgProc can iterate ranges when
// switching tabs (hide all, show one tab's set).
#pragma once

#define IDD_SETTINGS         2000

// General section
#define IDC_SET_AUTOOPEN     2010
// ADR-026 — toggle persistent nav bar at top of every MaxPane instance.
#define IDC_SET_SHOWNAVBAR   2011
// ADR-055 — collapse a pane's tab bar to a sliver when it has a single tab.
#define IDC_SET_HIDETABBAR   2012
// U12/ADR-068 — clean mode: hide the header of every occupied pane.
#define IDC_SET_HIDEALLTABS  2013
// U15/ADR-069 — hide the floating MaxPane from the Windows taskbar.
// Row emitted on Windows only (v2.4.0 platform gating — see below).
#define IDC_SET_HIDETASKBAR  2014
// U14/ADR-070 — experimental: follow the selected track's FX in pane 1.
#define IDC_SET_FOLLOWFX     2015
// B2/v2.4.0 — tie the floating MaxPane to REAPER's main window.
// Row emitted on Windows + Linux (inert on mac — platform gating below).
#define IDC_SET_TIEMAIN      2016
// F7/v2.4.0 — startup workspace dropdown (loads when a project carries no
// MaxPane state; "None" = today's empty launcher).
#define IDC_SET_STARTWS      2017
// F12/v2.4.0 — focus the captured FX window on a user tab switch (MIDI
// controllers targeting REAPER's focused FX). Default OFF.
#define IDC_SET_FOCUSFX      2018

// Appearance section
#define IDC_SET_DARK_GROUP   2020
#define IDC_SET_DARK_CYCLE   2021
// U13/ADR-068 — inter-pane border (splitter) color preset cycle.
#define IDC_SET_BORDER_GROUP 2022
#define IDC_SET_BORDER_CYCLE 2023

// Hotkeys section
#define IDC_SET_HOTKEYS_LBL  2030
#define IDC_SET_OPEN_ACTIONS 2031

// About section (v2.0.3)
#define IDC_SET_VERSION_LBL  2040
#define IDC_SET_LICENSE_LBL  2041
#define IDC_SET_RESET        2042
#define IDC_SET_GITHUB       2043
#define IDC_SET_KOFI         2044
#define IDC_SET_BMC          2045
#define IDC_SET_PAYPAL       2046
#define IDC_SET_CHECK_UPDATE 2047
// v2.0.4 #3 — async auto-on-startup update check (ADR-039).
#define IDC_SET_AUTO_UPDATE  2048

// Bottom buttons — platform defaults (1, 2).

// ---------------------------------------------------------------------------
// v2.4.0 — platform-gated rows (owner mac-smoke feedback: "(Windows)" rows
// are inert dead weight on macOS). Truth table:
//   hide_from_taskbar — Windows-only in effect (WS_EX_TOOLWINDOW; the mac
//     and Linux ApplyFloatingWindowChrome variants ignore the flag).
//   float_tie_to_main — Windows + Linux (owner slot / GDK transient-for);
//     the mac variant ignores the owner param (SWELL owner list on mac is
//     a destroy cascade, not z-order grouping — declined in ADR-076).
// Inert rows are not emitted on platforms where they do nothing, and every
// row below shifts up statically via these Y constants (dialog units).
// swell_resgen.pl copies control lines verbatim and rc.exe resolves simple
// #define constants, so identifiers are safe in coordinate fields on all
// three toolchains. Layouts:
//   Windows: 8 checkboxes (taskbar + tie)         → height 414
//   Linux:   7 checkboxes (tie, no taskbar) −16   → height 398
//   macOS:   6 checkboxes (neither)         −32   → height 382
#if defined(_WIN32)
  #define SD_Y_FOLLOWFX     122
  #define SD_Y_TIEMAIN      138
  #define SD_Y_FOCUSFX      154
  #define SD_Y_SEC_APPEAR   174
  #define SD_Y_DARK_BTN     188
  #define SD_Y_DARK_LBL     192
  #define SD_Y_BORDER_BTN   210
  #define SD_Y_BORDER_LBL   214
  #define SD_Y_SEC_HOTKEYS  234
  #define SD_Y_HOTKEYS_LBL  248
  #define SD_Y_ACTIONS_BTN  262
  #define SD_Y_SEC_UPDATES  288
  #define SD_Y_UPDATE_BTN   302
  #define SD_Y_AUTO_UPDATE  305
  #define SD_Y_SEC_ABOUT    326
  #define SD_Y_VERSION_LBL  340
  #define SD_Y_LICENSE_LBL  352
  #define SD_Y_LINK_BTNS    368
  #define SD_Y_BOTTOM_BTNS  390
  #define SD_DLG_H          414
#elif defined(__APPLE__)
  #define SD_Y_FOLLOWFX     106
  #define SD_Y_FOCUSFX      122
  #define SD_Y_SEC_APPEAR   142
  #define SD_Y_DARK_BTN     156
  #define SD_Y_DARK_LBL     160
  #define SD_Y_BORDER_BTN   178
  #define SD_Y_BORDER_LBL   182
  #define SD_Y_SEC_HOTKEYS  202
  #define SD_Y_HOTKEYS_LBL  216
  #define SD_Y_ACTIONS_BTN  230
  #define SD_Y_SEC_UPDATES  256
  #define SD_Y_UPDATE_BTN   270
  #define SD_Y_AUTO_UPDATE  273
  #define SD_Y_SEC_ABOUT    294
  #define SD_Y_VERSION_LBL  308
  #define SD_Y_LICENSE_LBL  320
  #define SD_Y_LINK_BTNS    336
  #define SD_Y_BOTTOM_BTNS  358
  #define SD_DLG_H          382
#else  // Linux
  #define SD_Y_FOLLOWFX     106
  #define SD_Y_TIEMAIN      122
  #define SD_Y_FOCUSFX      138
  #define SD_Y_SEC_APPEAR   158
  #define SD_Y_DARK_BTN     172
  #define SD_Y_DARK_LBL     176
  #define SD_Y_BORDER_BTN   194
  #define SD_Y_BORDER_LBL   198
  #define SD_Y_SEC_HOTKEYS  218
  #define SD_Y_HOTKEYS_LBL  232
  #define SD_Y_ACTIONS_BTN  246
  #define SD_Y_SEC_UPDATES  272
  #define SD_Y_UPDATE_BTN   286
  #define SD_Y_AUTO_UPDATE  289
  #define SD_Y_SEC_ABOUT    310
  #define SD_Y_VERSION_LBL  324
  #define SD_Y_LICENSE_LBL  336
  #define SD_Y_LINK_BTNS    352
  #define SD_Y_BOTTOM_BTNS  374
  #define SD_DLG_H          398
#endif
