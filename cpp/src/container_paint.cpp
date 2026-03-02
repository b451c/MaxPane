// container_paint.cpp — Rendering methods for ReDockItContainer
// (OnPaint, DrawTabBar)
#include "container.h"
#include "config.h"

// =========================================================================
// OnPaint
// =========================================================================

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

      if (m_brushEmptyHeader) FillRect(hdc, &headerRect, m_brushEmptyHeader);

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

// =========================================================================
// DrawTabBar
// =========================================================================

void ReDockItContainer::DrawTabBar(HDC hdc, int paneId, const RECT& paneRect)
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || ps->tabCount == 0) return;

  int tabBarTop = paneRect.top;
  int tabBarBottom = tabBarTop + TAB_BAR_HEIGHT;
  int paneWidth = paneRect.right - paneRect.left;

  RECT barRect = { paneRect.left, tabBarTop, paneRect.right, tabBarBottom };
  if (m_brushTabBarBg) FillRect(hdc, &barRect, m_brushTabBarBg);

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
      bool isHover = (paneId == m_hoverPane && t == m_hoverTab);
      bool hasCustomColor = (ci > 0 && ci < TAB_COLOR_COUNT);
      if (!isHover && !hasCustomColor) {
        // Fast path: use cached brush for default colors
        HBRUSH cached = (t == ps->activeTab) ? m_brushTabActive : m_brushTabInactive;
        if (cached) FillRect(hdc, &tabRect, cached);
      } else {
        // Dynamic color: custom tab color and/or hover highlight
        COLORREF bgColor;
        if (hasCustomColor) {
          const TabColor& tc = TAB_COLORS[ci];
          if (t == ps->activeTab) {
            bgColor = RGB(tc.r, tc.g, tc.b);
          } else {
            bgColor = RGB(tc.r / 2, tc.g / 2, tc.b / 2);
          }
        } else {
          bgColor = (t == ps->activeTab) ? COLOR_TAB_ACTIVE_BG : COLOR_TAB_INACTIVE_BG;
        }
        if (isHover) {
          int r = GetRValue(bgColor) + TAB_HOVER_LIGHTEN;
          int g = GetGValue(bgColor) + TAB_HOVER_LIGHTEN;
          int b = GetBValue(bgColor) + TAB_HOVER_LIGHTEN;
          bgColor = RGB(r < 255 ? r : 255, g < 255 ? g : 255, b < 255 ? b : 255);
        }
        HBRUSH tabBrush = CreateSolidBrush(bgColor);
        FillRect(hdc, &tabRect, tabBrush);
        DeleteObject(tabBrush);
      }
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
