// container_nav.cpp — Navigation bar + Home overlay + drag-to-dock methods
// (ADR-026).
//
// Keeps container.cpp from growing further. All methods declared in
// container.h and named with their owning class are implemented here.
#include "container.h"
#include "config.h"
#include "globals.h"
#include "debug.h"
#include "context_menu.h"
#include "favorites_manager.h"
#include "workspace_manager.h"
#include "quick_switcher.h"
#include "settings_dialog.h"
#include "save_workspace_dialog.h"
#include "swell_cocoa_helpers.h"
#include "nav_bar.h"
#include "drag_dock.h"
#include "launcher.h"
#include <cstdio>
#include <cstring>

// =========================================================================
// Nav bar visibility preference
// =========================================================================

void MaxPaneContainer::LoadNavBarPref()
{
  m_navBarVisible = true;  // default ON for premium-first impression
  if (!g_GetExtState) return;
  // Read from the legacy global section (matches auto_open / dark_mode).
  // Per-instance override is a v2.1 candidate; for v2.0 the pref is global
  // so the Settings dialog (which has no per-instance awareness) DTRT.
  const char* val = g_GetExtState(EXT_SECTION, "show_nav_bar");
  if (val && val[0] == '0' && val[1] == '\0') m_navBarVisible = false;
}

void MaxPaneContainer::SaveNavBarPref()
{
  if (!g_SetExtState) return;
  g_SetExtState(EXT_SECTION, "show_nav_bar",
                m_navBarVisible ? "1" : "0", true);
}

void MaxPaneContainer::SetNavBarVisible(bool v)
{
  if (m_navBarVisible == v) return;
  m_navBarVisible = v;
  SaveNavBarPref();
  // Re-origin the tree so pane rects shift to match new available area.
  if (m_hwnd) {
    m_tree.SetOrigin(0, NavBarReservedHeight());
    RefreshLayout();
  }
}

// =========================================================================
// Nav bar mouse handling
// =========================================================================

bool MaxPaneContainer::OnNavBarMouseMove(int x, int y)
{
  if (!m_navBarVisible) return false;

  RECT rc;
  GetClientRect(m_hwnd, &rc);
  NavBar::Layout lay = NavBar::Compute(rc);
  if (!lay.visible) return false;

  // Only consume the event if cursor is actually over the bar; otherwise
  // let the caller continue to pane-grid hover paths.
  bool overBar = (y >= lay.barRect.top && y < lay.barRect.bottom);

  int hit = overBar ? NavBar::HitTest(lay, x, y) : NavBar::BTN_NONE;
  if (hit != m_navHover) {
    int oldHover = m_navHover;
    m_navHover = hit;
    // Dirty union of old + new button rects + tooltip area.
    RECT dirty = lay.barRect;
    // include a tooltip strip below the bar so previous tooltip clears.
    dirty.bottom += 36;
    InvalidateRect(m_hwnd, &dirty, FALSE);

    // Tooltip delay: kill any pending tooltip; arm a new one if hovering
    // a real button.
    KillTimer(m_hwnd, TIMER_ID_NAVBAR_TIP);
    if (m_navTooltipBtn != NavBar::BTN_NONE) {
      m_navTooltipBtn = NavBar::BTN_NONE;
    }
    if (hit != NavBar::BTN_NONE) {
      SetTimer(m_hwnd, TIMER_ID_NAVBAR_TIP, NAVBAR_TOOLTIP_DELAY_MS, nullptr);
    }
    (void)oldHover;
  }

  return overBar;
}

bool MaxPaneContainer::OnNavBarClick(int x, int y)
{
  if (!m_navBarVisible) return false;

  RECT rc;
  GetClientRect(m_hwnd, &rc);
  NavBar::Layout lay = NavBar::Compute(rc);
  if (!lay.visible) return false;
  // Outside the bar strip — not for us.
  if (y < lay.barRect.top || y >= lay.barRect.bottom) return false;

  int hit = NavBar::HitTest(lay, x, y);
  if (hit == NavBar::BTN_NONE) return true;   // consumed but no action

  DispatchNavBar(hit, x, y);
  return true;
}

// =========================================================================
// Nav bar action dispatch
// =========================================================================

void MaxPaneContainer::DispatchNavBar(int buttonId, int xClient, int yClient)
{
  switch (buttonId) {
    case NavBar::BTN_HOME:
      if (m_homeOverlay) CloseHomeOverlay();
      else               OpenHomeOverlay();
      break;

    case NavBar::BTN_DRAG:
      if (m_drag.mode != DragDock::IDLE) ExitDragMode();
      else                                EnterDragMode();
      break;

    case NavBar::BTN_SWITCH:
      OpenQuickSwitcher(m_hwnd);
      break;

    case NavBar::BTN_SAVE: {
      m_wsMgr->LoadList();
      char wsName[MAX_WORKSPACE_NAME] = "";
      if (OpenSaveWorkspaceDialog(m_hwnd, *m_wsMgr, wsName, sizeof(wsName))) {
        const bool wasReplace = (m_wsMgr->Find(wsName) != nullptr);
        SaveWorkspace(wsName);
        char msg[MAX_WORKSPACE_NAME + 64];
        snprintf(msg, sizeof(msg),
                 wasReplace ? "Replaced '%s'" : "Saved '%s'", wsName);
        ShowToast(msg);
      }
      break;
    }

    case NavBar::BTN_LOAD:
      OpenLoadDropdown(xClient, yClient);
      break;

    case NavBar::BTN_SETTINGS:
      OpenSettingsDialog(m_hwnd);
      // dark mode override may have changed — repaint container.
      InvalidateRect(m_hwnd, nullptr, FALSE);
      // Settings may toggle "show_nav_bar"; re-read so live state stays
      // in sync without requiring a restart.
      {
        bool wasVisible = m_navBarVisible;
        LoadNavBarPref();
        if (wasVisible != m_navBarVisible) {
          m_tree.SetOrigin(0, NavBarReservedHeight());
          RefreshLayout();
        }
      }
      break;

    case NavBar::BTN_SUPPORT: {
      // ADR-026 — native HMENU. Themed popup attempt reverted: on macOS
      // Cocoa, child NSViews (captured REAPER windows) always paint above
      // the parent NSView in z-order — there is no WS_CLIPCHILDREN
      // equivalent for an in-parent overlay to sit on top of them. The
      // only way to render a popup above captured panes is a separate
      // NSWindow, which is exactly what NSMenu/HMENU already gives us.
      // We accept that the native menu follows system appearance instead
      // of MaxPane's dark-mode override — premium-OSS deferred to v2.1.
      enum {
        SUP_KOFI = 13001, SUP_BMC = 13002, SUP_PAYPAL = 13003,
        SUP_GITHUB = 13010, SUP_FORUM = 13011,
      };
      struct Item { int id; const char* label; const char* url; };
      const Item items[] = {
        { SUP_KOFI,   "Ko-fi",                "https://ko-fi.com/quickmd" },
        { SUP_BMC,    "Buy Me a Coffee",      "https://buymeacoffee.com/bsroczynskh" },
        { SUP_PAYPAL, "PayPal",               "https://paypal.me/b451c" },
        { 0,          nullptr, nullptr },    // separator marker
        { SUP_GITHUB, "GitHub",               "https://github.com/b451c/MaxPane" },
        { SUP_FORUM,  "REAPER forum thread",  "https://forum.cockos.com/showthread.php?t=307267" },
      };
      HMENU menu = CreatePopupMenu();
      if (!menu) break;
      int pos = 0;
      {
        MENUITEMINFO mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE | MIIM_STATE;
        mi.fType = MFT_STRING;
        mi.fState = MFS_DISABLED;
        mi.dwTypeData = (char*)"Support MaxPane development";
        InsertMenuItem(menu, pos++, TRUE, &mi);
      }
      {
        MENUITEMINFO sep = {};
        sep.cbSize = sizeof(sep);
        sep.fMask = MIIM_TYPE;
        sep.fType = MFT_SEPARATOR;
        InsertMenuItem(menu, pos++, TRUE, &sep);
      }
      for (const Item& it : items) {
        if (!it.label) {
          MENUITEMINFO sep = {};
          sep.cbSize = sizeof(sep);
          sep.fMask = MIIM_TYPE;
          sep.fType = MFT_SEPARATOR;
          InsertMenuItem(menu, pos++, TRUE, &sep);
          continue;
        }
        MENUITEMINFO mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_ID | MIIM_TYPE;
        mi.fType = MFT_STRING;
        mi.wID = it.id;
        mi.dwTypeData = (char*)it.label;
        InsertMenuItem(menu, pos++, TRUE, &mi);
      }

      RECT rc;
      GetClientRect(m_hwnd, &rc);
      NavBar::Layout nav = NavBar::Compute(rc);
      POINT pt;
      if (nav.visible) {
        pt.x = nav.buttons[NavBar::BTN_SUPPORT].left;
        pt.y = nav.barRect.bottom;
      } else {
        pt.x = xClient;
        pt.y = yClient;
      }
      ClientToScreen(m_hwnd, &pt);
      int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                pt.x, pt.y, 0, m_hwnd, nullptr);
      DestroyMenu(menu);

      for (const Item& it : items) {
        if (!it.label) continue;
        if (cmd == it.id) {
          OpenUrlPlatform(it.url);
          ShowToast("Opening in browser...");
          break;
        }
      }
      break;
    }

    default:
      break;
  }
}

// =========================================================================
// Load dropdown
// =========================================================================

void MaxPaneContainer::OpenLoadDropdown(int xClient, int yClient)
{
  m_wsMgr->LoadList();

  HMENU menu = CreatePopupMenu();
  if (!menu) return;

  int pos = 0;
  bool anyAdded = false;
  for (int i = 0; i < m_wsMgr->GetCount(); i++) {
    const WorkspaceEntry& ws = m_wsMgr->Get(i);
    if (!ws.used || !ws.name[0]) continue;
    char label[MAX_WORKSPACE_NAME + 16];
    snprintf(label, sizeof(label), "%d  %s", i + 1, ws.name);
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::WS_LOAD_BASE + i;
    mi.dwTypeData = label;
    InsertMenuItem(menu, pos++, TRUE, &mi);
    anyAdded = true;
  }
  if (!anyAdded) {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.fState = MFS_DISABLED;
    mi.dwTypeData = (char*)"(no saved workspaces)";
    InsertMenuItem(menu, pos++, TRUE, &mi);
  }

  POINT pt = { xClient, yClient };
  ClientToScreen(m_hwnd, &pt);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                           pt.x, pt.y, 0, m_hwnd, nullptr);
  DestroyMenu(menu);

  if (cmd >= MenuIds::WS_LOAD_BASE && cmd < MenuIds::WS_LOAD_BASE + MAX_WORKSPACES) {
    int idx = cmd - MenuIds::WS_LOAD_BASE;
    if (idx >= 0 && idx < m_wsMgr->GetCount()) {
      const WorkspaceEntry& ws = m_wsMgr->Get(idx);
      if (ws.used) LoadWorkspace(ws.name);
    }
  }
}

// =========================================================================
// Home overlay
// =========================================================================

// Hide every active captured window. SWELL doesn't implement WS_CLIPCHILDREN,
// so the Home overlay's backdrop FillRect alone can't cover captured panes
// (Routing Matrix etc. would bleed through the cards). Symmetric counterpart
// ShowActiveCaptures restores visibility on cancel.
namespace {
  void HideActiveCaptures(WindowManager& wm)
  {
    for (int p = 0; p < MAX_PANES; p++) {
      const PaneState* ps = wm.GetPaneState(p);
      if (!ps || ps->tabCount == 0) continue;
      if (ps->activeTab < 0 || ps->activeTab >= ps->tabCount) continue;
      const TabEntry& tab = ps->tabs[ps->activeTab];
      if (tab.captured && tab.hwnd && IsWindow(tab.hwnd)) {
        ShowWindow(tab.hwnd, SW_HIDE);
      }
    }
  }
  void ShowActiveCaptures(WindowManager& wm)
  {
    for (int p = 0; p < MAX_PANES; p++) {
      const PaneState* ps = wm.GetPaneState(p);
      if (!ps || ps->tabCount == 0) continue;
      if (ps->activeTab < 0 || ps->activeTab >= ps->tabCount) continue;
      const TabEntry& tab = ps->tabs[ps->activeTab];
      if (tab.captured && tab.hwnd && IsWindow(tab.hwnd)) {
        ShowWindow(tab.hwnd, SW_SHOWNA);
      }
    }
  }
}

void MaxPaneContainer::OpenHomeOverlay()
{
  if (m_homeOverlay) return;
  // If drag mode is armed, leaving Home as the active visual would be
  // ambiguous — cancel drag first.
  if (m_drag.mode != DragDock::IDLE) ExitDragMode();
  m_wsMgr->LoadList();
  m_homeOverlay = true;
  m_homeOverlayHover = -1;
  HideActiveCaptures(m_winMgr);
  InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MaxPaneContainer::CloseHomeOverlay()
{
  if (!m_homeOverlay) return;
  m_homeOverlay = false;
  m_homeOverlayHover = -1;
  // Sprint 1 Entry 18 — clear tooltip state so reopening the overlay
  // shows fresh state, not a stale tooltip from the previous session.
  m_launcherTooltipCard = -1;
  KillTimer(m_hwnd, TIMER_ID_LAUNCHER_TIP);
  // Restore the captures we hid in OpenHomeOverlay. If a workspace was
  // loaded instead (OnHomeOverlayClick card path), it bypasses this
  // function and lets LoadWorkspace release the captures cleanly.
  ShowActiveCaptures(m_winMgr);
  InvalidateRect(m_hwnd, nullptr, FALSE);
}

bool MaxPaneContainer::OnHomeOverlayMouseMove(int x, int y)
{
  if (!m_homeOverlay) return false;
  RECT rc;
  GetClientRect(m_hwnd, &rc);
  RECT below = rc;
  below.top += NavBarReservedHeight();
  Launcher::Layout lay = Launcher::Compute(below, *m_wsMgr);
  int hit = Launcher::HitTest(lay, x, y);
  if (hit != m_homeOverlayHover) {
    m_homeOverlayHover = hit;
    // Sprint 1 Entry 18 — home overlay tooltip wiring. Tooltip worked in
    // empty-launcher mode but not when home was opened over an active
    // workspace; the hover handler tracked m_homeOverlayHover but never
    // armed TIMER_ID_LAUNCHER_TIP. Symmetric to launcher hover (see
    // OnMouseMove launcher branch).
    if (m_launcherTooltipCard >= 0) m_launcherTooltipCard = -1;
    if (hit >= 0) SetTimer(m_hwnd, TIMER_ID_LAUNCHER_TIP, LAUNCHER_TOOLTIP_DELAY_MS, nullptr);
    else          KillTimer(m_hwnd, TIMER_ID_LAUNCHER_TIP);
    InvalidateRect(m_hwnd, nullptr, FALSE);
  }
  return true;
}

bool MaxPaneContainer::OnHomeOverlayClick(int x, int y)
{
  if (!m_homeOverlay) return false;
  RECT rc;
  GetClientRect(m_hwnd, &rc);
  RECT below = rc;
  below.top += NavBarReservedHeight();
  Launcher::Layout lay = Launcher::Compute(below, *m_wsMgr);
  int hit = Launcher::HitTest(lay, x, y);
  if (hit >= 0 && hit < m_wsMgr->GetCount()) {
    // Bypass CloseHomeOverlay's re-show — LoadWorkspace will release the
    // currently-captured windows anyway; re-showing them between hide and
    // release would produce a one-frame flash.
    const WorkspaceEntry ws = m_wsMgr->Get(hit);
    m_homeOverlay = false;
    m_homeOverlayHover = -1;
    InvalidateRect(m_hwnd, nullptr, FALSE);
    if (ws.used) LoadWorkspace(ws.name);
    return true;
  }
  if (hit == Launcher::HIT_CAPTURE_BUTTON) {
    // Capture-mode CTA: close overlay (re-show captures) then enter
    // capture-by-click on focused pane. User expects to see existing
    // captures normally while selecting the target window.
    int paneId = m_focusedPaneId;
    if (!m_tree.IsPaneIdUsed(paneId)) {
      if (m_tree.GetLeafCount() > 0) {
        paneId = m_tree.GetPaneId(m_tree.GetLeafList()[0]);
      }
    }
    CloseHomeOverlay();
    EnterCaptureMode(paneId);  // ADR-048 — single arm path (guards paneId<0)
    return true;
  }
  // Clicked outside cards/CTA → close overlay (non-destructive return).
  CloseHomeOverlay();
  return true;
}

// =========================================================================
// Drag-to-dock state machine
// =========================================================================

void MaxPaneContainer::EnterDragMode()
{
  // Cancel any overlapping UI states.
  if (m_homeOverlay) CloseHomeOverlay();
  if (m_captureMode.active) {
    ExitCaptureMode();  // ADR-048 — pops the capture crosshair
  }

  DragDock::Reset(m_drag);
  m_drag.mode = DragDock::ARMED;
  m_drag.lastBtnDown = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
  m_drag.targetPaneId = -1;
  m_drag.zone = DragDock::ZONE_NONE;

  SetTimer(m_hwnd, TIMER_ID_DRAG_DOCK, DRAG_DOCK_TICK_MS, nullptr);
  InvalidateRect(m_hwnd, nullptr, FALSE);
  DBG("[MaxPane] EnterDragMode[%s]\n", m_extSection);
}

void MaxPaneContainer::ExitDragMode()
{
  if (m_drag.mode == DragDock::IDLE) return;
  KillTimer(m_hwnd, TIMER_ID_DRAG_DOCK);
  DragDock::Reset(m_drag);
  InvalidateRect(m_hwnd, nullptr, FALSE);
  DBG("[MaxPane] ExitDragMode[%s]\n", m_extSection);
}

void MaxPaneContainer::DragModeTick()
{
  if (m_drag.mode == DragDock::IDLE) {
    KillTimer(m_hwnd, TIMER_ID_DRAG_DOCK);
    return;
  }

  // Esc cancels at any time.
  if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
    ExitDragMode();
    return;
  }

  bool btnDown = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
  bool shift   = ((GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0);
  m_drag.shiftHeld = shift;

  POINT screenPt;
  GetCursorPos(&screenPt);
  m_drag.cursorScreenPos = screenPt;

  if (m_drag.mode == DragDock::ARMED) {
    // Transition btn-up → btn-down outside MaxPane → start tracking.
    if (!m_drag.lastBtnDown && btnDown) {
      HWND target = WindowFromPoint(screenPt);
      // Skip clicks on the MaxPane container itself (nav bar release etc.)
      HWND walk = target;
      bool overSelf = false;
      while (walk) {
        if (walk == m_hwnd) { overSelf = true; break; }
        walk = GetParent(walk);
      }
      if (overSelf) {
        m_drag.lastBtnDown = btnDown;
        return;
      }

      // Walk up to a sensible top-level (skip own dock frames).
      HWND topLevel = target;
      HWND parent = GetParent(topLevel);
      while (parent && parent != g_reaperMainHwnd) {
        topLevel = parent;
        parent = GetParent(topLevel);
      }
      if (!topLevel || topLevel == m_hwnd || topLevel == g_reaperMainHwnd) {
        // Not a window we can capture; ignore this click, stay armed.
        m_drag.lastBtnDown = btnDown;
        return;
      }
      if (m_winMgr.IsWindowCaptured(topLevel)) {
        // Already captured — refuse.
        m_drag.lastBtnDown = btnDown;
        return;
      }

      char title[256];
      GetWindowText(topLevel, title, sizeof(title));
      if (!title[0]) {
        m_drag.lastBtnDown = btnDown;
        return;
      }
      // Track potential dock-frame split (mirrors capture-by-click logic).
      HWND captureHwnd = topLevel;
      char* dockedSuffix = strstr(title, " (docked)");
      if (dockedSuffix) {
        *dockedSuffix = '\0';
        HWND child = WindowManager::FindChildInParent(topLevel, title);
        if (child) captureHwnd = child;
      }

      m_drag.mode = DragDock::TRACKING;
      m_drag.sourceHwnd = captureHwnd;
      m_drag.sourceToggleAction = LookupToggleAction(title);
      safe_strncpy(m_drag.sourceTitle, title, sizeof(m_drag.sourceTitle));
      m_drag.mouseDownScreenPos = screenPt;
      DBG("[MaxPane] DragMode: TRACKING source='%s' hwnd=%p\n", title, captureHwnd);
    }
    m_drag.lastBtnDown = btnDown;
    return;
  }

  // mode == TRACKING
  RefreshDragPreview();

  if (m_drag.lastBtnDown && !btnDown) {
    // Release — commit if we have a valid target zone, otherwise cancel.
    if (m_drag.targetPaneId >= 0 && m_drag.zone != DragDock::ZONE_NONE) {
      CommitDrop();
    } else {
      ExitDragMode();
    }
    return;
  }
  m_drag.lastBtnDown = btnDown;
}

void MaxPaneContainer::RefreshDragPreview()
{
  POINT screenPt = m_drag.cursorScreenPos;
  POINT clientPt = screenPt;
  ScreenToClient(m_hwnd, &clientPt);

  // Skip the nav bar strip — drops on the bar are meaningless.
  int prevPane = m_drag.targetPaneId;
  DragDock::DropZone prevZone = m_drag.zone;
  int newPane = -1;
  DragDock::DropZone newZone = DragDock::ZONE_NONE;

  RECT rc;
  GetClientRect(m_hwnd, &rc);
  bool inClient = (clientPt.x >= rc.left && clientPt.x < rc.right &&
                   clientPt.y >= rc.top  && clientPt.y < rc.bottom);
  bool inNavBar = (clientPt.y < rc.top + NavBarReservedHeight());

  if (inClient && !inNavBar) {
    int leafIdx = m_tree.LeafAtPoint(clientPt.x, clientPt.y);
    if (leafIdx >= 0) {
      int paneId = m_tree.GetPaneId(leafIdx);
      if (paneId >= 0) {
        newPane = paneId;
        newZone = DragDock::ComputeZone(m_tree.GetPaneRect(paneId),
                                        clientPt.x, clientPt.y,
                                        m_drag.shiftHeld);
      }
    }
  }

  m_drag.targetPaneId = newPane;
  m_drag.zone = newZone;

  // Invalidate prev + current target rects so preview redraws.
  RECT dirty = {};
  if (prevPane >= 0 && m_tree.IsPaneIdUsed(prevPane))
    ExpandRect(dirty, m_tree.GetPaneRect(prevPane));
  if (newPane  >= 0 && m_tree.IsPaneIdUsed(newPane))
    ExpandRect(dirty, m_tree.GetPaneRect(newPane));
  if (prevZone != newZone && prevPane == newPane && newPane < 0) {
    // No-op (both empty).
  }
  if (dirty.right > dirty.left) InvalidateRect(m_hwnd, &dirty, FALSE);
  else if (prevPane != newPane || prevZone != newZone)
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// =========================================================================
// Drop dispatcher
// =========================================================================

void MaxPaneContainer::CommitDrop()
{
  int paneId       = m_drag.targetPaneId;
  DragDock::DropZone zone = m_drag.zone;
  HWND sourceHwnd  = m_drag.sourceHwnd;
  int  toggleAction = m_drag.sourceToggleAction;
  char sourceTitle[256];
  safe_strncpy(sourceTitle, m_drag.sourceTitle, sizeof(sourceTitle));

  // Reset state up front so any failure leaves us IDLE.
  KillTimer(m_hwnd, TIMER_ID_DRAG_DOCK);
  DragDock::Reset(m_drag);

  if (!sourceHwnd || !IsWindow(sourceHwnd) || paneId < 0) {
    InvalidateRect(m_hwnd, nullptr, FALSE);
    return;
  }

  // Re-validate the source HWND wasn't captured between mouse-down and now
  // (e.g. user fired a workspace load via hotkey mid-drag — unlikely but
  // defensive).
  if (m_winMgr.IsWindowCaptured(sourceHwnd)) {
    InvalidateRect(m_hwnd, nullptr, FALSE);
    return;
  }

  switch (zone) {
    case DragDock::ZONE_TAB_BAR:
    case DragDock::ZONE_BODY_CENTER:
      // Add as a new tab in the target pane.
      m_winMgr.CaptureArbitraryWindow(paneId, sourceHwnd, sourceTitle,
                                       m_hwnd, toggleAction);
      break;

    case DragDock::ZONE_REPLACE: {
      // Close active tab in pane, capture new in same pane (becomes new active).
      const PaneState* ps = m_winMgr.GetPaneState(paneId);
      if (ps && ps->activeTab >= 0 && ps->activeTab < ps->tabCount) {
        const TabEntry& t = ps->tabs[ps->activeTab];
        if (t.captured && t.toggleAction > 0) {
          AppendActionToStaleListForSection(m_extSection, t.toggleAction);
        }
        m_winMgr.CloseTab(paneId, ps->activeTab);
      }
      m_winMgr.CaptureArbitraryWindow(paneId, sourceHwnd, sourceTitle,
                                       m_hwnd, toggleAction);
      break;
    }

    case DragDock::ZONE_SPLIT_LEFT:
    case DragDock::ZONE_SPLIT_RIGHT:
    case DragDock::ZONE_SPLIT_TOP:
    case DragDock::ZONE_SPLIT_BOTTOM: {
      // Zone label semantics — LEFT/TOP zones create the new pane adjacent
      // to the target (right/bottom of original). SplitTree::SplitLeaf
      // produces childA = original pane (keeps existing tabs), childB = new
      // leaf (gets the dropped window). User reads the drop as "split here,
      // put it alongside" — matches VS Code drop indicators. Always split
      // 50/50 and capture into the new pane (childB).
      SplitterOrientation orient =
        (zone == DragDock::ZONE_SPLIT_TOP || zone == DragDock::ZONE_SPLIT_BOTTOM)
          ? SPLIT_HORIZONTAL : SPLIT_VERTICAL;
      int nodeIdx = m_tree.NodeForPane(paneId);
      int newLeaf = (nodeIdx >= 0) ? m_tree.SplitLeaf(nodeIdx, orient, 0.5f) : -1;
      if (newLeaf < 0) {
        // Split failed (e.g. MAX_LEAVES reached) — fall back to tab-append.
        m_winMgr.CaptureArbitraryWindow(paneId, sourceHwnd, sourceTitle,
                                         m_hwnd, toggleAction);
        break;
      }
      int newPaneId = m_tree.GetPaneId(newLeaf);
      // The tree layout has changed — recompute so the new pane has a real
      // rect before capture sizes the window into it.
      RefreshLayout();
      if (newPaneId >= 0) {
        m_winMgr.CaptureArbitraryWindow(newPaneId, sourceHwnd, sourceTitle,
                                         m_hwnd, toggleAction);
      } else {
        // Fallback: capture into original pane.
        m_winMgr.CaptureArbitraryWindow(paneId, sourceHwnd, sourceTitle,
                                         m_hwnd, toggleAction);
      }
      break;
    }

    default:
      break;
  }

  RefreshLayout();
  SaveState();
  InvalidateRect(m_hwnd, nullptr, FALSE);
}
