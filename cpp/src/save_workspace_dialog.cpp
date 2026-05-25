// Save Workspace dialog implementation (Sprint 3.3 / ADR-020).
//
// Dialog template definition lives in cpp/resources/save_workspace_dialog.rc
// — on macOS/Linux processed by swell_resgen.pl into .rc_mac_dlg, which we
// include at the bottom of this file (must be at TU scope, after function
// definitions, per SWELL convention). On Windows the .rc is compiled
// directly by rc.exe and linked as a Win32 resource.

#include "save_workspace_dialog.h"
#include "../resources/save_workspace_dialog.h"
#include "workspace_manager.h"
#include "config.h"
#include "globals.h"

#include <cstdio>
#include <cstring>

namespace {

struct SaveDialogState {
  const WorkspaceManager* wsMgr;
  char outName[MAX_WORKSPACE_NAME];
  bool accepted;
};

// Refresh the dynamic status label based on the current name field.
void UpdateStatus(HWND dlg)
{
  SaveDialogState* st = (SaveDialogState*)GetWindowLongPtr(dlg, GWLP_USERDATA);
  if (!st) return;

  char buf[MAX_WORKSPACE_NAME];
  buf[0] = '\0';
  GetWindowText(GetDlgItem(dlg, IDC_SAVE_NAME), buf, sizeof(buf));

  HWND label = GetDlgItem(dlg, IDC_SAVE_STATUS);
  if (!buf[0]) {
    SetWindowText(label, "");
    return;
  }
  if (st->wsMgr->Find(buf)) {
    char msg[MAX_WORKSPACE_NAME + 64];
    snprintf(msg, sizeof(msg), "Will replace existing workspace '%s'", buf);
    SetWindowText(label, msg);
  } else {
    SetWindowText(label, "Will save as new workspace");
  }
}

INT_PTR CALLBACK SaveDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg) {
    case WM_INITDIALOG: {
      SaveDialogState* st = (SaveDialogState*)lParam;
      SetWindowLongPtr(dlg, GWLP_USERDATA, (LONG_PTR)st);

      HWND list = GetDlgItem(dlg, IDC_SAVE_LIST);
      for (int i = 0; i < st->wsMgr->GetCount(); i++) {
        const WorkspaceEntry& ws = st->wsMgr->Get(i);
        if (ws.used) {
          SendMessage(list, LB_ADDSTRING, 0, (LPARAM)ws.name);
        }
      }
      UpdateStatus(dlg);
      SetFocus(GetDlgItem(dlg, IDC_SAVE_NAME));
      return 0;  // we set focus explicitly
    }

    case WM_COMMAND: {
      const int id    = LOWORD(wParam);
      const int notif = HIWORD(wParam);

      if (id == IDC_SAVE_NAME && notif == EN_CHANGE) {
        UpdateStatus(dlg);
        return 0;
      }

      if (id == IDC_SAVE_LIST && notif == LBN_SELCHANGE) {
        HWND list = GetDlgItem(dlg, IDC_SAVE_LIST);
        const int sel = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
        if (sel >= 0) {
          char name[MAX_WORKSPACE_NAME];
          name[0] = '\0';
          SendMessage(list, LB_GETTEXT, sel, (LPARAM)name);
          SetWindowText(GetDlgItem(dlg, IDC_SAVE_NAME), name);
          UpdateStatus(dlg);
        }
        return 0;
      }

      if (id == 1 /* IDOK */) {
        SaveDialogState* st = (SaveDialogState*)GetWindowLongPtr(dlg, GWLP_USERDATA);
        if (st) {
          char buf[MAX_WORKSPACE_NAME];
          buf[0] = '\0';
          GetWindowText(GetDlgItem(dlg, IDC_SAVE_NAME), buf, sizeof(buf));
          if (buf[0]) {
            safe_strncpy(st->outName, buf, sizeof(st->outName));
            st->accepted = true;
            EndDialog(dlg, 1);
          }
        }
        return 0;
      }

      if (id == 2 /* IDCANCEL */) {
        SaveDialogState* st = (SaveDialogState*)GetWindowLongPtr(dlg, GWLP_USERDATA);
        if (st) st->accepted = false;
        EndDialog(dlg, 2);
        return 0;
      }
      break;
    }

    case WM_CLOSE: {
      SaveDialogState* st = (SaveDialogState*)GetWindowLongPtr(dlg, GWLP_USERDATA);
      if (st) st->accepted = false;
      EndDialog(dlg, 2);
      return 0;
    }
  }
  return 0;
}

}  // namespace

bool OpenSaveWorkspaceDialog(HWND parent, const WorkspaceManager& wsMgr,
                             char* outName, int outNameSize)
{
  if (!outName || outNameSize <= 0) return false;

  SaveDialogState st = {};
  st.wsMgr = &wsMgr;
  st.accepted = false;

  DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_SAVE_WORKSPACE),
                 parent, SaveDialogProc, (LPARAM)&st);

  if (st.accepted && st.outName[0]) {
    safe_strncpy(outName, st.outName, (size_t)outNameSize);
    return true;
  }
  return false;
}

// SWELL dialog template registration — must be at translation-unit scope,
// after the DlgProc, so SWELL_DEFINE_DIALOG_RESOURCE_* macros can register
// the template into the module's resource head. On Windows the .rc is
// compiled by rc.exe and linked directly; no include needed.
#ifndef _WIN32
// SWELL doesn't define LBS_NOTIFY (listbox notifies by default on SWELL).
// Provide the Win32 value so the .rc_mac_dlg compiles; SWELL ignores it.
#ifndef LBS_NOTIFY
#define LBS_NOTIFY 0x0001L
#endif
#include "swell/swell-dlggen.h"
#include "save_workspace_dialog.rc_mac_dlg"
#endif
