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
#include "debug.h"                 // v2.5.0 — g_dbgEnabled live flip

#include <cstdio>
#include <cstring>
#include <cstdlib>   // strtol (F7 combo) — MSVC strict-include discipline

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
// in effect; since v2.4.0 the row is only emitted on Windows (platform
// gating — see settings_dialog.h), so mac/Linux users never see it.
#ifdef _WIN32
bool ReadHideFromTaskbar()
{
  if (!g_GetExtState) return false;
  const char* v = g_GetExtState(EXT_SECTION, "hide_from_taskbar");
  return (v && v[0] == '1' && v[1] == '\0');
}
#endif

// F12 (ADR-079, bertrand #73) — focus the captured FX window on a USER tab
// switch so MIDI controllers targeting REAPER's focused FX follow the tab.
// Default OFF (SW_SHOWNA no-steal is the deliberate baseline).
bool ReadFocusFxOnTab()
{
  if (!g_GetExtState) return false;
  const char* v = g_GetExtState(EXT_SECTION, "focus_fx_on_tab_switch");
  return (v && v[0] == '1' && v[1] == '\0');
}

// B2 (v2.4.0, mb945 #78) — tie the floating MaxPane to REAPER's main window
// (Win32 owned-window semantics: minimize-follow + group z-order; Linux gets
// the same via GDK transient-for). Default OFF (owner decision D1). Inert on
// mac, so the row is not emitted there (platform gating, settings_dialog.h).
#ifndef __APPLE__
bool ReadTieToMain()
{
  if (!g_GetExtState) return false;
  const char* v = g_GetExtState(EXT_SECTION, "float_tie_to_main");
  return (v && v[0] == '1' && v[1] == '\0');
}
#endif

// F7 (v2.4.0, mb945 #78) — startup workspace dropdown. Raw workspace-name
// storage backing the combo entries: index 0 = "None (show launcher)",
// entries 1..count map to g_startupWsNames[i-1]. A pref naming a workspace
// that no longer exists is appended as "<name> (missing)" so committing any
// other choice replaces it.
static char g_startupWsNames[MAX_WORKSPACES + 1][128];
static int  g_startupWsCount = 0;

void PopulateStartupWorkspaceCombo(HWND dlg)
{
  g_startupWsCount = 0;
  SendDlgItemMessage(dlg, IDC_SET_STARTWS, CB_RESETCONTENT, 0, 0);
  SendDlgItemMessage(dlg, IDC_SET_STARTWS, CB_ADDSTRING, 0,
                     (LPARAM)"None (show launcher)");

  const char* pref = g_GetExtState ? g_GetExtState(EXT_SECTION, "startup_workspace")
                                   : nullptr;
  int selected = 0;

  // Raw list read (ws_count / ws_<N>_name) — no WorkspaceManager needed.
  const char* countStr = g_GetExtState ? g_GetExtState(EXT_SECTION, "ws_count") : nullptr;
  int count = 0;
  if (countStr) count = (int)strtol(countStr, nullptr, 10);
  if (count < 0) count = 0;
  if (count > MAX_WORKSPACES) count = MAX_WORKSPACES;
  for (int w = 0; w < count; w++) {
    char key[64];
    std::snprintf(key, sizeof(key), "ws_%d_name", w);
    const char* name = g_GetExtState(EXT_SECTION, key);
    if (!name || !name[0]) continue;
    safe_strncpy(g_startupWsNames[g_startupWsCount], name,
                 sizeof(g_startupWsNames[0]));
    SendDlgItemMessage(dlg, IDC_SET_STARTWS, CB_ADDSTRING, 0, (LPARAM)name);
    g_startupWsCount++;
    if (pref && pref[0] && strcmp(pref, name) == 0) selected = g_startupWsCount;
  }
  if (pref && pref[0] && selected == 0) {
    // Pref names a workspace that no longer exists — surface it.
    char label[160];
    std::snprintf(label, sizeof(label), "%s (missing)", pref);
    safe_strncpy(g_startupWsNames[g_startupWsCount], pref,
                 sizeof(g_startupWsNames[0]));
    SendDlgItemMessage(dlg, IDC_SET_STARTWS, CB_ADDSTRING, 0, (LPARAM)label);
    g_startupWsCount++;
    selected = g_startupWsCount;
  }
  SendDlgItemMessage(dlg, IDC_SET_STARTWS, CB_SETCURSEL, (WPARAM)selected, 0);
}

// U13 (ADR-068) — pending splitter-color preset index (cycle button, same
// pattern as the dark-mode cycle: staged here, committed on OK).
static int g_pendingSplitterIdx = 0;

// v2.5.0 (quar_edm #91) — pending pane-background override, staged as the
// ExtState string: "auto" | "#RRGGBB". The cycle is Auto → Black → Custom...
// (native REAPER color picker; Cancel falls back to Auto) → Auto.
static char g_pendingPaneBg[16] = "auto";

static void PaneBgButtonLabel(const char* v, char* out, int outSize)
{
  COLORREF c;
  if (!ParsePaneBgOverride(v, &c)) { safe_strncpy(out, "Auto (theme)", outSize); return; }
  if (c == RGB(0, 0, 0)) { safe_strncpy(out, "Black", outSize); return; }
  std::snprintf(out, outSize, "Custom (#%02X%02X%02X)",
                GetRValue(c), GetGValue(c), GetBValue(c));
}

// Native REAPER color picker seeded with the current pane color. Writes
// "#RRGGBB" into g_pendingPaneBg on OK; returns false on Cancel / no API.
static bool PickCustomPaneBg(HWND dlg)
{
  if (!g_GR_SelectColor || !g_ColorFromNative || !g_ColorToNative) return false;
  COLORREF cur;
  if (!ParsePaneBgOverride(g_pendingPaneBg, &cur)) cur = GetPaneBgColor();
  int native = g_ColorToNative(GetRValue(cur), GetGValue(cur), GetBValue(cur));
  if (!g_GR_SelectColor(dlg, &native)) return false;
  int r = 0, g = 0, b = 0;
  g_ColorFromNative(native, &r, &g, &b);
  std::snprintf(g_pendingPaneBg, sizeof(g_pendingPaneBg), "#%02X%02X%02X",
                r & 0xFF, g & 0xFF, b & 0xFF);
  return true;
}

// v2.5.0 — runtime debug log pref (see debug.h). Default OFF in Release;
// Debug builds log regardless of the pref (g_dbgEnabled starts true there).
bool ReadDebugLogPref()
{
  if (!g_GetExtState) return false;
  const char* v = g_GetExtState(EXT_SECTION, "debug_log");
  return (v && v[0] == '1' && v[1] == '\0');
}

void LoadValues(HWND dlg)
{
  CheckDlgButton(dlg, IDC_SET_AUTOOPEN, IsAutoOpenEnabled() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dlg, IDC_SET_SHOWNAVBAR, ReadShowNavBar() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dlg, IDC_SET_HIDETABBAR, ReadHideSingleTabBar() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dlg, IDC_SET_HIDEALLTABS, ReadHideAllTabBars() ? BST_CHECKED : BST_UNCHECKED);
  // v2.4.0 platform gating — rows inert on this platform are not emitted
  // (see settings_dialog.h); load/commit mirror the same #if structure so
  // an absent control never round-trips a pref it can't edit.
#ifdef _WIN32
  CheckDlgButton(dlg, IDC_SET_HIDETASKBAR, ReadHideFromTaskbar() ? BST_CHECKED : BST_UNCHECKED);
#endif
  CheckDlgButton(dlg, IDC_SET_FOLLOWFX, ReadFollowTrackFx() ? BST_CHECKED : BST_UNCHECKED);
#ifndef __APPLE__
  CheckDlgButton(dlg, IDC_SET_TIEMAIN, ReadTieToMain() ? BST_CHECKED : BST_UNCHECKED);
#endif
  CheckDlgButton(dlg, IDC_SET_FOCUSFX, ReadFocusFxOnTab() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dlg, IDC_SET_DEBUGLOG, ReadDebugLogPref() ? BST_CHECKED : BST_UNCHECKED);
  PopulateStartupWorkspaceCombo(dlg);
  CheckDlgButton(dlg, IDC_SET_AUTO_UPDATE, IsAutoUpdateEnabled() ? BST_CHECKED : BST_UNCHECKED);
  g_pendingDarkMode = ReadDarkMode();
  SetDlgItemText(dlg, IDC_SET_DARK_CYCLE, DarkModeButtonLabel(g_pendingDarkMode));
  g_pendingSplitterIdx = GetSplitterColorPresetIndex();
  SetDlgItemText(dlg, IDC_SET_BORDER_CYCLE,
                 SPLITTER_COLOR_PRESETS[g_pendingSplitterIdx].name);
  {
    const char* pb = g_GetExtState ? g_GetExtState(EXT_SECTION, "pane_bg") : nullptr;
    safe_strncpy(g_pendingPaneBg, (pb && pb[0]) ? pb : "auto", sizeof(g_pendingPaneBg));
    char label[48];
    PaneBgButtonLabel(g_pendingPaneBg, label, sizeof(label));
    SetDlgItemText(dlg, IDC_SET_PANEBG_CYCLE, label);
  }

  // About section — fill version label from compile-time constant.
  char ver[128];
  std::snprintf(ver, sizeof(ver), "MaxPane %s -- MIT License", MAXPANE_VERSION_STRING);
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
#ifdef _WIN32
    const bool hideTb = (IsDlgButtonChecked(dlg, IDC_SET_HIDETASKBAR) == BST_CHECKED);
    g_SetExtState(EXT_SECTION, "hide_from_taskbar", hideTb ? "1" : "0", true);
#endif
    const bool follow = (IsDlgButtonChecked(dlg, IDC_SET_FOLLOWFX) == BST_CHECKED);
    g_SetExtState(EXT_SECTION, "follow_track_fx", follow ? "1" : "0", true);
#ifndef __APPLE__
    const bool tie = (IsDlgButtonChecked(dlg, IDC_SET_TIEMAIN) == BST_CHECKED);
    g_SetExtState(EXT_SECTION, "float_tie_to_main", tie ? "1" : "0", true);
#endif
    const bool focusFx = (IsDlgButtonChecked(dlg, IDC_SET_FOCUSFX) == BST_CHECKED);
    g_SetExtState(EXT_SECTION, "focus_fx_on_tab_switch", focusFx ? "1" : "0", true);
    // F7 — commit the startup-workspace choice by RAW name (index 0 = off).
    {
      int sel = (int)SendDlgItemMessage(dlg, IDC_SET_STARTWS, CB_GETCURSEL, 0, 0);
      const char* name = (sel > 0 && sel <= g_startupWsCount)
                             ? g_startupWsNames[sel - 1] : "";
      g_SetExtState(EXT_SECTION, "startup_workspace", name, true);
    }
    g_SetExtState(EXT_SECTION, "splitter_color",
                  SPLITTER_COLOR_PRESETS[g_pendingSplitterIdx].key, true);
    // v2.5.0 — pane background override; the cache shares the dark-mode
    // invalidate above, so the next paint + RefreshChromeBrushes see it.
    g_SetExtState(EXT_SECTION, "pane_bg", g_pendingPaneBg, true);
    // v2.5.0 — debug log: persist + flip live (a Release build starts
    // logging from this moment; Debug builds always log).
    const bool dbgLog = (IsDlgButtonChecked(dlg, IDC_SET_DEBUGLOG) == BST_CHECKED);
    g_SetExtState(EXT_SECTION, "debug_log", dbgLog ? "1" : "0", true);
#ifndef MAXPANE_DEBUG
    g_dbgEnabled = dbgLog;
#endif
    DBG("[MaxPane] Settings: debug_log=%d pane_bg=%s\n", dbgLog ? 1 : 0, g_pendingPaneBg);
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

      if (id == IDC_SET_PANEBG_CYCLE) {
        // v2.5.0 — Auto → Black → Custom... → Auto (picker Cancel = Auto).
        COLORREF c;
        const bool isOverride = ParsePaneBgOverride(g_pendingPaneBg, &c);
        if (!isOverride) {
          safe_strncpy(g_pendingPaneBg, "#000000", sizeof(g_pendingPaneBg));
        } else if (c == RGB(0, 0, 0)) {
          if (!PickCustomPaneBg(dlg))
            safe_strncpy(g_pendingPaneBg, "auto", sizeof(g_pendingPaneBg));
        } else {
          safe_strncpy(g_pendingPaneBg, "auto", sizeof(g_pendingPaneBg));
        }
        char label[48];
        PaneBgButtonLabel(g_pendingPaneBg, label, sizeof(label));
        SetDlgItemText(dlg, IDC_SET_PANEBG_CYCLE, label);
        return 0;
      }

      if (id == IDC_SET_OPEN_LOG) {
        // v2.5.0 — reveal maxpane_debug.log in the file manager (folder if
        // it does not exist yet). Same path resolution as the writer.
        char path[512];
        MaxPaneDebugLogPath(path, sizeof(path));
        RevealFileInFolderPlatform(path);
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
