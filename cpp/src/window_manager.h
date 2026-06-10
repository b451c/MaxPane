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
  bool isReaImGui;               // ReaImGui / Lua-gfx host — the only capture kind
                                 // that cascade-resizes REAPER's main window below its
                                 // content min (Bug I / ADR-045). Gates the dock min-clamp
                                 // + pane floor-hide so toolbars / FX / native captures
                                 // (legitimately small) are never hidden. Set at capture.
  bool dynamicTitle;             // title changes at runtime (e.g. MIDI Editor) — use searchTitle as prefix
  int colorIndex;  // 0 = default (no color), 1-8 = palette color
  char actionCmd[128];           // stable command string ("_RSxxx" or "12345")
  bool pinned;                   // C2 (ADR-027) — sticky, sorted to left, exempt from Close Others
  unsigned short recaptureBackoff;  // audit M3.3 — runtime-only miss counter for the
                                    // uncaptured-dynamic-tab probe in CheckAlive; after
                                    // 10 misses the FindReaperWindow full-window-tree
                                    // enumeration drops from every tick (500 ms) to
                                    // every 8th (~4 s). Reset on recapture.
};

// Returns the stable search prefix for known dynamic-title windows,
// or nullptr if the window has a static title.
const char* GetDynamicTitlePrefix(const char* title);

// GetToolbarToggleAction / LookupToggleAction / GetSearchTitleForAction are
// declared in config.h (definitions in config.cpp since v2.1.2 / ADR-052);
// available here via the config.h include above.

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
  // ADR-055 — single-tab tab-bar collapse pref (Settings, global). Pushed in
  // by the container; consulted by RepositionAll and exposed via
  // PaneHeaderHeight so layout, paint and hit-testing share one answer.
  void SetHideSingleTabBar(bool v) { m_hideSingleTabBar = v; }
  bool GetHideSingleTabBar() const { return m_hideSingleTabBar; }
  // Effective header height for a pane: TAB_BAR_COLLAPSED_HEIGHT when the
  // pref is on and the pane holds exactly one tab; TAB_BAR_HEIGHT otherwise
  // (incl. empty panes — their header carries the capture CTA text).
  int PaneHeaderHeight(int paneId) const;

  // Bug I (ADR-045 / v2.1.1) — true if any captured tab is a ReaImGui / Lua-gfx
  // host. Gates the dock min-clamp + pane floor-hide (toolbars / FX don't
  // cascade and must never be size-guarded). The broader pre-v2.1.1
  // HasCapturedArbitrary() variant was removed as dead code (audit M3.4).
  bool HasCapturedReaImGui() const;

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

  // Linux-VM live debug 2026-06-10 — on X11 the title bar is the WINDOW
  // MANAGER's decoration: WindowFromPoint returns null there, so the
  // natural grab/click point of a window was invisible to capture-by-click
  // and drag-to-dock. The click/press focuses the window though — return
  // the foreground window when `screenPt` lies within its rect expanded
  // upward by the decoration band. Returns null on macOS (titlebars belong
  // to the NSWindow, WindowFromPoint already resolves them).
  static HWND ForegroundFallbackForPoint(POINT screenPt);
  // The capture/drag entry point for "what window is under this screen
  // point". Wraps WindowFromPoint with the per-platform corrections: Win32
  // adds the titlebar-band fallback; SWELL-generic FIRST prefers a floating
  // top-level whose rect contains the point, because SWELL's WindowFromPoint
  // walks windows in list order, not stacking order, and the main window
  // swallows points over floating windows (Linux-VM live debug 2026-06-10).
  static HWND WindowFromPointForCapture(POINT screenPt);

  // Audit M2.7 — shared resolution of REAPER dock frames. If `title`
  // carries DOCKED_TITLE_SUFFIX: strips it in place, resolves the inner
  // child to capture, sets *dockFrameOut to the frame (to hide after a
  // COMMITTED capture). When the suffix is absent, or no child matches,
  // returns topLevel with *dockFrameOut = nullptr. Replaces three
  // hand-rolled copies (Open Windows menu, capture-by-click, drag-to-dock).
  static HWND ResolveDockFrameChild(HWND topLevel, char* title, HWND* dockFrameOut);

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
  bool m_hideSingleTabBar = false;  // ADR-055 — Settings pref mirror
  bool DoCapture(TabEntry& tab, HWND targetHwnd, HWND containerHwnd);
  void DoRelease(TabEntry& tab, bool toggleOff = true, bool returnVisible = false);
};
