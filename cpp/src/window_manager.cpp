#include "window_manager.h"
#include "swell_cocoa_helpers.h"
#include "fx_capture.h"
#include "globals.h"
#include "debug.h"
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <psapi.h>  // EnumProcessModules, GetModuleInformation (Entry 12)
#else
#include <strings.h>  // strcasecmp / strncasecmp (Entry 15 cross-platform)
#endif

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
int GetToolbarToggleAction(const char* title)
{
  if (!title) return 0;
  if (strcmp(title, "Toolbar Docker") == 0) return TOOLBAR_DOCKER_ACTION;
  if (strncmp(title, "Toolbar ", 8) == 0) {
    int n = atoi(title + 8);
    if (n >= 1 && n <= TOOLBAR_ACTION_COUNT)
      return TOOLBAR_ACTION_BASE + (n - 1);
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
  // Toolbars: TOOLBAR_ACTION_BASE..+COUNT-1 → "Toolbar 1".."Toolbar 16"
  if (action >= TOOLBAR_ACTION_BASE &&
      action < TOOLBAR_ACTION_BASE + TOOLBAR_ACTION_COUNT) {
    snprintf(buf, bufSize, "Toolbar %d", action - TOOLBAR_ACTION_BASE + 1);
    return true;
  }
  if (action == TOOLBAR_DOCKER_ACTION) {
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
  // Sprint 1 Entry 15 follow-up — Win32 Direct2D plugins (ReaBeat, Reamix,
  // …) can have an empty WindowText after Main_OnCommand fires their
  // toggle. Without the module fallback FindReaperWindow misses them on
  // workspace load → 30 retries fail → plugin remains a floating orphan.
  // SWELL macOS/Linux keep the v2.0 hard-skip — no comparable module
  // identification path and their plugins have SetWindowText titles.
#ifdef _WIN32
  if (!buf[0]) {
    if (!WindowManager::TryGetAppNameFromModule(hwnd, buf, sizeof(buf)))
      return TRUE;
  }
#else
  if (!buf[0]) return TRUE;
#endif

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
    // B22: defend against known prefix collisions. "Mixer" otherwise greedily
    // matches "Mixer Master" (REAPER's master-strip inspector), capturing the
    // wrong window. For this specific search, require an exact title match.
    if (strcmp(data->searchTitle, "Mixer") == 0 && strcmp(matchBuf, "Mixer") != 0) {
      return TRUE;
    }
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

  // v2.0.4 #1 (ADR-037) — FX plugin windows have no global toggle action;
  // identity is a (track GUID, FX GUID) pair carried in tab.actionCmd.
  // Try FX detection BEFORE DiscoverActionForWindow because FX UIs can
  // appear in REAPER's action table as generic "Show all FX" entries and
  // produce a noisy/wrong score match.
  if (tab.toggleAction <= 0 && !(actionCmd && actionCmd[0])) {
    char fxIdentity[FxCapture::kIdentityMaxLen] = {};
    if (FxCapture::DetectFxIdentityForHwnd(targetHwnd, fxIdentity, sizeof(fxIdentity))) {
      safe_strncpy(tab.actionCmd, fxIdentity, sizeof(tab.actionCmd));
      // Stable display label from REAPER (survives subsequent renames).
      char fxName[256] = {};
      if (FxCapture::GetDisplayName(fxIdentity, fxName, sizeof(fxName)) && fxName[0]) {
        safe_strncpy(tab.name, fxName, sizeof(tab.name));
        safe_strncpy(tab.searchTitle, fxName, sizeof(tab.searchTitle));
      }
      DBG("[MaxPane] CaptureArbitraryWindow: FX identity captured '%s' name='%s'\n",
          fxIdentity, tab.name);
    }
  }

  // Sprint 1 Entry 15 — for empty-title plugins (ReaBeat, Reamix, Lua
  // scripts) LookupToggleAction returns 0. Scan REAPER's action table
  // for the matching show/hide action via module name + title-token
  // scoring; result drives correct workspace-restore + close-WM_CLOSE.
  // Skip when FX identity already populated tab.actionCmd above.
  if (tab.toggleAction <= 0 && !tab.actionCmd[0]) {
    tab.toggleAction = DiscoverActionForWindow(targetHwnd, displayName);
  }
  tab.isArbitrary = true;
  if (actionCmd && actionCmd[0] && !tab.actionCmd[0]) {
    safe_strncpy(tab.actionCmd, actionCmd, sizeof(tab.actionCmd));
  }
  // Auto-fill actionCmd so the workspace save round-trips through
  // ResolveActionCommand on load. Skip if caller already provided one.
  if (tab.toggleAction > 0 && !tab.actionCmd[0]) {
    GetActionCommandString(tab.toggleAction, tab.actionCmd, sizeof(tab.actionCmd));
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

// Verify that a SetParent call achieved the expected parent.
// Returns true if GetParent(target) == expectedParent. Logs on mismatch.
// SWELL on macOS does honor GetParent after SetParent; on Win32 SetParent can
// fail (NULL return / cross-thread / target in destruction) without us noticing.
// =========================================================================
// Sprint 1 Entry 12 — plugin module-DLL display-name resolution
// =========================================================================
// JUCE / Direct2D plugins (ReaBeat, Reamix) draw their own titlebars and
// never call SetWindowText — GetWindowText returns "". The Open Windows
// menu (Entry 9) and capture-by-click (Entry 13) need a human-readable
// label. Waterfall:
//   1) GetWindowText (already handled by callers)
//   2) TryGetAppNameFromModule — GCLP_HMODULE then DWLP_DLGPROC/GWLP_WNDPROC
//      walked through EnumProcessModules + GetModuleInformation
//   3) TryExtractAppNameFromChildren — longest Static-child label
//   4) Synthetic "#<classname> (untitled)" so the menu line is never empty.

#ifdef _WIN32
namespace {

// System DLLs whose ownership doesn't identify a user-installed plugin —
// dialog windows registered by user32/comctl32 etc. resolve to one of these.
static const char* const kSystemModules[] = {
  "user32", "kernel32", "kernelbase", "ntdll", "comctl32",
  "gdi32", "ole32", "shell32", "psapi", "uxtheme",
  "msctf", "imm32", "reaper", "reaper_maxpane",
  nullptr
};

static bool IsSystemModule(const char* name)
{
  if (!name || !name[0]) return true;
  for (int i = 0; kSystemModules[i]; i++) {
    if (_stricmp(name, kSystemModules[i]) == 0) return true;
  }
  return false;
}

// Strip directory, .dll suffix, "reaper_" prefix; capitalize first letter.
// Returns false if the cleaned name is empty or matches a system module.
static bool CleanPluginModuleName(const char* fullPath, char* buf, int bufSize)
{
  if (!fullPath || !buf || bufSize <= 0) return false;
  buf[0] = '\0';
  const char* lastSlash = strrchr(fullPath, '\\');
  if (!lastSlash) lastSlash = strrchr(fullPath, '/');
  const char* basename = lastSlash ? lastSlash + 1 : fullPath;
  char tmp[128];
  safe_strncpy(tmp, basename, sizeof(tmp));
  char* dot = strrchr(tmp, '.');
  if (dot) *dot = '\0';
  const char* nameStart = tmp;
  if (_strnicmp(tmp, "reaper_", 7) == 0) nameStart += 7;
  if (!nameStart[0]) return false;
  if (IsSystemModule(nameStart)) return false;
  safe_strncpy(buf, nameStart, (size_t)bufSize);
  if (buf[0] >= 'a' && buf[0] <= 'z') buf[0] = (char)(buf[0] - 'a' + 'A');
  return true;
}

// Find the loaded module whose address range contains `addr`.
static HMODULE FindModuleByAddress(void* addr)
{
  if (!addr) return nullptr;
  HMODULE mods[256];
  DWORD needed = 0;
  HANDLE proc = GetCurrentProcess();
  if (!EnumProcessModules(proc, mods, sizeof(mods), &needed)) return nullptr;
  const DWORD count = needed / sizeof(HMODULE);
  for (DWORD i = 0; i < count && i < 256; i++) {
    MODULEINFO mi = {};
    if (!GetModuleInformation(proc, mods[i], &mi, sizeof(mi))) continue;
    BYTE* base = (BYTE*)mi.lpBaseOfDll;
    BYTE* end  = base + mi.SizeOfImage;
    if ((BYTE*)addr >= base && (BYTE*)addr < end) return mods[i];
  }
  return nullptr;
}

// EnumChildWindows callback for TryExtractAppNameFromChildren.
struct ChildScanData { char best[256]; int bestLen; };
static BOOL CALLBACK ScanStaticChildProc(HWND child, LPARAM lp)
{
  ChildScanData* d = (ChildScanData*)lp;
  char cls[64] = {};
  GetClassNameA(child, cls, sizeof(cls));
  if (_stricmp(cls, "Static") != 0) return TRUE;
  char text[256];
  int n = GetWindowTextA(child, text, sizeof(text));
  if (n <= 0) return TRUE;
  bool hasLetter = false;
  for (int i = 0; text[i]; i++) {
    char c = text[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { hasLetter = true; break; }
  }
  if (!hasLetter) return TRUE;
  if (n > d->bestLen) {
    d->bestLen = n;
    safe_strncpy(d->best, text, sizeof(d->best));
  }
  return TRUE;
}

}  // namespace
#endif  // _WIN32

bool WindowManager::TryGetAppNameFromModule(HWND hwnd, char* buf, int bufSize)
{
  if (!hwnd || !buf || bufSize <= 0) return false;
  buf[0] = '\0';
#ifdef _WIN32
  if (!IsWindow(hwnd)) return false;

  char modPath[MAX_PATH];

  // 1) GCLP_HMODULE — owning DLL for custom-registered window classes.
  HMODULE classMod = (HMODULE)GetClassLongPtr(hwnd, GCLP_HMODULE);
  if (classMod && GetModuleFileNameA(classMod, modPath, sizeof(modPath))) {
    if (CleanPluginModuleName(modPath, buf, bufSize)) return true;
  }

  // 2)/3) Dialog DLGPROC or plain WNDPROC — walk address range to find
  //       the owning module via EnumProcessModules + GetModuleInformation.
  char clsName[64] = {};
  GetClassNameA(hwnd, clsName, sizeof(clsName));
  bool isDialog = (clsName[0] == '#') || (strstr(clsName, "Dialog") != nullptr);

  void* procAddr = nullptr;
  if (isDialog) {
    procAddr = (void*)GetWindowLongPtr(hwnd, DWLP_DLGPROC);
    if (!procAddr) procAddr = (void*)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
  } else {
    procAddr = (void*)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
  }

  HMODULE procMod = FindModuleByAddress(procAddr);
  if (procMod && GetModuleFileNameA(procMod, modPath, sizeof(modPath))) {
    if (CleanPluginModuleName(modPath, buf, bufSize)) return true;
  }
  return false;
#else
  (void)hwnd; (void)buf; (void)bufSize;
  return false;
#endif
}

bool WindowManager::TryExtractAppNameFromChildren(HWND hwnd, char* buf, int bufSize)
{
  if (!hwnd || !buf || bufSize <= 0) return false;
  buf[0] = '\0';
#ifdef _WIN32
  ChildScanData d = {{0}, 0};
  EnumChildWindows(hwnd, ScanStaticChildProc, (LPARAM)&d);
  if (d.bestLen > 0) {
    safe_strncpy(buf, d.best, (size_t)bufSize);
    return true;
  }
  return false;
#else
  (void)hwnd; (void)buf; (void)bufSize;
  return false;
#endif
}

void WindowManager::ResolveWindowDisplayName(HWND hwnd, char* buf, int bufSize)
{
  if (!buf || bufSize <= 0) return;
  buf[0] = '\0';
  if (!hwnd || !IsWindow(hwnd)) return;

  GetWindowText(hwnd, buf, bufSize);
  if (buf[0]) return;

  if (TryGetAppNameFromModule(hwnd, buf, bufSize)) return;
  if (TryExtractAppNameFromChildren(hwnd, buf, bufSize)) return;

#ifdef _WIN32
  char clsName[64] = {};
  GetClassNameA(hwnd, clsName, sizeof(clsName));
  snprintf(buf, (size_t)bufSize, "#%s (untitled)", clsName[0] ? clsName : "?");
#else
  snprintf(buf, (size_t)bufSize, "#untitled");
#endif
}

// =========================================================================
// Sprint 1 Entry 15 — DiscoverActionForWindow (action-table scoring)
// =========================================================================
// Cross-platform body (Entry 15 follow-up): the algorithm is REAPER-API only
// (g_GetToggleCommandState / g_ReverseNamedCommandLookup / g_kbd_getTextFromCmd
// — all available on macOS/Linux from REAPER 6.71). Only the optional
// module-DLL fallback (Entry 12 TryGetAppNameFromModule) is Win32-specific.
// macOS/Linux fall back to title-token scoring only, which covers the common
// case of REAPER plugins whose NSWindow / GTK window has a SetWindowText
// title (ReaBeat, Reamix, Lua scripts via ReaImGui).
namespace {

#ifdef _WIN32
  #define MP_STRICMP  _stricmp
  #define MP_STRNICMP _strnicmp
#else
  #define MP_STRICMP  strcasecmp
  #define MP_STRNICMP strncasecmp
#endif

// Per-(module, title) cache. ReaImGui hosts many scripts; a module-only cache
// would alias the first script's action to every later one. On macOS/Linux
// module is empty — cache keys on title alone.
struct DiscoverCacheEntry { char module[64]; char title[256]; int actionId; };
static DiscoverCacheEntry g_discoverCache[64];
static int g_discoverCacheCount = 0;

static int LookupDiscoverCache(const char* module, const char* title)
{
  if (!module || !title) return -1;
  for (int i = 0; i < g_discoverCacheCount; i++) {
    if (MP_STRICMP(g_discoverCache[i].module, module) == 0 &&
        MP_STRICMP(g_discoverCache[i].title, title) == 0) {
      return g_discoverCache[i].actionId;
    }
  }
  return -1;  // sentinel: not cached (a real 0 result IS cached as 0)
}

static void StoreDiscoverCache(const char* module, const char* title, int actionId)
{
  if (!module || !title) return;
  if (g_discoverCacheCount >= 64) {
    memmove(&g_discoverCache[0], &g_discoverCache[1],
            sizeof(DiscoverCacheEntry) * 63);
    g_discoverCacheCount = 63;
  }
  DiscoverCacheEntry& e = g_discoverCache[g_discoverCacheCount++];
  safe_strncpy(e.module, module, sizeof(e.module));
  safe_strncpy(e.title, title, sizeof(e.title));
  e.actionId = actionId;
}

static void StripArchSuffix(char* name)
{
  if (!name || !name[0]) return;
  static const char* const kArch[] = {
    "-x64", "-x86", "-arm64", "-arm", "-win32", "-win64", "-i386", nullptr
  };
  size_t len = strlen(name);
  for (int i = 0; kArch[i]; i++) {
    size_t slen = strlen(kArch[i]);
    if (len > slen && MP_STRICMP(name + len - slen, kArch[i]) == 0) {
      name[len - slen] = '\0';
      return;
    }
  }
}

// Tokenize on non-alphanumeric, lowercase, length ≥ 3, cap at maxTokens.
static int TokenizeTitle(const char* title, char tokens[][32], int maxTokens)
{
  if (!title) return 0;
  int count = 0;
  const char* p = title;
  auto isAN = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
  };
  while (*p && count < maxTokens) {
    while (*p && !isAN(*p)) p++;
    if (!*p) break;
    char buf[32]; int n = 0;
    while (*p && n < (int)sizeof(buf) - 1 && isAN(*p)) {
      char c = *p++;
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      buf[n++] = c;
    }
    buf[n] = '\0';
    if (n >= 3) { safe_strncpy(tokens[count], buf, 32); count++; }
  }
  return count;
}

static bool ContainsCaseInsensitive(const char* haystack, const char* needle)
{
  if (!haystack || !needle) return false;
  size_t hlen = strlen(haystack), nlen = strlen(needle);
  if (nlen == 0 || nlen > hlen) return false;
  for (size_t i = 0; i + nlen <= hlen; i++) {
    if (MP_STRNICMP(haystack + i, needle, nlen) == 0) return true;
  }
  return false;
}

}  // namespace

HWND WindowManager::ResolveCaptureSourceForClick(HWND underCursor)
{
  if (!underCursor || underCursor == g_reaperMainHwnd) return nullptr;
#ifdef _WIN32
  // Walk up looking for the shallowest plugin match. Two acceptance signals:
  //   (1) title resolves to a known REAPER toggle action (KNOWN_WINDOWS /
  //       toolbars / similar)
  //   (2) WndProc lives in a plugin DLL (via TryGetAppNameFromModule)
  // Track the shallowest plugin-DLL match; the moment the chain leaves a
  // plugin DLL the most recent plugin HWND wins. This avoids capturing the
  // REAPER docker frame's inner WS_CHILD container for docked plugins.
  HWND cur = underCursor;
  HWND bestPlugin = nullptr;
  int hops = 0;
  while (cur && cur != g_reaperMainHwnd && hops < 32) {
    char title[256] = {};
    GetWindowText(cur, title, sizeof(title));
    if (title[0] && LookupToggleAction(title) > 0) return cur;

    char modName[128] = {};
    if (TryGetAppNameFromModule(cur, modName, sizeof(modName))) {
      bestPlugin = cur;
    } else if (bestPlugin) {
      return bestPlugin;  // chain just exited plugin DLL — pick last plugin HWND
    }
    cur = GetParent(cur);
    hops++;
  }
  return bestPlugin;
#else
  HWND topLevel = underCursor;
  HWND parent = GetParent(topLevel);
  while (parent && parent != g_reaperMainHwnd) {
    topLevel = parent;
    parent = GetParent(topLevel);
  }
  return (topLevel == g_reaperMainHwnd) ? nullptr : topLevel;
#endif
}

int WindowManager::DiscoverActionForWindow(HWND hwnd, const char* windowTitle)
{
  if (!hwnd || !IsWindow(hwnd)) return 0;
  if (!g_GetToggleCommandState || !g_ReverseNamedCommandLookup ||
      !g_kbd_getTextFromCmd) return 0;

  // Win32 — module-DLL waterfall (Entry 12) gives us a fallback when title
  // tokens don't score confidently. macOS/Linux have no comparable per-
  // window module identification (NSWindow / GTK don't expose dylib
  // ownership in the same way), so module stays empty and we rely on
  // title-token scoring alone.
  char module[64] = {};
#ifdef _WIN32
  TryGetAppNameFromModule(hwnd, module, sizeof(module));
#endif

  const bool hasTitle = (windowTitle && windowTitle[0]);
  // Need at least one signal — empty title AND no module = no foothold.
  if (!module[0] && !hasTitle) return 0;

  const char* cacheModule = module[0] ? module : "";
  if (hasTitle) {
    int cached = LookupDiscoverCache(cacheModule, windowTitle);
    if (cached >= 0) return cached;
  }

  // Module base name lowered + arch-suffix stripped (Reabeat-x64 → reabeat).
  // Empty on macOS/Linux — moduleFallbackId stays 0 and the title threshold
  // is the sole gate.
  char modBase[64] = {};
  if (module[0]) {
    safe_strncpy(modBase, module, sizeof(modBase));
    StripArchSuffix(modBase);
    for (int i = 0; modBase[i]; i++) {
      if (modBase[i] >= 'A' && modBase[i] <= 'Z') {
        modBase[i] = (char)(modBase[i] - 'A' + 'a');
      }
    }
  }

  char tokens[8][32] = {{0}};
  int tokenCount = hasTitle ? TokenizeTitle(windowTitle, tokens, 8) : 0;

  // B-SCRIPT-RESTORE — split candidate pool: toggle actions on the strict
  // ≥2-hit track, ReaImGui scripts on a 1-hit track (the RS+state==-1
  // filter already narrows them to "currently running"). Single-active-
  // script fallback covers the "script title shares zero tokens with the
  // Script: filename.lua action name" case (e.g. "MIDI Lyrics" / lyrics.lua).
  int bestId = 0, bestHits = -1;
  int bestScriptId = 0, bestScriptHits = -1;
  int moduleFallbackId = 0;
  int singleActiveScriptId = 0;
  int activeScriptCount = 0;

  for (int id = 1; id < 200000; id++) {
    int state = g_GetToggleCommandState(id);
    bool isScript = false;
    if (state != 1) {
      if (state != -1) continue;
      const char* named = g_ReverseNamedCommandLookup(id);
      if (!named || named[0] != 'R' || named[1] != 'S') continue;  // RS = script
      isScript = true;
    }
    const char* actionName = g_kbd_getTextFromCmd(id, nullptr);
    if (!actionName || !actionName[0]) continue;

    int hits = 0;
    for (int t = 0; t < tokenCount; t++) {
      if (ContainsCaseInsensitive(actionName, tokens[t])) hits++;
    }
    if (isScript) {
      activeScriptCount++;
      singleActiveScriptId = id;
      if (hits > bestScriptHits) { bestScriptHits = hits; bestScriptId = id; }
    } else {
      if (hits > bestHits) { bestHits = hits; bestId = id; }
    }
    if (!moduleFallbackId && modBase[0] &&
        ContainsCaseInsensitive(actionName, modBase)) {
      moduleFallbackId = id;
    }
  }

  // Confidence thresholds. Toggle track: generic tokens like "imgui" appear
  // in every ReaImGui action; require ≥2 hits when title yields ≥2 tokens.
  // Script track: the RS prefix + state==-1 already narrows the pool, so
  // a single token hit is enough.
  const int toggleThreshold = (tokenCount >= 2) ? 2 : (tokenCount == 1 ? 1 : 0);
  const int scriptThreshold = (tokenCount >= 1) ? 1 : 0;
  int result = 0;
  if (bestHits >= toggleThreshold && bestId > 0)
    result = bestId;
  else if (bestScriptHits >= scriptThreshold && bestScriptId > 0)
    result = bestScriptId;
  else if (activeScriptCount == 1 && singleActiveScriptId > 0)
    result = singleActiveScriptId;
  else if (moduleFallbackId > 0)
    result = moduleFallbackId;

  if (hasTitle) StoreDiscoverCache(cacheModule, windowTitle, result);
  return result;
}

static bool VerifySetParent(HWND target, HWND expectedParent, const char* siteTag)
{
  HWND actual = GetParent(target);
  if (actual == expectedParent) return true;
  DBG("[MaxPane] SetParent VERIFY FAILED at %s: target=%p expected=%p actual=%p\n",
      siteTag, (void*)target, (void*)expectedParent, (void*)actual);
  return false;
}

// Sprint 1 Entry 11 — detach a previously WS_CHILD'd window back to top-level.
// Plain SetParent(hwnd, nullptr) on Win32 leaves the window WS_CHILD-of-desktop
// per MSDN SetParent Remarks: "if hWndNewParent is NULL, you should also clear
// the WS_CHILD bit and set the WS_POPUP style **after** calling SetParent".
// Without the style flip, Win32 silently parks the orphan under a default
// docker frame and the next DoCapture/Reaper-side close can surface zombie
// tabs. SWELL macOS/Linux preserve the v2.0 behaviour (plain SetParent).
static void DetachToTopLevel(HWND hwnd)
{
  if (!hwnd || !IsWindow(hwnd)) return;
#ifdef _WIN32
  ShowWindow(hwnd, SW_HIDE);
  SetParent(hwnd, nullptr);
  LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  style = (style & ~WS_CHILD) | WS_POPUP;
  SetWindowLongPtr(hwnd, GWL_STYLE, style);
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
  SetParent(hwnd, nullptr);
#endif
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
  // B27 — snapshot the window's screen rect BEFORE we steal it. DoRelease
  // restores this position to the orphan NSWindow before triggering close so
  // REAPER's wnd_vis saves a sensible coord (not the (0,0) default that
  // SWELL's recreated NSWindow lands at).
  GetWindowRect(targetHwnd, &tab.originalRect);

  char targetTitle[256] = {};
  GetWindowText(targetHwnd, targetTitle, sizeof(targetTitle));

  DBG("[MaxPane] DoCapture: target=%p title='%s' container=%p\n",
          (void*)targetHwnd, targetTitle, (void*)containerHwnd);

  // Detach from docker if needed
  HWND currentParent = tab.originalParent;
  if (currentParent && currentParent != g_reaperMainHwnd) {
    DBG("[MaxPane] DoCapture: detaching from docker parent=%p\n", (void*)currentParent);
    // B20: REAPER's docker manager (DockWindowAddEx system) keeps the captured
    // window registered as one of its tabs even after we SetParent the NSView
    // away. Result: ghost tab placeholder in REAPER's docker, and if user
    // closes that docker REAPER destroys the view (which is now ours in
    // MaxPane). Tell REAPER to forget about it BEFORE we steal the NSView.
    if (g_DockWindowRemove) {
      g_DockWindowRemove(targetHwnd);
      DBG("[MaxPane] DoCapture: DockWindowRemove called on %p\n", (void*)targetHwnd);
    }
    SetParent(targetHwnd, g_reaperMainHwnd);
    // Best-effort verify; if detach failed, the next reparent is still the gate.
    VerifySetParent(targetHwnd, g_reaperMainHwnd, "DoCapture/detach");
  }

#ifdef _WIN32
  // Sprint 1 Entries 4 + 14 — canonical Win32 top-level → child reparent:
  //   hide → strip top-level bits (incl. WS_POPUP, per MSDN SetParent +
  //   WS_CHILD docs: the two styles cannot co-exist) → strip GWL_EXSTYLE
  //   chrome bits (WS_EX_WINDOWEDGE / CLIENTEDGE / STATICEDGE /
  //   DLGMODALFRAME, otherwise dialog-class plugins like ReaBeat show
  //   a visible 3D frame inside the pane) → SWP_FRAMECHANGED so the NC
  //   area is recomputed for the new style → SetParent → restore
  //   WS_VISIBLE. The previous "SetParent first, style strip after" order
  //   left WS_POPUP and WS_CHILD set simultaneously and parked the GUI
  //   thread in NtUserSetParent (capture-of-REAPER-native = REAPER freeze).
  ShowWindow(targetHwnd, SW_HIDE);
  LONG_PTR origStyle   = GetWindowLongPtr(targetHwnd, GWL_STYLE);
  LONG_PTR origExStyle = GetWindowLongPtr(targetHwnd, GWL_EXSTYLE);
  LONG_PTR stripMask   = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_POPUP;
  LONG_PTR exStripMask = WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE
                       | WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME;
  LONG_PTR newStyle    = (origStyle & ~stripMask) | WS_CHILD;
  LONG_PTR newExStyle  = origExStyle & ~exStripMask;
  SetWindowLongPtr(targetHwnd, GWL_STYLE,   newStyle);
  SetWindowLongPtr(targetHwnd, GWL_EXSTYLE, newExStyle);
  SetWindowPos(targetHwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  SetParent(targetHwnd, containerHwnd);
  if (!VerifySetParent(targetHwnd, containerHwnd, "DoCapture/attach")) {
    // Reparent failed — restore style/exstyle/parent so we don't leave the
    // window in an indeterminate state, then abandon this capture.
    SetWindowLongPtr(targetHwnd, GWL_STYLE,   origStyle);
    SetWindowLongPtr(targetHwnd, GWL_EXSTYLE, origExStyle);
    SetParent(targetHwnd, tab.originalParent);
    DBG("[MaxPane] DoCapture: ABANDONED — reparent to container failed, restored originalParent=%p\n",
        (void*)tab.originalParent);
    return false;
  }
  SetWindowLongPtr(targetHwnd, GWL_STYLE,
      GetWindowLongPtr(targetHwnd, GWL_STYLE) | WS_VISIBLE);
#else
  // SWELL ordering: style transform safely runs after SetParent on macOS/Linux
  // (the Cocoa/GTK reparent doesn't observe Win32 WS_POPUP semantics).
  SetParent(targetHwnd, containerHwnd);
  if (!VerifySetParent(targetHwnd, containerHwnd, "DoCapture/attach")) {
    SetParent(targetHwnd, tab.originalParent);
    DBG("[MaxPane] DoCapture: ABANDONED — reparent to container failed, restored originalParent=%p\n",
        (void*)tab.originalParent);
    return false;
  }

  // Preserve original style bits, just add WS_CHILD | WS_VISIBLE and strip
  // top-level window chrome.  Stripping all styles breaks frameless windows
  // like toolbars whose rendering depends on flags like WS_CLIPCHILDREN.
  LONG_PTR origStyle = GetWindowLongPtr(targetHwnd, GWL_STYLE);
  LONG_PTR stripMask = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU;
  LONG_PTR newStyle = (origStyle & ~stripMask) | WS_CHILD | WS_VISIBLE;
  SetWindowLongPtr(targetHwnd, GWL_STYLE, newStyle);
#endif

  tab.hwnd = targetHwnd;
  tab.captured = true;

  ShowWindow(targetHwnd, SW_SHOWNA);

  // B23: reset frame to container area so a previously floating window
  // (whose NSView frame still has on-screen coordinates from its prior
  // top-level NSWindow) is visible inside the pane immediately. RefreshLayout
  // (called by every capture entry point) re-runs RepositionAll right after
  // and tightens this to the actual pane rect; this is just to avoid the
  // intermediate state where the view sits off-screen with negative coords.
  RECT cr;
  GetClientRect(containerHwnd, &cr);
  SetWindowPos(targetHwnd, nullptr, 0, 0, cr.right, cr.bottom,
               SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);

  // Force Cocoa layout + display pass.  SWELL's SetParent does NOT trigger
  // setNeedsLayout: on the reparented NSView, so child controls (e.g.
  // Routing Matrix grid) may have stale frames from before reparent.
  ForceViewLayoutAndDisplay(targetHwnd);
  InvalidateRect(targetHwnd, nullptr, TRUE);

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

    // v2.0.4 #1 (ADR-037) — FX identity path. REAPER's TrackFX_Show(_, _, 2)
    // closes the FX UI cleanly and updates its own tracker. Skips the
    // toggle/WM_CLOSE dispatch entirely. For workspace switch (toggleOff=
    // false) we still hide because the user-visible expectation is "this
    // layout's FX windows go away when I switch layouts" — the plugin
    // instance keeps running, only the floating UI hides.
    if (FxCapture::IsFxIdentity(tab.actionCmd)) {
      DBG("[MaxPane] DoRelease: FX identity '%s' — TrackFX_Show(hide)\n",
          tab.actionCmd);
      FxCapture::Hide(tab.actionCmd);

      if (IsWindow(tab.hwnd)) {
        // FX UI may still be alive as a top-level NSWindow if REAPER's
        // implementation chose to hide rather than destroy. Restore it to
        // top-level so the WS_CHILD relationship with MaxPane container is
        // gone — next TrackFX_Show(3) will give us a fresh HWND anyway.
        DetachToTopLevel(tab.hwnd);
        VerifySetParent(tab.hwnd, nullptr, "DoRelease/fx-detach");

        if (tab.originalRect.right > tab.originalRect.left &&
            tab.originalRect.bottom > tab.originalRect.top) {
          int w = tab.originalRect.right - tab.originalRect.left;
          int h = tab.originalRect.bottom - tab.originalRect.top;
          SetWindowPos(tab.hwnd, nullptr,
                       tab.originalRect.left, tab.originalRect.top, w, h,
                       SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        // FX windows are REAPER's own — they have their own chrome managed
        // by REAPER when re-shown. Skip ApplyFloatingWindowChrome to avoid
        // double-decoration.
      }
      goto fx_done;
    }

    if (toggleOff && tab.toggleAction > 0 && g_Main_OnCommand) {
      int preState = g_GetToggleCommandState
                       ? g_GetToggleCommandState(tab.toggleAction) : -1;

      // ===== B27 DEBUG INSTRUMENTATION =====
      // Capture full state at entry to diagnose why action toggle doesn't
      // update REAPER's tracker on close.
      RECT preReleaseRect = {};
      GetWindowRect(tab.hwnd, &preReleaseRect);
      DBG("[B27] === DoRelease ENTER ===\n");
      DBG("[B27]   name='%s' isArbitrary=%d hwnd=%p action=%d\n",
          tab.name, tab.isArbitrary, tab.hwnd, tab.toggleAction);
      DBG("[B27]   actionCmd='%s' searchTitle='%s'\n",
          tab.actionCmd[0] ? tab.actionCmd : "(empty)",
          tab.searchTitle[0] ? tab.searchTitle : "(empty)");
      DBG("[B27]   preState=%d preReleaseRect=(%ld,%ld,%ld,%ld) IsWindowVisible=%d\n",
          preState,
          (long)preReleaseRect.left, (long)preReleaseRect.top,
          (long)preReleaseRect.right, (long)preReleaseRect.bottom,
          IsWindowVisible(tab.hwnd) ? 1 : 0);
      DBG("[B27]   originalParent=%p current=GetParent=%p reaperMain=%p\n",
          tab.originalParent, GetParent(tab.hwnd), g_reaperMainHwnd);
      DBG("[B27]   originalRect=(%ld,%ld,%ld,%ld)\n",
          (long)tab.originalRect.left, (long)tab.originalRect.top,
          (long)tab.originalRect.right, (long)tab.originalRect.bottom);

      // Fire toggle while WS_CHILD.
      if (preState != 0) {
        DBG("[B27] >>> calling g_Main_OnCommand(%d, 0) pre-detach\n", tab.toggleAction);
        g_Main_OnCommand(tab.toggleAction, 0);
        int postState = g_GetToggleCommandState
                          ? g_GetToggleCommandState(tab.toggleAction) : -1;
        DBG("[B27] <<< action done: postState=%d (was %d), IsWindowVisible=%d\n",
            postState, preState, IsWindowVisible(tab.hwnd) ? 1 : 0);
        if (postState == 1 && preState == 1) {
          DBG("[B27] >>> action NO-OP, sending WM_CLOSE\n");
          SendMessage(tab.hwnd, WM_CLOSE, 0, 0);
#ifdef MAXPANE_DEBUG
          int post2 = g_GetToggleCommandState
                        ? g_GetToggleCommandState(tab.toggleAction) : -1;
          DBG("[B27] <<< WM_CLOSE done: postState=%d, IsWindowVisible=%d\n",
              post2, IsWindowVisible(tab.hwnd) ? 1 : 0);
#endif
        }
      } else {
        DBG("[B27] preState==0 — skipping toggle/close\n");
      }

      // Detach. Entry 11 — DetachToTopLevel flips WS_CHILD → WS_POPUP after
      // SetParent on Win32; SWELL macOS/Linux keeps plain SetParent.
      DBG("[B27] >>> DetachToTopLevel (post-toggle)\n");
      DetachToTopLevel(tab.hwnd);
      VerifySetParent(tab.hwnd, nullptr, "DoRelease/post-toggle");
      DBG("[B27] <<< SetParent done: GetParent=%p IsWindowVisible=%d\n",
          GetParent(tab.hwnd), IsWindowVisible(tab.hwnd) ? 1 : 0);

      // Restore position.
      if (tab.originalRect.right > tab.originalRect.left &&
          tab.originalRect.bottom > tab.originalRect.top) {
        int w = tab.originalRect.right - tab.originalRect.left;
        int h = tab.originalRect.bottom - tab.originalRect.top;
        DBG("[B27] >>> SetWindowPos to originalRect %dx%d at (%ld,%ld)\n",
            w, h, (long)tab.originalRect.left, (long)tab.originalRect.top);
        SetWindowPos(tab.hwnd, nullptr,
                     tab.originalRect.left, tab.originalRect.top, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        RECT after = {};
        GetWindowRect(tab.hwnd, &after);
        DBG("[B27] <<< rect after SetWindowPos: (%ld,%ld,%ld,%ld)\n",
            (long)after.left, (long)after.top, (long)after.right, (long)after.bottom);
      }

      // Apply chrome.
      if (tab.isArbitrary && GetToolbarToggleAction(tab.name) <= 0) {
        DBG("[B27] >>> ApplyFloatingWindowChrome('%s')\n", tab.name);
        ApplyFloatingWindowChrome(tab.hwnd, tab.name);
        DBG("[B27] <<< chrome applied\n");
      } else {
        DBG("[B27] chrome skipped (isArbitrary=%d toolbar=%d)\n",
            tab.isArbitrary, GetToolbarToggleAction(tab.name));
      }

#ifdef MAXPANE_DEBUG
      int finalState = g_GetToggleCommandState
                        ? g_GetToggleCommandState(tab.toggleAction) : -1;
      DBG("[B27] === DoRelease FINAL: toggle state=%d visible=%d ===\n",
          finalState, IsWindowVisible(tab.hwnd) ? 1 : 0);
#endif
    } else {
      // No toggle action known — custom plugins / ReaImGui scripts captured
      // via click/drag/Open Windows where LookupToggleAction couldn't
      // resolve. Best-effort close: send WM_CLOSE so the window's own
      // close handler runs. ReaImGui's NSWindow delegate typically
      // responds with script teardown, which updates REAPER's tracker
      // (toolbar buttons, menu checkmarks) via the script's own logic.
      // For windows that don't respond — at least we tried.
      RECT preClose = {};
      GetWindowRect(tab.hwnd, &preClose);
      DBG("[B27] no-action ELSE branch: name='%s' toggleOff=%d actionCmd='%s'\n",
          tab.name, toggleOff,
          tab.actionCmd[0] ? tab.actionCmd : "(empty)");
      DBG("[B27]   preClose rect=(%ld,%ld,%ld,%ld) visible=%d\n",
          (long)preClose.left, (long)preClose.top,
          (long)preClose.right, (long)preClose.bottom,
          IsWindowVisible(tab.hwnd) ? 1 : 0);

      // B27 v7 — chrome-restore approach. Sequence:
      // 1. WM_CLOSE while WS_CHILD: script's NSWindow delegate fires,
      //    script hides its current view-host NSWindow.
      // 2. SetParent(nullptr): SWELL creates a NEW NSWindow with the
      //    NSView as its contentView. THIS is the key — only contentView
      //    NSWindows can have chrome applied.
      // 3. ApplyFloatingWindowChrome: titled + closable + resizable mask
      //    on the new orphan NSWindow.
      // 4. Restore originalRect on the orphan.
      // 5. Below: SW_HIDE + ForceHide — orphan is hidden.
      //
      // When the user re-fires the script action, the script logic finds
      // the existing NSView (alive) and shows its current NSWindow
      // (which is OUR chromed orphan) → window appears with proper frame.
      // Sprint 1 follow-up — ReaImGui crash fix. Scripts whose toggle
      // state is -1 (fire-and-show, no on/off tracking) track their
      // own windows internally via ImGui Docker. Reparenting the window
      // externally (DetachToTopLevel below) leaves ImGui's Docker
      // pointer stale; the next heartbeat dereferences a freed field
      // and crashes inside Docker::moveTo. Fire the script's own
      // action so it tears down its window cleanly — this MUST happen
      // even on workspace switch (toggleOff=false), otherwise the
      // workspace-switch path is the canonical crash trigger.
      bool isReaImGuiScript = false;
      if (tab.toggleAction > 0 && g_GetToggleCommandState &&
          g_GetToggleCommandState(tab.toggleAction) == -1) {
        isReaImGuiScript = true;
      }

      if (isReaImGuiScript && g_Main_OnCommand) {
        DBG("[B27] >>> Main_OnCommand(%d) for live ReaImGui script (pre-detach, toggleOff=%d)\n",
            tab.toggleAction, toggleOff);
        g_Main_OnCommand(tab.toggleAction, 0);
        DBG("[B27] <<< script closed: alive=%d visible=%d\n",
            IsWindow(tab.hwnd) ? 1 : 0,
            (IsWindow(tab.hwnd) && IsWindowVisible(tab.hwnd)) ? 1 : 0);
      } else if (toggleOff) {
        DBG("[B27] >>> SendMessage(WM_CLOSE) on WS_CHILD (no-action path)\n");
        SendMessage(tab.hwnd, WM_CLOSE, 0, 0);
        DBG("[B27] <<< WM_CLOSE done: alive=%d visible=%d\n",
            IsWindow(tab.hwnd) ? 1 : 0,
            (IsWindow(tab.hwnd) && IsWindowVisible(tab.hwnd)) ? 1 : 0);
      }

      if (IsWindow(tab.hwnd)) {
        // Entry 11 — DetachToTopLevel on Win32 (WS_CHILD→WS_POPUP after
        // SetParent); SWELL keeps plain SetParent (B27 path — Cocoa
        // recreates the NSWindow when SetParent(nullptr) is called, so
        // the NSView ends up as the contentView of a fresh orphan window).
        DBG("[B27] >>> DetachToTopLevel (no-action path)\n");
        DetachToTopLevel(tab.hwnd);
        VerifySetParent(tab.hwnd, nullptr, "DoRelease/no-action-detach");
        DBG("[B27] <<< detached, GetParent=%p\n", GetParent(tab.hwnd));

        // Chrome NOW applies (view IS contentView of the new orphan).
        if (tab.isArbitrary && GetToolbarToggleAction(tab.name) <= 0) {
          DBG("[B27] >>> ApplyFloatingWindowChrome\n");
          ApplyFloatingWindowChrome(tab.hwnd, tab.name);
          DBG("[B27] <<< chrome applied to orphan\n");
        }

        // Restore originalRect so the chromed orphan has sensible geometry.
        if (tab.originalRect.right > tab.originalRect.left &&
            tab.originalRect.bottom > tab.originalRect.top) {
          int w = tab.originalRect.right - tab.originalRect.left;
          int h = tab.originalRect.bottom - tab.originalRect.top;
          DBG("[B27] >>> SetWindowPos to (%ld,%ld) %dx%d\n",
              (long)tab.originalRect.left, (long)tab.originalRect.top, w, h);
          SetWindowPos(tab.hwnd, nullptr,
                       tab.originalRect.left, tab.originalRect.top, w, h,
                       SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
      }
    }
fx_done:
    ShowWindow(tab.hwnd, SW_HIDE);
    // B14: SWELL's SW_HIDE on a top-level NSWindow that "lives" in REAPER's
    // HWND tree but visually occupies its own NSWindow (Media Explorer, FX
    // Browser, Undo History — REAPER actions don't reliably toggle them off
    // either) does not actually orderOut: the NSWindow. Bypass SWELL: call
    // Cocoa orderOut: directly so the user-visible window vanishes regardless
    // of REAPER's wnd_vis tracking.
    ForceHideWindow(tab.hwnd);
    DBG("[MaxPane] DoRelease: ForceHide applied to '%s' hwnd=%p, visible=%d\n",
        tab.name, (void*)tab.hwnd,
        (IsWindow(tab.hwnd) && IsWindowVisible(tab.hwnd)) ? 1 : 0);
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

void WindowManager::SetTabPinned(int paneId, int tabIndex, bool pinned)
{
  if (paneId < 0 || paneId >= MAX_PANES) return;
  PaneState& ps = m_panes[paneId];
  if (tabIndex < 0 || tabIndex >= ps.tabCount) return;
  if (ps.tabs[tabIndex].pinned == pinned) return;  // no-op

  // Capture identity of tabs before reordering so we can track activeTab.
  HWND activeHwnd = (ps.activeTab >= 0 && ps.activeTab < ps.tabCount)
    ? ps.tabs[ps.activeTab].hwnd : nullptr;

  ps.tabs[tabIndex].pinned = pinned;

  // Stable partition: pinned tabs first (in their original order), unpinned
  // after (in their original order). Copy out → write back in two passes.
  TabEntry sorted[MAX_TABS_PER_PANE];
  int outCount = 0;
  for (int pass = 0; pass < 2; pass++) {
    const bool wantPinned = (pass == 0);
    for (int t = 0; t < ps.tabCount; t++) {
      if (ps.tabs[t].pinned == wantPinned) {
        sorted[outCount++] = ps.tabs[t];
      }
    }
  }
  for (int t = 0; t < outCount; t++) ps.tabs[t] = sorted[t];

  // Restore activeTab to follow the same HWND through the reorder.
  if (activeHwnd) {
    for (int t = 0; t < ps.tabCount; t++) {
      if (ps.tabs[t].hwnd == activeHwnd) {
        ps.activeTab = t;
        break;
      }
    }
  }
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
        // Sprint 1 Entry 19 — Direct2D-rendered children (ReaImGui, JUCE,
        // Reabeat) leave the previous client-area bits on screen after
        // SetWindowPos because the default copy-bits behaviour copies old
        // pixels to the new position and the plugin's swap chain doesn't
        // repaint synchronously — old tab-bar text stacks during interactive
        // resize. SWP_NOCOPYBITS discards stale bits and lets the plugin
        // present a fresh frame. KNOWN_WINDOWS native captures (GDI-based)
        // stay on the default copy-bits path — flicker-free for them.
        UINT swpFlags = SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED;
        if (tab.isArbitrary) swpFlags |= SWP_NOCOPYBITS;
        SetWindowPos(tab.hwnd, HWND_TOP, x, y, w, h, swpFlags);
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
        bool dead = (!tab.hwnd || !IsWindow(tab.hwnd));
        if (!dead) {
          // B2: detect external reparent. IsWindow is still true, but REAPER
          // (e.g. on project-tab switch) may have pulled the window back into
          // its own docker. Our state has desynced from reality.
          HWND p = GetParent(tab.hwnd);
          if (p != m_containerHwnd) {
            DBG("[MaxPane] CheckAlive: external reparent for '%s' hwnd=%p actual=%p expected=%p\n",
                tab.name, (void*)tab.hwnd, (void*)p, (void*)m_containerHwnd);
            // Try to reclaim. DoCapture (with B1 verification) returns false
            // if SetParent doesn't stick — then we fall through to release.
            HWND savedOriginal = tab.originalParent;
            if (DoCapture(tab, tab.hwnd, m_containerHwnd)) {
              tab.originalParent = savedOriginal;  // keep the truly-original parent
              if (t == ps.activeTab) ShowWindow(tab.hwnd, SW_SHOWNA);
              else                   ShowWindow(tab.hwnd, SW_HIDE);
              DBG("[MaxPane] CheckAlive: reclaimed '%s' into container\n", tab.name);
              changed = true;
            } else {
              DBG("[MaxPane] CheckAlive: reclaim failed for '%s', releasing tab\n", tab.name);
              // B6 follow-up: the HWND is alive but no longer ours. Strip
              // our toolbar subclass before abandoning it, or it lingers on
              // an HWND we don't track anymore.
              if (tab.hwnd && IsWindow(tab.hwnd)) UnsubclassToolbar(tab.hwnd);
              dead = true;
            }
          }
        }
        if (dead) {
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
          // B3: re-verify capturability between find and capture. REAPER may
          // have destroyed the window (project switch) or moved it into a
          // docker we shouldn't grab — DoCapture would silently fail otherwise.
          bool capturable = IsWindow(h);
          HWND hp = capturable ? GetParent(h) : nullptr;
          if (capturable && hp && hp != g_reaperMainHwnd) capturable = false;
          if (!capturable) {
            DBG("[MaxPane] CheckAlive: B3 race — '%s' hwnd=%p not capturable (parent=%p alive=%d), retry next tick\n",
                tab.searchTitle, (void*)h, (void*)hp, IsWindow(h) ? 1 : 0);
          } else if (DoCapture(tab, h, m_containerHwnd)) {
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
