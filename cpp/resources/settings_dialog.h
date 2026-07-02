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
#define IDC_SET_HIDETASKBAR  2014
// U14/ADR-070 — experimental: follow the selected track's FX in pane 1.
#define IDC_SET_FOLLOWFX     2015

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
