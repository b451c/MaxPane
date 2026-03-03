// container_paint.cpp — Rendering methods for MaxPaneContainer
// (OnPaint, DrawTabBar)
#include "container.h"
#include "config.h"
#include "swell_cocoa_helpers.h"

// =========================================================================
// OnPaint
// =========================================================================

void MaxPaneContainer::OnPaint(HDC hdc)
{
  RECT rc;
  GetClientRect(m_hwnd, &rc);

  // Background — paint only areas NOT occupied by captured child windows.
  // SWELL doesn't implement WS_CLIPCHILDREN, so a full-area FillRect would
  // overwrite child windows that don't repaint frequently (e.g. toolbars).
  // We fill: splitter rects, tab bar areas, and content areas of empty panes.
  // Content areas of panes with a visible captured child are left untouched.
  for (int i = 0; i < m_tree.GetLeafCount(); i++) {
    int nodeIdx = m_tree.GetLeafList()[i];
    int paneId = m_tree.GetPaneId(nodeIdx);
    if (paneId < 0 || paneId >= MAX_PANES) continue;
    const RECT& pr = m_tree.GetPaneRect(paneId);
    // Tab bar area — always paint
    RECT tabBarRect = { pr.left, pr.top, pr.right, pr.top + TAB_BAR_HEIGHT };
    FillRect(hdc, &tabBarRect, m_brushPaneBg);
    // Content area — only paint if pane has no visible captured child
    const PaneState* ps = m_winMgr.GetPaneState(paneId);
    bool hasVisibleChild = false;
    if (ps && ps->activeTab >= 0 && ps->activeTab < ps->tabCount) {
      const TabEntry* tab = &ps->tabs[ps->activeTab];
      hasVisibleChild = (tab->captured && tab->hwnd && IsWindow(tab->hwnd));
    }
    if (!hasVisibleChild) {
      RECT contentRect = { pr.left, pr.top + TAB_BAR_HEIGHT, pr.right, pr.bottom };
      FillRect(hdc, &contentRect, m_brushPaneBg);
    }
  }
  // Splitter rects
  for (int i = 0; i < m_tree.GetBranchCount(); i++) {
    int branchIdx = m_tree.GetBranchList()[i];
    const SplitNode& n = m_tree.GetNode(branchIdx);
    FillRect(hdc, &n.splitterRect, m_brushPaneBg);
  }

  // Draw splitter bars
  for (int i = 0; i < m_tree.GetBranchCount(); i++) {
    int branchIdx = m_tree.GetBranchList()[i];
    const SplitNode& n = m_tree.GetNode(branchIdx);
    FillRect(hdc, &n.splitterRect, m_brushSplitter);
    if (branchIdx == m_hoverSplitter) {
      RECT hr = n.splitterRect;
      if (n.orient == SPLIT_VERTICAL) {
        hr.left += SPLITTER_HIGHLIGHT_INSET;
        hr.right -= SPLITTER_HIGHLIGHT_INSET;
      } else {
        hr.top += SPLITTER_HIGHLIGHT_INSET;
        hr.bottom -= SPLITTER_HIGHLIGHT_INSET;
      }
      FillRect(hdc, &hr, m_brushSplitterHover);
    }
  }

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

      // Subtle diagonal lines in empty panes (45°, disappear when window is captured)
      {
        HPEN oldPen = (HPEN)SelectObject(hdc, m_penGridLine);

        int cx = contentRect.left, cy = contentRect.top;
        int cw = contentRect.right - contentRect.left;
        int ch = contentRect.bottom - contentRect.top;

        for (int ox = -ch; ox < cw; ox += PANE_GRID_SPACING) {
          // Full line: (cx+ox, cy+ch) → (cx+ox+ch, cy) at 45° (\\ → /)
          // Clip horizontally: x stays within [cx, cx+cw)
          int x1 = cx + ox,      y1 = cy + ch;
          int x2 = cx + ox + ch, y2 = cy;
          if (x1 < cx) { y1 -= (cx - x1); x1 = cx; }
          if (x2 > cx + cw) { y2 += (x2 - (cx + cw)); x2 = cx + cw; }
          if (x1 < x2) {
            MoveToEx(hdc, x1, y1, nullptr);
            LineTo  (hdc, x2, y2);
          }
        }

        SelectObject(hdc, oldPen);
      }

      SetTextColor(hdc, IsSystemDarkMode() ? RGB(120, 120, 120) : RGB(80, 80, 80));
      DrawText(hdc, "Click header to assign a window", -1, &contentRect,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
  }

  // Drag highlight (cross-pane)
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

  // Intra-pane reorder insertion indicator (vertical line)
  if (m_dragState.active && m_dragState.dragStarted && m_dragState.insertTabIndex >= 0) {
    int srcPane = m_dragState.sourcePaneId;
    if (srcPane >= 0 && m_tree.IsPaneIdUsed(srcPane)) {
      RECT insertTabRect = GetTabRect(srcPane, m_dragState.insertTabIndex);
      int lineX = insertTabRect.left;
      // Draw insertion line at the left edge of the target tab
      // If dragging rightward past source, draw at right edge instead
      if (m_dragState.insertTabIndex > m_dragState.sourceTabIndex) {
        lineX = insertTabRect.right;
      }
      HPEN insertPen = CreatePen(PS_SOLID, 2, COLOR_DRAG_HIGHLIGHT);
      HPEN oldPen = (HPEN)SelectObject(hdc, insertPen);
      MoveToEx(hdc, lineX, insertTabRect.top + 2, nullptr);
      LineTo(hdc, lineX, insertTabRect.bottom - 2);
      SelectObject(hdc, oldPen);
      DeleteObject(insertPen);
    }
  }
}

// =========================================================================
// DrawTabBar
// =========================================================================

void MaxPaneContainer::DrawTabBar(HDC hdc, int paneId, const RECT& paneRect)
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || ps->tabCount == 0) return;

  int tabBarTop = paneRect.top;
  int tabBarBottom = tabBarTop + TAB_BAR_HEIGHT;

  RECT barRect = { paneRect.left, tabBarTop, paneRect.right, tabBarBottom };
  if (m_brushTabBarBg) FillRect(hdc, &barRect, m_brushTabBarBg);

  TabBarLayout lay = CalcTabBarLayout(paneId);

  // Draw tabs
  for (int t = 0; t < ps->tabCount; t++) {
    int tabLeft = lay.tabAreaLeft + t * lay.tabWidth;
    int tabRight = tabLeft + lay.tabWidth;
    if (tabRight > lay.tabAreaRight) tabRight = lay.tabAreaRight;

    RECT tabRect = { tabLeft, tabBarTop, tabRight, tabBarBottom };

    {
      int ci = ps->tabs[t].colorIndex;
      bool isHover = (paneId == m_hoverPane && t == m_hoverTab);
      bool hasCustomColor = (ci > 0 && ci < TAB_COLOR_COUNT);
      if (!isHover && !hasCustomColor) {
        HBRUSH cached = (t == ps->activeTab) ? m_brushTabActive : m_brushTabInactive;
        if (cached) FillRect(hdc, &tabRect, cached);
      } else {
        COLORREF bgColor;
        if (hasCustomColor) {
          const TabColor& tc = TAB_COLORS[ci];
          bgColor = (t == ps->activeTab) ? RGB(tc.r, tc.g, tc.b) : RGB(tc.r / 2, tc.g / 2, tc.b / 2);
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
    {
      COLORREF textColor;
      int ci = ps->tabs[t].colorIndex;
      if (ci > 0 && ci < TAB_COLOR_COUNT && t == ps->activeTab) {
        // Active tab with custom color: pick black or white based on luminance
        const TabColor& tc = TAB_COLORS[ci];
        int lum = (tc.r * 299 + tc.g * 587 + tc.b * 114) / 1000;
        textColor = lum > 140 ? RGB(0, 0, 0) : COLOR_TAB_ACTIVE_TEXT;
      } else {
        textColor = (t == ps->activeTab) ? COLOR_TAB_ACTIVE_TEXT : COLOR_TAB_INACTIVE_TEXT;
      }
      SetTextColor(hdc, textColor);
    }
    RECT textRect = tabRect;
    textRect.left += TAB_TEXT_LEFT_PAD;
    textRect.right -= TAB_TEXT_RIGHT_MARGIN;
    const char* tabName = ps->tabs[t].name[0] ? ps->tabs[t].name : "?";
    DrawText(hdc, tabName, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Only draw close button if tab is wide enough to avoid overlap
    if (lay.tabWidth >= TAB_MIN_WIDTH) {
      SetTextColor(hdc, COLOR_TAB_CLOSE_TEXT);
      RECT closeRect = { tabRight - 16, tabBarTop + 2, tabRight - 2, tabBarBottom - 2 };
      DrawText(hdc, "x", 1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (t < ps->tabCount - 1) {
      HPEN oldPen = (HPEN)SelectObject(hdc, m_penTabSeparator);
      MoveToEx(hdc, tabRight, tabBarTop + 2, nullptr);
      LineTo(hdc, tabRight, tabBarBottom - 2);
      SelectObject(hdc, oldPen);
    }
  }

  // Draw pane menu button (▼) — rightmost PANE_MENU_BTN_WIDTH px
  {
    bool menuBtnHover = (paneId == m_hoverPane && m_hoverTab == -2);
    RECT btnRect = { paneRect.right - PANE_MENU_BTN_WIDTH, tabBarTop, paneRect.right, tabBarBottom };
    if (menuBtnHover) {
      HBRUSH hoverBrush = CreateSolidBrush(RGB(70, 70, 70));
      FillRect(hdc, &btnRect, hoverBrush);
      DeleteObject(hoverBrush);
    }
    // Separator line left of button
    HPEN oldPen = (HPEN)SelectObject(hdc, m_penTabSeparator);
    MoveToEx(hdc, paneRect.right - PANE_MENU_BTN_WIDTH, tabBarTop + 2, nullptr);
    LineTo(hdc, paneRect.right - PANE_MENU_BTN_WIDTH, tabBarBottom - 2);
    SelectObject(hdc, oldPen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, menuBtnHover ? RGB(230, 230, 230) : RGB(190, 190, 190));
    DrawText(hdc, "\xe2\x96\xbc", -1, &btnRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
}
