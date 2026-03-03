#include "container.h"
#include "config.h"
#include "globals.h"
#include "debug.h"
#include "capture_queue.h"
#include "favorites_manager.h"
#include "workspace_manager.h"
#include "context_menu.h"
#include "project_state.h"
#include "state_accessor.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Expand dirty rect to include src (in-place union, handles empty dst/src)
static void ExpandRect(RECT& dst, const RECT& src)
{
  if (src.right <= src.left || src.bottom <= src.top) return;
  if (dst.right <= dst.left || dst.bottom <= dst.top) { dst = src; return; }
  if (src.left   < dst.left)   dst.left   = src.left;
  if (src.top    < dst.top)    dst.top    = src.top;
  if (src.right  > dst.right)  dst.right  = src.right;
  if (src.bottom > dst.bottom) dst.bottom = src.bottom;
}

// =========================================================================
// Constructor / lifecycle
// =========================================================================

MaxPaneContainer::MaxPaneContainer()
  : m_hwnd(nullptr)
  , m_visible(false)
  , m_captureQueue(std::make_unique<CaptureQueue>())
  , m_favMgr(std::make_unique<FavoritesManager>())
  , m_wsMgr(std::make_unique<WorkspaceManager>())
  , m_hoverSplitter(-1)
  , m_hoverPane(-1)
  , m_hoverTab(-1)
  , m_pendingRppLoad(false)
{
  m_captureMode.active = false;
  m_captureMode.targetPaneId = -1;
  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  m_winMgr.Init();

  // Create cached GDI brushes for static colors
  m_brushTabBarBg = CreateSolidBrush(COLOR_TAB_BAR_BG);
  m_brushTabActive = CreateSolidBrush(COLOR_TAB_ACTIVE_BG);
  m_brushTabInactive = CreateSolidBrush(COLOR_TAB_INACTIVE_BG);
  m_brushEmptyHeader = CreateSolidBrush(COLOR_EMPTY_HEADER_BG);
}

MaxPaneContainer::~MaxPaneContainer()
{
  Shutdown();
  SafeDeleteBrush(m_brushTabBarBg);
  SafeDeleteBrush(m_brushTabActive);
  SafeDeleteBrush(m_brushTabInactive);
  SafeDeleteBrush(m_brushEmptyHeader);
}

bool MaxPaneContainer::Create()
{
  if (m_hwnd) return true;

  m_hwnd = CreateMaxPaneDialog(g_reaperMainHwnd, DlgProc, (LPARAM)this);
  if (!m_hwnd) return false;

  SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
  m_favMgr->Load();
  LoadState();

  if (g_DockWindowAddEx) {
    g_DockWindowAddEx(m_hwnd, "MaxPane", "MaxPane_container", true);
  }

  SetTimer(m_hwnd, TIMER_ID_CHECK, TIMER_INTERVAL, nullptr);
  ShowWindow(m_hwnd, SW_SHOW);
  m_visible = true;

  if (g_SetExtState) {
    g_SetExtState("MaxPane_cpp", "was_visible", "1", true);
  }

  return true;
}

// Defined in main.cpp — set by onAtExit to prevent Shutdown from overwriting with empty panes
extern "C" bool g_atexitSaved;

void MaxPaneContainer::Shutdown()
{
  if (!m_hwnd) return;

  DBG("[MaxPane] Shutdown: starting, hwnd=%p atexitSaved=%d\n", m_hwnd, g_atexitSaved);

  if (!g_atexitSaved) {
    SaveState();
  }

  if (m_captureMode.active) {
    KillTimer(m_hwnd, TIMER_ID_CAPTURE);
    m_captureMode.active = false;
  }
  m_captureQueue->CancelAll();

  // Log what we're about to release
  for (int p = 0; p < MAX_PANES; p++) {
    const PaneState* ps = m_winMgr.GetPaneState(p);
    if (ps) {
      for (int t = 0; t < ps->tabCount; t++) {
        if (ps->tabs[t].captured) {
          DBG("[MaxPane] Shutdown: releasing pane %d tab %d '%s' action=%d toggleState=%d\n",
              p, t, ps->tabs[t].name ? ps->tabs[t].name : "(null)",
              ps->tabs[t].toggleAction,
              (g_GetToggleCommandState && ps->tabs[t].toggleAction > 0)
                ? g_GetToggleCommandState(ps->tabs[t].toggleAction) : -1);
        }
      }
    }
  }

  m_winMgr.ReleaseAll();
  KillTimer(m_hwnd, TIMER_ID_CHECK);
  DBG("[MaxPane] Shutdown: complete\n");

  if (g_DockWindowRemove) {
    g_DockWindowRemove(m_hwnd);
  }

  DestroyWindow(m_hwnd);
  m_hwnd = nullptr;
  m_visible = false;
}

void MaxPaneContainer::Show()
{
  if (!m_hwnd) { Create(); return; }
  ShowWindow(m_hwnd, SW_SHOW);
  m_visible = true;
}

void MaxPaneContainer::Toggle()
{
  if (!m_hwnd) { Create(); return; }
  DBG("[MaxPane] Toggle: closing\n");
  // User explicitly closing — mark as not visible for next startup
  if (g_SetExtState) {
    g_SetExtState("MaxPane_cpp", "was_visible", "0", true);
  }
  Shutdown();
}

bool MaxPaneContainer::IsVisible() const
{
  return m_hwnd && IsWindowVisible(m_hwnd);
}

// =========================================================================
// RefreshLayout
// =========================================================================

void MaxPaneContainer::RefreshLayout()
{
  if (!m_hwnd) return;
  RECT rc;
  GetClientRect(m_hwnd, &rc);
  m_tree.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
  m_winMgr.RepositionAll(m_tree);
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

// =========================================================================
// Capture timer helpers
// =========================================================================

void MaxPaneContainer::StartCaptureTimer()
{
  SetTimer(m_hwnd, TIMER_ID_CAPTURE, TIMER_CAPTURE_INTERVAL, nullptr);
}

void MaxPaneContainer::StopCaptureTimerIfIdle()
{
  if (!m_captureMode.active && !m_captureQueue->HasPending()) {
    KillTimer(m_hwnd, TIMER_ID_CAPTURE);
  }
}

// =========================================================================
// Layout preset / Split / Merge
// =========================================================================

void MaxPaneContainer::ApplyPreset(LayoutPreset preset)
{
  if (preset < 0 || preset >= PRESET_COUNT) return;

  // Release all windows before rebuilding tree
  m_winMgr.ReleaseAll();

  m_tree.BuildPreset(preset);

  RefreshLayout();
  SaveState();
}

void MaxPaneContainer::SplitPane(int paneId, SplitterOrientation orient)
{
  int nodeIdx = m_tree.NodeForPane(paneId);
  if (nodeIdx < 0) return;
  if (m_tree.GetLeafCount() >= MAX_LEAVES) return;

  m_tree.SplitLeaf(nodeIdx, orient, 0.5f);
  RefreshLayout();
  SaveState();
}

void MaxPaneContainer::MergePane(int paneId)
{
  int nodeIdx = m_tree.NodeForPane(paneId);
  if (nodeIdx < 0) return;
  if (!m_tree.CanMerge(nodeIdx)) return;

  // Release the pane being merged
  m_winMgr.ReleaseWindow(paneId);

  m_tree.MergeNode(nodeIdx);
  RefreshLayout();
  SaveState();
}

// =========================================================================
// Timer
// =========================================================================

void MaxPaneContainer::OnTimer()
{
  // Check if deferred RPP state has become available.
  // REAPER parses the RPP <MAXPANE_STATE> chunk asynchronously —
  // it may arrive after Create()/LoadState() already ran.
  if (m_pendingRppLoad && g_pendingProjectState.valid) {
    DBG("[MaxPane] OnTimer: deferred RPP state now available (%d lines), applying\n",
        g_pendingProjectState.lineCount);
    m_pendingRppLoad = false;

    RppReadAccessor rppAcc(g_pendingProjectState.lines, g_pendingProjectState.lineCount);
    const char* treeVer = rppAcc.Get(EXT_SECTION, "tree_version");
    bool hasTreeFormat = (treeVer && strcmp(treeVer, "2") == 0);
    if (hasTreeFormat) {
      NodeSnapshot snap[MAX_TREE_NODES];
      memset(snap, 0, sizeof(snap));
      int nodeCount = WorkspaceManager::ReadTreeNodesStatic("", snap, rppAcc);
      if (nodeCount > 0) {
        PaneSnapshot panes[MAX_PANES];
        memset(panes, 0, sizeof(panes));
        WorkspaceManager::ReadPaneTabsStatic("", panes, MAX_PANES, rppAcc);

        // Release any windows from default state before applying RPP state
        m_captureQueue->CancelAll();
        m_winMgr.ReleaseAll(false);

        if (!m_tree.LoadSnapshot(snap, nodeCount)) {
          DBG("[MaxPane] OnTimer: deferred RPP tree corrupt, resetting\n");
          m_tree.Reset();
        }

        RECT rc;
        GetClientRect(m_hwnd, &rc);
        m_tree.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
        ApplyPaneState(panes, MAX_PANES, true);
        m_winMgr.RepositionAll(m_tree);
        InvalidateRect(m_hwnd, nullptr, TRUE);

        DBG("[MaxPane] OnTimer: deferred RPP state applied (nodes=%d)\n", nodeCount);
      }
    }
    g_pendingProjectState.valid = false;  // consumed
  }

  m_winMgr.CheckAlive();
}

// =========================================================================
// Context menu (builds menu via context_menu module, dispatches commands)
// =========================================================================

void MaxPaneContainer::OnContextMenu(int x, int y)
{
  int paneId = PaneAtPoint(x, y);
  if (paneId < 0) return;

  // Check if click is on tab bar
  int tabIdx = TabHitTest(paneId, x, y);
  if (tabIdx >= 0) {
    HMENU menu = BuildTabContextMenu(paneId, tabIdx, m_tree, m_winMgr, *m_favMgr);
    if (!menu) return;

    POINT pt = {x, y};
    ClientToScreen(m_hwnd, &pt);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    HandleTabMenuCommand(cmd, paneId, tabIdx);
    return;
  }

  // If in capture mode and user clicked inside the container, cancel
  if (m_captureMode.active) {
    KillTimer(m_hwnd, TIMER_ID_CAPTURE);
    m_captureMode.active = false;
    InvalidateRect(m_hwnd, nullptr, TRUE);
    return;
  }

  m_wsMgr->LoadList();
  HMENU menu = BuildPaneContextMenu(paneId, m_hwnd, m_tree, m_winMgr, *m_favMgr, *m_wsMgr);
  if (!menu) return;

  POINT pt = {x, y};
  ClientToScreen(m_hwnd, &pt);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
  DestroyMenu(menu);

  HandlePaneMenuCommand(cmd, paneId);
}

void MaxPaneContainer::OnPaneMenuButtonClick(int paneId, int x, int y)
{
  m_wsMgr->LoadList();
  HMENU menu = BuildPaneContextMenu(paneId, m_hwnd, m_tree, m_winMgr, *m_favMgr, *m_wsMgr);
  if (!menu) return;

  POINT pt = {x, y};
  ClientToScreen(m_hwnd, &pt);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
  DestroyMenu(menu);

  HandlePaneMenuCommand(cmd, paneId);
}

void MaxPaneContainer::HandleTabMenuCommand(int cmd, int paneId, int tabIdx)
{
  if (cmd == MenuIds::TAB_CLOSE) {
    m_winMgr.CloseTab(paneId, tabIdx);
    RefreshLayout();
    SaveState();
  } else if (cmd >= MenuIds::TAB_MOVE_BASE && cmd < MenuIds::TAB_MOVE_BASE + MAX_PANES) {
    int targetPane = cmd - MenuIds::TAB_MOVE_BASE;
    m_winMgr.MoveTab(paneId, tabIdx, targetPane);
    RefreshLayout();
    SaveState();
  } else if (cmd >= MenuIds::TAB_COLOR_BASE && cmd < MenuIds::TAB_COLOR_BASE + TAB_COLOR_COUNT) {
    int newColor = cmd - MenuIds::TAB_COLOR_BASE;
    m_winMgr.SetTabColor(paneId, tabIdx, newColor);
    // Targeted: only invalidate the changed tab's rect
    RECT tr = GetTabRect(paneId, tabIdx);
    if (tr.right > tr.left) InvalidateRect(m_hwnd, &tr, TRUE);
    else InvalidateRect(m_hwnd, nullptr, TRUE);
    SaveState();
  } else if (cmd == MenuIds::FAV_ADD) {
    const TabEntry* tab = m_winMgr.GetTab(paneId, tabIdx);
    if (tab && tab->captured && tab->name) {
      char actionCmd[128] = "";
      if (tab->isArbitrary) {
        if (tab->arbitraryActionCmd[0]) {
          safe_strncpy(actionCmd, tab->arbitraryActionCmd, sizeof(actionCmd));
        } else if (g_GetUserInputs) {
          char inputBuf[128] = "";
          if (g_GetUserInputs("Add to Favorites", 1,
                              "Action ID or _command (for auto-reopen):",
                              inputBuf, sizeof(inputBuf))) {
            if (inputBuf[0]) {
              safe_strncpy(actionCmd, inputBuf, sizeof(actionCmd));
            }
          }
        }
      } else {
        GetActionCommandString(tab->toggleAction, actionCmd, sizeof(actionCmd));
      }
      m_favMgr->Add(tab->name,
                     tab->searchTitle ? tab->searchTitle : tab->name,
                     actionCmd,
                     !tab->isArbitrary);
    }
  }
}

void MaxPaneContainer::HandlePaneMenuCommand(int cmd, int paneId)
{
  // Layout preset
  if (cmd >= MenuIds::LAYOUT_BASE && cmd < MenuIds::LAYOUT_BASE + PRESET_COUNT) {
    ApplyPreset((LayoutPreset)(cmd - MenuIds::LAYOUT_BASE));
    return;
  }

  // Known window selection — async capture
  if (cmd >= MenuIds::KNOWN_BASE && cmd < MenuIds::KNOWN_BASE + NUM_KNOWN_WINDOWS) {
    int idx = cmd - MenuIds::KNOWN_BASE;

    DBG("[MaxPane] Menu: selected '%s' for pane %d\n", KNOWN_WINDOWS[idx].name, paneId);

    HWND found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[idx].searchTitle, m_hwnd);
    if (!found && KNOWN_WINDOWS[idx].altSearchTitle) {
      found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[idx].altSearchTitle, m_hwnd);
    }

    if (found) {
      m_winMgr.CaptureByIndex(paneId, idx, m_hwnd);
      RefreshLayout();
      SaveState();
    } else if (g_Main_OnCommand) {
      m_captureQueue->EnqueueKnown(paneId, idx);
      StartCaptureTimer();
    }
    return;
  }

  // Open Windows selection
  if (cmd >= MenuIds::OPEN_WINDOWS_BASE && cmd < MenuIds::OPEN_WINDOWS_MAX) {
    int idx = cmd - MenuIds::OPEN_WINDOWS_BASE;
    if (idx >= 0 && idx < GetOpenWindowCount()) {
      const OpenWindowEntry& owe = GetOpenWindow(idx);
      HWND targetHwnd = owe.hwnd;
      char title[256];
      safe_strncpy(title, owe.title, sizeof(title));

      HWND dockFrame = nullptr;
      char* docked = strstr(title, " (docked)");
      if (docked) {
        *docked = '\0';
        dockFrame = targetHwnd;
        HWND child = WindowManager::FindChildInParent(targetHwnd, title);
        if (child) {
          targetHwnd = child;
        } else {
          dockFrame = nullptr;
        }
      }
      if (targetHwnd && IsWindow(targetHwnd)) {
        m_winMgr.CaptureArbitraryWindow(paneId, targetHwnd, title, m_hwnd);

        if (dockFrame && IsWindow(dockFrame)) {
          ShowWindow(dockFrame, SW_HIDE);
        }

        RefreshLayout();
        SaveState();
      }
    }
    return;
  }

  // Capture by Click
  if (cmd == MenuIds::CAPTURE_BY_CLICK) {
    m_captureMode.active = true;
    m_captureMode.targetPaneId = paneId;
    InvalidateRect(m_hwnd, nullptr, TRUE);
    StartCaptureTimer();
    return;
  }

  // Split Horizontal
  if (cmd == MenuIds::SPLIT_H) {
    SplitPane(paneId, SPLIT_HORIZONTAL);
    return;
  }

  // Split Vertical
  if (cmd == MenuIds::SPLIT_V) {
    SplitPane(paneId, SPLIT_VERTICAL);
    return;
  }

  // Merge with Sibling
  if (cmd == MenuIds::MERGE) {
    MergePane(paneId);
    return;
  }

  // Delete Pane (release all tabs + merge)
  if (cmd == MenuIds::DELETE_PANE) {
    MergePane(paneId);  // MergePane already calls ReleaseWindow which releases all tabs
    return;
  }

  // Close (release active tab only)
  if (cmd == MenuIds::RELEASE) {
    const PaneState* ps = m_winMgr.GetPaneState(paneId);
    if (ps && ps->activeTab >= 0) {
      m_winMgr.CloseTab(paneId, ps->activeTab);
    }
    RefreshLayout();
    SaveState();
    return;
  }

  // Auto-open toggle
  if (cmd == MenuIds::AUTO_OPEN) {
    SetAutoOpenEnabled(!IsAutoOpenEnabled());
    return;
  }

  // Close MaxPane container
  if (cmd == MenuIds::CLOSE_CONTAINER) {
    Shutdown();
    return;
  }

  // Favorites — capture from favorite
  if (cmd >= MenuIds::FAV_BASE && cmd < MenuIds::FAV_BASE + MAX_FAVORITES) {
    int favIdx = cmd - MenuIds::FAV_BASE;
    if (favIdx >= 0 && favIdx < m_favMgr->GetCount()) {
      const FavoriteEntry& fav = m_favMgr->Get(favIdx);
      DBG("[MaxPane] FAV CLICK: '%s' search='%s' action=%d cmd='%s' isKnown=%d pane=%d\n",
          fav.name, fav.searchTitle, fav.toggleAction, fav.actionCommand, fav.isKnown, paneId);

      HWND found = WindowManager::FindReaperWindow(fav.searchTitle, m_hwnd);
      DBG("[MaxPane] FAV CLICK: FindReaperWindow('%s') -> %p\n", fav.searchTitle, (void*)found);
      if (found && fav.isKnown) {
        // Known windows don't need dock frame logic — capture directly
        DBG("[MaxPane] FAV CLICK: known window found, capturing directly\n");
        for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
          if (strcmp(KNOWN_WINDOWS[j].name, fav.name) == 0) {
            m_winMgr.CaptureByIndex(paneId, j, m_hwnd);
            break;
          }
        }
        RefreshLayout();
        SaveState();
      } else if (found && !fav.isKnown) {
        // Arbitrary window found — check if it's a dock frame or inner window.
        // Always go through CaptureQueue so dock frame wait logic applies.
        char foundTitle[512];
        GetWindowText(found, foundTitle, sizeof(foundTitle));
        bool isDockFrame = (strstr(foundTitle, "(docked)") != nullptr);
        if (isDockFrame) {
          // Dock frame found — capture directly, it has the UI
          DBG("[MaxPane] FAV CLICK: dock frame found, capturing directly\n");
          m_winMgr.CaptureArbitraryWindow(paneId, found, fav.name, m_hwnd,
                                           fav.toggleAction, fav.actionCommand);
          RefreshLayout();
          SaveState();
        } else {
          // Inner window found — enqueue via CaptureQueue to wait for dock frame
          DBG("[MaxPane] FAV CLICK: inner window found, enqueue to wait for dock frame\n");
          m_captureQueue->EnqueueArbitrary(paneId, fav.searchTitle, fav.toggleAction, fav.actionCommand);
          StartCaptureTimer();
        }
      } else if (fav.toggleAction > 0) {
        DBG("[MaxPane] FAV CLICK: window not found, has toggle action=%d -> enqueue + fire action\n",
            fav.toggleAction);
        if (fav.isKnown) {
          for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
            if (strcmp(KNOWN_WINDOWS[j].name, fav.name) == 0) {
              m_captureQueue->EnqueueKnown(paneId, j);
              break;
            }
          }
        } else {
          m_captureQueue->EnqueueArbitrary(paneId, fav.searchTitle, fav.toggleAction, fav.actionCommand);
        }
        StartCaptureTimer();
      } else {
        DBG("[MaxPane] FAV CLICK: window not found, NO toggle action -> enqueue for polling\n");
        if (fav.isKnown) {
          for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
            if (strcmp(KNOWN_WINDOWS[j].name, fav.name) == 0) {
              m_captureQueue->EnqueueArbitrary(paneId, fav.searchTitle, 0, fav.actionCommand);
              break;
            }
          }
        } else {
          m_captureQueue->EnqueueArbitrary(paneId, fav.searchTitle, 0, fav.actionCommand);
        }
        StartCaptureTimer();
      }
    }
    return;
  }

  // Favorites — delete
  if (cmd >= MenuIds::FAV_DELETE_BASE && cmd < MenuIds::FAV_DELETE_BASE + MAX_FAVORITES) {
    int favIdx = cmd - MenuIds::FAV_DELETE_BASE;
    m_favMgr->Remove(favIdx);
    return;
  }

  // Workspace load
  if (cmd >= MenuIds::WS_LOAD_BASE && cmd < MenuIds::WS_LOAD_BASE + MAX_WORKSPACES) {
    int idx = cmd - MenuIds::WS_LOAD_BASE;
    if (idx >= 0 && idx < m_wsMgr->GetCount()) {
      const WorkspaceEntry& ws = m_wsMgr->Get(idx);
      if (ws.used) {
        LoadWorkspace(ws.name);
      }
    }
    return;
  }

  // Workspace save
  if (cmd == MenuIds::WS_SAVE) {
    if (g_GetUserInputs) {
      char wsName[MAX_WORKSPACE_NAME] = "";
      if (g_GetUserInputs("Save Workspace", 1, "Name:", wsName, sizeof(wsName))) {
        if (wsName[0]) {
          SaveWorkspace(wsName);
        }
      }
    }
    return;
  }

  // Workspace delete
  if (cmd >= MenuIds::WS_DELETE_BASE && cmd < MenuIds::WS_DELETE_BASE + MAX_WORKSPACES) {
    int idx = cmd - MenuIds::WS_DELETE_BASE;
    if (idx >= 0 && idx < m_wsMgr->GetCount()) {
      const WorkspaceEntry& ws = m_wsMgr->Get(idx);
      if (ws.used) {
        DeleteWorkspace(ws.name);
      }
    }
    return;
  }
}

// =========================================================================
// Dialog Procedure
// =========================================================================

INT_PTR CALLBACK MaxPaneContainer::DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  MaxPaneContainer* self = (MaxPaneContainer*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

  switch (msg) {
    case WM_INITDIALOG: {
      self = (MaxPaneContainer*)lParam;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
      if (self) self->m_hwnd = hwnd;
      return 0;
    }

    case WM_SIZE: {
      if (self) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int cx = rc.right - rc.left;
        int cy = rc.bottom - rc.top;
        if (cx > 0 && cy > 0) {
          self->OnSize(cx, cy);
        }
      }
      return 0;
    }

    case WM_PAINT: {
      if (self) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        self->OnPaint(hdc);
        EndPaint(hwnd, &ps);
      }
      return 0;
    }

    case WM_LBUTTONDOWN: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);

        int splitter = self->m_tree.HitTestSplitter(x, y);
        if (splitter >= 0) {
          self->m_tree.StartDrag(splitter);
          SetCapture(hwnd);
          return 0;
        }

        // Check tab clicks across all leaf panes
        for (int li = 0; li < self->m_tree.GetLeafCount(); li++) {
          int paneId = self->m_tree.GetPaneId(self->m_tree.GetLeafList()[li]);
          if (paneId < 0) continue;

          int tabIdx = self->TabHitTest(paneId, x, y);
          if (tabIdx == -2) {
            // Pane menu button ▼
            self->OnPaneMenuButtonClick(paneId, x, y);
            return 0;
          } else if (tabIdx >= 0) {
            if (self->IsOnTabCloseButton(paneId, tabIdx, x, y)) {
              self->m_winMgr.CloseTab(paneId, tabIdx);
              self->RefreshLayout();
              self->SaveState();
            } else {
              self->m_winMgr.SetActiveTab(paneId, tabIdx);
              self->RefreshLayout();
              self->StartTabDrag(paneId, tabIdx, x, y);
            }
            return 0;
          }
        }

        // Check if click is on an empty pane's header bar
        for (int li = 0; li < self->m_tree.GetLeafCount(); li++) {
          int paneId = self->m_tree.GetPaneId(self->m_tree.GetLeafList()[li]);
          if (paneId < 0) continue;

          const PaneState* ps = self->m_winMgr.GetPaneState(paneId);
          if (!ps || ps->tabCount == 0) {
            const RECT& r = self->m_tree.GetPaneRect(paneId);
            if (x >= r.left && x < r.right && y >= r.top && y < r.top + TAB_BAR_HEIGHT) {
              self->OnContextMenu(x, y);
              return 0;
            }
          }
        }
      }
      return 0;
    }

    case WM_MOUSEMOVE: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnMouseMove(x, y);
      }
      return 0;
    }


    case WM_LBUTTONUP: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnLButtonUp(x, y);
      }
      return 0;
    }

    case WM_NCHITTEST:
      return HTCLIENT;

    case WM_SETCURSOR: {
      if (self && (HWND)wParam == hwnd) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        int splitter = self->m_tree.HitTestSplitter(pt.x, pt.y);
        if (splitter >= 0) {
          if (self->m_tree.GetSplitterOrientation(splitter) == SPLIT_VERTICAL) {
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
          } else {
            SetCursor(LoadCursor(nullptr, IDC_SIZENS));
          }
          return 1;
        }
      }
      break;
    }

    case WM_LBUTTONDBLCLK: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnContextMenu(x, y);
      }
      return 0;
    }

    case WM_RBUTTONUP: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnContextMenu(x, y);
      }
      return 0;
    }

    case WM_RBUTTONDOWN: {
      return 0;
    }

    case WM_CONTEXTMENU: {
      if (self) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        self->OnContextMenu(pt.x, pt.y);
      }
      return 0;
    }

    case WM_TIMER: {
      if (self && wParam == TIMER_ID_CHECK) {
        self->OnTimer();
      }
      else if (self && wParam == TIMER_ID_HOVER) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (pt.x < rc.left || pt.x >= rc.right || pt.y < rc.top || pt.y >= rc.bottom) {
          // Cache hover positions before clearing — for targeted invalidation
          int oldSplitter = self->m_hoverSplitter;
          int oldPane     = self->m_hoverPane;
          int oldTab      = self->m_hoverTab;
          self->m_hoverSplitter = -1;
          self->m_hoverPane = -1;
          self->m_hoverTab = -1;
          KillTimer(hwnd, TIMER_ID_HOVER);

          RECT dirty = {};
          // Splitter hover
          if (oldSplitter >= 0)
            ExpandRect(dirty, self->m_tree.GetNode(oldSplitter).splitterRect);
          // Tab or menu button hover
          if (oldPane >= 0 && oldTab >= 0)
            ExpandRect(dirty, self->GetTabRect(oldPane, oldTab));
          else if (oldPane >= 0 && oldTab == -2) {
            const RECT& pr = self->m_tree.GetPaneRect(oldPane);
            RECT btnR = { pr.right - PANE_MENU_BTN_WIDTH, pr.top,
                          pr.right, pr.top + TAB_BAR_HEIGHT };
            ExpandRect(dirty, btnR);
          }

          if (dirty.right > dirty.left) InvalidateRect(hwnd, &dirty, TRUE);
          else InvalidateRect(hwnd, nullptr, TRUE);
        }
      }
      else if (self && wParam == TIMER_ID_CAPTURE) {
        // Handle capture-by-click mode
        if (self->m_captureMode.active) {
          POINT pt;
          GetCursorPos(&pt);
          short mouseState = GetAsyncKeyState(VK_LBUTTON);
          if (mouseState & 0x8000) {
            HWND underCursor = WindowFromPoint(pt);
            if (underCursor && underCursor != self->m_hwnd &&
                underCursor != g_reaperMainHwnd &&
                !self->m_winMgr.IsWindowCaptured(underCursor)) {
              HWND topLevel = underCursor;
              HWND parent = GetParent(topLevel);
              while (parent && parent != g_reaperMainHwnd) {
                topLevel = parent;
                parent = GetParent(topLevel);
              }

              if (topLevel && topLevel != self->m_hwnd && topLevel != g_reaperMainHwnd) {
                char title[256];
                GetWindowText(topLevel, title, sizeof(title));
                if (title[0]) {
                  HWND captureHwnd = topLevel;
                  HWND dockFrame = nullptr;
                  char* dockedSuffix = strstr(title, " (docked)");
                  if (dockedSuffix) {
                    *dockedSuffix = '\0';
                    HWND child = WindowManager::FindChildInParent(topLevel, title);
                    if (child) {
                      captureHwnd = child;
                      dockFrame = topLevel;
                    }
                  }
                  int pId = self->m_captureMode.targetPaneId;
                  self->m_winMgr.CaptureArbitraryWindow(pId, captureHwnd, title, self->m_hwnd);

                  if (dockFrame && IsWindow(dockFrame)) {
                    ShowWindow(dockFrame, SW_HIDE);
                  }

                  self->RefreshLayout();
                  self->SaveState();
                }
              }

              self->m_captureMode.active = false;
              self->StopCaptureTimerIfIdle();
              InvalidateRect(self->m_hwnd, nullptr, TRUE);
            }
          }

          short escState = GetAsyncKeyState(VK_ESCAPE);
          if (escState & 0x8000) {
            self->m_captureMode.active = false;
            self->StopCaptureTimerIfIdle();
            InvalidateRect(self->m_hwnd, nullptr, TRUE);
          }
        }
        // Handle async capture queue
        else if (self->m_captureQueue->HasPending()) {
          if (self->m_captureQueue->Tick(self->m_hwnd, self->m_winMgr)) {
            self->RefreshLayout();
          }
          if (!self->m_captureQueue->HasPending()) {
            self->StopCaptureTimerIfIdle();
            self->SaveState();
          }
        }
        else {
          self->StopCaptureTimerIfIdle();
        }
      }
      return 0;
    }

    case WM_COMMAND: {
      if (self && LOWORD(wParam) == IDCANCEL) {
        self->Toggle();  // Toggle handles was_visible + captured_actions + Shutdown
        return 0;
      }
      break;
    }

    case WM_CLOSE: {
      if (self) {
        self->Toggle();  // Toggle handles was_visible + captured_actions + Shutdown
      }
      return 0;
    }

    case WM_DESTROY: {
      if (self) {
        KillTimer(hwnd, TIMER_ID_CHECK);
        KillTimer(hwnd, TIMER_ID_CAPTURE);
      }
      return 0;
    }
  }

  return 0;
}
