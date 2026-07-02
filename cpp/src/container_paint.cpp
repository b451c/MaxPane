// container_paint.cpp — Rendering methods for MaxPaneContainer
// (OnPaint, DrawTabBar)
#include "container.h"
#include "config.h"
#include "launcher.h"
#include "nav_bar.h"
#include "drag_dock.h"
#include "workspace_manager.h"
#include "swell_cocoa_helpers.h"

// =========================================================================
// OnPaint
// =========================================================================

void MaxPaneContainer::OnPaint(HDC hdc)
{
  RECT rc;
  GetClientRect(m_hwnd, &rc);
  bool dark = MaxPaneIsDarkMode();

  // ADR-026 — paint the persistent nav bar at the very top first. The
  // pane grid below renders into the reserved client area (tree origin
  // already set to NAV_BAR_HEIGHT). Done before any other paint so all
  // states paint *under* the bar, never overlapping into it.
  // Feature A — pass current workspace name + dirty flag so NavBar paints
  // the centered label between the two button groups.
  NavBar::Layout navLay = {};
  NavBar::State navState{};
  if (m_navBarVisible) {
    navState.hoverButton    = m_navHover;
    navState.collapsed      = m_navBarCollapsed;
    navState.dragModeArmed  = (m_drag.mode != DragDock::IDLE);
    navState.homeActive     = m_homeOverlay;
    navState.workspaceName  = m_currentWorkspaceName[0] ? m_currentWorkspaceName : nullptr;
    navState.workspaceDirty = m_workspaceDirty;
    navLay = NavBar::Compute(rc, navState, hdc);
    NavBar::Paint(hdc, navLay, navState, dark);
  }

  // Empty-container launcher hero (ADR-013): when the whole container is
  // empty and idle, replace the generic empty-pane UI with a premium
  // workspace launcher. Disappears as soon as any pane has content. We
  // pass a sub-rect that excludes the nav bar strip so the launcher
  // re-centers below the toolbar.
  if (IsInLauncherMode()) {
    RECT below = rc;
    below.top += NavBarReservedHeight();
    Launcher::Layout lay = Launcher::Compute(below, *m_wsMgr);
    Launcher::Paint(hdc, lay, *m_wsMgr, m_launcherHover, dark, m_captureMode.active,
                    m_captureMode.hoverName);
    if (m_launcherTooltipCard >= 0) {
      Launcher::PaintTooltip(hdc, lay, *m_wsMgr, m_launcherTooltipCard, dark);
    }
    if (m_navBarVisible && m_navTooltipBtn != NavBar::BTN_NONE) {
      NavBar::PaintTooltip(hdc, navLay, m_navTooltipBtn, navState, dark);
    }
    PaintToast(hdc, rc);
    return;
  }

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
    // Tab bar area — always paint. U12 (ADR-068): height via PaneHeaderHeight
    // (single source of truth), else clean mode left a stale gray strip where
    // the hardcoded TAB_BAR_HEIGHT fill no longer matched the real header.
    const int hdrH = m_winMgr.PaneHeaderHeight(paneId);
    if (hdrH > 0) {
      RECT tabBarRect = { pr.left, pr.top, pr.right, pr.top + hdrH };
      FillRect(hdc, &tabBarRect, m_brushPaneBg);
    }
    // Content area — only paint if pane has no visible captured child
    const PaneState* ps = m_winMgr.GetPaneState(paneId);
    const TabEntry* activeTab = nullptr;
    bool hasVisibleChild = false;
    if (ps && ps->activeTab >= 0 && ps->activeTab < ps->tabCount) {
      activeTab = &ps->tabs[ps->activeTab];
      // ADR-062 — a floor-hidden ReaImGui child counts as NOT covering the
      // pane: without the visibility check the content fill is skipped and
      // the hint text accumulates over the previous tab's stale last frame
      // (Win32 repro: smeared hint + Track Manager ghost under it).
      hasVisibleChild = (activeTab->captured && activeTab->hwnd &&
                         IsWindow(activeTab->hwnd) &&
                         IsWindowVisible(activeTab->hwnd));
    }
    if (!hasVisibleChild) {
      RECT contentRect = { pr.left, pr.top + hdrH, pr.right, pr.bottom };
      FillRect(hdc, &contentRect, m_brushPaneBg);
    } else {
      // U8 (ADR-068, poydepzaj1616 #65) — a visible child smaller than its
      // pane (a window that refuses our resize) leaves a strip OnPaint never
      // filled and WM_ERASEBKGND never erases (returns 1): stale pixels
      // flicker at the pane bottom on partial repaints. Fill the uncovered
      // remainder below and to the right of the child.
      int cx, cy, cw, ch;
      WindowManager::GetChildRectInParentClient(activeTab->hwnd, m_hwnd,
                                                &cx, &cy, &cw, &ch);
      if (cy + ch < pr.bottom) {
        RECT below = { pr.left, cy + ch, pr.right, pr.bottom };
        FillRect(hdc, &below, m_brushPaneBg);
      }
      if (cx + cw < pr.right) {
        RECT rightStrip = { cx + cw, pr.top + hdrH, pr.right, pr.bottom };
        FillRect(hdc, &rightStrip, m_brushPaneBg);
      }
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
      // U12 (ADR-068) — clean mode: no header to draw for occupied panes.
      if (m_winMgr.PaneHeaderHeight(paneId) > 0)
        DrawTabBar(hdc, paneId, paneRect);
      // ADR-062 — floor-hidden hint. A captured ReaImGui window whose pane
      // is smaller than its (learned) content-min is HIDDEN by RepositionAll
      // instead of overflowing the pane (the overflow used to cover the
      // splitter + the next pane's tab bar). Without a hint the pane reads
      // as a dead grey hole — say why and how to get the window back.
      const TabEntry* at = (ps->activeTab >= 0 && ps->activeTab < ps->tabCount)
                             ? &ps->tabs[ps->activeTab] : nullptr;
      if (at && at->captured && at->isReaImGui && at->hwnd &&
          IsWindow(at->hwnd) && !IsWindowVisible(at->hwnd)) {
        int needW = ARB_PANE_MIN, needH = ARB_PANE_MIN;
        if (at->arbMinW > needW) needW = at->arbMinW;
        if (at->arbMinH > needH) needH = at->arbMinH;
        RECT hintRect = paneRect;
        hintRect.top += m_winMgr.PaneHeaderHeight(paneId);
        hintRect.left += 8;
        hintRect.right -= 8;
        char hint[400];
        snprintf(hint, sizeof(hint),
                 "'%.200s' needs at least %dx%d px.\nEnlarge this pane to show it.",
                 at->name, needW, needH);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, MaxPaneIsDarkMode() ? RGB(150, 150, 150)
                                              : RGB(80, 80, 80));
        // DT_CALCRECT-free vertical centering: let DrawText wrap within the
        // pane and offset the rect to roughly mid-height for short hints.
        RECT centered = hintRect;
        int paneH = hintRect.bottom - hintRect.top;
        if (paneH > 60) centered.top += paneH / 2 - 24;
        DrawTextUtf8(hdc, hint, -1, &centered,
                     DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
      }
    } else {
      RECT headerRect = paneRect;
      headerRect.bottom = headerRect.top + TAB_BAR_HEIGHT;

      if (m_brushEmptyHeader) FillRect(hdc, &headerRect, m_brushEmptyHeader);

      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, COLOR_EMPTY_HEADER_TEXT);
      char headerText[320];
      if (m_captureMode.active && m_captureMode.targetPaneId == paneId) {
        // ADR-048 — live preview of the resolved target before the user clicks.
        if (m_captureMode.hoverName[0]) {
          snprintf(headerText, sizeof(headerText),
                   " Capture: %s   (%s)", m_captureMode.hoverName,
                   CAPTURE_CANCEL_HINT);
        } else {
          snprintf(headerText, sizeof(headerText),
                   " Click a window to capture...   (%s)", CAPTURE_CANCEL_HINT);
        }
      } else {
        // Show sequential visual index (1-based position in leaf list)
        snprintf(headerText, sizeof(headerText), " Pane %d (click to assign)", i + 1);
      }
      RECT headerTextRect = headerRect;
      headerTextRect.left += 4;
      DrawTextUtf8(hdc, headerText, -1, &headerTextRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

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

      SetTextColor(hdc, MaxPaneIsDarkMode() ? RGB(120, 120, 120) : RGB(80, 80, 80));
      DrawTextUtf8(hdc, "Click header to assign a window", -1, &contentRect,
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

  // ADR-026 — drag-to-dock preview during TRACKING. Renders the live
  // drop-zone highlight + hint label inside whatever pane the cursor is
  // hovering. Painted after pane grid so the highlight sits on top of
  // tab bars + content; nav bar still wins because it occupies its own
  // reserved strip.
  if (m_drag.mode == DragDock::TRACKING &&
      m_drag.targetPaneId >= 0 &&
      m_drag.zone != DragDock::ZONE_NONE &&
      m_tree.IsPaneIdUsed(m_drag.targetPaneId)) {
    const RECT& pr = m_tree.GetPaneRect(m_drag.targetPaneId);
    DragDock::PaintPreview(hdc, pr, m_drag.zone, dark);
    DragDock::PaintIndicator(hdc, pr, m_drag.zone, dark, m_drag.sourceTitle);
  }

  // ADR-026 — Home overlay: paint a backdrop over the pane grid + the
  // workspace launcher cards on top. Non-destructive — ESC / click
  // outside cards returns to the underlying pane state untouched.
  if (m_homeOverlay) {
    RECT below = rc;
    below.top += NavBarReservedHeight();
    // Backdrop — solid panel-bg color (SWELL has no per-pixel alpha; the
    // pane grid below is fully obscured for a clean "modal" feel).
    HBRUSH backdrop = CreateSolidBrush(dark ? RGB(28, 28, 28) : RGB(240, 240, 240));
    FillRect(hdc, &below, backdrop);
    DeleteObject(backdrop);
    Launcher::Layout lay = Launcher::Compute(below, *m_wsMgr);
    Launcher::Paint(hdc, lay, *m_wsMgr, m_homeOverlayHover, dark);
    // Sprint 1 Entry 18 — tooltip on home-overlay cards. Symmetric to
    // the IsInLauncherMode branch above; the previous block only ran
    // PaintTooltip when no captures were active.
    if (m_launcherTooltipCard >= 0) {
      Launcher::PaintTooltip(hdc, lay, *m_wsMgr, m_launcherTooltipCard, dark);
    }
  }

  // ADR-026 — nav bar tooltip, drawn last so it sits above every other
  // pane-grid pixel (tooltip dips into the pane area below the bar).
  if (m_navBarVisible && m_navTooltipBtn != NavBar::BTN_NONE) {
    NavBar::PaintTooltip(hdc, navLay, m_navTooltipBtn, navState, dark);
  }

  // Toast overlay last so it sits above every other layer (Sprint 3.2).
  PaintToast(hdc, rc);
}

// =========================================================================
// DrawTabBar
// =========================================================================

void MaxPaneContainer::DrawTabBar(HDC hdc, int paneId, const RECT& paneRect)
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || ps->tabCount == 0) return;

  int tabBarTop = paneRect.top;
  const int hdrH = m_winMgr.PaneHeaderHeight(paneId);
  int tabBarBottom = tabBarTop + hdrH;

  RECT barRect = { paneRect.left, tabBarTop, paneRect.right, tabBarBottom };
  if (m_brushTabBarBg) FillRect(hdc, &barRect, m_brushTabBarBg);

  // ADR-055 — collapsed sliver: a flat strip in the (single) tab's color —
  // the pane keeps its identity cue without text, close button or icons.
  // Hit-testing maps the whole strip to tab 0 (TabHitTest), so the strip is
  // still the pane's right-click + drag surface.
  if (hdrH < TAB_BAR_HEIGHT) {
    int ci = ps->tabs[0].colorIndex;
    if (ci > 0 && ci < TAB_COLOR_COUNT) {
      const TabColor& tc = TAB_COLORS[ci];
      HBRUSH b = CreateSolidBrush(RGB(tc.r, tc.g, tc.b));
      FillRect(hdc, &barRect, b);
      DeleteObject(b);
    } else if (m_brushTabActive) {
      FillRect(hdc, &barRect, m_brushTabActive);
    }
    return;
  }

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
    // C2 (ADR-027) — pinned tabs prepend a small bullet glyph as a pin
    // indicator. Kept ASCII-safe ("• ") instead of 📌 emoji because the
    // dock font on macOS doesn't always have the emoji glyph at this
    // size; the bullet renders at every system font.
    if (ps->tabs[t].pinned) {
      RECT pinRect = textRect;
      pinRect.right = pinRect.left + 10;
      DrawTextUtf8(hdc, "\xE2\x80\xA2", -1, &pinRect,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      textRect.left += 10;
    }
    const char* tabName = ps->tabs[t].name[0] ? ps->tabs[t].name : "?";
    DrawTextUtf8(hdc, tabName, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Only draw close button if tab is wide enough to avoid overlap (B9)
    if (lay.tabWidth >= TAB_CLOSE_MIN_WIDTH) {
      SetTextColor(hdc, COLOR_TAB_CLOSE_TEXT);
      RECT closeRect = { tabRight - 16, tabBarTop + 2, tabRight - 2, tabBarBottom - 2 };
      DrawTextUtf8(hdc, "x", 1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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
    DrawTextUtf8(hdc, "\xe2\x96\xbc", -1, &btnRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
}
