#include "window_manager.h"
#include "swell_cocoa_helpers.h"
#include "fx_capture.h"
#include "globals.h"
#include "instance_manager.h"   // U4 (ADR-065) — cross-instance capture arbitration
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

// U7-B (ADR-069, poydepzaj1616 #47) — the toolbar currently inside DoRelease.
// Childless (MIDI) toolbars forward in-bounds clicks (ADR-057), so the
// release gesture's WM_LBUTTONUP could still reach REAPER's toolbar proc
// mid-transition and "randomly activate" a button. While a toolbar is being
// released, its subclass eats every left-click unconditionally.
static HWND s_releasingToolbarHwnd = nullptr;

static LRESULT CALLBACK ToolbarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC origProc = (WNDPROC)(INT_PTR)GetProp(hwnd, kOrigWndProcProp);
  if (!origProc) return DefWindowProc(hwnd, msg, wParam, lParam);

  if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK) {
    if (hwnd == s_releasingToolbarHwnd) {
      DBG("[MaxPane] ToolbarSubclass: ate click during release (U7-B)\n");
      return 0;
    }
    int x = (short)LOWORD(lParam);
    int y = (short)HIWORD(lParam);
#ifdef MAXPANE_DEBUG
    const char* msgName = msg == WM_LBUTTONDOWN ? "WM_LBUTTONDOWN"
                        : msg == WM_LBUTTONUP   ? "WM_LBUTTONUP"
                                                : "WM_LBUTTONDBLCLK";
#endif
    RECT cr;
    GetClientRect(hwnd, &cr);
    if (x < 0 || y < 0 || x >= cr.right || y >= cr.bottom) {
      // Stuck-capture guard (v2.2, MIDI-toolbar freeze) — after capturing a
      // toolbar straight out of REAPER's docker, SWELL mouse capture can get
      // stuck on it: EVERY click in the app then arrives here with far
      // out-of-bounds coords (smoke log: (-2005,-304)) and the unconditional
      // background-eat below froze all interaction until a popup menu reset
      // the capture. Drop the capture and eat the stray — forwarding it
      // could fire a button via REAPER's coord lookup (F-D class).
      ReleaseCapture();
      DBG("[MaxPane] ToolbarSubclass: out-of-bounds %s at (%d,%d) — ReleaseCapture + eat\n",
          msgName, x, y);
      return 0;
    }
    if (!GetWindow(hwnd, GW_CHILD)) {
      // Childless toolbar (MIDI toolbars) — its buttons are painted by the
      // toolbar proc itself, not child windows, so the background-eat below
      // would swallow every click and leave the toolbar dead in the pane.
      // Forward in-bounds clicks; drag-to-undock protection only applies to
      // toolbars whose buttons are real children (main Toolbar 1-32).
    }
    else if (!PointHitsChild(hwnd, x, y)) {
      // Click on toolbar background — eat the whole down/up/dblclk on background.
      // DOWN blocks REAPER's drag-to-undock; UP/DBLCLK (F-D, forum v2.0.6) stop
      // a stray release event from reaching REAPER's toolbar proc and firing a
      // button. Eating only DOWN left the matching UP to slip through during
      // DoRelease's toggle/reparent and "randomly activate" a toolbar action.
      DBG("[MaxPane] ToolbarSubclass: ate %s on background at (%d,%d)\n",
          msgName, x, y);
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

#ifdef _WIN32
// U10 (ADR-067, X-Raym #67) — a captured ReaImGui window still runs ImGui's
// own window-move logic: a left-click-drag on its background or embedded
// titlebar issues Platform_SetWindowPos with SCREEN coords, SWELL applies
// them PARENT-relative, and the child visibly slides out of its pane until
// the 500ms CheckAlive snap-back (ADR-061 #3b) yanks it home. Instead of
// fighting the move after the fact, veto it at the source: subclass the
// captured child and force SWP_NOMOVE onto any reposition MaxPane did not
// initiate. ImGui's next Platform_GetWindowPos read-back (ClientToScreen)
// then still matches reality, so input mapping stays correct — the drag
// simply does nothing, which is the docked-window behavior users expect.
// Win32-only: SWELL-generic/mac do not route WM_WINDOWPOSCHANGING through
// child wndprocs; Linux keeps the snap-back fallback, macOS is ADR-063
// known-limitation territory.
static const char* const kImGuiGuardProp = "MaxPane_ImGuiGuardOrig";
static bool s_selfChildMove = false;

static LRESULT CALLBACK ImGuiMoveGuardProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC origProc = (WNDPROC)(INT_PTR)GetProp(hwnd, kImGuiGuardProp);
  if (!origProc) return DefWindowProc(hwnd, msg, wParam, lParam);
  if (msg == WM_WINDOWPOSCHANGING && !s_selfChildMove) {
    WINDOWPOS* wp = (WINDOWPOS*)lParam;
    if (wp && !(wp->flags & SWP_NOMOVE)) {
      DBG("[MaxPane] ImGuiMoveGuard: vetoed external move of %p to (%d,%d)\n",
          (void*)hwnd, wp->x, wp->y);
      wp->flags |= SWP_NOMOVE;
    }
  }
  return CallWindowProc(origProc, hwnd, msg, wParam, lParam);
}

static void InstallImGuiMoveGuard(HWND hwnd)
{
  if (!hwnd || GetProp(hwnd, kImGuiGuardProp)) return;
  WNDPROC orig = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
  if (orig && orig != ImGuiMoveGuardProc) {
    SetProp(hwnd, kImGuiGuardProp, (HANDLE)(INT_PTR)orig);
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)ImGuiMoveGuardProc);
    DBG("[MaxPane] InstallImGuiMoveGuard: hwnd=%p orig=%p\n", (void*)hwnd, (void*)orig);
  }
}

static void RemoveImGuiMoveGuard(HWND hwnd)
{
  if (!hwnd) return;
  WNDPROC orig = (WNDPROC)(INT_PTR)GetProp(hwnd, kImGuiGuardProp);
  if (orig) {
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)orig);
    RemoveProp(hwnd, kImGuiGuardProp);
    DBG("[MaxPane] RemoveImGuiMoveGuard: hwnd=%p restored=%p\n", (void*)hwnd, (void*)orig);
  }
}
// RAII marker for MaxPane-initiated child repositions (RepositionAll, the
// CheckAlive snap-back) so the move guard lets them through.
struct SelfChildMoveScope {
  SelfChildMoveScope() { s_selfChildMove = true; }
  ~SelfChildMoveScope() { s_selfChildMove = false; }
};
#else
static inline void InstallImGuiMoveGuard(HWND) {}
static inline void RemoveImGuiMoveGuard(HWND) {}
struct SelfChildMoveScope {
  SelfChildMoveScope() {}
  ~SelfChildMoveScope() {}
};
#endif

// Known dynamic-title window prefixes.
// Windows whose titles start with one of these change their title at runtime
// (e.g. MIDI Editor title changes per MIDI item / project tab).
const char* GetDynamicTitlePrefix(const char* title)
{
  if (!title) return nullptr;
  if (strncmp(title, "MIDI take:", 10) == 0) return "MIDI take:";
  return nullptr;
}

// GetToolbarToggleAction / LookupToggleAction / GetSearchTitleForAction moved
// to config.cpp (v2.1.2, ADR-052) — pure title↔action-ID logic, no SWELL, so
// the unit-test target can cover the toolbar action table.

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
#ifdef _WIN32
  // U1 — EnumWindows visits every top-level window on the desktop, and the
  // titleless-window module fallback below then runs OpenProcess-class
  // syscalls against OTHER apps' windows (a large share of one search's
  // 10-20ms). A window outside the REAPER process can never be a capture
  // target (SetParent is same-process), and title-matching foreign apps was
  // a latent mis-capture ("Toolbar 3 - Chrome" substring-matched "Toolbar 3").
  DWORD winPid = 0;
  GetWindowThreadProcessId(hwnd, &winPid);
  if (winPid != GetCurrentProcessId()) return TRUE;
#endif
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

  // Strip DOCKED_TITLE_SUFFIX suffix for matching (ReaImGui scripts use this)
  char matchBuf[512];
  safe_strncpy(matchBuf, buf, sizeof(matchBuf));
  char* dockedSuffix = strstr(matchBuf, DOCKED_TITLE_SUFFIX);
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
    snprintf(dockedTitle, sizeof(dockedTitle), "%s%s", title, DOCKED_TITLE_SUFFIX);
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
      snprintf(dockedTitle, sizeof(dockedTitle), "%s%s", title, DOCKED_TITLE_SUFFIX);
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
      snprintf(dockedTitle, sizeof(dockedTitle), "%s%s", title, DOCKED_TITLE_SUFFIX);
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

// U4 (ADR-065) — cross-instance capture arbitration. Each container owns its
// own WindowManager and there was NO registry of who holds which HWND: two
// instances whose saved state referenced the same window both captured it,
// and each CheckAlive tick "reclaimed" it from the other — a 500ms parent
// ping-pong the user saw as permanent flicker (#61). Query live instances at
// decision time instead of keeping a registry that release paths could
// desync (the release/close path is a known regression magnet).
static bool IsCapturedByOtherInstance(HWND hwnd, const WindowManager* self)
{
  if (!hwnd) return false;
  bool owned = false;
  InstanceManager::Get().ForEach([&](int /*id*/, MaxPaneContainer& c) {
    if (owned) return;
    const WindowManager& wm = c.GetWinMgr();
    if (&wm == self) return;
    if (wm.IsWindowCaptured(hwnd)) owned = true;
  });
  return owned;
}

bool WindowManager::IsCapturedByAnotherInstance(HWND hwnd, const WindowManager* self)
{
  return IsCapturedByOtherInstance(hwnd, self);
}

// Diagnostic: dump all visible window titles (call when debugging search failures)
#ifdef MAXPANE_DEBUG
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
#endif

void WindowManager::DumpAllWindowTitles(const char* context)
{
#ifndef MAXPANE_DEBUG
  // Audit M3.3 — the enumeration (EnumWindows + EnumChildWindows +
  // GetWindowText per window) ran in Release builds too, feeding DBG calls
  // that compile to nothing. Pure waste on every capture-retry diagnostic.
  (void)context;
  return;
#else
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
#endif
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
  // U4 (ADR-065) — never steal a window another live instance holds.
  if (IsCapturedByOtherInstance(hwnd, this)) {
    DBG("[MaxPane] CaptureByIndex: '%s' hwnd=%p already captured by another instance — rejecting\n",
        def.name, (void*)hwnd);
    return false;
  }

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

// Defined below (after ResolveWindowDisplayName); fwd-declared so the capture
// path can flag ReaImGui hosts for the Bug I size-guard scope (v2.1.1).
static bool IsReaImGuiHostWindow(HWND hwnd);
// ADR-061 amendment (v2.2.1) — capture-refusal reason channel. The capture
// gates live deep in WindowManager but toasts belong to the container; the
// refusal site leaves a message here and the failing UI path (click / menu /
// drag) consumes it for the toast instead of the generic "couldn't capture".
static char s_captureRefusal[200] = "";
void WindowManager::SetCaptureRefusal(const char* msg)
{
  safe_strncpy(s_captureRefusal, msg ? msg : "", sizeof(s_captureRefusal));
}
const char* WindowManager::TakeCaptureRefusal()
{
  static char out[200];
  if (!s_captureRefusal[0]) return nullptr;
  safe_strncpy(out, s_captureRefusal, sizeof(out));
  s_captureRefusal[0] = '\0';
  return out;
}

#if !defined(_WIN32) && !defined(__APPLE__)
// ADR-061 — narrow GL-only ReaImGui probe; defined next to
// IsReaImGuiHostWindow, used by the Linux capture blocks below.
static bool IsImGuiGlContextWindow(HWND hwnd);
// ADR-061 amendment (v2.2.1, VM-verified live 2026-06-10): a GL ReaImGui
// window survives capture when ReaImGui renders via its offscreen software
// path — "Disable hardware acceleration" in Preferences > Plug-ins >
// ReaImGui ([reaimgui] forcecpu_gdk=1 in reaper.ini). The GL context then
// lives on a hidden offscreen GdkWindow and the script blits through
// WM_PAINT, so SWELL-generic SetParent destroying the child's OS window no
// longer pulls the surface out from under it. Read live on every gate pass:
// GetPrivateProfileInt is cheap and the user can flip the pref mid-session.
// Residual risk (documented): a script STARTED before the pref was enabled
// keeps its hardware-GL renderer for its lifetime (ReaImGui caches the flag
// per context) — hence the "restart the script" wording in the toast.
static const char* const kGlCaptureRefusalMsg =
  "To capture ReaImGui windows on Linux, enable 'Disable hardware "
  "acceleration' in Preferences > Plug-ins > ReaImGui, then restart the script.";
static bool IsReaImGuiSoftwareRenderingOn()
{
  if (!g_get_ini_file) return false;
  return GetPrivateProfileInt("reaimgui", "forcecpu_gdk", 0, g_get_ini_file()) == 1;
}
#endif

bool WindowManager::CaptureArbitraryWindow(int paneId, HWND targetHwnd, const char* displayName, HWND containerHwnd, int toggleAction, const char* actionCmd, bool transient)
{
  DBG("[MaxPane] CaptureArbitraryWindow: pane=%d name='%s' hwnd=%p action=%d cmd='%s'\n",
      paneId, displayName ? displayName : "(null)", (void*)targetHwnd, toggleAction,
      actionCmd ? actionCmd : "(null)");
  // ADR-061 amendment — drop any stale refusal (e.g. left by a hover pass
  // over a non-capturable window) so failure toasts never show a reason
  // that belongs to a different window.
  SetCaptureRefusal(nullptr);
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
  // U4 (ADR-065) — never steal a window another live instance holds.
  if (IsCapturedByOtherInstance(targetHwnd, this)) {
    DBG("[MaxPane] CaptureArbitraryWindow: REJECTED hwnd=%p captured by another instance\n",
        (void*)targetHwnd);
    return false;
  }

  TabEntry& tab = ps.tabs[ps.tabCount];
  memset(&tab, 0, sizeof(TabEntry));

  safe_strncpy(tab.name, displayName, sizeof(tab.name));
  safe_strncpy(tab.searchTitle, displayName, sizeof(tab.searchTitle));
  tab.transient = transient;  // U14 (ADR-070) — never persisted
  // Audit M3.4 — tab.name round-trips through "KEY VALUE" lines in the RPP
  // <MAXPANE_STATE> chunk; a script window titled with an embedded newline
  // would split the chunk line and corrupt the saved project block. Flatten
  // control characters to spaces at the single point where titles enter.
  for (char* c = tab.name; *c; c++)        { if (*c == '\n' || *c == '\r') *c = ' '; }
  for (char* c = tab.searchTitle; *c; c++) { if (*c == '\n' || *c == '\r') *c = ' '; }
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
  // ADR-061 follow-up (v2.2.1, live Linux repro): NEVER discover from a
  // synthetic '#<class> (untitled)' fallback name — it is a window CLASS,
  // not a title, and every untitled ReaImGui script shares the same one.
  // Token scoring then matches whichever ReaImGui action happens to score
  // ('imgui' matched "Custom: ReaImGui_Demo.lua"), so closing TK Patchbay
  // launched the demo. action=0 + empty cmd → close path just hides — safe.
  // ADR-062 follow-up (live Win32 repro): the Win32 empty-title fallback
  // derives the display name from the plugin MODULE instead — and for any
  // untitled ReaImGui script that module is the shared renderer
  // (reaper_imgui-x64.dll → 'Imgui-x64'), so token 'imgui' matched the demo
  // again, through a name that doesn't start with '#'. Same rule, other
  // door: a ReaImGui-class window with no real window title has nothing
  // discovery could legitimately match — skip. Non-ReaImGui empty-title
  // plugins (ReaBeat, Reamix) keep module discovery: their module IS the
  // plugin, not a shared renderer.
  char realTitle[128] = "";
  if (targetHwnd) GetWindowText(targetHwnd, realTitle, sizeof(realTitle));
  if (tab.toggleAction <= 0 && !tab.actionCmd[0] && displayName[0] != '#' &&
      !(IsReaImGuiHostWindow(targetHwnd) && !realTitle[0])) {
    tab.toggleAction = DiscoverActionForWindow(targetHwnd, displayName);
  }
  // ADR-052 — never capture the Main toolbar (main-window top chrome).
  // Checked by resolved action, not just title: a switched main toolbar can
  // carry the displayed toolbar's name (seen live as 'Toolbar 32'), and
  // DiscoverActionForWindow can attribute 41651 to it either way. Nothing is
  // committed yet (tab slot is past tabCount), so bailing here is clean.
  if (tab.toggleAction == MAIN_TOOLBAR_ACTION ||
      strcmp(displayName, "Main toolbar") == 0) {
    DBG("[MaxPane] CaptureArbitraryWindow: REJECTED Main toolbar (action=%d name='%s') — ADR-052\n",
        tab.toggleAction, displayName);
    return false;
  }
#if !defined(_WIN32) && !defined(__APPLE__)
  // ADR-061 — a GL-backed ReaImGui window cannot survive capture on Linux:
  // SWELL-generic SetParent destroys the real X11/GDK window under the
  // script's GL context, and the script's next frame kills the whole REAPER
  // (TK Patchbay, live VM 2026-06-10 — DoCapture logged DONE, death came on
  // the heartbeat). Reject before anything is committed; this is the single
  // choke point for menu / favorites / workspace-restore captures too.
  // Lua_LICE_gfx windows paint via LICE into the window DC and survive
  // (MIDI Lyrics captured + released fine) — only the GL class is blocked.
  if (IsImGuiGlContextWindow(targetHwnd) && !IsReaImGuiSoftwareRenderingOn()) {
    DBG("[MaxPane] CaptureArbitraryWindow: REJECTED ReaImGui-GL window '%s' on Linux (ADR-061, forcecpu off)\n",
        displayName);
    SetCaptureRefusal(kGlCaptureRefusalMsg);
    return false;
  }
#endif
  tab.isArbitrary = true;
  if (actionCmd && actionCmd[0] && !tab.actionCmd[0]) {
    safe_strncpy(tab.actionCmd, actionCmd, sizeof(tab.actionCmd));
  }
  // Auto-fill actionCmd so the workspace save round-trips through
  // ResolveActionCommand on load. Skip if caller already provided one.
  if (tab.toggleAction > 0 && !tab.actionCmd[0]) {
    GetActionCommandString(tab.toggleAction, tab.actionCmd, sizeof(tab.actionCmd));
  }

  // Bug I scope (ADR-045 / v2.1.1) — only a ReaImGui / Lua-gfx host gets the
  // dock min-clamp + pane floor-hide; toolbars / FX / native captures are
  // legitimately small and must stay visible. Class match, plus the toggle-state
  // == -1 ReaImGui signal as a fallback for action-less click captures.
  tab.isReaImGui = IsReaImGuiHostWindow(targetHwnd) ||
                   (tab.toggleAction > 0 && g_GetToggleCommandState &&
                    g_GetToggleCommandState(tab.toggleAction) == -1);
  DBG("[MaxPane] CaptureArbitraryWindow: '%s' isReaImGui=%d\n", tab.name, tab.isReaImGui);
  // ADR-061 item #2 — the learned-min fields are runtime-only; reset them so
  // a reused tab slot can't inherit a previous window's floor.
  tab.arbMinW = tab.arbMinH = 0;
  tab.lastSetW = tab.lastSetH = 0;
  tab.pendMinW = tab.pendMinH = 0;
  tab.lastSetX = tab.lastSetY = 0;

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
//
// F-E (forum v2.0.6) — EnumProcessModules followed by a GetModuleInformation
// call per DLL is O(loaded modules); with 100-200 VST DLLs that costs 10-100ms.
// It used to run for EVERY titleless window EnumWindows visited, for EVERY
// FindReaperWindow call, so restoring a project full of captured plugins stalled
// REAPER's main thread for 10-15s on Windows. Cache the module address-range
// table and reuse it across calls; the costly GetModuleInformation rebuild fires
// only when the loaded-module count actually changes (a plugin DLL loaded since
// the last build), detected via a cheap EnumProcessModules count probe on miss.
// Main-thread only (FindReaperWindow), so no locking.
struct ModRange { BYTE* base; BYTE* end; HMODULE mod; };
static ModRange g_modCache[512];
static int g_modCacheCount = -1;        // -1 = never built; else cached ranges
static int g_modKnownModuleCount = -1;  // raw module count at last build

static void BuildModuleCache()
{
  g_modCacheCount = 0;
  HMODULE mods[512];
  DWORD needed = 0;
  HANDLE proc = GetCurrentProcess();
  if (!EnumProcessModules(proc, mods, sizeof(mods), &needed)) {
    g_modKnownModuleCount = 0;
    return;
  }
  const DWORD count = needed / sizeof(HMODULE);
  g_modKnownModuleCount = (int)count;
  for (DWORD i = 0; i < count && i < 512; i++) {
    MODULEINFO mi = {};
    if (!GetModuleInformation(proc, mods[i], &mi, sizeof(mi))) continue;
    BYTE* base = (BYTE*)mi.lpBaseOfDll;
    g_modCache[g_modCacheCount].base = base;
    g_modCache[g_modCacheCount].end  = base + mi.SizeOfImage;
    g_modCache[g_modCacheCount].mod  = mods[i];
    g_modCacheCount++;
  }
}

static HMODULE LookupModuleCache(void* addr)
{
  for (int i = 0; i < g_modCacheCount; i++) {
    if ((BYTE*)addr >= g_modCache[i].base && (BYTE*)addr < g_modCache[i].end)
      return g_modCache[i].mod;
  }
  return nullptr;
}

static HMODULE FindModuleByAddress(void* addr)
{
  if (!addr) return nullptr;
  if (g_modCacheCount < 0) BuildModuleCache();
  HMODULE m = LookupModuleCache(addr);
  if (m) return m;
  // Miss. Cheap probe: did the module set grow since the cache was built? Only a
  // newly-loaded DLL warrants the costly GetModuleInformation rebuild; a miss
  // against an unchanged set is a genuine non-module address — skip it without
  // paying the rebuild. This is what bounds the cost during a bulk restore.
  HMODULE mods[512];
  DWORD needed = 0;
  if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
    int now = (int)(needed / sizeof(HMODULE));
    if (now != g_modKnownModuleCount) {
      BuildModuleCache();
      m = LookupModuleCache(addr);
    }
  }
  return m;
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

  // Linux-VM live debug 2026-06-10 — the bare '#untitled' fallback hid that
  // the rejected windows were core main-window views (REAPERTrackListWindow,
  // REAPERTimeDisplay). GetClassName works on both SWELLs (the ADR-044 probe
  // used it on macOS), so use the Win32 class-derived name everywhere.
  char clsName[64] = {};
  GetClassName(hwnd, clsName, sizeof(clsName));
  snprintf(buf, (size_t)bufSize, "#%s (untitled)", clsName[0] ? clsName : "untitled");
}

// Bug I (ADR-045 / v2.1.1) — is this captured window a ReaImGui / Lua-gfx host?
// Those are the ONLY captures that cascade-resize REAPER's own main window when
// sized below their content min, so only they get the dock min-clamp + pane
// floor-hide. Toolbars, FX UIs and native GDI windows are legitimately small and
// must never be floor-hidden. Match by window class on the host and its direct
// children (the ImGui render surface is a direct child). Cross-platform: the
// class strings are stable and GetClassNameA works on SWELL (the ADR-044 probe
// read them on macOS).
static bool IsReaImGuiClass(const char* cls)
{
  return cls && (strcmp(cls, "reaper_imgui_context") == 0 ||
                 strcmp(cls, "Lua_LICE_gfx_standalone") == 0);
}
static BOOL CALLBACK ReaImGuiChildScanProc(HWND child, LPARAM lp)
{
  char cls[64] = {};
  GetClassName(child, cls, sizeof(cls));
  if (IsReaImGuiClass(cls)) { *(bool*)lp = true; return FALSE; }
  return TRUE;
}
static bool IsReaImGuiHostWindow(HWND hwnd)
{
  if (!hwnd || !IsWindow(hwnd)) return false;
  char cls[64] = {};
  GetClassName(hwnd, cls, sizeof(cls));
  if (IsReaImGuiClass(cls)) return true;
  bool found = false;
  EnumChildWindows(hwnd, ReaImGuiChildScanProc, (LPARAM)&found);
  return found;
}

#if !defined(_WIN32) && !defined(__APPLE__)
// ADR-061 — GL-only variant of the probe above: class 'reaper_imgui_context'
// on the host or a direct child, EXCLUDING 'Lua_LICE_gfx_standalone'. On
// SWELL-generic a child window has no OS window of its own, so SetParent
// destroys the X11/GDK window the script's GL context renders into —
// ReaImGui crashes REAPER on its next frame. LICE-gfx windows paint into
// the window DC and survive reparenting, so they stay capturable.
static BOOL CALLBACK ImGuiGlChildScanProc(HWND child, LPARAM lp)
{
  char cls[64] = {};
  GetClassName(child, cls, sizeof(cls));
  if (strcmp(cls, "reaper_imgui_context") == 0) {
    *(bool*)lp = true;
    return FALSE;
  }
  return TRUE;
}
static bool IsImGuiGlContextWindow(HWND hwnd)
{
  if (!hwnd || !IsWindow(hwnd)) return false;
  char cls[64] = {};
  GetClassName(hwnd, cls, sizeof(cls));
  if (strcmp(cls, "reaper_imgui_context") == 0) return true;
  bool found = false;
  EnumChildWindows(hwnd, ImGuiGlChildScanProc, (LPARAM)&found);
  return found;
}
#endif

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
  HWND lastOwnTopMost = nullptr;
  int hops = 0;
  while (cur && cur != g_reaperMainHwnd && hops < 32) {
    char title[256] = {};
    GetWindowText(cur, title, sizeof(title));
    if (title[0] && LookupToggleAction(title) > 0) return cur;
    // F-35 — REAPER-native dynamic-title windows (e.g. the MIDI editor,
    // "MIDI take: ...") have no toggle action and resolve to the system
    // "reaper" module, so both acceptance checks here reject them and the
    // click is silently dropped. macOS/Linux capture-by-click accepts any
    // top-level window; mirror that for these recognised native windows.
    if (title[0] && GetDynamicTitlePrefix(title)) return cur;

    char modName[128] = {};
    if (TryGetAppNameFromModule(cur, modName, sizeof(modName))) {
      bestPlugin = cur;
    } else if (bestPlugin) {
      return bestPlugin;  // chain just exited plugin DLL — pick last plugin HWND
    }
    // Track the shallowest same-process window before the chain hits main.
    // Same-process only: Win32 WindowFromPoint sees OTHER apps' windows too
    // (unlike both SWELLs), and those must never become capture sources.
    DWORD pid = 0;
    GetWindowThreadProcessId(cur, &pid);
    if (pid == GetCurrentProcessId()) lastOwnTopMost = cur;
    cur = GetParent(cur);
    hops++;
  }
  if (bestPlugin) return bestPlugin;
  // Win-VM live debug 2026-06-10 — parity with the #else branch (ADR-048
  // model): the legacy allow-list-only resolver returned null for every
  // native REAPER window outside KNOWN_WINDOWS (Region Render Matrix,
  // Screensets/Layouts, ...), so click-capture and drag-to-dock silently
  // no-op'd on Win32. Return the outermost own-process candidate and let
  // IsCapturableTarget do the gating — its ADR-053 WS_CHILD test still
  // rejects core main-window views (arrange/ruler/TCP).
  return lastOwnTopMost;
#elif defined(__APPLE__)
  HWND topLevel = underCursor;
  HWND parent = GetParent(topLevel);
  while (parent && parent != g_reaperMainHwnd) {
    topLevel = parent;
    parent = GetParent(topLevel);
  }
  return (topLevel == g_reaperMainHwnd) ? nullptr : topLevel;
#else
  // Linux-VM live debug 2026-06-10 — the plain walk-to-top resolved every
  // click on a docker-docked window to the untitled 'REAPER_dock' container,
  // which IsCapturableTarget rightly rejects (WS_CHILD of main, no ID) — so
  // docked windows were silently un-clickable. Two improvements, mirroring
  // the Win32 branch's shape:
  //  - a hop whose title resolves to a known toggle action wins immediately;
  //  - remember the click path's direct child of 'REAPER_dock' (that IS the
  //    docked window) and prefer it over the container. Its parent is the
  //    dock, not main, so the ADR-053 embedded test passes and DoCapture's
  //    docker-detach branch (B20) handles the actual capture.
  // Core main-window views are unaffected: their parent chain has no
  // REAPER_dock hop and ends at main, so they still resolve to themselves
  // and the embedded test still demands a positive ID.
  HWND topLevel = underCursor;
  HWND prev = nullptr;
  HWND dockedChild = nullptr;
  HWND cur = underCursor;
  int hops = 0;
  while (cur && cur != g_reaperMainHwnd && hops < 32) {
    char t[256] = {};
    GetWindowText(cur, t, sizeof(t));
    if (t[0] && LookupToggleAction(t) > 0) return cur;
    if (t[0] && strcmp(t, "REAPER_dock") == 0 && prev && !dockedChild)
      dockedChild = prev;
    prev = cur;
    topLevel = cur;
    cur = GetParent(cur);
    hops++;
  }
  if (dockedChild) return dockedChild;
  return (topLevel == g_reaperMainHwnd) ? nullptr : topLevel;
#endif
}

#ifndef __APPLE__
namespace {
struct TitleBandHit {
  POINT pt;
  HWND hit;
};
// A point in the 48px band directly ABOVE a visible top-level window's rect
// is (with overwhelming likelihood) on its WM titlebar. Strictly above only:
// points inside windows are WindowFromPoint's job, which kills false
// positives from overlapping windows. First match wins (SWELL enumerates
// roughly in z-order; on Win32 this path barely ever runs).
BOOL CALLBACK TitleBandEnumProc(HWND w, LPARAM lp)
{
  TitleBandHit* s = (TitleBandHit*)lp;
  if (!IsWindowVisible(w) || w == g_reaperMainHwnd) return TRUE;
  // ADR-060 — never resolve to our own overlay strips. On SWELL-generic the
  // whole-rect+band test below would otherwise catch the frame's bottom
  // strip for points in the target's lower 48px (the strips are top-level
  // SWELL windows, so EnumWindows sees them).
  if (IsCaptureHighlightWindow(w)) return TRUE;
#ifdef _WIN32
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  if (pid != GetCurrentProcessId()) return TRUE;
#endif
  RECT r = {};
  GetWindowRect(w, &r);
#ifdef _WIN32
  // Win32: only the decoration band ABOVE the window — inside-window points
  // are native WindowFromPoint's job (correct z-order there).
  const bool hit = (s->pt.x >= r.left && s->pt.x < r.right &&
                    s->pt.y >= r.top - 48 && s->pt.y < r.top);
#else
  // SWELL-generic: the WHOLE rect plus the WM decoration band. Its
  // WindowFromPoint walks SWELL_topwindows in LIST order (vendored
  // swell-wnd-generic.cpp:7019), so the main window swallows points that
  // visually belong to a floating window stacked above it (Track Grouping
  // Matrix over the arrange — Linux-VM live debug 2026-06-10).
  const bool hit = (s->pt.x >= r.left && s->pt.x < r.right &&
                    s->pt.y >= r.top - 48 && s->pt.y < r.bottom);
#endif
  if (hit) {
    s->hit = w;
    return FALSE;
  }
  return TRUE;
}
}  // namespace
#endif

HWND WindowManager::ForegroundFallbackForPoint(POINT screenPt)
{
#ifdef __APPLE__
  (void)screenPt;
  return nullptr;
#else
  // SWELL-generic facts (swell-wnd-generic.cpp): GetForegroundWindow() is
  // GetFocus(), which descends to the focused CHILD control — useless for a
  // titlebar rect test (first attempt failed exactly there). Scan top-level
  // windows for one whose decoration band contains the point instead — no
  // focus timing involved, works on the first click.
  TitleBandHit s = { screenPt, nullptr };
  EnumWindows(TitleBandEnumProc, (LPARAM)&s);
  if (s.hit) {
    DBG("[MaxPane] TitleBandFallback: pt=(%ld,%ld) -> %p\n",
        (long)screenPt.x, (long)screenPt.y, (void*)s.hit);
  }
  return s.hit;
#endif
}

HWND WindowManager::WindowFromPointForCapture(POINT screenPt)
{
#if defined(__APPLE__)
  return WindowFromPoint(screenPt);
#elif defined(_WIN32)
  HWND under = WindowFromPoint(screenPt);
  if (!under) under = ForegroundFallbackForPoint(screenPt);  // titlebar band
  return under;
#else
  // SWELL-generic: prefer a floating top-level whose rect (incl. titlebar
  // band) contains the point — they are visually above the main window even
  // when SWELL's list-order hit-test says otherwise. Fall back to the plain
  // hit-test for everything else (docked windows, MaxPane itself, ...).
  HWND f = ForegroundFallbackForPoint(screenPt);
  return f ? f : WindowFromPoint(screenPt);
#endif
}

HWND WindowManager::ResolveDockFrameChild(HWND topLevel, char* title, HWND* dockFrameOut)
{
  if (dockFrameOut) *dockFrameOut = nullptr;
  if (!topLevel || !title) return topLevel;
  char* suffix = strstr(title, DOCKED_TITLE_SUFFIX);
  if (!suffix) return topLevel;
  *suffix = '\0';
  HWND child = FindChildInParent(topLevel, title);
  if (child && child != topLevel) {
    if (dockFrameOut) *dockFrameOut = topLevel;
    return child;
  }
  return topLevel;
}

bool WindowManager::IsCapturableTarget(HWND topLevel, const char* title)
{
  if (!topLevel || !title) return false;
  // ADR-052 — the Main toolbar (main-window top chrome, toggle 41651) is
  // never capturable. Title check here gates the click path early; the
  // action-based backstop lives in CaptureArbitraryWindow (a switched main
  // toolbar can carry the displayed toolbar's name instead).
  if (strcmp(title, "Main toolbar") == 0) return false;
#if !defined(_WIN32) && !defined(__APPLE__)
  // ADR-061 — GL ReaImGui windows can't be captured on Linux (see
  // CaptureArbitraryWindow); reject here too, BEFORE the not-embedded
  // early-accept below, so the hover preview never advertises them.
  if (IsImGuiGlContextWindow(topLevel) && !IsReaImGuiSoftwareRenderingOn()) {
    DBG("[MaxPane] IsCapturableTarget: reject ReaImGui-GL '%s' on Linux (ADR-061, forcecpu off)\n",
        title);
    SetCaptureRefusal(kGlCaptureRefusalMsg);
    return false;
  }
#endif
  // Separate windows (floating FX / ReaImGui / dockers) are always grabbable.
  // Only views embedded in REAPER's MAIN window need a positive ID, so an
  // unidentified core view (arrange/ruler/TCP) can't be torn out into a pane
  // and blank the main window (ADR-048).
#ifdef __APPLE__
  // GetParent is unreliable on macOS SWELL (floating windows report main as
  // their owner) — compare NSWindow identity instead.
  bool embedded = IsEmbeddedInMainWindow(topLevel);
#else
  // ADR-053 — Win32 GetParent returns the OWNER for popup windows (MSDN),
  // and SWELL-generic returns m_parent ? m_parent : m_owner, so a floating
  // window OWNED by REAPER main masqueraded as "embedded" and was rejected
  // without a positive ID. That is the same owner-vs-parent trap the macOS
  // branch above documents, and it shipped here in v2.1.0: floating ReaImGui
  // windows (e.g. TK Patchbay) became silently unclickable on Win/Linux
  // (forum report #51). "Embedded in main" means a real WS_CHILD view
  // parented to the main window — the owner shortcut never applies to
  // WS_CHILD windows, so requiring the style bit defuses the trap on both
  // platforms.
  bool embedded = (GetWindowLong(topLevel, GWL_STYLE) & WS_CHILD) != 0 &&
                  GetParent(topLevel) == g_reaperMainHwnd;
#endif
  if (!embedded) return true;
  bool ok = LookupToggleAction(title) > 0
      || strstr(title, DOCKED_TITLE_SUFFIX) != nullptr
      || GetDynamicTitlePrefix(title) != nullptr;
#ifndef __APPLE__
  // Linux-VM live debug 2026-06-10 — on SWELL-generic a FLOATING ReaImGui
  // window is a genuine WS_CHILD of main (TK-Patchbay-class '#untitled':
  // style=0x42300000, parent==main), so the ADR-053 embedded test classifies
  // it as a core view and the click/drag paths reject it — forum #51's exact
  // symptom, alive on Linux. The ReaImGui / Lua-gfx window class is the
  // positive ID that separates these from arrange/ruler/TCP. NOT enabled on
  // macOS: there 'embedded' means docked-in-main (NSWindow identity), and
  // capturing docked ReaImGui is the documented ADR-048 trade-off.
  if (!ok) ok = IsReaImGuiHostWindow(topLevel);
#endif
  if (!ok) {
    // Linux-VM live debug 2026-06-10 — '#untitled' (TK-Patchbay-class
    // ReaImGui float) was still rejected here despite ADR-053; dump the
    // signals so the embedded-test assumptions can be checked per-platform.
    char cls[64] = {};
    GetClassName(topLevel, cls, sizeof(cls));
    DBG("[MaxPane] IsCapturableTarget reject: '%s' hwnd=%p class='%s' "
        "style=0x%08x parent=%p main=%p\n",
        title, (void*)topLevel, cls,
        (unsigned)GetWindowLong(topLevel, GWL_STYLE),
        (void*)GetParent(topLevel), (void*)g_reaperMainHwnd);
  }
  return ok;
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

  // U10 (ADR-067) — captured ReaImGui children get the Win32 move-veto
  // subclass so background/titlebar drags can't slide them out of the pane.
  if (tab.isReaImGui) InstallImGuiMoveGuard(targetHwnd);

  DBG("[MaxPane] DoCapture: DONE hwnd=%p captured=true\n", (void*)targetHwnd);
  return true;
}

bool WindowManager::CanReturnVisible(const TabEntry* tab) const
{
  if (!tab || !tab->captured) return false;
  if (!tab->isArbitrary) return true;                      // known/toggle windows
  if (FxCapture::IsFxIdentity(tab->actionCmd)) return true; // FX (TrackFX identity)
  if (GetToolbarToggleAction(tab->name) > 0) return true;   // toolbars
  // A KNOWN REAPER window (Routing Matrix, Track Manager, Screensets, …) is
  // saved by name on the restore path (isArbitrary=false → handled above), but
  // capture-by-click always routes through the arbitrary path (isArbitrary=
  // true) and resolves the same numeric toggle action. Without this, such a
  // window loses "Release Window" after a release + click re-capture. Safe to
  // float back via its toggle. EXCLUDE ReaImGui / Lua scripts (named "_RS…"
  // command) — reparent-without-teardown crashes ImGui Docker (ADR-035).
  if (tab->toggleAction > 0 && strncmp(tab->actionCmd, "_RS", 3) != 0)
    return true;
#if !defined(_WIN32) && !defined(__APPLE__)
  // ADR-061 amendment (v2.2.1, owner request after the live forcecpu loop):
  // on SWELL-generic the return-visible path was proven live for ReaImGui —
  // detach recreates the GdkWindow and the offscreen-software renderer (the
  // only kind the Linux capture gate admits) keeps drawing; the demo and TK
  // Patchbay both came back floating and alive. macOS keeps the ADR-035
  // gating until the same proof exists there.
  if (tab->isReaImGui) return true;
#endif
  // ReaImGui / plain arbitrary click-captures with no real toggle: no safe
  // floating-return. Release hidden.
  return false;
}

// =========================================================================
// DoRelease protocol bodies (audit M2.2 follow-up). Three release protocols
// extracted from DoRelease's old if/else chain as TU-local helpers; the
// bodies below are verbatim moves (only dedented). Dispatch conditions and
// the shared release tail stay in DoRelease.
// =========================================================================

// v2.0.4 #1 (ADR-037) — FX identity path. REAPER's TrackFX_Show(_, _, 2)
// closes the FX UI cleanly and updates its own tracker. Skips the
// toggle/WM_CLOSE dispatch entirely. For workspace switch (toggleOff=
// false) we still hide because the user-visible expectation is "this
// layout's FX windows go away when I switch layouts" — the plugin
// instance keeps running, only the floating UI hides.
static void ReleaseFxIdentity(TabEntry& tab, bool returnVisible)
{
  if (!returnVisible) {
    DBG("[MaxPane] DoRelease: FX identity '%s' — TrackFX_Show(hide)\n",
        tab.actionCmd);
    FxCapture::Hide(tab.actionCmd);
  } else {
    // F-Release (v2.0.6) — keep the FX floating. REAPER still tracks it as
    // shown (showFlag 3 from capture); we only detach it from MaxPane and
    // show it below, so no FxCapture::Hide and no fx_done hide.
    DBG("[MaxPane] DoRelease: FX identity '%s' — return-visible (keep floating)\n",
        tab.actionCmd);
  }

  if (IsWindow(tab.hwnd)) {
    // FX UI may still be alive as a top-level NSWindow if REAPER's
    // implementation chose to hide rather than destroy. Restore it to
    // top-level so the WS_CHILD relationship with MaxPane container is
    // gone — next TrackFX_Show(3) will give us a fresh HWND anyway.
    DetachToTopLevel(tab.hwnd);
    VerifySetParent(tab.hwnd, nullptr, "DoRelease/fx-detach");

    if (tab.originalRect.right > tab.originalRect.left &&
        tab.originalRect.bottom > tab.originalRect.top) {
      RECT rr = tab.originalRect;
      // Return-visible: keep the floating FX on-screen (mirrors the
      // toggle/known branch). Close path: raw rect, behavior unchanged.
      if (returnVisible) ClampRectToVisibleScreen(&rr);
      SetWindowPos(tab.hwnd, nullptr,
                   rr.left, rr.top, rr.right - rr.left, rr.bottom - rr.top,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    // FX windows are REAPER's own — they have their own chrome managed
    // by REAPER when re-shown. Skip ApplyFloatingWindowChrome to avoid
    // double-decoration.
  }
}

// Toggle-known protocol — windows with a real on/off toggle action (Mixer,
// manager windows, toolbars). Fire the toggle while WS_CHILD with a WM_CLOSE
// fallback, F-G pre-detach hide + post-detach retry, then restore geometry
// and chrome on the detached window.
static void ReleaseToggleKnown(TabEntry& tab, bool returnVisible)
{
  int preState = g_GetToggleCommandState
                   ? g_GetToggleCommandState(tab.toggleAction) : -1;

  // ===== B27 DEBUG INSTRUMENTATION =====
  // Capture full state at entry to diagnose why action toggle doesn't
  // update REAPER's tracker on close. Audit M2.2 — the GetWindowRect
  // probes used to run in Release too, feeding no-op DBG calls.
#ifdef MAXPANE_DEBUG
  RECT preReleaseRect = {};
  GetWindowRect(tab.hwnd, &preReleaseRect);
#endif
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

  // Fire toggle while WS_CHILD. Skipped for return-visible: we deliberately
  // leave REAPER's toggle state at 1 so it keeps the window open, then detach
  // + re-show it below (v2.0.6 Release).
  if (!returnVisible && preState != 0) {
    DBG("[B27] >>> calling g_Main_OnCommand(%d, 0) pre-detach\n", tab.toggleAction);
    g_Main_OnCommand(tab.toggleAction, 0);
    int postState = g_GetToggleCommandState
                      ? g_GetToggleCommandState(tab.toggleAction) : -1;
    DBG("[B27] <<< action done: postState=%d (was %d), IsWindowVisible=%d\n",
        postState, preState,
        (IsWindow(tab.hwnd) && IsWindowVisible(tab.hwnd)) ? 1 : 0);
    if (postState == 1 && preState == 1 && IsWindow(tab.hwnd)) {
      DBG("[B27] >>> action NO-OP, sending WM_CLOSE\n");
      SendMessage(tab.hwnd, WM_CLOSE, 0, 0);
#ifdef MAXPANE_DEBUG
      int post2 = g_GetToggleCommandState
                    ? g_GetToggleCommandState(tab.toggleAction) : -1;
      DBG("[B27] <<< WM_CLOSE done: postState=%d, IsWindowVisible=%d\n",
          post2, (IsWindow(tab.hwnd) && IsWindowVisible(tab.hwnd)) ? 1 : 0);
#endif
    }
  } else {
    DBG("[B27] skipping toggle/close (returnVisible=%d preState=%d)\n",
        returnVisible ? 1 : 0, preState);
  }

  // The toggle can DESTROY the window outright when tab.toggleAction is a
  // script command (ReaImGui / Lua-gfx, preState==-1): the script tears its
  // window down inside Main_OnCommand. On SWELL-generic an HWND is a heap
  // pointer — every touch after that is use-after-free, and close-X on a
  // captured script tab killed the whole REAPER on Linux (v2.2.0 live
  // report; macOS never crashed because NSViews are obj-c-retained).
  // SWELL-generic IsWindow walks the window list comparing pointers without
  // dereferencing the candidate, so it is the one safe probe everywhere.
  // ReleaseNoAction and ReleaseFxIdentity already guard this way.
  if (!IsWindow(tab.hwnd)) {
    DBG("[B27] window destroyed by its own toggle — skip hide/detach/restore\n");
    return;
  }

  // F-G (forum v2.0.6) — hide BEFORE detaching. Manager windows (Routing
  // Matrix, Track Manager) ignore both the toggle AND WM_CLOSE while they
  // are our WS_CHILD — REAPER keeps their toggle state==1. SWELL's
  // SetParent(nullptr) on a still-VISIBLE child then re-creates a visible
  // top-level NSWindow at the view's screen frame, so the window floats
  // back as a ghost (most visible when it was an INACTIVE tab whose
  // RepositionAll SW_HIDE didn't take). Forcing it hidden first makes the
  // detach land it cleanly off-screen, matching the benign path the window
  // already takes when it happened to be hidden at release time. Runs after
  // the toggle/WM_CLOSE above so those still act on the visible window.
  if (!returnVisible) {
    DBG("[B27] >>> pre-detach hide (F-G): visible=%d\n", IsWindowVisible(tab.hwnd) ? 1 : 0);
    ShowWindow(tab.hwnd, SW_HIDE);
    ForceHideWindow(tab.hwnd);
    DBG("[B27] <<< pre-detach hide done: visible=%d\n", IsWindowVisible(tab.hwnd) ? 1 : 0);
  }

  // Detach. Entry 11 — DetachToTopLevel flips WS_CHILD → WS_POPUP after
  // SetParent on Win32; SWELL macOS/Linux keeps plain SetParent.
  DBG("[B27] >>> DetachToTopLevel (post-toggle)\n");
  DetachToTopLevel(tab.hwnd);
  VerifySetParent(tab.hwnd, nullptr, "DoRelease/post-toggle");
  DBG("[B27] <<< SetParent done: GetParent=%p IsWindowVisible=%d\n",
      GetParent(tab.hwnd), IsWindowVisible(tab.hwnd) ? 1 : 0);

  // F-G (forum v2.0.6) — if the window STILL reports open after toggle +
  // WM_CLOSE + detach, REAPER re-floats it on a later tick (state stays 1
  // for manager windows whose close no-ops while reparented). state==1 only
  // floats when REAPER re-shows it (focus-dependent: the active tab closes
  // clean, an inactive one floats). Forcing state→0 stops the re-float in
  // every case. Retry the toggle now that the window is detached.
  if (!returnVisible &&
      g_GetToggleCommandState && tab.toggleAction > 0 && g_Main_OnCommand &&
      g_GetToggleCommandState(tab.toggleAction) == 1) {
    DBG("[B27] >>> F-G post-detach toggle retry (state still 1)\n");
    g_Main_OnCommand(tab.toggleAction, 0);
    DBG("[B27] <<< F-G retry: state=%d visible=%d\n",
        g_GetToggleCommandState(tab.toggleAction),
        IsWindowVisible(tab.hwnd) ? 1 : 0);
  }

  // Restore position. Close path (else): raw originalRect, unchanged.
  // Return-visible: clamp on-screen, and skip toolbars entirely — their
  // captured rect is the degenerate docked 42x42@(0,0) (B24); let REAPER
  // re-float them at their own geometry instead of pinning a corner sliver.
  if (returnVisible) {
    if (GetToolbarToggleAction(tab.name) <= 0 &&
        tab.originalRect.right > tab.originalRect.left &&
        tab.originalRect.bottom > tab.originalRect.top) {
      RECT rr = tab.originalRect;
      ClampRectToVisibleScreen(&rr);
      SetWindowPos(tab.hwnd, nullptr, rr.left, rr.top,
                   rr.right - rr.left, rr.bottom - rr.top,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
      DBG("[B27] >>> return-visible SetWindowPos clamped (%ld,%ld,%ld,%ld)\n",
          (long)rr.left, (long)rr.top, (long)rr.right, (long)rr.bottom);
    }
  } else if (tab.originalRect.right > tab.originalRect.left &&
      tab.originalRect.bottom > tab.originalRect.top) {
    int w = tab.originalRect.right - tab.originalRect.left;
    int h = tab.originalRect.bottom - tab.originalRect.top;
    DBG("[B27] >>> SetWindowPos to originalRect %dx%d at (%ld,%ld)\n",
        w, h, (long)tab.originalRect.left, (long)tab.originalRect.top);
    SetWindowPos(tab.hwnd, nullptr,
                 tab.originalRect.left, tab.originalRect.top, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#ifdef MAXPANE_DEBUG
    RECT after = {};
    GetWindowRect(tab.hwnd, &after);
    DBG("[B27] <<< rect after SetWindowPos: (%ld,%ld,%ld,%ld)\n",
        (long)after.left, (long)after.top, (long)after.right, (long)after.bottom);
#endif
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
}

static void ReleaseNoAction(TabEntry& tab, bool toggleOff, bool returnVisible)
{
  // No toggle action known — custom plugins / ReaImGui scripts captured
  // via click/drag/Open Windows where LookupToggleAction couldn't
  // resolve. Best-effort close: send WM_CLOSE so the window's own
  // close handler runs. ReaImGui's NSWindow delegate typically
  // responds with script teardown, which updates REAPER's tracker
  // (toolbar buttons, menu checkmarks) via the script's own logic.
  // For windows that don't respond — at least we tried.
#ifdef MAXPANE_DEBUG
  RECT preClose = {};
  GetWindowRect(tab.hwnd, &preClose);
#endif
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
  } else if (toggleOff && !returnVisible) {
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

void WindowManager::DoRelease(TabEntry& tab, bool toggleOff, bool returnVisible)
{
  if (!tab.captured) return;

  DBG("[MaxPane] DoRelease: '%s' hwnd=%p toggleOff=%d action=%d alive=%d\n",
      tab.name[0] ? tab.name : "(null)", tab.hwnd, toggleOff, tab.toggleAction,
      (tab.hwnd && IsWindow(tab.hwnd)) ? 1 : 0);

  if (tab.hwnd && IsWindow(tab.hwnd)) {
    // U7-B (ADR-069) — swallow every click on this toolbar for the duration
    // of the release; the childless-toolbar forward path could otherwise
    // fire a button from the release gesture's WM_LBUTTONUP (#47).
    s_releasingToolbarHwnd = tab.hwnd;

    // U10 (ADR-067) — drop the move-veto FIRST: the release protocols below
    // reposition the window to its pre-capture rect, and the guard would
    // veto that as an "external" move.
    if (tab.isReaImGui) RemoveImGuiMoveGuard(tab.hwnd);

    // F-D (forum v2.0.6) — the toolbar subclass is now removed at fx_done,
    // AFTER the toggle/reparent below finishes pumping messages. Removing it
    // here (the old spot) let a stray WM_LBUTTONUP from the release gesture
    // reach REAPER's toolbar proc once it was unsubclassed, firing a button.
    // Keeping ToolbarSubclassProc installed through the transition lets it eat
    // those background events symmetrically.

    // Dispatch — one of three release protocols (bodies extracted above;
    // conditions verbatim from the old chain).
    if (FxCapture::IsFxIdentity(tab.actionCmd)) {
      ReleaseFxIdentity(tab, returnVisible);
    }
    else if (toggleOff && tab.toggleAction > 0 && g_Main_OnCommand) {
      ReleaseToggleKnown(tab, returnVisible);
    } else {
      ReleaseNoAction(tab, toggleOff, returnVisible);
    }

    // Shared release tail (all three protocols funnel here; was `fx_done:`).
    // F-D (forum v2.0.6) — unsubclass here, after toggle/reparent finished.
    // No-op for non-toolbars (GetProp returns null). Runs in BOTH modes —
    // the toolbar subclass must go regardless of close vs release. The
    // IsWindow re-check is mandatory: a script window can be DESTROYED by
    // its own toggle inside the release protocol (Linux close-X crash —
    // GetProp on a freed SWELL-generic HWND is use-after-free).
    if (IsWindow(tab.hwnd)) { UnsubclassToolbar(tab.hwnd); RemoveImGuiMoveGuard(tab.hwnd); }
    if (returnVisible) {
      // F-Release (v2.0.6) — the whole point: leave the detached window VISIBLE
      // instead of hiding it. SW_SHOW + a forced Cocoa layout/display pass (SWELL
      // SetParent doesn't trigger setNeedsLayout/Display). For toggle/known +
      // toolbar windows REAPER also keeps state==1 and re-floats; for FX REAPER
      // still tracks showFlag 3. No toggle-off, no stale-list entry → the
      // startup ghost-cleanup (B15/B17) leaves this deliberately-open window alone.
      if (IsWindow(tab.hwnd)) {            // defensive: hwnd can't die on this path
        ShowWindow(tab.hwnd, SW_SHOW);
        ForceViewLayoutAndDisplay(tab.hwnd);
      }
      DBG("[MaxPane] DoRelease: return-visible show '%s' hwnd=%p, visible=%d\n",
          tab.name, (void*)tab.hwnd,
          (IsWindow(tab.hwnd) && IsWindowVisible(tab.hwnd)) ? 1 : 0);
    } else if (IsWindow(tab.hwnd)) {  // script toggles can destroy the hwnd
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
    } else {
      DBG("[MaxPane] DoRelease: hwnd already destroyed — skip hide tail '%s'\n",
          tab.name);
    }

    s_releasingToolbarHwnd = nullptr;  // U7-B (ADR-069) — release finished
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

// U14 (ADR-070) — follow-mode switch: drop every transient tab in the pane.
// FX windows release on the hide path (DoRelease returnVisible=false ->
// TrackFX-managed hide, no toggle, no stale-list entry).
int WindowManager::ReleaseTransientTabs(int paneId)
{
  if (paneId < 0 || paneId >= MAX_PANES) return 0;
  PaneState& ps = m_panes[paneId];
  int released = 0;
  for (int t = ps.tabCount - 1; t >= 0; t--) {
    if (!ps.tabs[t].transient) continue;
    DoRelease(ps.tabs[t], /*toggleOff=*/false, /*returnVisible=*/false);
    ShiftTabsLeft(ps.tabs, ps.tabCount, t);
    if (t < ps.activeTab) ps.activeTab--;
    released++;
  }
  if (ps.tabCount == 0) ps.activeTab = -1;
  else if (ps.activeTab >= ps.tabCount) ps.activeTab = ps.tabCount - 1;
  return released;
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
    // F-39 repaint — a freshly-shown reparented FX/plugin view stays grey
    // until SWELL is told to lay out + display it; ShowWindow alone does not
    // cascade (MEMORY: SWELL SetParent/InvalidateRect don't trigger
    // setNeedsLayout/Display). Without this the plugin draws only after the
    // user switches tabs again.
    ForceViewLayoutAndDisplay(cur.hwnd);
  }
}

void WindowManager::CloseTab(int paneId, int tabIndex, bool returnVisible)
{
  if (paneId < 0 || paneId >= MAX_PANES) return;
  PaneState& ps = m_panes[paneId];
  if (tabIndex < 0 || tabIndex >= ps.tabCount) return;

  // toggleOff stays true so the toggle/known branch is entered (it does the
  // detach + reposition); returnVisible then decides hide-vs-show inside.
  DoRelease(ps.tabs[tabIndex], /*toggleOff=*/true, returnVisible);

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

// ADR-055 — single source of truth for a pane's header height; layout
// (RepositionAll), paint (DrawTabBar) and hit-testing (TabHitTest etc.)
// all consult this so the captured window and the bar can never disagree.
int WindowManager::PaneHeaderHeight(int paneId) const
{
  const PaneState* ps = GetPaneState(paneId);
  // U12 (ADR-068) — clean mode: panes that hold a window get NO header at
  // all (Win/Linux; WS_CHILD children are clipped, splitters + nav bar stay
  // as the control surface). macOS keeps the ADR-026 8px sliver — captured
  // child views paint ABOVE the container, so zero would leave the pane
  // without a mouse surface. Empty panes always keep the full header (CTA).
  if (m_hideAllTabBars && ps && ps->tabCount > 0) {
    // U14 follow-up (owner decision 2026-07-02) — the follow pane keeps its
    // full tab bar even in clean mode: its tabs are the FX-chain switcher,
    // and hiding them made follow mode unusable with clean mode on.
    if (m_followActive && paneId == 0) return TAB_BAR_HEIGHT;
#ifdef __APPLE__
    return TAB_BAR_COLLAPSED_HEIGHT;
#else
    return 0;
#endif
  }
  if (!m_hideSingleTabBar) return TAB_BAR_HEIGHT;
  if (ps && ps->tabCount == 1) return TAB_BAR_COLLAPSED_HEIGHT;
  return TAB_BAR_HEIGHT;  // multi-tab AND empty panes (capture CTA) keep full
}

void WindowManager::RepositionAll(const SplitTree& tree)
{
  // U10 (ADR-067) — everything RepositionAll does to captured children is a
  // MaxPane-intended reposition; let it through the ImGui move guard.
  SelfChildMoveScope selfMove;

  const int* leafList = tree.GetLeafList();
  int leafCount = tree.GetLeafCount();

  for (int i = 0; i < leafCount; i++) {
    int paneId = tree.GetPaneId(leafList[i]);
    if (paneId < 0 || paneId >= MAX_PANES) continue;

    PaneState& ps = m_panes[paneId];
    if (ps.tabCount == 0) continue;

    const RECT& paneRect = tree.GetPaneRect(paneId);
    int headerOffset = PaneHeaderHeight(paneId);

    int x = paneRect.left;
    int y = paneRect.top + headerOffset;
    int w = paneRect.right - paneRect.left;
    int h = paneRect.bottom - paneRect.top - headerOffset;

    if (w <= 0 || h <= 0) {
      // F-A (forum v2.0.6) — degenerate pane geometry: a collapsed splitter,
      // or exiting solo on a container that was shrunk while soloed, can yield
      // a pane with non-positive area. Leaving a captured window SHOWN at its
      // stale (pre-collapse) size corrupts Direct2D/ImGui hosts — ReaImGui
      // (e.g. TK Patchbay) asserts in ImGui_EndChild when its host window's
      // size desyncs from the ImGui frame. Hide every captured window in this
      // pane instead of skipping it visible. RefreshLayout re-runs this pass
      // once the pane regains positive area, re-showing + repositioning it.
      for (int t = 0; t < ps.tabCount; t++) {
        TabEntry& tab = ps.tabs[t];
        if (tab.captured && tab.hwnd && IsWindow(tab.hwnd))
          ShowWindow(tab.hwnd, SW_HIDE);
      }
      continue;
    }

    for (int t = 0; t < ps.tabCount; t++) {
      TabEntry& tab = ps.tabs[t];
      if (!tab.captured || !tab.hwnd) continue;
      if (!IsWindow(tab.hwnd)) continue;

      if (t == ps.activeTab) {
        // Bug I — never size a captured ReaImGui / Lua-gfx window below its
        // content min: it pushes back by resizing REAPER's own main window
        // (collapse). The container min-size clamp (WM_GETMINMAXINFO) protects
        // the dock-divider axis; this covers the perpendicular axis (driven by
        // REAPER's window, which ignores our reported min) by HIDING the capture
        // before the pane squeezes it small. Both axes guarded so it works for
        // any dock edge + floating. The reShow logic below repaints it when the
        // pane grows back. ReaImGui-only (v2.1.1) — toolbars / FX / native GDI
        // captures don't cascade and are legitimately small, so they are never
        // floor-hidden (regression in v2.1.0 hid docked toolbars in small panes).
        // ADR-061 item #2 (v2.2.1) — per-window learned min on top of the
        // fixed safety floor: a script that re-asserts a size larger than
        // the pane (e.g. TK Patchbay's 600x400 SetNextWindowSizeConstraints)
        // would otherwise overflow the pane on SWELL-generic and cover the
        // splitter + the next pane's tab bar. Learned by CheckAlive's drift
        // watchdog; 0 until a disagreement is observed, so behaviour is
        // unchanged for windows that accept our sizing.
        int floorW = ARB_PANE_MIN, floorH = ARB_PANE_MIN;
        if (tab.arbMinW > floorW) floorW = tab.arbMinW;
        if (tab.arbMinH > floorH) floorH = tab.arbMinH;
        if (tab.isReaImGui && (w < floorW || h < floorH)) {
          ShowWindow(tab.hwnd, SW_HIDE);
          // ADR-062 — clear the window's stale pixels: on SWELL-generic
          // hiding a child does NOT repaint the parent area it covered, so
          // the script's last frame lingered under the floor-hidden hint
          // (read as a dead/frozen window). Erase + repaint the pane rect.
          InvalidateRect(m_containerHwnd, &paneRect, TRUE);
          DBG("[MaxPane] Bug I: hid ReaImGui '%s' below floor pane=(w%d h%d) floor=(w%d h%d)\n",
              tab.name, w, h, floorW, floorH);
          continue;
        }
        // Sprint 1 Entry 19 — Direct2D-rendered children (ReaImGui, JUCE,
        // Reabeat) leave the previous client-area bits on screen after
        // SetWindowPos because the default copy-bits behaviour copies old
        // pixels to the new position and the plugin's swap chain doesn't
        // repaint synchronously — old tab-bar text stacks during interactive
        // resize. SWP_NOCOPYBITS discards stale bits and lets the plugin
        // present a fresh frame. KNOWN_WINDOWS native captures (GDI-based)
        // stay on the default copy-bits path — flicker-free for them.
        // Bug I — detect a hidden→shown transition (the window was hidden by
        // the height floor-hide above and the pane has now grown back). A
        // ReaImGui view re-shown via SWP_SHOWWINDOW stays grey until the user
        // switches tabs; an explicit SW_SHOWNA (what SetActiveTab does and what
        // visibly fixes it) gives it a clean show. Handled after the resize.
        bool reShow = tab.isArbitrary && !IsWindowVisible(tab.hwnd);

        UINT swpFlags = SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED;
        if (tab.isArbitrary) swpFlags |= SWP_NOCOPYBITS;
        SetWindowPos(tab.hwnd, HWND_TOP, x, y, w, h, swpFlags);
        SendMessage(tab.hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(w, h));
        // ADR-061 item #2 — remember what we asked for; CheckAlive's drift
        // watchdog compares the actual rect against this to learn the
        // script's asserted content-min.
        if (tab.isReaImGui) {
          tab.lastSetW = w;
          tab.lastSetH = h;
          tab.lastSetX = x;
          tab.lastSetY = y;
          // ADR-061 item #3 — a reparented SWELL child never receives
          // WM_MOVE, so ReaImGui's viewport keeps its pre-capture screen
          // position and maps all mouse input against that stale rect: the
          // window renders but is mouse-dead (ImGui hit-tests MousePos in
          // screen space against viewport->Pos). Its WM_MOVE handler only
          // sets PlatformRequestMove and re-reads ClientToScreen, so a
          // synthetic WM_MOVE after every reposition keeps it current.
          SendMessage(tab.hwnd, WM_MOVE, 0, MAKELPARAM(x, y));
          DBG("[MaxPane][DRIFT] RepositionAll set '%s' local=(%d,%d %dx%d) + WM_MOVE\n",
              tab.name, x, y, w, h);
        }

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

        // Bug I — clean re-show for a window unhidden by the floor-hide, so it
        // repaints without a manual tab switch (mirrors SetActiveTab).
        if (reShow) {
          ShowWindow(tab.hwnd, SW_SHOWNA);
          InvalidateRect(tab.hwnd, nullptr, TRUE);
          ForceViewLayoutAndDisplay(tab.hwnd);
        }
      } else {
        ShowWindow(tab.hwnd, SW_HIDE);
      }
    }
  }
}

bool WindowManager::HasCapturedReaImGui() const
{
  for (int i = 0; i < MAX_PANES; i++) {
    const PaneState& ps = m_panes[i];
    for (int t = 0; t < ps.tabCount; t++) {
      if (ps.tabs[t].captured && ps.tabs[t].isReaImGui) return true;
    }
  }
  return false;
}

// ADR-062 follow-up — read a child's rect in its parent's CLIENT space,
// normalized so (x,y) is the visual top-left and w/h are positive. Raw
// GetWindowRect subtraction is platform-poisoned for this: mac SWELL
// returns rects in NATIVE y-up screen coords (a child view can read back
// top > bottom, making bottom-top negative), and on Win32 a floating
// container's window rect includes the caption/frame while SetWindowPos
// coords are client-relative. ScreenToClient of both corners + min/max
// lands in the same coordinate system RepositionAll's SetWindowPos uses,
// on all three platforms.
void WindowManager::GetChildRectInParentClient(HWND child, HWND parent,
                                               int* x, int* y, int* w, int* h)
{
  RECT r = {};
  GetWindowRect(child, &r);
  POINT a = { r.left, r.top };
  POINT b = { r.right, r.bottom };
  ScreenToClient(parent, &a);
  ScreenToClient(parent, &b);
  *x = (a.x < b.x) ? a.x : b.x;
  *y = (a.y < b.y) ? a.y : b.y;
  *w = (a.x < b.x) ? (b.x - a.x) : (a.x - b.x);
  *h = (a.y < b.y) ? (b.y - a.y) : (a.y - b.y);
}

// U11 (ADR-067, mequaz #66) — a script/JSFX can close and re-open its window
// at will (the Swing sampler cycles its per-pad FX / browser / step-sequencer
// windows from a JSFX). A tab whose window can come back by title should
// survive the window's death and auto-recapture on reappearance — removing it
// (the old behavior) meant every script round-trip ejected the window from
// its pane. FX-identity tabs are excluded: closing an FX float is a routine
// user action and silently re-grabbing it would be a behavior regression.
static bool TabSurvivesWindowClose(const TabEntry& tab)
{
  if (!tab.isArbitrary || !tab.searchTitle[0]) return false;
  if (FxCapture::IsFxIdentity(tab.actionCmd)) return false;
  return tab.actionCmd[0] != '\0' || tab.toggleAction > 0;
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
            // U4 (ADR-065) — if ANOTHER live instance holds this window,
            // do not reclaim: that instance owns it now, and reclaiming
            // starts the 500ms parent tug-of-war the user sees as flicker
            // (#61). Drop our tab and leave the window alone (dead-tab path
            // below strips our toolbar subclass and removes the entry).
            if (IsCapturedByOtherInstance(tab.hwnd, this)) {
              DBG("[MaxPane] CheckAlive: '%s' owned by another instance — dropping our tab\n",
                  tab.name);
              if (tab.hwnd && IsWindow(tab.hwnd)) { UnsubclassToolbar(tab.hwnd); RemoveImGuiMoveGuard(tab.hwnd); }
              dead = true;
            } else {
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
              if (tab.hwnd && IsWindow(tab.hwnd)) { UnsubclassToolbar(tab.hwnd); RemoveImGuiMoveGuard(tab.hwnd); }
              dead = true;
            }
            }  // U4 arbitration else-block end
          }
          // ADR-061 item #2 (v2.2.1) — drift watchdog. A captured ReaImGui
          // script can re-assert its own size every frame (ImGui size
          // constraints); on SWELL-generic the grown child then overflows
          // the pane, covering the splitter + the next pane's tab bar (seen
          // live on the Linux VM: set 547x329, script forced 600x400). When
          // the actual size disagrees with what RepositionAll last set and
          // the SAME size shows up on two consecutive ticks (rules out the
          // one-frame lag while the script adopts a new pane size), learn
          // it per axis as the window's content-min; returning changed=true
          // makes OnTimer re-run RepositionAll, whose floor-hide now honours
          // the learned min. No disagreement observed → nothing changes.
          else if (tab.isReaImGui && t == ps.activeTab && IsWindowVisible(tab.hwnd) &&
                   tab.lastSetW > 0) {
            // ADR-062 follow-up (live macOS repro: TK still cascaded
            // REAPER's own window smaller): the original screen-rect
            // subtraction read a NEGATIVE height on mac (y-up child rects),
            // so minH never learned and the floor-hide never engaged.
            // Client-space conversion fixes learning on mac and kills the
            // caption-offset snap-back false-fire on a floating Win32
            // container.
            int relX, relY, aw, ah;
            GetChildRectInParentClient(tab.hwnd, m_containerHwnd,
                                       &relX, &relY, &aw, &ah);
            // ADR-061 item #3b — ImGui "drag window by background" moves the
            // captured child: ImGui issues Platform_SetWindowPos with SCREEN
            // coords, SWELL applies them PARENT-relative, and the child sails
            // out of its pane (owner repro: grab TK Patchbay background →
            // the whole view slides sideways until the next relayout). Snap
            // it back to the pane slot and send WM_MOVE so ImGui re-reads
            // the real position and keeps mapping input correctly.
            if ((relX > tab.lastSetX + 4 || relX < tab.lastSetX - 4 ||
                 relY > tab.lastSetY + 4 || relY < tab.lastSetY - 4)) {
              DBG("[MaxPane][DRIFT] '%s' moved to local=(%d,%d), snapping back to (%d,%d)\n",
                  tab.name, relX, relY, tab.lastSetX, tab.lastSetY);
              // U10 (ADR-067) — mark as self-move so the guard admits it.
              SelfChildMoveScope selfMove;
              SetWindowPos(tab.hwnd, nullptr, tab.lastSetX, tab.lastSetY, 0, 0,
                           SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
              SendMessage(tab.hwnd, WM_MOVE, 0,
                          MAKELPARAM(tab.lastSetX, tab.lastSetY));
              GetChildRectInParentClient(tab.hwnd, m_containerHwnd,
                                         &relX, &relY, &aw, &ah);  // fresh data
            }
            // ±4 px tolerance — SWELL/GDK scaling produces off-by-one rect
            // read-backs (seen live: set 471, read 472); don't learn a floor
            // from rounding noise.
            const int eps = 4;
            if (aw > tab.lastSetW + eps || ah > tab.lastSetH + eps) {
              if (aw == tab.pendMinW && ah == tab.pendMinH) {
                const int minW = (aw > tab.lastSetW + eps) ? aw : 0;
                const int minH = (ah > tab.lastSetH + eps) ? ah : 0;
                if (tab.arbMinW != minW || tab.arbMinH != minH) {
                  tab.arbMinW = minW;
                  tab.arbMinH = minH;
                  DBG("[MaxPane][DRIFT] learned content-min for '%s': %dx%d (set %dx%d, actual %dx%d)\n",
                      tab.name, minW, minH, tab.lastSetW, tab.lastSetH, aw, ah);
                  changed = true;  // OnTimer re-runs RepositionAll → floor-hide
                }
              } else {
                tab.pendMinW = aw;
                tab.pendMinH = ah;
                DBG("[MaxPane][DRIFT] '%s' actual %dx%d != set %dx%d (pending confirm)\n",
                    tab.name, aw, ah, tab.lastSetW, tab.lastSetH);
              }
            } else {
              tab.pendMinW = tab.pendMinH = 0;
            }
          }
        }
        if (dead) {
          if (tab.dynamicTitle || TabSurvivesWindowClose(tab)) {
            // Dynamic-title tab lost its HWND (e.g. MIDI Editor on project
            // switch), or a script-openable arb tab whose window a script
            // closed (U11). Keep the tab entry for recapture on next tick.
            DBG("[MaxPane] CheckAlive: %s tab '%s' lost HWND, waiting for recapture\n",
                tab.dynamicTitle ? "dynamic" : "script-sticky", tab.name);
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
      } else if (tab.searchTitle[0] && (tab.dynamicTitle || TabSurvivesWindowClose(tab))) {
        // Uncaptured dynamic or script-sticky tab (U11) — try to recapture.
        // Audit M3.3 — FindReaperWindow is a full-window-tree enumeration
        // (EnumWindows + GetWindowText per window). A tab whose window never
        // comes back (closed MIDI editor) used to pay that every 500 ms
        // FOREVER; back off to every 8th tick (~4 s) after 10 misses.
        if (tab.recaptureBackoff >= 10 && (tab.recaptureBackoff & 7) != 0) {
          tab.recaptureBackoff++;
          continue;
        }
        tab.recaptureBackoff++;
        HWND h = FindReaperWindow(tab.searchTitle, m_containerHwnd);
        // U4 (ADR-065) — a window held by another instance is not a recapture
        // candidate for this one.
        if (h && !IsWindowCaptured(h) && !IsCapturedByOtherInstance(h, this)) {
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
            tab.recaptureBackoff = 0;
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
