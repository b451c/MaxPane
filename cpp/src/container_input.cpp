// container_input.cpp — Input handling for MaxPaneContainer
// (mouse events, tab drag/drop, hit testing, resize)
#include "container.h"
#include "config.h"

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
  int tabBarBottom = tabBarTop + TAB_BAR_HEIGHT;
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
  int tabBarBottom = tabBarTop + TAB_BAR_HEIGHT;

  if (y < tabBarTop || y >= tabBarBottom) return -1;
  if (x < paneRect.left || x >= paneRect.right) return -1;

  // Menu button (rightmost PANE_MENU_BTN_WIDTH pixels)
  if (x >= paneRect.right - PANE_MENU_BTN_WIDTH) return -2;

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

  short escState = GetAsyncKeyState(VK_ESCAPE);
  if (escState & 0x8000) {
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
  m_tree.Recalculate(cx, cy);
  m_winMgr.RepositionAll(m_tree);
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
    m_tree.Drag(x, y, rc.right - rc.left, rc.bottom - rc.top);
    m_winMgr.RepositionAll(m_tree);
    InvalidateRect(m_hwnd, nullptr, FALSE);
    return;
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
