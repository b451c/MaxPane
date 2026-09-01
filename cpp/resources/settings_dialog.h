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
// v2.5.0 — runtime debug log for bug reports (LorenzoB #90). All platforms.
#define IDC_SET_DEBUGLOG     2019
// v2.5.0 — reveal the log file in the file manager (owner request).
#define IDC_SET_OPEN_LOG     2026

// Appearance section
#define IDC_SET_DARK_GROUP   2020
#define IDC_SET_DARK_CYCLE   2021
// U13/ADR-068 — inter-pane border (splitter) color preset cycle.
#define IDC_SET_BORDER_GROUP 2022
#define IDC_SET_BORDER_CYCLE 2023
// v2.5.0 (quar_edm #91) — pane background: Auto (theme) / Black / Custom.
#define IDC_SET_PANEBG_GROUP 2024
#define IDC_SET_PANEBG_CYCLE 2025

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
// v2.5.0 — one more GENERAL checkbox on every platform (debug log, +16) and
// a third APPEARANCE row (pane background, +22). Then a COMPACT pass (Win-VM
// smoke: 452 du ≈ 928 px ran off a 900 px display — OK/Cancel unreachable):
// APPEARANCE pitch 22→20, HOTKEYS label + button on one row, ABOUT collapsed
// to one line (version + license) with the four link buttons beside it.
// → Windows 400 / Linux 384 / macOS 368 (was 452 / 436 / 420).
#if defined(_WIN32)
  #define SD_Y_FOLLOWFX     122
  #define SD_Y_TIEMAIN      138
  #define SD_Y_FOCUSFX      154
  #define SD_Y_DEBUGLOG     170
  #define SD_Y_SEC_APPEAR   188
  #define SD_Y_DARK_BTN     200
  #define SD_Y_DARK_LBL     204
  #define SD_Y_BORDER_BTN   220
  #define SD_Y_BORDER_LBL   224
  #define SD_Y_PANEBG_BTN   240
  #define SD_Y_PANEBG_LBL   244
  #define SD_Y_SEC_HOTKEYS  264
  #define SD_Y_HOTKEYS_LBL  278
  #define SD_Y_ACTIONS_BTN  276
  #define SD_Y_SEC_UPDATES  300
  #define SD_Y_UPDATE_BTN   314
  #define SD_Y_AUTO_UPDATE  317
  #define SD_Y_SEC_ABOUT    338
  #define SD_Y_VERSION_LBL  352
  #define SD_Y_LICENSE_LBL  352
  #define SD_Y_LINK_BTNS    349
  #define SD_Y_BOTTOM_BTNS  376
  #define SD_DLG_H          400
#elif defined(__APPLE__)
  #define SD_Y_FOLLOWFX     106
  #define SD_Y_FOCUSFX      122
  #define SD_Y_DEBUGLOG     138
  #define SD_Y_SEC_APPEAR   156
  #define SD_Y_DARK_BTN     168
  #define SD_Y_DARK_LBL     172
  #define SD_Y_BORDER_BTN   188
  #define SD_Y_BORDER_LBL   192
  #define SD_Y_PANEBG_BTN   208
  #define SD_Y_PANEBG_LBL   212
  #define SD_Y_SEC_HOTKEYS  232
  #define SD_Y_HOTKEYS_LBL  246
  #define SD_Y_ACTIONS_BTN  244
  #define SD_Y_SEC_UPDATES  268
  #define SD_Y_UPDATE_BTN   282
  #define SD_Y_AUTO_UPDATE  285
  #define SD_Y_SEC_ABOUT    306
  #define SD_Y_VERSION_LBL  320
  #define SD_Y_LICENSE_LBL  320
  #define SD_Y_LINK_BTNS    317
  #define SD_Y_BOTTOM_BTNS  344
  #define SD_DLG_H          368
#else  // Linux
  #define SD_Y_FOLLOWFX     106
  #define SD_Y_TIEMAIN      122
  #define SD_Y_FOCUSFX      138
  #define SD_Y_DEBUGLOG     154
  #define SD_Y_SEC_APPEAR   172
  #define SD_Y_DARK_BTN     184
  #define SD_Y_DARK_LBL     188
  #define SD_Y_BORDER_BTN   204
  #define SD_Y_BORDER_LBL   208
  #define SD_Y_PANEBG_BTN   224
  #define SD_Y_PANEBG_LBL   228
  #define SD_Y_SEC_HOTKEYS  248
  #define SD_Y_HOTKEYS_LBL  262
  #define SD_Y_ACTIONS_BTN  260
  #define SD_Y_SEC_UPDATES  284
  #define SD_Y_UPDATE_BTN   298
  #define SD_Y_AUTO_UPDATE  301
  #define SD_Y_SEC_ABOUT    322
  #define SD_Y_VERSION_LBL  336
  #define SD_Y_LICENSE_LBL  336
  #define SD_Y_LINK_BTNS    333
  #define SD_Y_BOTTOM_BTNS  360
  #define SD_DLG_H          384
#endif
