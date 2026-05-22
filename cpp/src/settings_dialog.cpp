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

#include <cstring>

namespace {

// Dark-mode cycle state — read from ExtState on dialog open, mutated by
// the cycle button, written back on OK. Order: auto → dark → light → auto.
enum DarkMode { DM_AUTO = 0, DM_DARK = 1, DM_LIGHT = 2, DM_COUNT = 3 };

DarkMode ReadDarkMode()
{
  const char* dm = g_GetExtState ? g_GetExtState("MaxPane_cpp", "dark_mode") : nullptr;
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
  const char* v = g_GetExtState("MaxPane_cpp", "show_nav_bar");
  if (v && v[0] == '0' && v[1] == '\0') return false;
  return true;
}

void LoadValues(HWND dlg)
{
  CheckDlgButton(dlg, IDC_SET_AUTOOPEN, IsAutoOpenEnabled() ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(dlg, IDC_SET_SHOWNAVBAR, ReadShowNavBar() ? BST_CHECKED : BST_UNCHECKED);
  g_pendingDarkMode = ReadDarkMode();
  SetDlgItemText(dlg, IDC_SET_DARK_CYCLE, DarkModeButtonLabel(g_pendingDarkMode));
}

void CommitValues(HWND dlg)
{
  SetAutoOpenEnabled(IsDlgButtonChecked(dlg, IDC_SET_AUTOOPEN) == BST_CHECKED);
  if (g_SetExtState) {
    g_SetExtState("MaxPane_cpp", "dark_mode", DarkModeExtStateValue(g_pendingDarkMode), true);
    InvalidateMaxPaneDarkModeCache();
    const bool showNav = (IsDlgButtonChecked(dlg, IDC_SET_SHOWNAVBAR) == BST_CHECKED);
    g_SetExtState("MaxPane_cpp", "show_nav_bar", showNav ? "1" : "0", true);
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

      if (id == IDC_SET_OPEN_ACTIONS) {
        if (g_Main_OnCommand) g_Main_OnCommand(40605, 0);
        return 0;
      }

      if (id == IDC_SET_RESET) {
        if (g_SetExtState) {
          g_SetExtState("MaxPane_cpp", "auto_open", "1", true);
          g_SetExtState("MaxPane_cpp", "dark_mode", "auto", true);
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
  DialogBoxParam(nullptr, MAKEINTRESOURCE(IDD_SETTINGS),
                 parent, SettingsDialogProc, 0);
}

#ifndef _WIN32
#include "swell/swell-dlggen.h"
#include "settings_dialog.rc_mac_dlg"
#endif
