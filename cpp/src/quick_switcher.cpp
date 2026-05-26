// Quick Switcher implementation (F4, Sprint 4 finale).
//
// Modal dialog: search box (EDIT) routes typing into a live fuzzy filter
// over a flat index of [captured tabs across instances, workspaces,
// favorites]. ↑↓ moves the listbox selection without taking focus off
// the EDIT (via WNDPROC subclass on the EDIT control). Enter activates;
// Esc cancels. Double-click in the listbox also activates.

#include "quick_switcher.h"
#include "../resources/quick_switcher.h"
#include "container.h"
#include "instance_manager.h"
#include "window_manager.h"
#include "workspace_manager.h"
#include "favorites_manager.h"
#include "globals.h"
#include "config.h"

#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace {

enum class EntryType { Tab, Workspace, Favorite };

struct IndexEntry {
  EntryType type;
  char label[320];   // displayed in listbox
  int instanceId;    // Tab
  int paneId;        // Tab
  int tabIdx;        // Tab
  int wsIndex;       // Workspace
  int favIdx;        // Favorite
};

struct QSState {
  std::vector<IndexEntry> entries;
  std::vector<int> filtered;  // indices into entries, sorted by score desc
  bool ignoreSelChange = false;
};

// Subclass'd EDIT control's original WndProc — stored at file scope because
// we only ever have one Quick Switcher dialog open at a time (modal).
WNDPROC s_origEditProc = nullptr;

// Case-insensitive subsequence match with prefix + consecutive bonuses.
// Returns 0 if `query` cannot be found as a subsequence in `target`.
// Empty query returns a constant non-zero score so all entries pass.
int FuzzyScore(const char* query, const char* target)
{
  if (!target || !target[0]) return 0;
  if (!query || !query[0]) return 1;
  int qlen = (int)strlen(query);
  int score = 0;
  int qidx = 0;
  int consecutive = 0;
  for (int t = 0; target[t] && qidx < qlen; t++) {
    char qc = (char)tolower((unsigned char)query[qidx]);
    char tc = (char)tolower((unsigned char)target[t]);
    if (qc == tc) {
      score += (t == 0) ? 15 : 5;   // prefix bonus
      score += consecutive;           // streak bonus
      consecutive = 2;
      qidx++;
    } else {
      consecutive = 0;
    }
  }
  return (qidx == qlen) ? score : 0;
}

// Build the flat index over tabs (across all instances), workspaces,
// and favorites. Called once at WM_INITDIALOG — the dialog's lifetime is
// short enough that we don't need to refresh on external changes.
void BuildIndex(QSState& st)
{
  st.entries.clear();

  // Tabs across instances.
  InstanceManager::Get().ForEach([&](int instId, MaxPaneContainer& c) {
    const WindowManager& wm = c.GetWinMgr();
    for (int p = 0; p < MAX_PANES; p++) {
      const PaneState* ps = wm.GetPaneState(p);
      if (!ps) continue;
      for (int t = 0; t < ps->tabCount; t++) {
        if (!ps->tabs[t].captured) continue;
        if (!ps->tabs[t].name[0]) continue;
        IndexEntry e = {};
        e.type = EntryType::Tab;
        e.instanceId = instId;
        e.paneId = p;
        e.tabIdx = t;
        if (instId == 0) {
          snprintf(e.label, sizeof(e.label), "%s", ps->tabs[t].name);
        } else {
          snprintf(e.label, sizeof(e.label), "%s  [Instance %d]",
                   ps->tabs[t].name, instId + 1);
        }
        st.entries.push_back(e);
      }
    }
  });

  // Workspaces + favorites — shared store; pull from focused or first instance.
  MaxPaneContainer* base = InstanceManager::Get().GetFocused();
  if (!base) base = InstanceManager::Get().GetExisting(0);
  if (!base) return;

  const WorkspaceManager& ws = base->GetWsMgr();
  for (int i = 0; i < ws.GetCount(); i++) {
    const WorkspaceEntry& w = ws.Get(i);
    if (!w.used || !w.name[0]) continue;
    IndexEntry e = {};
    e.type = EntryType::Workspace;
    e.wsIndex = i;
    snprintf(e.label, sizeof(e.label), "Workspace: %s", w.name);
    st.entries.push_back(e);
  }

  const FavoritesManager& fav = base->GetFavMgr();
  for (int i = 0; i < fav.GetCount(); i++) {
    const FavoriteEntry& f = fav.Get(i);
    if (!f.used || !f.name[0]) continue;
    IndexEntry e = {};
    e.type = EntryType::Favorite;
    e.favIdx = i;
    snprintf(e.label, sizeof(e.label), "%s %s", "\xe2\x98\x85" /* ★ */, f.name);
    st.entries.push_back(e);
  }
}

void RefreshList(HWND dlg, QSState& st, const char* query)
{
  struct Scored { int idx; int score; };
  std::vector<Scored> scored;
  scored.reserve(st.entries.size());
  for (int i = 0; i < (int)st.entries.size(); i++) {
    int s = FuzzyScore(query, st.entries[i].label);
    if (s > 0) scored.push_back({i, s});
  }
  std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b){
    return a.score > b.score;
  });

  st.filtered.clear();
  st.filtered.reserve(scored.size());
  for (auto& s : scored) st.filtered.push_back(s.idx);

  HWND list = GetDlgItem(dlg, IDC_QS_LIST);
  st.ignoreSelChange = true;
  SendMessage(list, LB_RESETCONTENT, 0, 0);
  for (int idx : st.filtered) {
    SendMessage(list, LB_ADDSTRING, 0, (LPARAM)st.entries[idx].label);
  }
  if (!st.filtered.empty()) {
    SendMessage(list, LB_SETCURSEL, 0, 0);
  }
  st.ignoreSelChange = false;
}

void Activate(HWND dlg, const IndexEntry& e)
{
  switch (e.type) {
    case EntryType::Tab: {
      MaxPaneContainer* c = InstanceManager::Get().GetExisting(e.instanceId);
      if (!c) break;
      c->GetWinMgr().SetActiveTab(e.paneId, e.tabIdx);
      InstanceManager::Get().SetFocused(e.instanceId);
      if (c->GetHwnd()) {
        ShowWindow(c->GetHwnd(), SW_SHOWNA);
        InvalidateRect(c->GetHwnd(), nullptr, FALSE);
      }
      break;
    }
    case EntryType::Workspace: {
      MaxPaneContainer* base = InstanceManager::Get().GetFocused();
      if (!base) base = InstanceManager::Get().GetExisting(0);
      if (!base) break;
      const WorkspaceEntry& w = base->GetWsMgr().Get(e.wsIndex);
      if (w.used) base->LoadWorkspace(w.name);
      break;
    }
    case EntryType::Favorite: {
      MaxPaneContainer* base = InstanceManager::Get().GetFocused();
      if (!base) base = InstanceManager::Get().GetExisting(0);
      if (!base) break;
      base->ActivateFavorite(e.favIdx, -1);
      break;
    }
  }
  EndDialog(dlg, 1);
}

// EDIT control subclass — routes ↑↓ to the LISTBOX so the user can
// navigate results while keeping focus on the search box. Enter and Esc
// fall through to the dialog's default DEFPUSHBUTTON / IDCANCEL routing.
INT_PTR CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_KEYDOWN && (wParam == VK_UP || wParam == VK_DOWN)) {
    HWND dlg = GetParent(hwnd);
    HWND list = GetDlgItem(dlg, IDC_QS_LIST);
    int n = (int)SendMessage(list, LB_GETCOUNT, 0, 0);
    if (n > 0) {
      int cur = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
      if (cur < 0) cur = 0;
      else if (wParam == VK_UP) cur = (cur > 0) ? cur - 1 : 0;
      else                       cur = (cur < n - 1) ? cur + 1 : n - 1;
      SendMessage(list, LB_SETCURSEL, (WPARAM)cur, 0);
    }
    return 0;  // eat
  }
  return CallWindowProc(s_origEditProc, hwnd, msg, wParam, lParam);
}

INT_PTR CALLBACK QSDialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg) {
    case WM_INITDIALOG: {
      QSState* st = (QSState*)lParam;
      SetWindowLongPtr(dlg, GWLP_USERDATA, (LONG_PTR)st);
      BuildIndex(*st);
      RefreshList(dlg, *st, "");

      HWND edit = GetDlgItem(dlg, IDC_QS_SEARCH);
      s_origEditProc = (WNDPROC)SetWindowLongPtr(edit, GWLP_WNDPROC,
                                                 (LONG_PTR)EditSubclassProc);
      SetFocus(edit);
      return 0;  // we set focus explicitly
    }

    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      const int notif = HIWORD(wParam);
      QSState* st = (QSState*)GetWindowLongPtr(dlg, GWLP_USERDATA);
      if (!st) break;

      if (id == IDC_QS_SEARCH && notif == EN_CHANGE) {
        char query[256];
        query[0] = '\0';
        GetWindowText(GetDlgItem(dlg, IDC_QS_SEARCH), query, sizeof(query));
        RefreshList(dlg, *st, query);
        return 0;
      }

      if (id == IDC_QS_LIST && notif == LBN_DBLCLK) {
        HWND list = GetDlgItem(dlg, IDC_QS_LIST);
        int sel = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < (int)st->filtered.size()) {
          Activate(dlg, st->entries[st->filtered[sel]]);
        }
        return 0;
      }

      if (id == IDC_QS_LIST && notif == LBN_SELCHANGE) {
        // Programmatic LB_SETCURSEL after RefreshList fires this — coalesce.
        if (st->ignoreSelChange) return 0;
        return 0;
      }

      if (id == 1 /* IDOK */) {
        HWND list = GetDlgItem(dlg, IDC_QS_LIST);
        int sel = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < (int)st->filtered.size()) {
          Activate(dlg, st->entries[st->filtered[sel]]);
        } else {
          EndDialog(dlg, 2);
        }
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

void OpenQuickSwitcher(HWND parent)
{
  QSState st;
  DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_QUICK_SWITCHER),
                 parent, QSDialogProc, (LPARAM)&st);
  // EDIT subclass is restored implicitly — when the dialog HWND is
  // destroyed, the subclassed WndProc pointer dies with it. No leak.
}

#ifndef _WIN32
// SWELL omits LBS_NOTIFY (listboxes notify by default); the .rc_mac_dlg
// still references the symbol so define it for compilation. Mirrors
// save_workspace_dialog.cpp.
#ifndef LBS_NOTIFY
#define LBS_NOTIFY 0x0001L
#endif
// B-LINUX-MODALS — see settings_dialog.cpp for the full diagnosis. Switch
// to the explicit-scale _BEGIN form via SET_IDD_*_SCALE; the
// SWELL_DLG_WS_DEFAULT_SCALING shim works around the WDL upstream macro
// referencing a symbol that is only defined under SWELL_TARGET_OSX.
#ifndef SWELL_DLG_WS_DEFAULT_SCALING
#define SWELL_DLG_WS_DEFAULT_SCALING 0
#endif
#define SET_IDD_QUICK_SWITCHER_SCALE 1.9
#include "swell/swell-dlggen.h"
#include "quick_switcher.rc_mac_dlg"
#endif
