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

#define TIMER_ID_CHECK 1
#define TIMER_INTERVAL 500
#define TIMER_ID_CAPTURE 2
#define TIMER_CAPTURE_INTERVAL 50
#define TIMER_ID_HOVER 3

// =========================================================================
// Constructor / lifecycle
// =========================================================================

ReDockItContainer::ReDockItContainer()
  : m_hwnd(nullptr)
  , m_visible(false)
  , m_captureQueue(std::make_unique<CaptureQueue>())
  , m_favMgr(std::make_unique<FavoritesManager>())
  , m_wsMgr(std::make_unique<WorkspaceManager>())
  , m_hoverSplitter(-1)
  , m_hoverPane(-1)
  , m_hoverTab(-1)
  , m_shutdownGraceTicks(0)
  , m_pendingRppLoad(false)
{
  m_captureMode.active = false;
  m_captureMode.targetPaneId = -1;
  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  m_winMgr.Init();
}

ReDockItContainer::~ReDockItContainer()
{
  Shutdown();
}

bool ReDockItContainer::Create()
{
  if (m_hwnd) return true;

  m_hwnd = SWELL_CreateDialog(nullptr, nullptr, g_reaperMainHwnd, DlgProc, (LPARAM)this);
  if (!m_hwnd) return false;

  SetWindowLong(m_hwnd, GWL_USERDATA, (LONG_PTR)this);
  m_favMgr->Load();
  LoadState();

  if (g_DockWindowAddEx) {
    g_DockWindowAddEx(m_hwnd, "ReDockIt", "ReDockIt_container", true);
  }

  SetTimer(m_hwnd, TIMER_ID_CHECK, TIMER_INTERVAL, nullptr);
  ShowWindow(m_hwnd, SW_SHOW);
  m_visible = true;

  if (g_SetExtState) {
    g_SetExtState("ReDockIt_cpp", "was_visible", "1", true);
  }

  return true;
}

// Defined in main.cpp — set by onAtExit to prevent Shutdown from overwriting with empty panes
extern "C" bool g_atexitSaved;

void ReDockItContainer::Shutdown()
{
  if (!m_hwnd) return;

  DBG("[ReDockIt] Shutdown: starting, hwnd=%p atexitSaved=%d\n", m_hwnd, g_atexitSaved);

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
          DBG("[ReDockIt] Shutdown: releasing pane %d tab %d '%s' action=%d toggleState=%d\n",
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
  DBG("[ReDockIt] Shutdown: complete\n");

  if (g_DockWindowRemove) {
    g_DockWindowRemove(m_hwnd);
  }

  DestroyWindow(m_hwnd);
  m_hwnd = nullptr;
  m_visible = false;
}

void ReDockItContainer::Show()
{
  if (!m_hwnd) { Create(); return; }
  ShowWindow(m_hwnd, SW_SHOW);
  m_visible = true;
}

void ReDockItContainer::Toggle()
{
  if (!m_hwnd) { Create(); return; }
  DBG("[ReDockIt] Toggle: closing\n");
  // User explicitly closing — mark as not visible for next startup
  if (g_SetExtState) {
    g_SetExtState("ReDockIt_cpp", "was_visible", "0", true);
  }
  Shutdown();
}

bool ReDockItContainer::IsVisible() const
{
  return m_hwnd && IsWindowVisible(m_hwnd);
}

// =========================================================================
// RefreshLayout
// =========================================================================

void ReDockItContainer::RefreshLayout()
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

void ReDockItContainer::StartCaptureTimer()
{
  SetTimer(m_hwnd, TIMER_ID_CAPTURE, TIMER_CAPTURE_INTERVAL, nullptr);
}

void ReDockItContainer::StopCaptureTimerIfIdle()
{
  if (!m_captureMode.active && !m_captureQueue->HasPending()) {
    KillTimer(m_hwnd, TIMER_ID_CAPTURE);
  }
}

// =========================================================================
// Layout preset / Split / Merge
// =========================================================================

void ReDockItContainer::ApplyPreset(LayoutPreset preset)
{
  if (preset < 0 || preset >= PRESET_COUNT) return;

  // Release all windows before rebuilding tree
  m_winMgr.ReleaseAll();

  m_tree.BuildPreset(preset);

  RefreshLayout();
  SaveState();
}

void ReDockItContainer::SplitPane(int paneId, SplitterOrientation orient)
{
  int nodeIdx = m_tree.NodeForPane(paneId);
  if (nodeIdx < 0) return;
  if (m_tree.GetLeafCount() >= MAX_LEAVES) return;

  m_tree.SplitLeaf(nodeIdx, orient, 0.5f);
  RefreshLayout();
  SaveState();
}

void ReDockItContainer::MergePane(int paneId)
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
// Save / Load state (delegates to WorkspaceManager for serialization)
// =========================================================================

void ReDockItContainer::SaveState()
{
  m_wsMgr->SaveCurrentState(m_tree, m_winMgr);  // global (default + backward compat)
  // Per-project state is now saved via project_config_extension_t (SaveExtensionConfig)
  // which REAPER calls automatically when saving the RPP file.
  // We still save to ProjExtState for immediate in-session state (e.g., project switch).
  if (g_EnumProjects) {
    ReaProject* proj = g_EnumProjects(-1, nullptr, 0);
    if (proj)
      m_wsMgr->SaveProjectState(proj, m_tree, m_winMgr);
  }
}

void ReDockItContainer::LoadState()
{
  if (!g_GetExtState) return;

  NodeSnapshot snap[MAX_TREE_NODES];
  int nodeCount = 0;
  PaneSnapshot panes[MAX_PANES];
  bool hasTreeFormat = false;

  // Try pending RPP state (from project_config_extension_t), then ProjExtState, then global
  bool loadedProject = false;
  if (g_pendingProjectState.valid) {
    DBG("[ReDockIt] LoadState: using pending RPP state (%d lines)\n",
        g_pendingProjectState.lineCount);
    RppReadAccessor rppAcc(g_pendingProjectState.lines, g_pendingProjectState.lineCount);
    const char* treeVer = rppAcc.Get(EXT_SECTION, "tree_version");
    hasTreeFormat = (treeVer && strcmp(treeVer, "2") == 0);
    if (hasTreeFormat) {
      memset(snap, 0, sizeof(snap));
      nodeCount = WorkspaceManager::ReadTreeNodesStatic("", snap, rppAcc);
      if (nodeCount > 0) {
        memset(panes, 0, sizeof(panes));
        WorkspaceManager::ReadPaneTabsStatic("", panes, MAX_PANES, rppAcc);
        loadedProject = true;
        DBG("[ReDockIt] LoadState: loaded RPP state (nodes=%d)\n", nodeCount);
      }
    }
    g_pendingProjectState.valid = false;  // consumed
    m_pendingRppLoad = false;
  }

  if (!loadedProject && g_EnumProjects) {
    ReaProject* proj = g_EnumProjects(-1, nullptr, 0);
    DBG("[ReDockIt] LoadState: project=%p, hasState=%d\n",
        proj, proj ? m_wsMgr->HasProjectState(proj) : -1);
    if (proj && m_wsMgr->HasProjectState(proj)) {
      loadedProject = m_wsMgr->LoadProjectState(proj, snap, nodeCount, panes, hasTreeFormat);
      DBG("[ReDockIt] LoadState: loaded per-project ProjExtState (nodes=%d)\n", nodeCount);
      m_pendingRppLoad = false;
    }
  }

  bool loaded = loadedProject;
  if (!loaded) {
    loaded = m_wsMgr->LoadCurrentState(snap, nodeCount, panes, hasTreeFormat);
    DBG("[ReDockIt] LoadState: loaded global state (nodes=%d)\n", nodeCount);
    // Restore panes from global state — recapture the same windows as last session.
    // If RPP data arrives later, OnTimer will override with project-specific state.
    m_pendingRppLoad = true;
    DBG("[ReDockIt] LoadState: will recapture from global state, RPP may override later\n");
  }

  if (hasTreeFormat) {
    if (nodeCount < 1) {
      m_tree.Reset();
    } else {
      if (!m_tree.LoadSnapshot(snap, nodeCount)) {
        DBG("[ReDockIt] LoadState: corrupt tree detected, saving clean reset state\n");
        SaveState();
      }
    }
  } else {
    const char* presetStr = g_GetExtState(EXT_SECTION, "layout_preset");
    int p = (presetStr && presetStr[0]) ? atoi(presetStr) : 0;
    if (p < 0 || p >= PRESET_COUNT) p = 0;
    m_tree.BuildPreset((LayoutPreset)p);
  }

  ApplyPaneState(panes, MAX_PANES, true);

}

void ReDockItContainer::ApplyPaneState(const PaneSnapshot* panes, int maxPanes, bool deferActions)
{
  bool needsCaptureTimer = false;

  for (int i = 0; i < maxPanes && i < MAX_PANES; i++) {
    if (!m_tree.IsPaneIdUsed(i)) continue;
    if (panes[i].tabCount == 0) continue;

    for (int t = 0; t < panes[i].tabCount && t < MAX_TABS_PER_PANE; t++) {
      const char* winName = panes[i].tabs[t].name;
      if (!winName[0]) continue;

      if (panes[i].tabs[t].isArbitrary) {
        int arbAction = panes[i].tabs[t].toggleAction;
        const char* arbCmd = panes[i].tabs[t].actionCommand;
        // If saved state has no action, check favorites for a matching entry
        if ((!arbCmd || !arbCmd[0]) && m_favMgr) {
          int favIdx = m_favMgr->FindByName(winName);
          if (favIdx >= 0) {
            const FavoriteEntry& fav = m_favMgr->Get(favIdx);
            if (fav.actionCommand[0] && strcmp(fav.actionCommand, "0") != 0) {
              arbAction = fav.toggleAction;
              arbCmd = fav.actionCommand;
              DBG("[ReDockIt] ApplyPaneState: enriched '%s' from favorites: action=%d cmd='%s'\n",
                  winName, arbAction, arbCmd);
            }
          }
        }
        HWND h = WindowManager::FindReaperWindow(winName, m_hwnd);
        if (h) {
          m_winMgr.CaptureArbitraryWindow(i, h, winName, m_hwnd, arbAction, arbCmd);
        } else {
          m_captureQueue->EnqueueArbitrary(i, winName, arbAction, arbCmd, deferActions);
          needsCaptureTimer = true;
        }
      } else {
        for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
          if (strcmp(KNOWN_WINDOWS[j].name, winName) == 0) {
            HWND h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].searchTitle, m_hwnd);
            if (!h && KNOWN_WINDOWS[j].altSearchTitle) {
              h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].altSearchTitle, m_hwnd);
            }
            if (!h && g_Main_OnCommand) {
              m_captureQueue->EnqueueKnown(i, j, deferActions);
              needsCaptureTimer = true;
            } else if (h) {
              m_winMgr.CaptureByIndex(i, j, m_hwnd);
            }
            break;
          }
        }
      }
    }

    // Restore tab colors from snapshot
    int loadedTabs = m_winMgr.GetTabCount(i);
    for (int t = 0; t < loadedTabs && t < panes[i].tabCount; t++) {
      int ci = panes[i].tabs[t].colorIndex;
      if (ci > 0 && ci < TAB_COLOR_COUNT) {
        m_winMgr.SetTabColor(i, t, ci);
      }
    }

    // Set active tab
    if (panes[i].activeTab >= 0 && panes[i].activeTab < m_winMgr.GetTabCount(i)) {
      m_winMgr.SetActiveTab(i, panes[i].activeTab);
    }
  }

  if (needsCaptureTimer) {
    StartCaptureTimer();
  }
}

// =========================================================================
// Workspace management (delegates to WorkspaceManager)
// =========================================================================

void ReDockItContainer::SaveWorkspace(const char* name)
{
  m_wsMgr->Save(name, m_tree, m_winMgr);
}

void ReDockItContainer::LoadWorkspace(const char* name)
{
  if (!name || !name[0]) return;

  const WorkspaceEntry* ws = m_wsMgr->Find(name);
  if (!ws) return;

  m_winMgr.ReleaseAll(false);  // just reparent, don't toggle off

  if (ws->treeVersion == 2) {
    m_tree.LoadSnapshot(ws->nodes, ws->nodeCount);
  } else {
    // Legacy format
    int lp = ws->layoutPreset;
    if (lp < 0 || lp >= PRESET_COUNT) lp = 0;
    m_tree.BuildPreset((LayoutPreset)lp);
  }

  {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_tree.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
  }

  ApplyPaneState(ws->panes, MAX_PANES, false);
  m_winMgr.RepositionAll(m_tree);
  InvalidateRect(m_hwnd, nullptr, TRUE);
  SaveState();
}

void ReDockItContainer::DeleteWorkspace(const char* name)
{
  m_wsMgr->Delete(name);
}

// =========================================================================
// Helpers
// =========================================================================

int ReDockItContainer::PaneAtPoint(int x, int y) const
{
  int nodeIdx = m_tree.LeafAtPoint(x, y);
  if (nodeIdx < 0) return -1;
  return m_tree.GetPaneId(nodeIdx);
}

// =========================================================================
// Tab hit testing
// =========================================================================

int ReDockItContainer::TabHitTest(int paneId, int x, int y) const
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || ps->tabCount == 0) return -1;

  const RECT& paneRect = m_tree.GetPaneRect(paneId);
  int tabBarTop = paneRect.top;
  int tabBarBottom = tabBarTop + TAB_BAR_HEIGHT;

  if (y < tabBarTop || y >= tabBarBottom) return -1;

  int paneWidth = paneRect.right - paneRect.left;
  int tabWidth = paneWidth / ps->tabCount;
  if (tabWidth < TAB_MIN_WIDTH) tabWidth = TAB_MIN_WIDTH;
  if (tabWidth > TAB_MAX_WIDTH) tabWidth = TAB_MAX_WIDTH;

  int relX = x - paneRect.left;
  int tabIdx = relX / tabWidth;
  if (tabIdx < 0 || tabIdx >= ps->tabCount) return -1;
  return tabIdx;
}

bool ReDockItContainer::IsOnTabCloseButton(int paneId, int tabIndex, int x, int y) const
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || tabIndex < 0 || tabIndex >= ps->tabCount) return false;

  const RECT& paneRect = m_tree.GetPaneRect(paneId);
  int tabBarTop = paneRect.top;

  int paneWidth = paneRect.right - paneRect.left;
  int tabWidth = paneWidth / ps->tabCount;
  if (tabWidth < TAB_MIN_WIDTH) tabWidth = TAB_MIN_WIDTH;
  if (tabWidth > TAB_MAX_WIDTH) tabWidth = TAB_MAX_WIDTH;

  int tabRight = paneRect.left + (tabIndex + 1) * tabWidth;
  if (tabRight > paneRect.right) tabRight = paneRect.right;

  int closeRight = tabRight - CLOSE_BTN_RIGHT_MARGIN;
  int closeLeft = closeRight - CLOSE_BTN_WIDTH;
  int closeTop = tabBarTop + CLOSE_BTN_VERT_MARGIN;
  int closeBottom = tabBarTop + TAB_BAR_HEIGHT - CLOSE_BTN_VERT_MARGIN;

  return (x >= closeLeft && x <= closeRight && y >= closeTop && y <= closeBottom);
}

// =========================================================================
// Drag and drop
// =========================================================================

void ReDockItContainer::StartTabDrag(int paneId, int tabIndex, int x, int y)
{
  m_dragState.active = false;
  m_dragState.sourcePaneId = paneId;
  m_dragState.sourceTabIndex = tabIndex;
  m_dragState.startPt.x = x;
  m_dragState.startPt.y = y;
  m_dragState.highlightPaneId = -1;
  m_dragState.dragStarted = false;
  SetCapture(m_hwnd);
}

void ReDockItContainer::UpdateTabDrag(int x, int y)
{
  if (!m_dragState.dragStarted) {
    int dx = x - m_dragState.startPt.x;
    int dy = y - m_dragState.startPt.y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx < DRAG_THRESHOLD_PX && ady < DRAG_THRESHOLD_PX) return;
    m_dragState.dragStarted = true;
    m_dragState.active = true;
  }

  int oldHighlight = m_dragState.highlightPaneId;
  m_dragState.highlightPaneId = PaneAtPoint(x, y);

  if (m_dragState.highlightPaneId == m_dragState.sourcePaneId) {
    m_dragState.highlightPaneId = -1;
  }

  if (m_dragState.highlightPaneId >= 0) {
    if (m_winMgr.GetTabCount(m_dragState.highlightPaneId) >= MAX_TABS_PER_PANE) {
      m_dragState.highlightPaneId = -1;
    }
  }

  if (m_dragState.highlightPaneId != oldHighlight) {
    InvalidateRect(m_hwnd, nullptr, TRUE);
  }

  short escState = GetAsyncKeyState(VK_ESCAPE);
  if (escState & 0x8000) {
    CancelTabDrag();
  }
}

void ReDockItContainer::EndTabDrag(int x, int y)
{
  if (!m_dragState.active || !m_dragState.dragStarted) {
    memset(&m_dragState, 0, sizeof(m_dragState));
    m_dragState.sourcePaneId = -1;
    m_dragState.highlightPaneId = -1;
    ReleaseCapture();
    return;
  }

  int targetPane = PaneAtPoint(x, y);
  if (targetPane >= 0 && targetPane != m_dragState.sourcePaneId) {
    if (m_winMgr.GetTabCount(targetPane) < MAX_TABS_PER_PANE) {
      m_winMgr.MoveTab(m_dragState.sourcePaneId, m_dragState.sourceTabIndex, targetPane);
      RefreshLayout();
      SaveState();
    }
  }

  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  ReleaseCapture();
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ReDockItContainer::CancelTabDrag()
{
  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  ReleaseCapture();
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

// =========================================================================
// Event handlers
// =========================================================================

void ReDockItContainer::OnSize(int cx, int cy)
{
  m_tree.Recalculate(cx, cy);
  m_winMgr.RepositionAll(m_tree);
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ReDockItContainer::OnPaint(HDC hdc)
{
  RECT rc;
  GetClientRect(m_hwnd, &rc);

  // Background
  HBRUSH bgBrush = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
  FillRect(hdc, &rc, bgBrush);
  DeleteObject(bgBrush);

  // Draw splitter bars
  HBRUSH splitterBrush = CreateSolidBrush(GetSysColor(COLOR_3DSHADOW));
  HBRUSH hoverBrush = CreateSolidBrush(COLOR_SPLITTER_HIGHLIGHT);
  for (int i = 0; i < m_tree.GetBranchCount(); i++) {
    int branchIdx = m_tree.GetBranchList()[i];
    const SplitNode& n = m_tree.GetNode(branchIdx);
    FillRect(hdc, &n.splitterRect, splitterBrush);
    if (branchIdx == m_hoverSplitter) {
      RECT hr = n.splitterRect;
      if (n.orient == SPLIT_VERTICAL) {
        hr.left += SPLITTER_HIGHLIGHT_INSET;
        hr.right -= SPLITTER_HIGHLIGHT_INSET;
      } else {
        hr.top += SPLITTER_HIGHLIGHT_INSET;
        hr.bottom -= SPLITTER_HIGHLIGHT_INSET;
      }
      FillRect(hdc, &hr, hoverBrush);
    }
  }
  DeleteObject(hoverBrush);
  DeleteObject(splitterBrush);

  // Draw pane tab bars or empty headers
  for (int i = 0; i < m_tree.GetLeafCount(); i++) {
    int nodeIdx = m_tree.GetLeafList()[i];
    int paneId = m_tree.GetPaneId(nodeIdx);
    if (paneId < 0 || paneId >= MAX_PANES) continue;

    const RECT& paneRect = m_tree.GetPaneRect(paneId);
    const PaneState* ps = m_winMgr.GetPaneState(paneId);

    if (ps && ps->tabCount > 0) {
      DrawTabBar(hdc, paneId, paneRect);
    } else {
      RECT headerRect = paneRect;
      headerRect.bottom = headerRect.top + TAB_BAR_HEIGHT;

      HBRUSH headerBrush = CreateSolidBrush(COLOR_EMPTY_HEADER_BG);
      FillRect(hdc, &headerRect, headerBrush);
      DeleteObject(headerBrush);

      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, COLOR_EMPTY_HEADER_TEXT);
      char headerText[128];
      if (m_captureMode.active && m_captureMode.targetPaneId == paneId) {
        snprintf(headerText, sizeof(headerText), " Click a window to capture...");
      } else {
        // Show sequential visual index (1-based position in leaf list)
        snprintf(headerText, sizeof(headerText), " Pane %d (click to assign)", i + 1);
      }
      RECT headerTextRect = headerRect;
      headerTextRect.left += 4;
      DrawText(hdc, headerText, -1, &headerTextRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      RECT contentRect = paneRect;
      contentRect.top += TAB_BAR_HEIGHT;
      SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
      DrawText(hdc, "Click header to assign a window", -1, &contentRect,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
  }

  // Drag highlight
  if (m_dragState.active && m_dragState.dragStarted && m_dragState.highlightPaneId >= 0) {
    const RECT& r = m_tree.GetPaneRect(m_dragState.highlightPaneId);
    HPEN highlightPen = CreatePen(PS_SOLID, 3, COLOR_DRAG_HIGHLIGHT);
    HPEN oldPen = (HPEN)SelectObject(hdc, highlightPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(highlightPen);
  }
}

void ReDockItContainer::DrawTabBar(HDC hdc, int paneId, const RECT& paneRect)
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || ps->tabCount == 0) return;

  int tabBarTop = paneRect.top;
  int tabBarBottom = tabBarTop + TAB_BAR_HEIGHT;
  int paneWidth = paneRect.right - paneRect.left;

  RECT barRect = { paneRect.left, tabBarTop, paneRect.right, tabBarBottom };
  HBRUSH barBg = CreateSolidBrush(COLOR_TAB_BAR_BG);
  FillRect(hdc, &barRect, barBg);
  DeleteObject(barBg);

  int tabWidth = paneWidth / ps->tabCount;
  if (tabWidth < TAB_MIN_WIDTH) tabWidth = TAB_MIN_WIDTH;
  if (tabWidth > TAB_MAX_WIDTH) tabWidth = TAB_MAX_WIDTH;

  for (int t = 0; t < ps->tabCount; t++) {
    int tabLeft = paneRect.left + t * tabWidth;
    int tabRight = tabLeft + tabWidth;
    if (tabRight > paneRect.right) tabRight = paneRect.right;

    RECT tabRect = { tabLeft, tabBarTop, tabRight, tabBarBottom };

    {
      int ci = ps->tabs[t].colorIndex;
      COLORREF bgColor;
      if (ci > 0 && ci < TAB_COLOR_COUNT) {
        const TabColor& tc = TAB_COLORS[ci];
        if (t == ps->activeTab) {
          bgColor = RGB(tc.r, tc.g, tc.b);
        } else {
          bgColor = RGB(tc.r / 2, tc.g / 2, tc.b / 2);
        }
      } else {
        bgColor = (t == ps->activeTab) ? COLOR_TAB_ACTIVE_BG : COLOR_TAB_INACTIVE_BG;
      }
      if (paneId == m_hoverPane && t == m_hoverTab) {
        int r = GetRValue(bgColor) + TAB_HOVER_LIGHTEN;
        int g = GetGValue(bgColor) + TAB_HOVER_LIGHTEN;
        int b = GetBValue(bgColor) + TAB_HOVER_LIGHTEN;
        bgColor = RGB(r < 255 ? r : 255, g < 255 ? g : 255, b < 255 ? b : 255);
      }
      HBRUSH tabBrush = CreateSolidBrush(bgColor);
      FillRect(hdc, &tabRect, tabBrush);
      DeleteObject(tabBrush);
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, (t == ps->activeTab) ? COLOR_TAB_ACTIVE_TEXT : COLOR_TAB_INACTIVE_TEXT);
    RECT textRect = tabRect;
    textRect.left += TAB_TEXT_LEFT_PAD;
    textRect.right -= TAB_TEXT_RIGHT_MARGIN;
    const char* tabName = ps->tabs[t].name ? ps->tabs[t].name : "?";
    DrawText(hdc, tabName, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    SetTextColor(hdc, COLOR_TAB_CLOSE_TEXT);
    RECT closeRect = { tabRight - 16, tabBarTop + 2, tabRight - 2, tabBarBottom - 2 };
    DrawText(hdc, "x", 1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (t < ps->tabCount - 1) {
      HPEN sepPen = CreatePen(PS_SOLID, 1, COLOR_TAB_SEPARATOR);
      HPEN oldPen = (HPEN)SelectObject(hdc, sepPen);
      MoveToEx(hdc, tabRight, tabBarTop + 2, nullptr);
      LineTo(hdc, tabRight, tabBarBottom - 2);
      SelectObject(hdc, oldPen);
      DeleteObject(sepPen);
    }
  }
}

void ReDockItContainer::OnMouseMove(int x, int y)
{
  if (m_dragState.sourcePaneId >= 0) {
    UpdateTabDrag(x, y);
    return;
  }

  if (m_tree.IsDragging()) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_tree.Drag(x, y, rc.right - rc.left, rc.bottom - rc.top);
    m_winMgr.RepositionAll(m_tree);
    InvalidateRect(m_hwnd, nullptr, TRUE);
    return;
  }

  // Hover highlight for splitters
  int hover = m_tree.HitTestSplitter(x, y);
  if (hover != m_hoverSplitter) {
    m_hoverSplitter = hover;
    InvalidateRect(m_hwnd, nullptr, TRUE);
    if (hover >= 0)
      SetTimer(m_hwnd, TIMER_ID_HOVER, 60, nullptr);
    else
      KillTimer(m_hwnd, TIMER_ID_HOVER);
  }

  // Hover highlight for tabs
  int hPane = -1, hTab = -1;
  for (int i = 0; i < m_tree.GetLeafCount(); i++) {
    int paneId = m_tree.GetPaneId(m_tree.GetLeafList()[i]);
    if (paneId < 0) continue;
    int t = TabHitTest(paneId, x, y);
    if (t >= 0) { hPane = paneId; hTab = t; break; }
  }
  if (hPane != m_hoverPane || hTab != m_hoverTab) {
    m_hoverPane = hPane;
    m_hoverTab = hTab;
    InvalidateRect(m_hwnd, nullptr, TRUE);
    if (hTab >= 0 && m_hoverSplitter < 0)
      SetTimer(m_hwnd, TIMER_ID_HOVER, 60, nullptr);
    else if (hTab < 0 && m_hoverSplitter < 0)
      KillTimer(m_hwnd, TIMER_ID_HOVER);
  }
}

void ReDockItContainer::OnLButtonUp(int x, int y)
{
  if (m_dragState.sourcePaneId >= 0) {
    EndTabDrag(x, y);
    return;
  }

  if (m_tree.IsDragging()) {
    m_tree.EndDrag();
    ReleaseCapture();
    SaveState();
  }
}

void ReDockItContainer::OnTimer()
{
  // Check if deferred RPP state has become available.
  // REAPER parses the RPP <REDOCKIT_STATE> chunk asynchronously —
  // it may arrive after Create()/LoadState() already ran.
  if (m_pendingRppLoad && g_pendingProjectState.valid) {
    DBG("[ReDockIt] OnTimer: deferred RPP state now available (%d lines), applying\n",
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
          DBG("[ReDockIt] OnTimer: deferred RPP tree corrupt, resetting\n");
          m_tree.Reset();
        }

        RECT rc;
        GetClientRect(m_hwnd, &rc);
        m_tree.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
        ApplyPaneState(panes, MAX_PANES, true);
        m_winMgr.RepositionAll(m_tree);
        InvalidateRect(m_hwnd, nullptr, TRUE);

        DBG("[ReDockIt] OnTimer: deferred RPP state applied (nodes=%d)\n", nodeCount);
      }
    }
    g_pendingProjectState.valid = false;  // consumed
  }

  m_winMgr.CheckAlive(m_hwnd);
}

// =========================================================================
// Context menu (builds menu via context_menu module, dispatches commands)
// =========================================================================

void ReDockItContainer::OnContextMenu(int x, int y)
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

void ReDockItContainer::HandleTabMenuCommand(int cmd, int paneId, int tabIdx)
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
    InvalidateRect(m_hwnd, nullptr, TRUE);
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

void ReDockItContainer::HandlePaneMenuCommand(int cmd, int paneId)
{
  // Layout preset
  if (cmd >= MenuIds::LAYOUT_BASE && cmd < MenuIds::LAYOUT_BASE + PRESET_COUNT) {
    ApplyPreset((LayoutPreset)(cmd - MenuIds::LAYOUT_BASE));
    return;
  }

  // Known window selection — async capture
  if (cmd >= MenuIds::KNOWN_BASE && cmd < MenuIds::KNOWN_BASE + NUM_KNOWN_WINDOWS) {
    int idx = cmd - MenuIds::KNOWN_BASE;

    DBG("[ReDockIt] Menu: selected '%s' for pane %d\n", KNOWN_WINDOWS[idx].name, paneId);

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

  // Close ReDockIt container
  if (cmd == MenuIds::CLOSE_CONTAINER) {
    Shutdown();
    return;
  }

  // Favorites — capture from favorite
  if (cmd >= MenuIds::FAV_BASE && cmd < MenuIds::FAV_BASE + MAX_FAVORITES) {
    int favIdx = cmd - MenuIds::FAV_BASE;
    if (favIdx >= 0 && favIdx < m_favMgr->GetCount()) {
      const FavoriteEntry& fav = m_favMgr->Get(favIdx);
      DBG("[ReDockIt] FAV CLICK: '%s' search='%s' action=%d cmd='%s' isKnown=%d pane=%d\n",
          fav.name, fav.searchTitle, fav.toggleAction, fav.actionCommand, fav.isKnown, paneId);

      HWND found = WindowManager::FindReaperWindow(fav.searchTitle, m_hwnd);
      DBG("[ReDockIt] FAV CLICK: FindReaperWindow('%s') -> %p\n", fav.searchTitle, (void*)found);
      if (found && fav.isKnown) {
        // Known windows don't need dock frame logic — capture directly
        DBG("[ReDockIt] FAV CLICK: known window found, capturing directly\n");
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
          DBG("[ReDockIt] FAV CLICK: dock frame found, capturing directly\n");
          m_winMgr.CaptureArbitraryWindow(paneId, found, fav.name, m_hwnd,
                                           fav.toggleAction, fav.actionCommand);
          RefreshLayout();
          SaveState();
        } else {
          // Inner window found — enqueue via CaptureQueue to wait for dock frame
          DBG("[ReDockIt] FAV CLICK: inner window found, enqueue to wait for dock frame\n");
          m_captureQueue->EnqueueArbitrary(paneId, fav.searchTitle, fav.toggleAction, fav.actionCommand);
          StartCaptureTimer();
        }
      } else if (fav.toggleAction > 0) {
        DBG("[ReDockIt] FAV CLICK: window not found, has toggle action=%d -> enqueue + fire action\n",
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
        DBG("[ReDockIt] FAV CLICK: window not found, NO toggle action -> enqueue for polling\n");
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

INT_PTR CALLBACK ReDockItContainer::DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  ReDockItContainer* self = (ReDockItContainer*)(LONG_PTR)GetWindowLong(hwnd, GWL_USERDATA);

  switch (msg) {
    case WM_INITDIALOG: {
      self = (ReDockItContainer*)lParam;
      SetWindowLong(hwnd, GWL_USERDATA, (LONG_PTR)self);
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
          if (tabIdx >= 0) {
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
          self->m_hoverSplitter = -1;
          self->m_hoverPane = -1;
          self->m_hoverTab = -1;
          KillTimer(hwnd, TIMER_ID_HOVER);
          InvalidateRect(hwnd, nullptr, TRUE);
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
            // Grace period: reparenting may temporarily hide our docker
            self->m_shutdownGraceTicks = SHUTDOWN_GRACE_TICKS;
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
