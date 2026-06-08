#pragma once
#include "config.h"
#include "split_tree.h"

struct TabEntry {
  char name[256];                // always owned (display name)
  char searchTitle[256];         // always owned (window search title, or prefix for dynamicTitle)
  int toggleAction;
  HWND hwnd;
  HWND originalParent;
  // B27 — pre-capture window rect in screen coords. Used by DoRelease to
  // restore the orphan NSWindow's position BEFORE WM_CLOSE/toggle so REAPER
  // saves sensible wnd_vis coordinates. SWELL's SetParent(nullptr) recreates
  // the NSWindow at default (0,0 ~ lower-left on macOS), and that position
  // would otherwise leak into REAPER's saved position for next launch.
  RECT originalRect;
  bool captured;
  bool isArbitrary;
  bool dynamicTitle;             // title changes at runtime (e.g. MIDI Editor) — use searchTitle as prefix
  int colorIndex;  // 0 = default (no color), 1-8 = palette color
  char actionCmd[128];           // stable command string ("_RSxxx" or "12345")
  bool pinned;                   // C2 (ADR-027) — sticky, sorted to left, exempt from Close Others
};

// Returns the stable search prefix for known dynamic-title windows,
// or nullptr if the window has a static title.
const char* GetDynamicTitlePrefix(const char* title);

// Returns the REAPER toggle action ID for a toolbar window, or 0 if not a toolbar.
int GetToolbarToggleAction(const char* title);

// Look up REAPER toggle action for any window title (toolbars + known windows).
int LookupToggleAction(const char* title);

// Reverse-lookup: given a toggle action ID, fill buf with the window search title.
// Returns true if a mapping was found.
bool GetSearchTitleForAction(int action, char* buf, int bufSize);

struct PaneState {
  TabEntry tabs[MAX_TABS_PER_PANE];
  int tabCount;   // 0 = empty pane
  int activeTab;  // -1 if none
};

class WindowManager {
public:
  WindowManager();

  void Init();

  // Capture appends a new tab (returns false if MAX_TABS reached)
  bool CaptureByIndex(int paneId, int knownWindowIndex, HWND containerHwnd);
  bool CaptureArbitraryWindow(int paneId, HWND targetHwnd, const char* displayName, HWND containerHwnd, int toggleAction = 0, const char* actionCmd = nullptr);

  // Tab management
  void SetActiveTab(int paneId, int tabIndex);
  // returnVisible=false (default) closes/hides the captured window (the
  // ghost-safe close path). returnVisible=true detaches it back to REAPER as a
  // free-floating, VISIBLE window (v2.0.6 "Release"). Only the user-facing
  // Release menu actions pass true; every internal/close caller keeps the
  // default so the B14/B16/Bug-G ghost-prevention path is byte-equivalent.
  void CloseTab(int paneId, int tabIndex, bool returnVisible = false);
  void MoveTab(int srcPane, int srcTab, int dstPane);
  void ReorderTab(int paneId, int fromIndex, int toIndex);
  void SetTabColor(int paneId, int tabIndex, int colorIndex);
  // C2 (ADR-027) — toggle pinned flag, then re-sort the pane so pinned tabs
  // cluster at the left (preserving relative order within each group).
  // Updates activeTab to track the moved tab.
  void SetTabPinned(int paneId, int tabIndex, bool pinned);

  // Release all tabs in a pane
  void ReleaseWindow(int paneId, bool toggleOff = true);
  void ReleaseAll(bool toggleOff = true);
  // Release all: toggle off tabs whose action is in staleActions, hide the rest
  void ReleaseAllSelective(const int* staleActions, int staleCount);
  void RepositionAll(const SplitTree& tree);
  bool CheckAlive();  // returns true if any tabs were removed or recaptured
  // Bug I — true if any pane holds a captured arbitrary (ReaImGui / Lua-gfx)
  // window. Gates the dock min-size clamp so empty / native-only MaxPanes can
  // still dock arbitrarily small.
  bool HasCapturedArbitrary() const;

  // Accessors
  const PaneState* GetPaneState(int paneId) const;
  const TabEntry* GetActiveTabEntry(int paneId) const;
  const TabEntry* GetTab(int paneId, int tabIndex) const;
  // v2.0.6 "Release" — true if this captured tab can be safely returned to
  // REAPER as a visible floating window. Known/toggle windows, FX (TrackFX
  // identity) and toolbars qualify. ReaImGui / plain arbitrary click-captures
  // do NOT: reparenting a live ImGui window without firing its own teardown
  // crashes in Docker::moveTo (ADR-035), so the Release menu item is hidden
  // for them (they keep "Close Tab"). null/uncaptured → false.
  bool CanReturnVisible(const TabEntry* tab) const;
  int GetTabCount(int paneId) const;
  bool IsWindowCaptured(HWND hwnd) const;

  static HWND FindReaperWindow(const char* title, HWND skipContainer = nullptr);
  static HWND FindChildInParent(HWND parent, const char* title);
  static void DumpAllWindowTitles(const char* context = nullptr);

  // Sprint 1 Entry 12 — plugin window display-name resolution. Empty-title
  // dialogs (Direct2D-rendered plugins like ReaBeat, Reamix) require a
  // module-DLL lookup waterfall to surface a human-readable label in the
  // Open Windows menu and capture-by-click. All three are no-ops on
  // non-Win32 platforms (return false / leave buf untouched).
  static bool TryGetAppNameFromModule(HWND hwnd, char* buf, int bufSize);
  static bool TryExtractAppNameFromChildren(HWND hwnd, char* buf, int bufSize);
  static void ResolveWindowDisplayName(HWND hwnd, char* buf, int bufSize);

  // Sprint 1 Entry 15 — scan REAPER's action table (cmd 1..200000) for the
  // plugin's show/hide action via title-token scoring against
  // kbd_getTextFromCmd output, plus a module-name fallback. Returns the
  // action ID, or 0 if no confident match. Cached per (module, title).
  // No-op on non-Win32 (returns 0).
  static int DiscoverActionForWindow(HWND hwnd, const char* windowTitle);

  // Sprint 1 Entry 13 — walk up from `underCursor` looking for the
  // SHALLOWEST window whose toggle action is known (LookupToggleAction)
  // or whose WndProc lives in a plugin DLL (TryGetAppNameFromModule).
  // Fixes docked-plugin capture where the legacy walk-to-top stopped at
  // the REAPER dock frame (a WS_CHILD inner container) instead of the
  // plugin window itself. SWELL uses the v2.0 walk-to-top behaviour.
  static HWND ResolveCaptureSourceForClick(HWND underCursor);

  // ADR-048 — capture allow-list. A window embedded in REAPER's MAIN window
  // (GetParent == g_reaperMainHwnd) is core UI (arrange / ruler / TCP /
  // transport) and is grabbable ONLY if it positively identifies as a real
  // dockable surface (known toggle action, a " (docked)" frame, or a
  // recognised dynamic-title native window). Top-level windows (floating FX /
  // ReaImGui / dockers) are always grabbable. Rejecting the unidentified
  // children stops capture-by-click from tearing REAPER's edit view into a
  // pane and leaving the main window blank (forum v2.0.6).
  static bool IsCapturableTarget(HWND topLevel, const char* title);

private:
  PaneState m_panes[MAX_PANES];
  HWND m_containerHwnd;  // stored for CheckAlive recapture
  bool DoCapture(TabEntry& tab, HWND targetHwnd, HWND containerHwnd);
  void DoRelease(TabEntry& tab, bool toggleOff = true, bool returnVisible = false);
};
