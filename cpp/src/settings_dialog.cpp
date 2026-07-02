// Settings dialog implementation (Sprint 3.1 / ADR-019).
//
// Single-page layout with SWELL-friendly widgets (CHECKBOX + PUSHBUTTON +
// LTEXT). Dark mode toggle uses a cycle-button (Auto → Dark → Light) rather
// than radio buttons because SWELL's .rc macro vocabulary doesn't include
// AUTORADIOBUTTON.
//
// Persisted settings (ExtState "MaxPane_cpp"):
//   - auto_open  ("0" or "1"; absent → true)
//   - dark_mode  ("auto" | "dark" | "light"; absent → "auto")

#include "settings_dialog.h"
#include "../resources/settings_dialog.h"
#include "globals.h"
#include "config.h"
#include "swell_cocoa_helpers.h"  // OpenUrlPlatform
#include "updater.h"               // Updater::CheckForUpdatesNow

#include <cstdio>
#include <cstring>

namespace {

// Dark-mode cycle state — read from ExtState on dialog open, mutated by
// the cycle button, written back on OK. Order: auto → dark → light → auto.
enum DarkMode { DM_AUTO = 0, DM_DARK = 1, DM_LIGHT = 2, DM_COUNT = 3 };

DarkMode ReadDarkMode()
{
  const char* dm = g_GetExtState ? g_GetExtState(EXT_SECTION, "dark_mode") : nullptr;
  if (dm && std::strcmp(dm, "dark") == 0) return DM_DARK;
  if (dm && std::strcmp(dm, "light") == 0) return DM_LIGHT;
  return DM_AUTO;
}

const char* DarkModeButtonLabel(DarkMode m)
{
  switch (m) {
    case DM_DARK:  return "Force dark";
    case DM_LIGHT: return "Force light";
    default:       return "Auto (follow system)";
  }
}

const char* DarkModeExtStateValue(DarkMode m)
{
  switch (m) {
    case DM_DARK:  return "dark";
    case DM_LIGHT: return "light";
    default:       return "auto";
  }
}

// Pending dark-mode value (not yet persisted — applied on OK).
static DarkMode g_pendingDarkMode = DM_AUTO;

// ADR-026 — nav bar visibility. Stored globally under "MaxPane_cpp" so all
// instances share the toggle. Default ON; only literal "0" turns it off.
bool ReadShowNavBar()
{
  if (!g_GetExtState) return true;
  const char* v = g_GetExtState(EXT_SECTION, "show_nav_bar");
  if (v && v[0] == '0' && v[1] == '\0') return false;
  return true;
}

// ADR-055 — collapse a pane's tab bar to a sliver when it holds a single
// window. Global, default OFF; only literal "1" turns it on.
bool ReadHideSingleTabBar()
{
  if (!g_GetExtState) return false;
  const char* v = g_GetExtState(EXT_SECTION, "hide_single_tab_bar");
  return (v && v[0] == '1' && v[1] == '\0');
}

// U12 (ADR-068) — clean mode: hide the header of every occupied pane
// (three forum users asked for a chrome-free look). Default OFF.
bool ReadHideAllTabBars()
{
  if (!g_GetExtState) return false;
  const char* v = g_GetExtState(EXT_SECTION, "hide_tab_bars");
  return (v && v[0] == '1' && v[1] == '\0');
}

// U14 (ADR-070) — experimental follow-selected-track mode (pane 1 shows the
// last-touched track's FX chain, updated live). Default OFF.
bool ReadFollowTrackFx()
{
  if (!g_GetExtState) return false;
  const char* v = g_GetExtState(EXT_SECTION, "follow_track_fx");
  return (v && v[0] == '1' && v[1] == '\0');
}

// U15 (ADR-069) — keep the floating MaxPane out of the Windows taskbar
// (WS_EX_TOOLWINDOW; also leaves Alt-Tab — documented trade-off). Win-only
// in effect; the checkbox is shown everywhere, harmless no-op on mac/Linux.
bool ReadHideFromTaskbar()
{
  if (!g_GetExtState) return false;
  const char* v = g_GetExtState(EXT_SECTION, "hide_from_taskbar");
  return (v && v[0] == '1' && v[1] == '\0');
}

// U13 (ADR-068) — pending splitter-color preset index (cycle button, same
// pattern as the dark-mode cycle: staged here, committed on OK).
static int g_pendingSplitterIdx = 0;

void LoadValues(HWND dlg)
{
  CheckDlgButton(dlg, IDC_SET_AUTOOPEN, IsAutoOpenEnabled() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dlg, IDC_SET_SHOWNAVBAR, ReadShowNavBar() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dlg, IDC_SET_HIDETABBAR, ReadHideSingleTabBar() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dlg, IDC_SET_HIDEALLTABS, ReadHideAllTabBars() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dlg, IDC_SET_HIDETASKBAR, ReadHideFromTaskbar() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dlg, IDC_SET_FOLLOWFX, ReadFollowTrackFx() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dlg, IDC_SET_AUTO_UPDATE, IsAutoUpdateEnabled() ? BST_CHECKED : BST_UNCHECKED);
  g_pendingDarkMode = ReadDarkMode();
  SetDlgItemText(dlg, IDC_SET_DARK_CYCLE, DarkModeButtonLabel(g_pendingDarkMode));
  g_pendingSplitterIdx = GetSplitterColorPresetIndex();
  SetDlgItemText(dlg, IDC_SET_BORDER_CYCLE,
                 SPLITTER_COLOR_PRESETS[g_pendingSplitterIdx].name);

  // About section — fill version label from compile-time constant.
  char ver[128];
  std::snprintf(ver, sizeof(ver), "MaxPane %s", MAXPANE_VERSION_STRING);
  SetDlgItemText(dlg, IDC_SET_VERSION_LBL, ver);
}

void CommitValues(HWND dlg)
{
  SetAutoOpenEnabled(IsDlgButtonChecked(dlg, IDC_SET_AUTOOPEN) == BST_CHECKED);
  SetAutoUpdateEnabled(IsDlgButtonChecked(dlg, IDC_SET_AUTO_UPDATE) == BST_CHECKED);
  if (g_SetExtState) {
    g_SetExtState(EXT_SECTION, "dark_mode", DarkModeExtStateValue(g_pendingDarkMode), true);
    InvalidateMaxPaneDarkModeCache();
    const bool showNav = (IsDlgButtonChecked(dlg, IDC_SET_SHOWNAVBAR) == BST_CHECKED);
    g_SetExtState(EXT_SECTION, "show_nav_bar", showNav ? "1" : "0", true);
    const bool hideTab = (IsDlgButtonChecked(dlg, IDC_SET_HIDETABBAR) == BST_CHECKED);
    g_SetExtState(EXT_SECTION, "hide_single_tab_bar", hideTab ? "1" : "0", true);
    const bool hideAll = (IsDlgButtonChecked(dlg, IDC_SET_HIDEALLTABS) == BST_CHECKED);
    g_SetExtState(EXT_SECTION, "hide_tab_bars", hideAll ? "1" : "0", true);
    const bool hideTb = (IsDlgButtonChecked(dlg, IDC_SET_HIDETASKBAR) == BST_CHECKED);
    g_SetExtState(EXT_SECTION, "hide_from_taskbar", hideTb ? "1" : "0", true);
    const bool follow = (IsDlgButtonChecked(dlg, IDC_SET_FOLLOWFX) == BST_CHECKED);
    g_SetExtState(EXT_SECTION, "follow_track_fx", follow ? "1" : "0", true);
    g_SetExtState(EXT_SECTION, "splitter_color",
                  SPLITTER_COLOR_PRESETS[g_pendingSplitterIdx].key, true);
  }
}

INT_PTR CALLBACK SettingsDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM /*lParam*/)
{
  switch (msg) {
    case WM_INITDIALOG:
      LoadValues(dlg);
      return 1;

    case WM_COMMAND: {
      const int id = LOWORD(wParam);

      if (id == IDC_SET_DARK_CYCLE) {
        g_pendingDarkMode = (DarkMode)((g_pendingDarkMode + 1) % DM_COUNT);
        SetDlgItemText(dlg, IDC_SET_DARK_CYCLE, DarkModeButtonLabel(g_pendingDarkMode));
        return 0;
      }

      if (id == IDC_SET_BORDER_CYCLE) {
        // U13 (ADR-068) — cycle the splitter-color preset; committed on OK.
        g_pendingSplitterIdx =
            (g_pendingSplitterIdx + 1) % NUM_SPLITTER_COLOR_PRESETS;
        SetDlgItemText(dlg, IDC_SET_BORDER_CYCLE,
                       SPLITTER_COLOR_PRESETS[g_pendingSplitterIdx].name);
        return 0;
      }

      if (id == IDC_SET_OPEN_ACTIONS) {
        if (g_Main_OnCommand) g_Main_OnCommand(40605, 0);
        return 0;
      }

      // About section — external links via the platform browser shim.
      if (id == IDC_SET_GITHUB) {
        OpenUrlPlatform("https://github.com/b451c/MaxPane");
        return 0;
      }
      if (id == IDC_SET_KOFI) {
        OpenUrlPlatform("https://ko-fi.com/quickmd");
        return 0;
      }
      if (id == IDC_SET_BMC) {
        OpenUrlPlatform("https://buymeacoffee.com/bsroczynskh");
        return 0;
      }
      if (id == IDC_SET_PAYPAL) {
        OpenUrlPlatform("https://paypal.me/b451c");
        return 0;
      }
      if (id == IDC_SET_CHECK_UPDATE) {
        // Synchronous HTTPS check — briefly blocks the UI (~1-3s).
        // showIfUpToDate=true so the user gets explicit feedback either
        // way when they hit this button.
        Updater::CheckForUpdatesNow(dlg, /*showIfUpToDate=*/true);
        return 0;
      }

      if (id == IDC_SET_RESET) {
        if (g_SetExtState) {
          g_SetExtState(EXT_SECTION, "auto_open", "1", true);
          g_SetExtState(EXT_SECTION, "dark_mode", "auto", true);
          InvalidateMaxPaneDarkModeCache();
        }
        LoadValues(dlg);
        return 0;
      }

      if (id == 1 /* IDOK */) {
        CommitValues(dlg);
        EndDialog(dlg, 1);
        return 0;
      }

      if (id == 2 /* IDCANCEL */) {
        EndDialog(dlg, 2);
        return 0;
      }
      break;
    }

    case WM_CLOSE:
      EndDialog(dlg, 2);
      return 0;
  }
  return 0;
}

}  // namespace

void OpenSettingsDialog(HWND parent)
{
  DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_SETTINGS),
                 parent, SettingsDialogProc, 0);
}

#ifndef _WIN32
// B-LINUX-MODALS — REAPER's bundled SWELL doesn't substitute SWELL_DEF_DLGSCALE2
// for the 0.0 default the _BEGIN2 macro variant emits, so dialogs render with
// zero-sized children on Linux. Switch to the explicit-scale _BEGIN form via
// SET_IDD_*_SCALE before the include. The shim below is required because the
// WDL upstream macro references SWELL_DLG_WS_DEFAULT_SCALING unconditionally
// but only defines it under SWELL_TARGET_OSX.
#ifndef SWELL_DLG_WS_DEFAULT_SCALING
#define SWELL_DLG_WS_DEFAULT_SCALING 0
#endif
#define SET_IDD_SETTINGS_SCALE 1.9
#include "swell/swell-dlggen.h"
#include "settings_dialog.rc_mac_dlg"
#endif
