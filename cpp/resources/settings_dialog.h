// Control ID defines for the Settings dialog (Sprint 3.1 / ADR-019).
// IDs are namespaced by tab so the DlgProc can iterate ranges when
// switching tabs (hide all, show one tab's set).
#pragma once

#define IDD_SETTINGS         2000

// General section
#define IDC_SET_AUTOOPEN     2010
// ADR-026 — toggle persistent nav bar at top of every MaxPane instance.
#define IDC_SET_SHOWNAVBAR   2011

// Appearance section
#define IDC_SET_DARK_GROUP   2020
#define IDC_SET_DARK_CYCLE   2021

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
