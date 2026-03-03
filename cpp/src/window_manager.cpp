#include "window_manager.h"
#include "swell_cocoa_helpers.h"
#include "globals.h"
#include "debug.h"
#include <cstring>
#include <cstdio>

// =========================================================================
// Toolbar subclass — block background drag (REAPER's undock-by-drag)
// =========================================================================
// REAPER toolbars initiate a drag-to-undock operation when you click+hold
// on the toolbar background (empty area between buttons).  When a toolbar
// is reparented as WS_CHILD inside MaxPane, this drag breaks rendering
// (buttons disappear, grey rectangle).
//
// Fix: subclass captured toolbar windows and eat WM_LBUTTONDOWN when the
// click is on the background (not on a child control / button).
// On macOS/SWELL, subview button clicks never reach the parent WndProc,
// but we add a child-hit guard for cross-platform safety.

static const char* const kOrigWndProcProp = "MaxPane_OrigWndProc";

static bool PointHitsChild(HWND parent, int x, int y)
{
  HWND child = GetWindow(parent, GW_CHILD);
  while (child) {
    if (IsWindowVisible(child)) {
      RECT cr;
      GetWindowRect(child, &cr);
      // Convert screen rect to parent client coords
      POINT tl = { cr.left, cr.top };
      POINT br = { cr.right, cr.bottom };
      ScreenToClient(parent, &tl);
      ScreenToClient(parent, &br);
      if (x >= tl.x && x < br.x && y >= tl.y && y < br.y)
        return true;
    }
    child = GetWindow(child, GW_HWNDNEXT);
  }
  return false;
}

static LRESULT CALLBACK ToolbarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC origProc = (WNDPROC)(INT_PTR)GetProp(hwnd, kOrigWndProcProp);
  if (!origProc) return DefWindowProc(hwnd, msg, wParam, lParam);

  if (msg == WM_LBUTTONDOWN) {
    int x = (short)LOWORD(lParam);
    int y = (short)HIWORD(lParam);
    if (!PointHitsChild(hwnd, x, y)) {
      // Click on toolbar background — eat it to prevent REAPER's drag-to-undock
      DBG("[MaxPane] ToolbarSubclass: ate WM_LBUTTONDOWN on background at (%d,%d)\n", x, y);
      return 0;
    }
  }

  return CallWindowProc(origProc, hwnd, msg, wParam, lParam);
}

static void SubclassToolbar(HWND hwnd)
{
  if (GetProp(hwnd, kOrigWndProcProp)) return;  // already subclassed
  WNDPROC orig = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
  if (orig && orig != ToolbarSubclassProc) {
    SetProp(hwnd, kOrigWndProcProp, (HANDLE)(INT_PTR)orig);
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)ToolbarSubclassProc);
    DBG("[MaxPane] SubclassToolbar: hwnd=%p orig=%p\n", (void*)hwnd, (void*)orig);
  }
}

static void UnsubclassToolbar(HWND hwnd)
{
  WNDPROC orig = (WNDPROC)(INT_PTR)GetProp(hwnd, kOrigWndProcProp);
  if (orig) {
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)orig);
    RemoveProp(hwnd, kOrigWndProcProp);
    DBG("[MaxPane] UnsubclassToolbar: hwnd=%p restored=%p\n", (void*)hwnd, (void*)orig);
  }
}

// Known dynamic-title window prefixes.
// Windows whose titles start with one of these change their title at runtime
// (e.g. MIDI Editor title changes per MIDI item / project tab).
const char* GetDynamicTitlePrefix(const char* title)
{
  if (!title) return nullptr;
  if (strncmp(title, "MIDI take:", 10) == 0) return "MIDI take:";
  return nullptr;
}

// Detect REAPER toggle action for toolbar windows by title.
// Returns action ID or 0 if not a toolbar.
// Toolbar 1-16: action 41679 + (N-1).  Toolbar Docker: 41084.
int GetToolbarToggleAction(const char* title)
{
  if (!title) return 0;
  if (strcmp(title, "Toolbar Docker") == 0) return 41084;
  if (strncmp(title, "Toolbar ", 8) == 0) {
    int n = atoi(title + 8);
    if (n >= 1 && n <= 16) return 41678 + n;
  }
  return 0;
}

// Look up REAPER toggle action for any window title.
// Checks toolbars first, then KNOWN_WINDOWS by searchTitle/altSearchTitle prefix.
int LookupToggleAction(const char* title)
{
  if (!title) return 0;
  int a = GetToolbarToggleAction(title);
  if (a > 0) return a;
  for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
    if (strstr(title, KNOWN_WINDOWS[i].searchTitle) == title)
      return KNOWN_WINDOWS[i].toggleActionId;
    if (KNOWN_WINDOWS[i].altSearchTitle &&
        strstr(title, KNOWN_WINDOWS[i].altSearchTitle) == title)
      return KNOWN_WINDOWS[i].toggleActionId;
  }
  return 0;
}

bool GetSearchTitleForAction(int action, char* buf, int bufSize)
{
  if (action <= 0 || !buf || bufSize <= 0) return false;
  // Toolbars: action 41679..41694 → "Toolbar 1".."Toolbar 16"
  if (action >= 41679 && action <= 41694) {
    snprintf(buf, bufSize, "Toolbar %d", action - 41678);
    return true;
  }
  if (action == 41084) {
    safe_strncpy(buf, "Toolbar Docker", bufSize);
    return true;
  }
  // Known windows
  for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
    if (KNOWN_WINDOWS[i].toggleActionId == action) {
      safe_strncpy(buf, KNOWN_WINDOWS[i].searchTitle, bufSize);
      return true;
    }
  }
  return false;
}

WindowManager::WindowManager()
  : m_containerHwnd(nullptr)
{
  memset(m_panes, 0, sizeof(m_panes));
  for (int i = 0; i < MAX_PANES; i++) {
    m_panes[i].tabCount = 0;
    m_panes[i].activeTab = -1;
  }
}

void WindowManager::Init()
{
  m_containerHwnd = nullptr;
  for (int i = 0; i < MAX_PANES; i++) {
    m_panes[i].tabCount = 0;
    m_panes[i].activeTab = -1;
    for (int t = 0; t < MAX_TABS_PER_PANE; t++) {
      memset(&m_panes[i].tabs[t], 0, sizeof(TabEntry));
    }
  }
}

// =========================================================================
// Window finding — unified enum proc with skip logic
// =========================================================================

struct FindWindowData {
  const char* searchTitle;
  HWND result;
  HWND skipContainer;
  const char* dbgPrefix;
};

static BOOL CALLBACK FindWindowEnumProc(HWND hwnd, LPARAM lParam)
{
  FindWindowData* data = (FindWindowData*)lParam;
  char buf[512];
  GetWindowText(hwnd, buf, sizeof(buf));
  if (!buf[0]) return TRUE;

  // Skip tiny controls/buttons — real REAPER windows are at least 50×50
  { RECT wr; GetClientRect(hwnd, &wr);
    if ((wr.right - wr.left) < 50 || (wr.bottom - wr.top) < 50) return TRUE; }

  // Skip windows inside our container
  if (data->skipContainer && (hwnd == data->skipContainer || IsChild(data->skipContainer, hwnd)))
    return TRUE;

  // Strip " (docked)" suffix for matching (ReaImGui scripts use this)
  char matchBuf[512];
  safe_strncpy(matchBuf, buf, sizeof(matchBuf));
  char* dockedSuffix = strstr(matchBuf, " (docked)");
  if (dockedSuffix) *dockedSuffix = '\0';

  if (strstr(matchBuf, data->searchTitle) == matchBuf) {
    DBG("[MaxPane] %s: PREFIX match '%s' for '%s' hwnd=%p\n", data->dbgPrefix, buf, data->searchTitle, (void*)hwnd);
    data->result = hwnd;
    return FALSE;
  }
  if (strstr(matchBuf, data->searchTitle)) {
    DBG("[MaxPane] %s: SUBSTR match '%s' for '%s' hwnd=%p\n", data->dbgPrefix, buf, data->searchTitle, (void*)hwnd);
    data->result = hwnd;
    return FALSE;
  }
  return TRUE;
}

HWND WindowManager::FindReaperWindow(const char* title, HWND skipContainer)
{
  if (!title) return nullptr;

  DBG("[MaxPane] FindReaperWindow: searching for '%s'\n", title);

  // 1. Prefer dock frame version "Title (docked)" — ReaImGui scripts use this
  //    The dock frame contains the actual rendered UI; the inner window is often empty/grey.
  {
    char dockedTitle[512];
    snprintf(dockedTitle, sizeof(dockedTitle), "%s (docked)", title);
    HWND hwnd = FindWindowEx(nullptr, nullptr, nullptr, dockedTitle);
    if (hwnd) {
      if (skipContainer && (hwnd == skipContainer || IsChild(skipContainer, hwnd))) {
        hwnd = nullptr;
      } else {
        DBG("[MaxPane] FindReaperWindow: DOCKED FRAME match '%s' hwnd=%p\n", dockedTitle, (void*)hwnd);
        return hwnd;
      }
    }
  }

  // 2. Exact match among top-level windows
  HWND hwnd = FindWindowEx(nullptr, nullptr, nullptr, title);
  if (hwnd) {
    // Make sure it's not inside our container
    if (skipContainer && (hwnd == skipContainer || IsChild(skipContainer, hwnd))) {
      hwnd = nullptr;
    } else {
      DBG("[MaxPane] FindReaperWindow: EXACT match hwnd=%p\n", (void*)hwnd);
      return hwnd;
    }
  }

  // 3. Top-level windows with prefix/substring match
  FindWindowData data = { title, nullptr, skipContainer, "EnumWindows" };
  EnumWindows(FindWindowEnumProc, (LPARAM)&data);
  if (data.result) {
    return data.result;
  }

  // 4. Search child windows of REAPER main window
  //    First pass: look for dock frame "Title (docked)" among children
  //    Second pass: look for inner window "Title" among children
  //    This ensures we prefer dock frames (which have rendered UI) over inner windows
  if (g_reaperMainHwnd) {
    // 4a. Search for dock frame among direct children
    {
      char dockedTitle[512];
      snprintf(dockedTitle, sizeof(dockedTitle), "%s (docked)", title);
      FindWindowData dockData = { dockedTitle, nullptr, skipContainer, "EnumChildWindows" };
      EnumChildWindows(g_reaperMainHwnd, FindWindowEnumProc, (LPARAM)&dockData);
      if (dockData.result) {
        DBG("[MaxPane] FindReaperWindow: DOCKED FRAME child match hwnd=%p\n", (void*)dockData.result);
        return dockData.result;
      }
    }

    // 4b. Search for dock frame among grandchildren (inside REAPER_dock containers)
    {
      char dockedTitle[512];
      snprintf(dockedTitle, sizeof(dockedTitle), "%s (docked)", title);
      FindWindowData dockData = { dockedTitle, nullptr, skipContainer, "EnumChildWindows" };
      HWND dockChild = nullptr;
      while ((dockChild = FindWindowEx(g_reaperMainHwnd, dockChild, nullptr, nullptr)) != nullptr) {
        if (skipContainer && (dockChild == skipContainer || IsChild(skipContainer, dockChild)))
          continue;
        dockData.result = nullptr;
        EnumChildWindows(dockChild, FindWindowEnumProc, (LPARAM)&dockData);
        if (dockData.result) {
          DBG("[MaxPane] FindReaperWindow: DOCKED FRAME grandchild match hwnd=%p\n", (void*)dockData.result);
          return dockData.result;
        }
      }
    }

    // 4c. Fallback: search for inner window among direct children
    data.result = nullptr;
    EnumChildWindows(g_reaperMainHwnd, FindWindowEnumProc, (LPARAM)&data);
    if (data.result) {
      return data.result;
    }

    // 5. Search grandchildren — windows inside REAPER_dock containers
    //    SWELL's EnumChildWindows may not recurse, so check each dock explicitly
    HWND dockChild = nullptr;
    while ((dockChild = FindWindowEx(g_reaperMainHwnd, dockChild, nullptr, nullptr)) != nullptr) {
      if (skipContainer && (dockChild == skipContainer || IsChild(skipContainer, dockChild)))
        continue;
      // Search ALL children of every child of main window, not just docks
      data.result = nullptr;
      EnumChildWindows(dockChild, FindWindowEnumProc, (LPARAM)&data);
      if (data.result) {
        char dockBuf[256];
        GetWindowText(dockChild, dockBuf, sizeof(dockBuf));
        DBG("[MaxPane] FindReaperWindow: found '%s' inside '%s' (hwnd=%p)\n",
            title, dockBuf, (void*)data.result);
        return data.result;
      }
    }
  }

  DBG("[MaxPane] FindReaperWindow: NOT FOUND '%s'\n", title);
  return nullptr;
}

// Diagnostic: dump all visible window titles (call when debugging search failures)
struct DumpWindowData {
  const char* targetTitle;
  int count;
};

static BOOL CALLBACK DumpWindowEnumProc(HWND hwnd, LPARAM lParam)
{
  DumpWindowData* data = (DumpWindowData*)lParam;
  if (!IsWindowVisible(hwnd)) return TRUE;
  char buf[512];
  GetWindowText(hwnd, buf, sizeof(buf));
  if (buf[0]) {
    DBG("[MaxPane] DumpWindows[%d]: '%s' hwnd=%p\n", data->count, buf, (void*)hwnd);
    data->count++;
  }
  return TRUE;
}

void WindowManager::DumpAllWindowTitles(const char* context)
{
  DBG("[MaxPane] === DumpAllWindowTitles: %s ===\n", context ? context : "");
  DumpWindowData data = { nullptr, 0 };

  // Top-level windows
  DBG("[MaxPane] -- Top-level windows --\n");
  data.count = 0;
  EnumWindows(DumpWindowEnumProc, (LPARAM)&data);

  // Children of REAPER main window
  if (g_reaperMainHwnd) {
    DBG("[MaxPane] -- Children of REAPER main window --\n");
    data.count = 0;
    EnumChildWindows(g_reaperMainHwnd, DumpWindowEnumProc, (LPARAM)&data);
  }
  DBG("[MaxPane] === End DumpAllWindowTitles ===\n");
}

HWND WindowManager::FindChildInParent(HWND parent, const char* title)
{
  if (!parent || !title) return nullptr;
  FindWindowData data = { title, nullptr, nullptr, "EnumChildWindows" };
  EnumChildWindows(parent, FindWindowEnumProc, (LPARAM)&data);
  return data.result;
}

// =========================================================================
// Capture / Release
// =========================================================================

bool WindowManager::CaptureByIndex(int paneId, int knownWindowIndex, HWND containerHwnd)
{
  if (paneId < 0 || paneId >= MAX_PANES) return false;
  if (knownWindowIndex < 0 || knownWindowIndex >= NUM_KNOWN_WINDOWS) return false;

  m_containerHwnd = containerHwnd;

  PaneState& ps = m_panes[paneId];
  if (ps.tabCount >= MAX_TABS_PER_PANE) return false;

  const WindowDef& def = KNOWN_WINDOWS[knownWindowIndex];

  DBG("[MaxPane] CaptureByIndex: pane=%d window='%s' search='%s' alt='%s'\n",
          paneId, def.name, def.searchTitle, def.altSearchTitle ? def.altSearchTitle : "(none)");

  HWND hwnd = FindReaperWindow(def.searchTitle, containerHwnd);
  if (!hwnd && def.altSearchTitle) {
    hwnd = FindReaperWindow(def.altSearchTitle, containerHwnd);
  }
  if (!hwnd) {
    DBG("[MaxPane] CaptureByIndex: FAILED — window not found\n");
    return false;
  }

  if (IsWindowCaptured(hwnd)) return false;

  TabEntry& tab = ps.tabs[ps.tabCount];
  memset(&tab, 0, sizeof(TabEntry));
  safe_strncpy(tab.name, def.name, sizeof(tab.name));
  safe_strncpy(tab.searchTitle, def.searchTitle, sizeof(tab.searchTitle));
  tab.toggleAction = def.toggleActionId;
  tab.isArbitrary = false;

  DBG("[MaxPane] CaptureByIndex: found hwnd=%p, calling DoCapture\n", (void*)hwnd);

  if (DoCapture(tab, hwnd, containerHwnd)) {
    if (ps.activeTab >= 0 && ps.activeTab < ps.tabCount) {
      TabEntry& oldTab = ps.tabs[ps.activeTab];
      if (oldTab.captured && oldTab.hwnd) {
        ShowWindow(oldTab.hwnd, SW_HIDE);
      }
    }
    ps.activeTab = ps.tabCount;
    ps.tabCount++;
    return true;
  }
  return false;
}

bool WindowManager::CaptureArbitraryWindow(int paneId, HWND targetHwnd, const char* displayName, HWND containerHwnd, int toggleAction, const char* actionCmd)
{
  DBG("[MaxPane] CaptureArbitraryWindow: pane=%d name='%s' hwnd=%p action=%d cmd='%s'\n",
      paneId, displayName ? displayName : "(null)", (void*)targetHwnd, toggleAction,
      actionCmd ? actionCmd : "(null)");
  if (paneId < 0 || paneId >= MAX_PANES) return false;
  if (!targetHwnd || !displayName) return false;

  m_containerHwnd = containerHwnd;

  PaneState& ps = m_panes[paneId];
  if (ps.tabCount >= MAX_TABS_PER_PANE) {
    DBG("[MaxPane] CaptureArbitraryWindow: REJECTED pane %d full (tabCount=%d)\n", paneId, ps.tabCount);
    return false;
  }
  if (IsWindowCaptured(targetHwnd)) {
    DBG("[MaxPane] CaptureArbitraryWindow: REJECTED hwnd=%p already captured\n", (void*)targetHwnd);
    return false;
  }

  TabEntry& tab = ps.tabs[ps.tabCount];
  memset(&tab, 0, sizeof(TabEntry));

  safe_strncpy(tab.name, displayName, sizeof(tab.name));
  safe_strncpy(tab.searchTitle, displayName, sizeof(tab.searchTitle));
  // Auto-detect toggle action from window title if caller didn't provide one
  tab.toggleAction = (toggleAction > 0) ? toggleAction : LookupToggleAction(displayName);
  tab.isArbitrary = true;
  if (actionCmd && actionCmd[0]) {
    safe_strncpy(tab.actionCmd, actionCmd, sizeof(tab.actionCmd));
  }

  // Detect dynamic-title windows (e.g. MIDI Editor "MIDI take: ...")
  const char* dynPrefix = GetDynamicTitlePrefix(displayName);
  if (dynPrefix) {
    tab.dynamicTitle = true;
    safe_strncpy(tab.searchTitle, dynPrefix, sizeof(tab.searchTitle));
    // Update display name to actual window title (displayName may be a stale saved name)
    char actualTitle[256];
    GetWindowText(targetHwnd, actualTitle, sizeof(actualTitle));
    if (actualTitle[0]) {
      safe_strncpy(tab.name, actualTitle, sizeof(tab.name));
    }
    DBG("[MaxPane] CaptureArbitraryWindow: dynamic title detected, prefix='%s' actual='%s'\n",
        dynPrefix, tab.name);
  }

  if (DoCapture(tab, targetHwnd, containerHwnd)) {
    if (ps.activeTab >= 0 && ps.activeTab < ps.tabCount) {
      TabEntry& oldTab = ps.tabs[ps.activeTab];
      if (oldTab.captured && oldTab.hwnd) {
        ShowWindow(oldTab.hwnd, SW_HIDE);
      }
    }
    ps.activeTab = ps.tabCount;
    ps.tabCount++;
    return true;
  }
  return false;
}

bool WindowManager::DoCapture(TabEntry& tab, HWND targetHwnd, HWND containerHwnd)
{
  if (!targetHwnd || !containerHwnd) return false;

  // Guard: never capture a window that is an ancestor of our container.
  // That would create a circular parent chain (e.g. capturing the Docker
  // that MaxPane itself is docked inside) and crash.
  {
    HWND ancestor = GetParent(containerHwnd);
    while (ancestor) {
      if (ancestor == targetHwnd) {
        DBG("[MaxPane] DoCapture: REJECTED — target %p is ancestor of container (circular)\n",
            (void*)targetHwnd);
        return false;
      }
      ancestor = GetParent(ancestor);
    }
  }

  tab.originalParent = GetParent(targetHwnd);

  char targetTitle[256] = {};
  GetWindowText(targetHwnd, targetTitle, sizeof(targetTitle));

  DBG("[MaxPane] DoCapture: target=%p title='%s' container=%p\n",
          (void*)targetHwnd, targetTitle, (void*)containerHwnd);

  // Detach from docker if needed
  HWND currentParent = tab.originalParent;
  if (currentParent && currentParent != g_reaperMainHwnd) {
    DBG("[MaxPane] DoCapture: detaching from docker parent=%p\n", (void*)currentParent);
    SetParent(targetHwnd, g_reaperMainHwnd);
  }

  // Reparent to our container
  SetParent(targetHwnd, containerHwnd);

  // Preserve original style bits, just add WS_CHILD | WS_VISIBLE and strip
  // top-level window chrome.  Stripping all styles breaks frameless windows
  // like toolbars whose rendering depends on flags like WS_CLIPCHILDREN.
  LONG_PTR origStyle = GetWindowLongPtr(targetHwnd, GWL_STYLE);
  LONG_PTR stripMask = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU;
  LONG_PTR newStyle = (origStyle & ~stripMask) | WS_CHILD | WS_VISIBLE;
  SetWindowLongPtr(targetHwnd, GWL_STYLE, newStyle);

  tab.hwnd = targetHwnd;
  tab.captured = true;

  ShowWindow(targetHwnd, SW_SHOWNA);
  SetWindowPos(targetHwnd, nullptr, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

  // Force Cocoa layout + display pass.  SWELL's SetParent does NOT trigger
  // setNeedsLayout: on the reparented NSView, so child controls (e.g.
  // Routing Matrix grid) may have stale frames from before reparent.
  ForceViewLayoutAndDisplay(targetHwnd);

  // Subclass toolbar windows to prevent REAPER's drag-to-undock on background click
  if (GetToolbarToggleAction(targetTitle) > 0) {
    SubclassToolbar(targetHwnd);
  }

  DBG("[MaxPane] DoCapture: DONE hwnd=%p captured=true\n", (void*)targetHwnd);
  return true;
}

void WindowManager::DoRelease(TabEntry& tab, bool toggleOff)
{
  if (!tab.captured) return;

  DBG("[MaxPane] DoRelease: '%s' hwnd=%p toggleOff=%d action=%d alive=%d\n",
      tab.name[0] ? tab.name : "(null)", tab.hwnd, toggleOff, tab.toggleAction,
      (tab.hwnd && IsWindow(tab.hwnd)) ? 1 : 0);

  if (tab.hwnd && IsWindow(tab.hwnd)) {
    // Remove toolbar subclass before reparenting
    UnsubclassToolbar(tab.hwnd);

    if (toggleOff && tab.toggleAction > 0 && g_Main_OnCommand) {
      // Restore the window to a true top-level NSWindow before toggling.
      // While WS_CHILD in our container SWELL destroys the NSWindow; calling
      // g_Main_OnCommand without a real NSWindow causes REAPER to open a NEW
      // floating window instead of closing the existing one (state stays 1).
      // SetParent(nullptr) recreates the NSWindow so REAPER can close it
      // properly and set the toggle state to 0.
      SetParent(tab.hwnd, nullptr);
      int toggleState = g_GetToggleCommandState ? g_GetToggleCommandState(tab.toggleAction) : -1;
      DBG("[MaxPane] DoRelease: restored to top-level, toggle state=%d action=%d\n",
          toggleState, tab.toggleAction);
      if (toggleState != 0)
        g_Main_OnCommand(tab.toggleAction, 0);
    } else {
      // No toggle — reparent to REAPER main and hide.
      HWND restoreParent = tab.originalParent;
      if (!restoreParent || !IsWindow(restoreParent)) restoreParent = g_reaperMainHwnd;
      SetParent(tab.hwnd, restoreParent);
      DBG("[MaxPane] DoRelease: reparented to %p\n", (void*)restoreParent);
    }
    ShowWindow(tab.hwnd, SW_HIDE);
  }

  tab.hwnd = nullptr;
  tab.originalParent = nullptr;
  tab.captured = false;
  tab.isArbitrary = false;
}

// =========================================================================
// Tab management
// =========================================================================

static void ShiftTabsLeft(TabEntry* tabs, int& count, int removeIndex)
{
  for (int i = removeIndex; i < count - 1; i++)
    tabs[i] = tabs[i + 1];
  count--;
  memset(&tabs[count], 0, sizeof(TabEntry));
}

void WindowManager::SetActiveTab(int paneId, int tabIndex)
{
  if (paneId < 0 || paneId >= MAX_PANES) return;
  PaneState& ps = m_panes[paneId];
  if (tabIndex < 0 || tabIndex >= ps.tabCount) return;

  if (ps.activeTab >= 0 && ps.activeTab < ps.tabCount && ps.activeTab != tabIndex) {
    TabEntry& old = ps.tabs[ps.activeTab];
    if (old.captured && old.hwnd && IsWindow(old.hwnd)) {
      ShowWindow(old.hwnd, SW_HIDE);
    }
  }

  ps.activeTab = tabIndex;

  TabEntry& cur = ps.tabs[tabIndex];
  if (cur.captured && cur.hwnd && IsWindow(cur.hwnd)) {
    ShowWindow(cur.hwnd, SW_SHOWNA);
  }
}

void WindowManager::CloseTab(int paneId, int tabIndex)
{
  if (paneId < 0 || paneId >= MAX_PANES) return;
  PaneState& ps = m_panes[paneId];
  if (tabIndex < 0 || tabIndex >= ps.tabCount) return;

  DoRelease(ps.tabs[tabIndex]);

  ShiftTabsLeft(ps.tabs, ps.tabCount, tabIndex);

  if (ps.tabCount == 0) {
    ps.activeTab = -1;
  } else if (tabIndex < ps.activeTab) {
    ps.activeTab--;
  } else if (ps.activeTab >= ps.tabCount) {
    ps.activeTab = ps.tabCount - 1;
  }

  if (ps.activeTab >= 0 && ps.activeTab < ps.tabCount) {
    TabEntry& cur = ps.tabs[ps.activeTab];
    if (cur.captured && cur.hwnd && IsWindow(cur.hwnd)) {
      ShowWindow(cur.hwnd, SW_SHOWNA);
    }
  }
}

void WindowManager::MoveTab(int srcPane, int srcTab, int dstPane)
{
  if (srcPane < 0 || srcPane >= MAX_PANES) return;
  if (dstPane < 0 || dstPane >= MAX_PANES) return;
  if (srcPane == dstPane) return;

  PaneState& src = m_panes[srcPane];
  PaneState& dst = m_panes[dstPane];
  if (srcTab < 0 || srcTab >= src.tabCount) return;
  if (dst.tabCount >= MAX_TABS_PER_PANE) return;

  dst.tabs[dst.tabCount] = src.tabs[srcTab];

  if (dst.activeTab >= 0 && dst.activeTab < dst.tabCount) {
    TabEntry& oldDst = dst.tabs[dst.activeTab];
    if (oldDst.captured && oldDst.hwnd && IsWindow(oldDst.hwnd)) {
      ShowWindow(oldDst.hwnd, SW_HIDE);
    }
  }
  dst.activeTab = dst.tabCount;
  dst.tabCount++;

  TabEntry& movedTab = dst.tabs[dst.activeTab];
  if (movedTab.captured && movedTab.hwnd && IsWindow(movedTab.hwnd)) {
    ShowWindow(movedTab.hwnd, SW_SHOWNA);
  }

  ShiftTabsLeft(src.tabs, src.tabCount, srcTab);

  if (src.tabCount == 0) {
    src.activeTab = -1;
  } else {
    if (srcTab < src.activeTab) src.activeTab--;
    if (src.activeTab >= src.tabCount) src.activeTab = src.tabCount - 1;
    TabEntry& curSrc = src.tabs[src.activeTab];
    if (curSrc.captured && curSrc.hwnd && IsWindow(curSrc.hwnd)) {
      ShowWindow(curSrc.hwnd, SW_SHOWNA);
    }
  }
}

void WindowManager::ReorderTab(int paneId, int fromIndex, int toIndex)
{
  if (paneId < 0 || paneId >= MAX_PANES) return;
  PaneState& ps = m_panes[paneId];
  if (fromIndex < 0 || fromIndex >= ps.tabCount) return;
  if (toIndex < 0 || toIndex >= ps.tabCount) return;
  if (fromIndex == toIndex) return;

  TabEntry temp = ps.tabs[fromIndex];

  if (fromIndex < toIndex) {
    for (int i = fromIndex; i < toIndex; i++)
      ps.tabs[i] = ps.tabs[i + 1];
  } else {
    for (int i = fromIndex; i > toIndex; i--)
      ps.tabs[i] = ps.tabs[i - 1];
  }
  ps.tabs[toIndex] = temp;

  // Adjust activeTab to follow the moved tab
  if (ps.activeTab == fromIndex) {
    ps.activeTab = toIndex;
  } else if (fromIndex < toIndex) {
    if (ps.activeTab > fromIndex && ps.activeTab <= toIndex) ps.activeTab--;
  } else {
    if (ps.activeTab >= toIndex && ps.activeTab < fromIndex) ps.activeTab++;
  }
}

void WindowManager::SetTabColor(int paneId, int tabIndex, int colorIndex)
{
  if (paneId < 0 || paneId >= MAX_PANES) return;
  PaneState& ps = m_panes[paneId];
  if (tabIndex < 0 || tabIndex >= ps.tabCount) return;
  ps.tabs[tabIndex].colorIndex = colorIndex;
}

// =========================================================================
// Release
// =========================================================================

void WindowManager::ReleaseWindow(int paneId, bool toggleOff)
{
  if (paneId < 0 || paneId >= MAX_PANES) return;
  PaneState& ps = m_panes[paneId];
  for (int t = 0; t < ps.tabCount; t++) {
    DoRelease(ps.tabs[t], toggleOff);
  }
  ps.tabCount = 0;
  ps.activeTab = -1;
}

void WindowManager::ReleaseAll(bool toggleOff)
{
  for (int i = 0; i < MAX_PANES; i++) {
    ReleaseWindow(i, toggleOff);
  }
}

void WindowManager::ReleaseAllSelective(const int* staleActions, int staleCount)
{
  for (int i = 0; i < MAX_PANES; i++) {
    PaneState& ps = m_panes[i];
    for (int t = 0; t < ps.tabCount; t++) {
      bool isStale = false;
      // Resolve toggle action if missing (old workspace data)
      int act = ps.tabs[t].toggleAction;
      if (act <= 0) act = LookupToggleAction(ps.tabs[t].name);
      if (act > 0) {
        // Update the tab so DoRelease can toggle it off
        if (ps.tabs[t].toggleAction <= 0) ps.tabs[t].toggleAction = act;
        for (int s = 0; s < staleCount; s++) {
          if (staleActions[s] == act) { isStale = true; break; }
        }
      }
      DBG("[MaxPane] ReleaseSelective: pane %d tab %d '%s' action=%d (resolved=%d) isStale=%d captured=%d hwnd=%p\n",
          i, t, ps.tabs[t].name, ps.tabs[t].toggleAction, act, isStale,
          ps.tabs[t].captured, (void*)ps.tabs[t].hwnd);
      DoRelease(ps.tabs[t], isStale);  // toggleOff=true for stale, false for shared
    }
    ps.tabCount = 0;
    ps.activeTab = -1;
  }
}

// =========================================================================
// Reposition / Check alive
// =========================================================================

void WindowManager::RepositionAll(const SplitTree& tree)
{
  const int* leafList = tree.GetLeafList();
  int leafCount = tree.GetLeafCount();

  for (int i = 0; i < leafCount; i++) {
    int paneId = tree.GetPaneId(leafList[i]);
    if (paneId < 0 || paneId >= MAX_PANES) continue;

    PaneState& ps = m_panes[paneId];
    if (ps.tabCount == 0) continue;

    const RECT& paneRect = tree.GetPaneRect(paneId);
    int headerOffset = TAB_BAR_HEIGHT;

    int x = paneRect.left;
    int y = paneRect.top + headerOffset;
    int w = paneRect.right - paneRect.left;
    int h = paneRect.bottom - paneRect.top - headerOffset;

    if (w <= 0 || h <= 0) continue;

    for (int t = 0; t < ps.tabCount; t++) {
      TabEntry& tab = ps.tabs[t];
      if (!tab.captured || !tab.hwnd) continue;
      if (!IsWindow(tab.hwnd)) continue;

      if (t == ps.activeTab) {
        SetWindowPos(tab.hwnd, HWND_TOP, x, y, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
        SendMessage(tab.hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(w, h));

        // Propagate WM_SIZE to child controls — SWELL doesn't cascade
        // layout changes to subviews after reparent (no setNeedsLayout).
        // Windows like Routing Matrix have a child grid control that needs
        // an explicit size message to lay out.
        HWND child = GetWindow(tab.hwnd, GW_CHILD);
        while (child) {
          if (IsWindow(child)) {
            RECT cr;
            GetClientRect(child, &cr);
            SendMessage(child, WM_SIZE, SIZE_RESTORED,
                        MAKELPARAM(cr.right, cr.bottom));
          }
          child = GetWindow(child, GW_HWNDNEXT);
        }
        // Force Cocoa display pass on the view and all subviews
        ForceViewLayoutAndDisplay(tab.hwnd);
      } else {
        ShowWindow(tab.hwnd, SW_HIDE);
      }
    }
  }
}

bool WindowManager::CheckAlive()
{
  bool changed = false;

  for (int i = 0; i < MAX_PANES; i++) {
    PaneState& ps = m_panes[i];
    for (int t = ps.tabCount - 1; t >= 0; t--) {
      TabEntry& tab = ps.tabs[t];

      if (tab.captured) {
        if (!tab.hwnd || !IsWindow(tab.hwnd)) {
          if (tab.dynamicTitle) {
            // Dynamic-title tab lost its HWND (e.g. MIDI Editor on project switch).
            // Keep the tab entry for recapture on next tick.
            DBG("[MaxPane] CheckAlive: dynamic tab '%s' lost HWND, waiting for recapture\n", tab.name);
            tab.hwnd = nullptr;
            tab.captured = false;
            changed = true;
          } else {
            // Static-title tab — remove as before
            tab.hwnd = nullptr;
            tab.captured = false;
            tab.isArbitrary = false;
            ShiftTabsLeft(ps.tabs, ps.tabCount, t);
            if (ps.tabCount == 0) {
              ps.activeTab = -1;
            } else {
              if (t < ps.activeTab) ps.activeTab--;
              if (ps.activeTab >= ps.tabCount) ps.activeTab = ps.tabCount - 1;
            }
            changed = true;
          }
        }
      } else if (tab.dynamicTitle && tab.searchTitle[0]) {
        // Uncaptured dynamic tab — try to recapture
        HWND h = FindReaperWindow(tab.searchTitle, m_containerHwnd);
        if (h && !IsWindowCaptured(h)) {
          if (DoCapture(tab, h, m_containerHwnd)) {
            // Update display name to new window title
            char newTitle[256];
            GetWindowText(h, newTitle, sizeof(newTitle));
            if (newTitle[0]) {
              safe_strncpy(tab.name, newTitle, sizeof(tab.name));
            }
            DBG("[MaxPane] CheckAlive: recaptured dynamic tab as '%s' hwnd=%p\n", tab.name, (void*)h);
            // Show/hide based on activeTab
            if (t == ps.activeTab) {
              ShowWindow(h, SW_SHOWNA);
            } else {
              ShowWindow(h, SW_HIDE);
            }
            changed = true;
          }
        }
      }
    }
  }

  return changed;
}

// =========================================================================
// Accessors
// =========================================================================

const PaneState* WindowManager::GetPaneState(int paneId) const
{
  if (paneId < 0 || paneId >= MAX_PANES) return nullptr;
  return &m_panes[paneId];
}

const TabEntry* WindowManager::GetActiveTabEntry(int paneId) const
{
  if (paneId < 0 || paneId >= MAX_PANES) return nullptr;
  const PaneState& ps = m_panes[paneId];
  if (ps.activeTab < 0 || ps.activeTab >= ps.tabCount) return nullptr;
  return &ps.tabs[ps.activeTab];
}

const TabEntry* WindowManager::GetTab(int paneId, int tabIndex) const
{
  if (paneId < 0 || paneId >= MAX_PANES) return nullptr;
  const PaneState& ps = m_panes[paneId];
  if (tabIndex < 0 || tabIndex >= ps.tabCount) return nullptr;
  return &ps.tabs[tabIndex];
}

int WindowManager::GetTabCount(int paneId) const
{
  if (paneId < 0 || paneId >= MAX_PANES) return 0;
  return m_panes[paneId].tabCount;
}

bool WindowManager::IsWindowCaptured(HWND hwnd) const
{
  if (!hwnd) return false;
  for (int i = 0; i < MAX_PANES; i++) {
    for (int t = 0; t < m_panes[i].tabCount; t++) {
      if (m_panes[i].tabs[t].captured && m_panes[i].tabs[t].hwnd == hwnd) return true;
    }
  }
  return false;
}
