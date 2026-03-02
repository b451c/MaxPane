// container_input.cpp — Input handling for ReDockItContainer
// (mouse events, tab drag/drop, hit testing, resize)
#include "container.h"
#include "config.h"

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
  // TODO(4.2): targeted InvalidateRect — compute dirty rect instead of full invalidate
  InvalidateRect(m_hwnd, nullptr, TRUE);
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
    // TODO(4.2): targeted InvalidateRect — compute dirty rect instead of full invalidate
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
