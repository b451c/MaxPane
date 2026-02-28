#include "window_manager.h"
#include "globals.h"
#include "debug.h"
#include <cstring>
#include <cstdio>

WindowManager::WindowManager()
{
  memset(m_panes, 0, sizeof(m_panes));
  for (int i = 0; i < MAX_PANES; i++) {
    m_panes[i].tabCount = 0;
    m_panes[i].activeTab = -1;
  }
}

void WindowManager::Init()
{
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

  if (strstr(buf, "toolbar") || strstr(buf, "Toolbar")) return TRUE;

  // Skip windows inside our container
  if (data->skipContainer && (hwnd == data->skipContainer || IsChild(data->skipContainer, hwnd)))
    return TRUE;

  // Strip " (docked)" suffix for matching (ReaImGui scripts use this)
  char matchBuf[512];
  safe_strncpy(matchBuf, buf, sizeof(matchBuf));
  char* dockedSuffix = strstr(matchBuf, " (docked)");
  if (dockedSuffix) *dockedSuffix = '\0';

  if (strstr(matchBuf, data->searchTitle) == matchBuf) {
    DBG("[ReDockIt] %s: PREFIX match '%s' for '%s' hwnd=%p\n", data->dbgPrefix, buf, data->searchTitle, (void*)hwnd);
    data->result = hwnd;
    return FALSE;
  }
  if (strstr(matchBuf, data->searchTitle)) {
    DBG("[ReDockIt] %s: SUBSTR match '%s' for '%s' hwnd=%p\n", data->dbgPrefix, buf, data->searchTitle, (void*)hwnd);
    data->result = hwnd;
    return FALSE;
  }
  return TRUE;
}

HWND WindowManager::FindReaperWindow(const char* title, HWND skipContainer)
{
  if (!title) return nullptr;

  DBG("[ReDockIt] FindReaperWindow: searching for '%s'\n", title);

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
        DBG("[ReDockIt] FindReaperWindow: DOCKED FRAME match '%s' hwnd=%p\n", dockedTitle, (void*)hwnd);
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
      DBG("[ReDockIt] FindReaperWindow: EXACT match hwnd=%p\n", (void*)hwnd);
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
        DBG("[ReDockIt] FindReaperWindow: DOCKED FRAME child match hwnd=%p\n", (void*)dockData.result);
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
          DBG("[ReDockIt] FindReaperWindow: DOCKED FRAME grandchild match hwnd=%p\n", (void*)dockData.result);
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
        DBG("[ReDockIt] FindReaperWindow: found '%s' inside '%s' (hwnd=%p)\n",
            title, dockBuf, (void*)data.result);
        return data.result;
      }
    }
  }

  DBG("[ReDockIt] FindReaperWindow: NOT FOUND '%s'\n", title);
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
    DBG("[ReDockIt] DumpWindows[%d]: '%s' hwnd=%p\n", data->count, buf, (void*)hwnd);
    data->count++;
  }
  return TRUE;
}

void WindowManager::DumpAllWindowTitles(const char* context)
{
  DBG("[ReDockIt] === DumpAllWindowTitles: %s ===\n", context ? context : "");
  DumpWindowData data = { nullptr, 0 };

  // Top-level windows
  DBG("[ReDockIt] -- Top-level windows --\n");
  data.count = 0;
  EnumWindows(DumpWindowEnumProc, (LPARAM)&data);

  // Children of REAPER main window
  if (g_reaperMainHwnd) {
    DBG("[ReDockIt] -- Children of REAPER main window --\n");
    data.count = 0;
    EnumChildWindows(g_reaperMainHwnd, DumpWindowEnumProc, (LPARAM)&data);
  }
  DBG("[ReDockIt] === End DumpAllWindowTitles ===\n");
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

  PaneState& ps = m_panes[paneId];
  if (ps.tabCount >= MAX_TABS_PER_PANE) return false;

  const WindowDef& def = KNOWN_WINDOWS[knownWindowIndex];

  DBG("[ReDockIt] CaptureByIndex: pane=%d window='%s' search='%s' alt='%s'\n",
          paneId, def.name, def.searchTitle, def.altSearchTitle ? def.altSearchTitle : "(none)");

  HWND hwnd = FindReaperWindow(def.searchTitle, containerHwnd);
  if (!hwnd && def.altSearchTitle) {
    hwnd = FindReaperWindow(def.altSearchTitle, containerHwnd);
  }
  if (!hwnd) {
    DBG("[ReDockIt] CaptureByIndex: FAILED — window not found\n");
    return false;
  }

  if (IsWindowCaptured(hwnd)) return false;

  TabEntry& tab = ps.tabs[ps.tabCount];
  memset(&tab, 0, sizeof(TabEntry));
  tab.name = def.name;
  tab.searchTitle = def.searchTitle;
  tab.toggleAction = def.toggleActionId;
  tab.isArbitrary = false;

  DBG("[ReDockIt] CaptureByIndex: found hwnd=%p, calling DoCapture\n", (void*)hwnd);

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
  DBG("[ReDockIt] CaptureArbitraryWindow: pane=%d name='%s' hwnd=%p action=%d cmd='%s'\n",
      paneId, displayName ? displayName : "(null)", (void*)targetHwnd, toggleAction,
      actionCmd ? actionCmd : "(null)");
  if (paneId < 0 || paneId >= MAX_PANES) return false;
  if (!targetHwnd || !displayName) return false;

  PaneState& ps = m_panes[paneId];
  if (ps.tabCount >= MAX_TABS_PER_PANE) {
    DBG("[ReDockIt] CaptureArbitraryWindow: REJECTED pane %d full (tabCount=%d)\n", paneId, ps.tabCount);
    return false;
  }
  if (IsWindowCaptured(targetHwnd)) {
    DBG("[ReDockIt] CaptureArbitraryWindow: REJECTED hwnd=%p already captured\n", (void*)targetHwnd);
    return false;
  }

  TabEntry& tab = ps.tabs[ps.tabCount];
  memset(&tab, 0, sizeof(TabEntry));

  safe_strncpy(tab.arbitraryName, displayName, sizeof(tab.arbitraryName));
  safe_strncpy(tab.arbitrarySearchTitle, displayName, sizeof(tab.arbitrarySearchTitle));

  tab.name = tab.arbitraryName;
  tab.searchTitle = tab.arbitrarySearchTitle;
  tab.toggleAction = toggleAction;
  tab.isArbitrary = true;
  if (actionCmd && actionCmd[0]) {
    safe_strncpy(tab.arbitraryActionCmd, actionCmd, sizeof(tab.arbitraryActionCmd));
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

  tab.originalParent = GetParent(targetHwnd);
  tab.originalStyle = GetWindowLong(targetHwnd, GWL_STYLE);
  tab.originalExStyle = GetWindowLong(targetHwnd, GWL_EXSTYLE);

  char targetTitle[256] = {};
  GetWindowText(targetHwnd, targetTitle, sizeof(targetTitle));

  DBG("[ReDockIt] DoCapture: target=%p title='%s' container=%p\n",
          (void*)targetHwnd, targetTitle, (void*)containerHwnd);

  // Detach from docker if needed
  HWND currentParent = tab.originalParent;
  if (currentParent && currentParent != g_reaperMainHwnd) {
    DBG("[ReDockIt] DoCapture: detaching from docker parent=%p\n", (void*)currentParent);
    SetParent(targetHwnd, g_reaperMainHwnd);
  }

  // Reparent to our container
  SetParent(targetHwnd, containerHwnd);

  LONG_PTR newStyle = WS_CHILD | WS_VISIBLE;
  SetWindowLong(targetHwnd, GWL_STYLE, newStyle);

  tab.hwnd = targetHwnd;
  tab.captured = true;

  ShowWindow(targetHwnd, SW_SHOWNA);
  InvalidateRect(targetHwnd, nullptr, TRUE);

  DBG("[ReDockIt] DoCapture: DONE hwnd=%p captured=true\n", (void*)targetHwnd);
  return true;
}

void WindowManager::DoRelease(TabEntry& tab)
{
  if (!tab.captured || !tab.hwnd) return;

  if (IsWindow(tab.hwnd)) {
    HWND restoreParent = tab.originalParent;
    if (!restoreParent || !IsWindow(restoreParent)) {
      restoreParent = g_reaperMainHwnd;
    }
    SetParent(tab.hwnd, restoreParent);
    SetWindowLong(tab.hwnd, GWL_STYLE, tab.originalStyle);
    SetWindowLong(tab.hwnd, GWL_EXSTYLE, tab.originalExStyle);

    if (tab.toggleAction > 0 && !tab.isArbitrary) {
      if (g_Main_OnCommand) {
        g_Main_OnCommand(tab.toggleAction, 0);
      }
    } else {
      ShowWindow(tab.hwnd, SW_HIDE);
    }
  }

  tab.hwnd = nullptr;
  tab.originalParent = nullptr;
  tab.captured = false;
  tab.isArbitrary = false;
}

// =========================================================================
// Tab management
// =========================================================================

static void FixTabPointers(TabEntry& tab)
{
  if (tab.isArbitrary) {
    tab.name = tab.arbitraryName;
    tab.searchTitle = tab.arbitrarySearchTitle;
  }
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

  for (int i = tabIndex; i < ps.tabCount - 1; i++) {
    ps.tabs[i] = ps.tabs[i + 1];
    FixTabPointers(ps.tabs[i]);
  }
  ps.tabCount--;
  memset(&ps.tabs[ps.tabCount], 0, sizeof(TabEntry));

  if (ps.tabCount == 0) {
    ps.activeTab = -1;
  } else if (ps.activeTab >= ps.tabCount) {
    ps.activeTab = ps.tabCount - 1;
  } else if (ps.activeTab == tabIndex && ps.activeTab >= ps.tabCount) {
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
  FixTabPointers(dst.tabs[dst.tabCount]);

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

  for (int i = srcTab; i < src.tabCount - 1; i++) {
    src.tabs[i] = src.tabs[i + 1];
    FixTabPointers(src.tabs[i]);
  }
  src.tabCount--;
  memset(&src.tabs[src.tabCount], 0, sizeof(TabEntry));

  if (src.tabCount == 0) {
    src.activeTab = -1;
  } else {
    if (src.activeTab >= src.tabCount) src.activeTab = src.tabCount - 1;
    TabEntry& curSrc = src.tabs[src.activeTab];
    if (curSrc.captured && curSrc.hwnd && IsWindow(curSrc.hwnd)) {
      ShowWindow(curSrc.hwnd, SW_SHOWNA);
    }
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

void WindowManager::ReleaseWindow(int paneId)
{
  if (paneId < 0 || paneId >= MAX_PANES) return;
  PaneState& ps = m_panes[paneId];
  for (int t = 0; t < ps.tabCount; t++) {
    DoRelease(ps.tabs[t]);
  }
  ps.tabCount = 0;
  ps.activeTab = -1;
}

void WindowManager::ReleaseAll()
{
  for (int i = 0; i < MAX_PANES; i++) {
    ReleaseWindow(i);
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
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
      } else {
        ShowWindow(tab.hwnd, SW_HIDE);
      }
    }
  }
}

void WindowManager::CheckAlive(HWND containerHwnd)
{
  for (int i = 0; i < MAX_PANES; i++) {
    PaneState& ps = m_panes[i];
    for (int t = ps.tabCount - 1; t >= 0; t--) {
      TabEntry& tab = ps.tabs[t];
      if (!tab.captured) continue;
      if (!tab.hwnd || !IsWindow(tab.hwnd)) {
        tab.hwnd = nullptr;
        tab.captured = false;
        tab.isArbitrary = false;
        for (int j = t; j < ps.tabCount - 1; j++) {
          ps.tabs[j] = ps.tabs[j + 1];
          FixTabPointers(ps.tabs[j]);
        }
        ps.tabCount--;
        memset(&ps.tabs[ps.tabCount], 0, sizeof(TabEntry));
        if (ps.tabCount == 0) {
          ps.activeTab = -1;
        } else if (ps.activeTab >= ps.tabCount) {
          ps.activeTab = ps.tabCount - 1;
        }
      }
    }
  }
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
