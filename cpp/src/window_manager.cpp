#include "window_manager.h"
#include <cstring>
#include <cstdio>

static FILE* dbgFile()
{
  static FILE* f = nullptr;
  if (!f) {
    f = fopen("/tmp/redockit_debug.log", "a");
    if (f) setbuf(f, nullptr);
  }
  return f;
}
#define DBG(...) do { FILE* _f = dbgFile(); if (_f) { fprintf(_f, __VA_ARGS__); } } while(0)

WindowManager::WindowManager()
  : m_activePaneCount(3)
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

void WindowManager::SetActivePaneCount(int count)
{
  if (count < 1) count = 1;
  if (count > MAX_PANES) count = MAX_PANES;
  m_activePaneCount = count;
}

// =========================================================================
// Window finding
// =========================================================================

struct FindWindowData {
  const char* searchTitle;
  HWND result;
};

static BOOL CALLBACK FindWindowEnumProc(HWND hwnd, LPARAM lParam)
{
  FindWindowData* data = (FindWindowData*)lParam;
  char buf[512];
  GetWindowText(hwnd, buf, sizeof(buf));
  if (!buf[0]) return TRUE;

  if (strstr(buf, "toolbar") || strstr(buf, "Toolbar")) return TRUE;

  // Skip "(docked)" windows — these are REAPER dock wrappers, not actual windows
  if (strstr(buf, "(docked)")) return TRUE;

  // Prefix match
  if (strstr(buf, data->searchTitle) == buf) {
    DBG("[ReDockIt] EnumWindows: PREFIX match '%s' for '%s' hwnd=%p\n", buf, data->searchTitle, (void*)hwnd);
    data->result = hwnd;
    return FALSE;
  }
  // Substring match
  if (strstr(buf, data->searchTitle)) {
    DBG("[ReDockIt] EnumWindows: SUBSTR match '%s' for '%s' hwnd=%p\n", buf, data->searchTitle, (void*)hwnd);
    data->result = hwnd;
    return FALSE;
  }
  return TRUE;
}

static BOOL CALLBACK FindChildWindowEnumProc(HWND hwnd, LPARAM lParam)
{
  FindWindowData* data = (FindWindowData*)lParam;
  char buf[512];
  GetWindowText(hwnd, buf, sizeof(buf));
  if (!buf[0]) return TRUE;

  if (strstr(buf, "toolbar") || strstr(buf, "Toolbar")) return TRUE;
  if (strstr(buf, "(docked)")) return TRUE;

  if (strstr(buf, data->searchTitle) == buf) {
    data->result = hwnd;
    return FALSE;
  }
  if (strstr(buf, data->searchTitle)) {
    data->result = hwnd;
    return FALSE;
  }
  return TRUE;
}

static BOOL CALLBACK DumpWindowEnumProc(HWND hwnd, LPARAM lParam)
{
  char buf[512];
  GetWindowText(hwnd, buf, sizeof(buf));
  if (buf[0]) {
    DBG("[ReDockIt] DUMP top-level: hwnd=%p title='%s' visible=%d parent=%p\n",
        (void*)hwnd, buf, IsWindowVisible(hwnd), (void*)GetParent(hwnd));
  }
  return TRUE;
}

static BOOL CALLBACK DumpChildEnumProc(HWND hwnd, LPARAM lParam)
{
  char buf[512];
  GetWindowText(hwnd, buf, sizeof(buf));
  if (buf[0]) {
    DBG("[ReDockIt] DUMP child: hwnd=%p title='%s' visible=%d parent=%p\n",
        (void*)hwnd, buf, IsWindowVisible(hwnd), (void*)GetParent(hwnd));
  }
  return TRUE;
}

void WindowManager::DumpAllWindows()
{
  extern HWND g_reaperMainHwnd;
  DBG("[ReDockIt] === DUMP ALL WINDOWS === mainHwnd=%p\n", (void*)g_reaperMainHwnd);
  DBG("[ReDockIt] --- Top-level windows ---\n");
  EnumWindows(DumpWindowEnumProc, 0);
  if (g_reaperMainHwnd) {
    DBG("[ReDockIt] --- Child windows of main ---\n");
    EnumChildWindows(g_reaperMainHwnd, DumpChildEnumProc, 0);
  }
  DBG("[ReDockIt] === END DUMP ===\n");
}

HWND WindowManager::FindReaperWindow(const char* title)
{
  if (!title) return nullptr;

  DBG("[ReDockIt] FindReaperWindow: searching for '%s'\n", title);

  // 1. Exact match among top-level windows
  HWND hwnd = FindWindowEx(nullptr, nullptr, nullptr, title);
  if (hwnd) {
    DBG("[ReDockIt] FindReaperWindow: EXACT match hwnd=%p\n", (void*)hwnd);
    return hwnd;
  }

  // 2. Top-level windows with prefix/substring match
  FindWindowData data = { title, nullptr };
  EnumWindows(FindWindowEnumProc, (LPARAM)&data);
  if (data.result) {
    return data.result;
  }

  // 3. Search child windows of REAPER main window
  extern HWND g_reaperMainHwnd;
  if (g_reaperMainHwnd) {
    data.result = nullptr;
    EnumChildWindows(g_reaperMainHwnd, FindChildWindowEnumProc, (LPARAM)&data);
    if (data.result) {
      return data.result;
    }
  }

  DBG("[ReDockIt] FindReaperWindow: NOT FOUND '%s'\n", title);
  return nullptr;
}

HWND WindowManager::FindChildInParent(HWND parent, const char* title)
{
  if (!parent || !title) return nullptr;
  FindWindowData data = { title, nullptr };
  EnumChildWindows(parent, FindChildWindowEnumProc, (LPARAM)&data);
  return data.result;
}

// =========================================================================
// Capture / Release
// =========================================================================

bool WindowManager::CaptureByIndex(int paneId, int knownWindowIndex, HWND containerHwnd)
{
  if (paneId < 0 || paneId >= m_activePaneCount) return false;
  if (knownWindowIndex < 0 || knownWindowIndex >= NUM_KNOWN_WINDOWS) return false;

  PaneState& ps = m_panes[paneId];
  if (ps.tabCount >= MAX_TABS_PER_PANE) return false;

  const WindowDef& def = KNOWN_WINDOWS[knownWindowIndex];

  DBG("[ReDockIt] CaptureByIndex: pane=%d window='%s' search='%s' alt='%s'\n",
          paneId, def.name, def.searchTitle, def.altSearchTitle ? def.altSearchTitle : "(none)");

  HWND hwnd = FindReaperWindow(def.searchTitle);
  if (!hwnd && def.altSearchTitle) {
    hwnd = FindReaperWindow(def.altSearchTitle);
  }
  if (!hwnd) {
    DBG("[ReDockIt] CaptureByIndex: FAILED — window not found\n");
    return false;
  }

  // Check if already captured
  if (IsWindowCaptured(hwnd)) return false;

  TabEntry& tab = ps.tabs[ps.tabCount];
  memset(&tab, 0, sizeof(TabEntry));
  tab.name = def.name;
  tab.searchTitle = def.searchTitle;
  tab.toggleAction = def.toggleActionId;
  tab.isArbitrary = false;

  DBG("[ReDockIt] CaptureByIndex: found hwnd=%p, calling DoCapture\n", (void*)hwnd);

  if (DoCapture(tab, hwnd, containerHwnd)) {
    // Hide previous active tab
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

bool WindowManager::CaptureArbitraryWindow(int paneId, HWND targetHwnd, const char* displayName, HWND containerHwnd)
{
  if (paneId < 0 || paneId >= m_activePaneCount) return false;
  if (!targetHwnd || !displayName) return false;

  PaneState& ps = m_panes[paneId];
  if (ps.tabCount >= MAX_TABS_PER_PANE) return false;
  if (IsWindowCaptured(targetHwnd)) return false;

  TabEntry& tab = ps.tabs[ps.tabCount];
  memset(&tab, 0, sizeof(TabEntry));

  strncpy(tab.arbitraryName, displayName, sizeof(tab.arbitraryName) - 1);
  tab.arbitraryName[sizeof(tab.arbitraryName) - 1] = '\0';
  strncpy(tab.arbitrarySearchTitle, displayName, sizeof(tab.arbitrarySearchTitle) - 1);
  tab.arbitrarySearchTitle[sizeof(tab.arbitrarySearchTitle) - 1] = '\0';

  tab.name = tab.arbitraryName;
  tab.searchTitle = tab.arbitrarySearchTitle;
  tab.toggleAction = 0;
  tab.isArbitrary = true;

  if (DoCapture(tab, targetHwnd, containerHwnd)) {
    // Hide previous active tab
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
  extern HWND g_reaperMainHwnd;
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
      extern HWND g_reaperMainHwnd;
      restoreParent = g_reaperMainHwnd;
    }
    SetParent(tab.hwnd, restoreParent);
    SetWindowLong(tab.hwnd, GWL_STYLE, tab.originalStyle);
    SetWindowLong(tab.hwnd, GWL_EXSTYLE, tab.originalExStyle);

    if (tab.toggleAction > 0 && !tab.isArbitrary) {
      extern void (*g_Main_OnCommand)(int, int);
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

// After copying/shifting TabEntry structs, fix name/searchTitle pointers
// for arbitrary tabs (they point into the struct's own char arrays)
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

  // Hide old active
  if (ps.activeTab >= 0 && ps.activeTab < ps.tabCount && ps.activeTab != tabIndex) {
    TabEntry& old = ps.tabs[ps.activeTab];
    if (old.captured && old.hwnd && IsWindow(old.hwnd)) {
      ShowWindow(old.hwnd, SW_HIDE);
    }
  }

  ps.activeTab = tabIndex;

  // Show new active
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

  // Shift remaining tabs left
  for (int i = tabIndex; i < ps.tabCount - 1; i++) {
    ps.tabs[i] = ps.tabs[i + 1];
    FixTabPointers(ps.tabs[i]);
  }
  ps.tabCount--;
  memset(&ps.tabs[ps.tabCount], 0, sizeof(TabEntry));

  // Fix activeTab
  if (ps.tabCount == 0) {
    ps.activeTab = -1;
  } else if (ps.activeTab >= ps.tabCount) {
    ps.activeTab = ps.tabCount - 1;
  } else if (ps.activeTab == tabIndex && ps.activeTab >= ps.tabCount) {
    ps.activeTab = ps.tabCount - 1;
  }

  // Show new active tab
  if (ps.activeTab >= 0 && ps.activeTab < ps.tabCount) {
    TabEntry& cur = ps.tabs[ps.activeTab];
    if (cur.captured && cur.hwnd && IsWindow(cur.hwnd)) {
      ShowWindow(cur.hwnd, SW_SHOWNA);
    }
  }
}

void WindowManager::MoveTab(int srcPane, int srcTab, int dstPane)
{
  if (srcPane < 0 || srcPane >= m_activePaneCount) return;
  if (dstPane < 0 || dstPane >= m_activePaneCount) return;
  if (srcPane == dstPane) return;

  PaneState& src = m_panes[srcPane];
  PaneState& dst = m_panes[dstPane];
  if (srcTab < 0 || srcTab >= src.tabCount) return;
  if (dst.tabCount >= MAX_TABS_PER_PANE) return;

  // Copy tab to destination
  dst.tabs[dst.tabCount] = src.tabs[srcTab];
  FixTabPointers(dst.tabs[dst.tabCount]);

  // Hide old active in dst, make moved tab active
  if (dst.activeTab >= 0 && dst.activeTab < dst.tabCount) {
    TabEntry& oldDst = dst.tabs[dst.activeTab];
    if (oldDst.captured && oldDst.hwnd && IsWindow(oldDst.hwnd)) {
      ShowWindow(oldDst.hwnd, SW_HIDE);
    }
  }
  dst.activeTab = dst.tabCount;
  dst.tabCount++;

  // Show moved tab
  TabEntry& movedTab = dst.tabs[dst.activeTab];
  if (movedTab.captured && movedTab.hwnd && IsWindow(movedTab.hwnd)) {
    ShowWindow(movedTab.hwnd, SW_SHOWNA);
  }

  // Remove from source (shift left)
  for (int i = srcTab; i < src.tabCount - 1; i++) {
    src.tabs[i] = src.tabs[i + 1];
    FixTabPointers(src.tabs[i]);
  }
  src.tabCount--;
  memset(&src.tabs[src.tabCount], 0, sizeof(TabEntry));

  // Fix source activeTab
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

void WindowManager::RepositionAll(const SplitterLayout& layout)
{
  int count = layout.GetPaneCount();
  for (int i = 0; i < count; i++) {
    PaneState& ps = m_panes[i];
    if (ps.tabCount == 0) continue;

    const Pane& pane = layout.GetPane(i);

    // Tab bar (or empty header) is always TAB_BAR_HEIGHT at pane top
    int headerOffset = TAB_BAR_HEIGHT;

    int x = pane.rect.left;
    int y = pane.rect.top + headerOffset;
    int w = pane.rect.right - pane.rect.left;
    int h = pane.rect.bottom - pane.rect.top - headerOffset;

    if (w <= 0 || h <= 0) continue;

    for (int t = 0; t < ps.tabCount; t++) {
      TabEntry& tab = ps.tabs[t];
      if (!tab.captured || !tab.hwnd) continue;
      if (!IsWindow(tab.hwnd)) continue;

      if (t == ps.activeTab) {
        SetWindowPos(tab.hwnd, HWND_TOP, x, y, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        // Note: do NOT SendMessage(WM_SIZE) — SetWindowPos already triggers it,
        // and some REAPER windows (Actions) crash if WM_SIZE is sent after
        // being detached from their docker (dangling sizer pointers)
      } else {
        ShowWindow(tab.hwnd, SW_HIDE);
      }
    }
  }
}

void WindowManager::CheckAlive(HWND containerHwnd)
{
  for (int i = 0; i < m_activePaneCount; i++) {
    PaneState& ps = m_panes[i];
    for (int t = ps.tabCount - 1; t >= 0; t--) {
      TabEntry& tab = ps.tabs[t];
      if (!tab.captured) continue;
      if (!tab.hwnd || !IsWindow(tab.hwnd)) {
        // Window died — remove this tab
        tab.hwnd = nullptr;
        tab.captured = false;
        tab.isArbitrary = false;
        // Shift remaining tabs left
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

bool WindowManager::IsAnyCaptured() const
{
  for (int i = 0; i < m_activePaneCount; i++) {
    if (m_panes[i].tabCount > 0) return true;
  }
  return false;
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
