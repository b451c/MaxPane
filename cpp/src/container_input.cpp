// container_input.cpp — Input handling for MaxPaneContainer
// (mouse events, tab drag/drop, hit testing, resize)
#include "container.h"
#include "config.h"
#include "launcher.h"
#include "workspace_manager.h"
#include "split_tree.h"
#include "window_manager.h"
#include "swell_cocoa_helpers.h"  // IsCaptureCancelKeyDown (audit M1.6)

// =========================================================================
// Helpers
// =========================================================================

int MaxPaneContainer::PaneAtPoint(int x, int y) const
{
  int nodeIdx = m_tree.LeafAtPoint(x, y);
  if (nodeIdx < 0) return -1;
  return m_tree.GetPaneId(nodeIdx);
}

// =========================================================================
// Tab bar layout calculation (shared by draw, hit-test, close-button)
// =========================================================================

MaxPaneContainer::TabBarLayout MaxPaneContainer::CalcTabBarLayout(int paneId) const
{
  TabBarLayout lay = {};
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || ps->tabCount == 0) return lay;

  const RECT& r = m_tree.GetPaneRect(paneId);
  int totalWidth = (r.right - r.left) - PANE_MENU_BTN_WIDTH;
  if (totalWidth < TAB_MIN_WIDTH) totalWidth = TAB_MIN_WIDTH;

  lay.tabAreaLeft  = r.left;
  lay.tabAreaRight = r.left + totalWidth;
  lay.tabWidth     = totalWidth / ps->tabCount;
  if (lay.tabWidth > TAB_MAX_WIDTH) lay.tabWidth = TAB_MAX_WIDTH;
  if (lay.tabWidth < 1) lay.tabWidth = 1;

  return lay;
}

RECT MaxPaneContainer::GetTabRect(int paneId, int tabIdx) const
{
  const RECT& r = m_tree.GetPaneRect(paneId);
  int tabBarTop = r.top;
  int tabBarBottom = tabBarTop + m_winMgr.PaneHeaderHeight(paneId);
  TabBarLayout lay = CalcTabBarLayout(paneId);

  int tabLeft  = lay.tabAreaLeft + tabIdx * lay.tabWidth;
  int tabRight = tabLeft + lay.tabWidth;
  if (tabRight > lay.tabAreaRight) tabRight = lay.tabAreaRight;

  RECT tr = { tabLeft, tabBarTop, tabRight, tabBarBottom };
  return tr;
}

// =========================================================================
// Tab hit testing
// =========================================================================

// Returns: >=0 tab index, -1 miss, -2 menu button, -3 left arrow, -4 right arrow
int MaxPaneContainer::TabHitTest(int paneId, int x, int y) const
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || ps->tabCount == 0) return -1;

  const RECT& paneRect = m_tree.GetPaneRect(paneId);
  int tabBarTop = paneRect.top;
  const int hdrH = m_winMgr.PaneHeaderHeight(paneId);
  int tabBarBottom = tabBarTop + hdrH;

  if (y < tabBarTop || y >= tabBarBottom) return -1;
  if (x < paneRect.left || x >= paneRect.right) return -1;

  // Menu button (rightmost PANE_MENU_BTN_WIDTH pixels)
  if (x >= paneRect.right - PANE_MENU_BTN_WIDTH) return -2;

  // ADR-055 — collapsed sliver: the whole strip (minus the menu-button zone
  // above) is the single tab, so right-click = tab menu and a press-drag
  // starts the tab drag exactly like a normal tab. tabWidth math would cap
  // the hit zone at TAB_MAX_WIDTH and dead-zone the rest of the strip.
  if (hdrH < TAB_BAR_HEIGHT) return 0;

  TabBarLayout lay = CalcTabBarLayout(paneId);

  // Tab area
  if (x < lay.tabAreaLeft || x >= lay.tabAreaRight) return -1;
  int relX = x - lay.tabAreaLeft;
  int tabIdx = relX / lay.tabWidth;
  if (tabIdx < 0 || tabIdx >= ps->tabCount) return -1;
  return tabIdx;
}

bool MaxPaneContainer::IsOnTabCloseButton(int paneId, int tabIndex, int x, int y) const
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || tabIndex < 0 || tabIndex >= ps->tabCount) return false;

  // ADR-055 — no close button on a collapsed sliver (nothing is drawn there).
  if (m_winMgr.PaneHeaderHeight(paneId) < TAB_BAR_HEIGHT) return false;

  // Close button hidden when tabs are narrow (matches DrawTabBar) (B9)
  TabBarLayout lay = CalcTabBarLayout(paneId);
  if (lay.tabWidth < TAB_CLOSE_MIN_WIDTH) return false;

  RECT tr = GetTabRect(paneId, tabIndex);
  // Empty rect means tab not visible
  if (tr.left == 0 && tr.right == 0) return false;

  int tabBarTop = m_tree.GetPaneRect(paneId).top;
  int closeRight = tr.right - CLOSE_BTN_RIGHT_MARGIN;
  int closeLeft = closeRight - CLOSE_BTN_WIDTH;
  int closeTop = tabBarTop + CLOSE_BTN_VERT_MARGIN;
  int closeBottom = tabBarTop + TAB_BAR_HEIGHT - CLOSE_BTN_VERT_MARGIN;

  return (x >= closeLeft && x <= closeRight && y >= closeTop && y <= closeBottom);
}

// =========================================================================
// Drag and drop
// =========================================================================

void MaxPaneContainer::StartTabDrag(int paneId, int tabIndex, int x, int y)
{
  m_dragState.active = false;
  m_dragState.sourcePaneId = paneId;
  m_dragState.sourceTabIndex = tabIndex;
  m_dragState.startPt.x = x;
  m_dragState.startPt.y = y;
  m_dragState.highlightPaneId = -1;
  m_dragState.dragStarted = false;
  m_dragState.insertTabIndex = -1;
  SetCapture(m_hwnd);
}

void MaxPaneContainer::UpdateTabDrag(int x, int y)
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
  int oldInsert = m_dragState.insertTabIndex;
  int targetPane = PaneAtPoint(x, y);
  m_dragState.insertTabIndex = -1;

  if (targetPane == m_dragState.sourcePaneId) {
    // Intra-pane reorder: compute insertion index from mouse position
    m_dragState.highlightPaneId = -1;
    int tabHit = TabHitTest(targetPane, x, y);
    if (tabHit >= 0 && tabHit != m_dragState.sourceTabIndex) {
      m_dragState.insertTabIndex = tabHit;
    }
  } else {
    m_dragState.highlightPaneId = targetPane;
    if (m_dragState.highlightPaneId >= 0) {
      if (m_winMgr.GetTabCount(m_dragState.highlightPaneId) >= MAX_TABS_PER_PANE) {
        m_dragState.highlightPaneId = -1;
      }
    }
  }

  if (m_dragState.highlightPaneId != oldHighlight || m_dragState.insertTabIndex != oldInsert) {
    // Targeted: union of old and new highlight pane rects + source tab bar
    RECT dirty = {};
    if (oldHighlight >= 0 && m_tree.IsPaneIdUsed(oldHighlight))
      ExpandRect(dirty, m_tree.GetPaneRect(oldHighlight));
    if (m_dragState.highlightPaneId >= 0 && m_tree.IsPaneIdUsed(m_dragState.highlightPaneId))
      ExpandRect(dirty, m_tree.GetPaneRect(m_dragState.highlightPaneId));
    // Intra-pane: invalidate source pane tab bar for insertion indicator
    if (m_dragState.insertTabIndex >= 0 || oldInsert >= 0) {
      int srcPane = m_dragState.sourcePaneId;
      if (srcPane >= 0 && m_tree.IsPaneIdUsed(srcPane)) {
        const RECT& pr = m_tree.GetPaneRect(srcPane);
        RECT tabBar = { pr.left, pr.top, pr.right, pr.top + TAB_BAR_HEIGHT };
        ExpandRect(dirty, tabBar);
      }
    }
    if (dirty.right > dirty.left) InvalidateRect(m_hwnd, &dirty, FALSE);
    else InvalidateRect(m_hwnd, nullptr, FALSE);
  }

  // Audit M1.6 — raw GetAsyncKeyState(VK_ESCAPE) is always 0 under both
  // SWELLs (macOS swell-kb.mm, Linux swell-generic-gdk), so Esc never
  // cancelled a tab drag off Windows. Route through the ADR-048 helper
  // (CG keycode probe on macOS, native on Win32; still dead on Linux —
  // mouse-release remains the Linux cancel).
  if (IsCaptureCancelKeyDown()) {
    CancelTabDrag();
  }
}

void MaxPaneContainer::EndTabDrag(int x, int y)
{
  if (!m_dragState.active || !m_dragState.dragStarted) {
    memset(&m_dragState, 0, sizeof(m_dragState));
    m_dragState.sourcePaneId = -1;
    m_dragState.highlightPaneId = -1;
    ReleaseCapture();
    return;
  }

  // Save positions before clearing state (for targeted invalidation)
  int savedSrc = m_dragState.sourcePaneId;
  int savedHL  = m_dragState.highlightPaneId;
  int savedInsert = m_dragState.insertTabIndex;
  int savedSrcTab = m_dragState.sourceTabIndex;

  int targetPane = PaneAtPoint(x, y);

  // Intra-pane reorder
  if (targetPane >= 0 && targetPane == savedSrc && savedInsert >= 0) {
    m_winMgr.ReorderTab(savedSrc, savedSrcTab, savedInsert);
    memset(&m_dragState, 0, sizeof(m_dragState));
    m_dragState.sourcePaneId = -1;
    m_dragState.highlightPaneId = -1;
    m_dragState.insertTabIndex = -1;
    ReleaseCapture();
    RefreshLayout();
    SaveState();
    return;
  }

  // Cross-pane move
  if (targetPane >= 0 && targetPane != savedSrc) {
    if (m_winMgr.GetTabCount(targetPane) < MAX_TABS_PER_PANE) {
      m_winMgr.MoveTab(savedSrc, savedSrcTab, targetPane);
      memset(&m_dragState, 0, sizeof(m_dragState));
      m_dragState.sourcePaneId = -1;
      m_dragState.highlightPaneId = -1;
      m_dragState.insertTabIndex = -1;
      ReleaseCapture();
      RefreshLayout();  // full invalidate inside
      SaveState();
      return;
    }
  }

  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  m_dragState.insertTabIndex = -1;
  ReleaseCapture();

  // Targeted: source pane tab bar + old highlight pane
  RECT dirty = {};
  if (savedSrc >= 0 && m_tree.IsPaneIdUsed(savedSrc)) {
    const RECT& pr = m_tree.GetPaneRect(savedSrc);
    RECT tabBar = { pr.left, pr.top, pr.right, pr.top + TAB_BAR_HEIGHT };
    ExpandRect(dirty, tabBar);
  }
  if (savedHL >= 0 && m_tree.IsPaneIdUsed(savedHL))
    ExpandRect(dirty, m_tree.GetPaneRect(savedHL));
  if (dirty.right > dirty.left) InvalidateRect(m_hwnd, &dirty, FALSE);
  else InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MaxPaneContainer::CancelTabDrag()
{
  int savedSrc = m_dragState.sourcePaneId;
  int savedHL  = m_dragState.highlightPaneId;
  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  m_dragState.insertTabIndex = -1;
  ReleaseCapture();

  // Targeted: source pane tab bar + old highlight pane
  RECT dirty = {};
  if (savedSrc >= 0 && m_tree.IsPaneIdUsed(savedSrc)) {
    const RECT& pr = m_tree.GetPaneRect(savedSrc);
    RECT tabBar = { pr.left, pr.top, pr.right, pr.top + TAB_BAR_HEIGHT };
    ExpandRect(dirty, tabBar);
  }
  if (savedHL >= 0 && m_tree.IsPaneIdUsed(savedHL))
    ExpandRect(dirty, m_tree.GetPaneRect(savedHL));
  if (dirty.right > dirty.left) InvalidateRect(m_hwnd, &dirty, FALSE);
  else InvalidateRect(m_hwnd, nullptr, FALSE);
}

// =========================================================================
// Event handlers
// =========================================================================

void MaxPaneContainer::OnSize(int cx, int cy)
{
  // ADR-026 — reserve nav bar height; tree origin already accounts for it.
  int navH = NavBarReservedHeight();
  int availH = cy - navH;
  if (availH < 1) availH = 1;
  m_tree.Recalculate(cx, availH);
  // ADR-081 §2 (owner smoke round 5) — same stutter as the splitter drag,
  // different entry point: a live window-edge resize fires WM_SIZE per
  // mouse move and each ran a full RepositionAll over heavy plugin UIs
  // (the 13x-in-200ms burst in the owner's log). Same ~20 Hz throttle
  // (shared tick — a splitter drag and a window resize never run at the
  // same time); a skipped pass arms the settle refresh so the exact final
  // layout lands on the next OnTimer tick after the resize stops.
  const DWORD nowTick = GetTickCount();
  if (nowTick - m_lastDragReposTick >= 50) {
    m_lastDragReposTick = nowTick;
    m_winMgr.RepositionAll(m_tree);
  } else {
    m_needsSettleRefresh = true;
  }
  InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MaxPaneContainer::OnMouseMove(int x, int y)
{
  if (m_dragState.sourcePaneId >= 0) {
    UpdateTabDrag(x, y);
    return;
  }

  if (m_tree.IsDragging()) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    // ADR-026 — splitter drag must respect nav bar reservation.
    int navH = NavBarReservedHeight();
    int availH = (rc.bottom - rc.top) - navH;
    if (availH < 1) availH = 1;
    m_tree.Drag(x, y, rc.right - rc.left, availH);
    // v2.4.0 (owner smoke feedback, mac) — live-resizing every captured
    // child on EVERY mouse move made heavy plugin UIs (iZotope-class)
    // stutter hard during splitter drags: each move = SetWindowPos +
    // WM_SIZE cascade + full plugin relayout. Throttle child repositioning
    // to ~20 Hz; the splitter line itself still tracks the mouse every
    // move (InvalidateRect below), and drag END lands the exact final
    // layout (OnLButtonUp → RefreshLayout).
    const DWORD nowTick = GetTickCount();
    if (nowTick - m_lastDragReposTick >= 50) {
      m_lastDragReposTick = nowTick;
      m_winMgr.RepositionAll(m_tree);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
    return;
  }

  // ADR-026 — nav bar hover. Consumes the event when cursor is over the
  // bar strip. When cursor leaves the bar we also clear m_navHover so
  // the next paint drops the highlight.
  if (m_navBarVisible) {
    bool overBar = OnNavBarMouseMove(x, y);
    if (overBar) return;
    if (m_navHover != NavBar::BTN_NONE) {
      m_navHover = NavBar::BTN_NONE;
      m_navTooltipBtn = NavBar::BTN_NONE;
      KillTimer(m_hwnd, TIMER_ID_NAVBAR_TIP);
      RECT rc; GetClientRect(m_hwnd, &rc);
      NavBar::Layout lay = ComputeNavBarLayout(rc);
      RECT dirty = lay.barRect;
      dirty.bottom += 36;
      InvalidateRect(m_hwnd, &dirty, FALSE);
    }
  }

  // ADR-026 — Home overlay hover (workspace cards painted over current
  // pane grid). Consumes the event when overlay is open.
  if (m_homeOverlay) {
    OnHomeOverlayMouseMove(x, y);
    return;
  }

  // Launcher hover (ADR-013) — short-circuit; no splitters/tabs to test.
  if (IsInLauncherMode()) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    rc.top += NavBarReservedHeight();  // ADR-048 — match Paint's launcher origin
    Launcher::Layout lay = Launcher::Compute(rc, *m_wsMgr);
    int hit = Launcher::HitTest(lay, x, y);
    if (hit != m_launcherHover) {
      int prev = m_launcherHover;
      m_launcherHover = hit;
      // Hide any active tooltip — new card (or non-card) restarts the wait.
      if (m_launcherTooltipCard >= 0) m_launcherTooltipCard = -1;
      // (Re)start tooltip delay when hovering a card; cancel otherwise.
      if (hit >= 0) {
        SetTimer(m_hwnd, TIMER_ID_LAUNCHER_TIP, LAUNCHER_TOOLTIP_DELAY_MS, nullptr);
      } else {
        KillTimer(m_hwnd, TIMER_ID_LAUNCHER_TIP);
      }
      (void)prev;
      InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    return;
  }
  if (m_launcherHover != Launcher::HIT_NONE) {
    m_launcherHover = Launcher::HIT_NONE;
    m_launcherTooltipCard = -1;
    KillTimer(m_hwnd, TIMER_ID_LAUNCHER_TIP);
  }

  // Hover highlight for splitters — targeted dirty rect
  int hover = m_tree.HitTestSplitter(x, y);
  if (hover != m_hoverSplitter) {
    RECT dirty = {};
    if (m_hoverSplitter >= 0) ExpandRect(dirty, m_tree.GetNode(m_hoverSplitter).splitterRect);
    m_hoverSplitter = hover;
    if (hover >= 0) ExpandRect(dirty, m_tree.GetNode(hover).splitterRect);
    if (dirty.right > dirty.left) InvalidateRect(m_hwnd, &dirty, FALSE);
    if (hover >= 0)
      SetTimer(m_hwnd, TIMER_ID_HOVER, 60, nullptr);
    else
      KillTimer(m_hwnd, TIMER_ID_HOVER);
  }

  // Hover highlight for tabs and menu button — targeted dirty rect
  int hPane = -1, hTab = -1;
  for (int i = 0; i < m_tree.GetLeafCount(); i++) {
    int paneId = m_tree.GetPaneId(m_tree.GetLeafList()[i]);
    if (paneId < 0) continue;
    int t = TabHitTest(paneId, x, y);
    if (t != -1) { hPane = paneId; hTab = t; break; }
  }
  if (hPane != m_hoverPane || hTab != m_hoverTab) {
    // Compute visual dirty rect for items that actually paint differently on hover
    // (tabs >= 0 and menu button == -2; scroll arrows -3/-4 have no hover paint)
    auto HoverItemRect = [&](int pane, int tab) -> RECT {
      RECT empty = {};
      if (pane < 0 || tab == -1) return empty;
      if (tab >= 0) return GetTabRect(pane, tab);
      if (tab == -2) {
        // Menu button rect
        const RECT& pr = m_tree.GetPaneRect(pane);
        RECT r = { pr.right - PANE_MENU_BTN_WIDTH, pr.top,
                   pr.right, pr.top + TAB_BAR_HEIGHT };
        return r;
      }
      return empty;
    };

    RECT dirty = {};
    ExpandRect(dirty, HoverItemRect(m_hoverPane, m_hoverTab));
    ExpandRect(dirty, HoverItemRect(hPane, hTab));

    m_hoverPane = hPane;
    m_hoverTab = hTab;

    if (dirty.right > dirty.left) InvalidateRect(m_hwnd, &dirty, FALSE);
    else InvalidateRect(m_hwnd, nullptr, FALSE);

    if (hTab != -1 && m_hoverSplitter < 0)
      SetTimer(m_hwnd, TIMER_ID_HOVER, 60, nullptr);
    else if (hTab == -1 && m_hoverSplitter < 0)
      KillTimer(m_hwnd, TIMER_ID_HOVER);
  }
}

void MaxPaneContainer::OnLButtonUp(int x, int y)
{
  if (m_dragState.sourcePaneId >= 0) {
    EndTabDrag(x, y);
    return;
  }

  if (m_tree.IsDragging()) {
    m_tree.EndDrag();
    ReleaseCapture();
    // v2.4.0 — with the drag-time reposition throttled to ~20 Hz, the last
    // mouse move may not have repositioned children; land the exact final
    // layout here (correct-height recalc + reposition + repaint).
    RefreshLayout();
    SaveState();
  }
}

// =========================================================================
// Keyboard navigation
// =========================================================================

void MaxPaneContainer::NextTab()
{
  if (!m_tree.IsPaneIdUsed(m_focusedPaneId)) m_focusedPaneId = 0;
  const PaneState* ps = m_winMgr.GetPaneState(m_focusedPaneId);
  if (!ps || ps->tabCount < 2) return;
  int next = (ps->activeTab + 1) % ps->tabCount;
  m_winMgr.SetActiveTab(m_focusedPaneId, next);
  FocusActiveFxTab(m_focusedPaneId);  // F12 — user-initiated hotkey switch
  m_winMgr.RepositionAll(m_tree);
  InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MaxPaneContainer::PrevTab()
{
  if (!m_tree.IsPaneIdUsed(m_focusedPaneId)) m_focusedPaneId = 0;
  const PaneState* ps = m_winMgr.GetPaneState(m_focusedPaneId);
  if (!ps || ps->tabCount < 2) return;
  int prev = (ps->activeTab - 1 + ps->tabCount) % ps->tabCount;
  m_winMgr.SetActiveTab(m_focusedPaneId, prev);
  FocusActiveFxTab(m_focusedPaneId);  // F12 — user-initiated hotkey switch
  m_winMgr.RepositionAll(m_tree);
  InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MaxPaneContainer::NextPane()
{
  int leafCount = m_tree.GetLeafCount();
  if (leafCount < 2) return;
  const int* leaves = m_tree.GetLeafList();

  // Find current leaf index
  int curIdx = 0;
  for (int i = 0; i < leafCount; i++) {
    if (m_tree.GetPaneId(leaves[i]) == m_focusedPaneId) {
      curIdx = i;
      break;
    }
  }

  int nextIdx = (curIdx + 1) % leafCount;
  m_focusedPaneId = m_tree.GetPaneId(leaves[nextIdx]);
  InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MaxPaneContainer::PrevPane()
{
  int leafCount = m_tree.GetLeafCount();
  if (leafCount < 2) return;
  const int* leaves = m_tree.GetLeafList();

  int curIdx = 0;
  for (int i = 0; i < leafCount; i++) {
    if (m_tree.GetPaneId(leaves[i]) == m_focusedPaneId) {
      curIdx = i;
      break;
    }
  }

  int prevIdx = (curIdx - 1 + leafCount) % leafCount;
  m_focusedPaneId = m_tree.GetPaneId(leaves[prevIdx]);
  InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MaxPaneContainer::SoloToggleFocused()
{
  if (m_soloActive) {
    ToggleSolo(m_soloPaneId);
  } else if (m_tree.IsPaneIdUsed(m_focusedPaneId)) {
    ToggleSolo(m_focusedPaneId);
  }
}
