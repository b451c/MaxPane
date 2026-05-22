#pragma once
#include "split_tree.h"
#include "window_manager.h"
#include "nav_bar.h"
#include "drag_dock.h"
#include <memory>

// Capture mode: user clicks any window to grab it into a pane
struct CaptureMode {
  bool active;
  int targetPaneId;
};

// Drag state: dragging a tab between panes or reordering within a pane
struct DragState {
  bool active;
  int sourcePaneId;
  int sourceTabIndex;
  POINT startPt;
  int highlightPaneId;
  bool dragStarted;
  int insertTabIndex;   // intra-pane reorder: insertion position, -1 = none
};

// Expand dirty rect to include src (in-place union, handles empty dst/src)
inline void ExpandRect(RECT& dst, const RECT& src)
{
  if (src.right <= src.left || src.bottom <= src.top) return;
  if (dst.right <= dst.left || dst.bottom <= dst.top) { dst = src; return; }
  if (src.left   < dst.left)   dst.left   = src.left;
  if (src.top    < dst.top)    dst.top    = src.top;
  if (src.right  > dst.right)  dst.right  = src.right;
  if (src.bottom > dst.bottom) dst.bottom = src.bottom;
}

struct PaneSnapshot;
class CaptureQueue;
class FavoritesManager;
class WorkspaceManager;

// Process stale_toggle_actions ExtState entries for the given section.
// Closes any windows REAPER restored at startup that the user had captured
// before quit but Shutdown's toggle didn't reliably switch off (REAPER caches
// wnd_vis BEFORE atexit, so any DoRelease toggle that races with the cache
// is lost). Called from startupTimerFunc for all instance sections before
// any MaxPane container is created.
// Returns true if a non-empty stale list was found and processed.
bool ProcessStaleActionsForSection(const char* section);

// Merge currently-captured action IDs into stale_toggle_actions ExtState for
// the given section. Reads existing list (preserving prior workspace-switch
// entries), dedupes against currently-captured action IDs, writes merged
// list back. Called on user-initiated Toggle close and from atexit — both
// paths must record their captures so the next REAPER startup's stale
// cleanup can close ghosts REAPER restored from its wnd_vis cache.
void MergeCapturesIntoStaleListForSection(const char* section, const WindowManager& wm);

// Append a single action ID to stale_toggle_actions ExtState (dedupe).
// Called by close-tab paths (X button, RELEASE menu, DELETE_PANE) BEFORE
// the tab is erased from PaneState — otherwise the action ID is lost and
// any wnd_vis cache mismatch (B13/B16 race for windows like Actions) leaks
// a ghost on next REAPER startup that ProcessStaleActionsForSection can't
// clean because it doesn't know the ID. Defense-in-depth: even when
// mid-session toggle succeeds, the startup verify pass for state==0 is a
// cheap no-op; the win comes from the failure case.
void AppendActionToStaleListForSection(const char* section, int action);

class MaxPaneContainer {
public:
  // Up to MAX_INSTANCES MaxPane containers can coexist (F2).
  // instance 0 uses legacy ExtState/dock/RPP identifiers for backward compat
  // with v1.5.x; instances 1-7 use suffixed forms.
  static constexpr int MAX_INSTANCES = 8;

  explicit MaxPaneContainer(int instanceId = 0);
  ~MaxPaneContainer();

  bool Create();
  void Shutdown();
  void Show();
  void Toggle();
  bool IsVisible() const;

  // F1a (ADR-024) — per-container floating mode. Detach turns the whole
  // container into a top-level OS window with own chrome (title bar, close,
  // resize). Re-dock reattaches to REAPER's docker. Geometry persists in
  // ExtState across sessions and float/dock transitions.
  bool IsFloating() const { return m_floating; }
  void DetachToFloating();
  void RedockToContainer();

  int InstanceId() const { return m_instanceId; }
  // Per-instance identifiers for ExtState section, REAPER dock ident, and
  // RPP chunk tag. Computed once in constructor — stable lifetime.
  const char* ExtSection()  const { return m_extSection; }
  const char* DockIdent()   const { return m_dockIdent; }
  const char* RppChunkTag() const { return m_rppChunkTag; }

  void ApplyPreset(LayoutPreset preset);
  void SplitPane(int paneId, SplitterOrientation orient);
  void MergePane(int paneId);
  void ToggleSolo(int paneId);
  bool IsSoloActive() const { return m_soloActive; }
  int NodeForPane(int paneId) const { return m_tree.NodeForPane(paneId); }

  void SaveState();
  void LoadState();

  // Workspace management
  void SaveWorkspace(const char* name);
  void LoadWorkspace(const char* name);
  void DeleteWorkspace(const char* name);

  // F6 — capture by favorite slot. paneId < 0 routes to m_focusedPaneId.
  // Mirrors the FAV_BASE branch of HandlePaneMenuCommand so REAPER actions
  // (MaxPane_FavSlot_*) can drive captures without going through a menu.
  void ActivateFavorite(int favIdx, int paneId = -1);

  // C1 (ADR-027) — reopen last closed tab via the ring buffer.
  // Returns true if a snapshot was popped and capture initiated.
  bool ReopenLastClosedTab();
  // Buffer status — used by `MaxPane: Reopen last closed tab` action
  // dispatch to no-op gracefully when there is nothing to reopen.
  bool HasRecentlyClosedTab() const { return m_recentClosedCount > 0; }

  // C4 (ADR-027) — Logic-style workspace pickup: single hotkey, prompts
  // for a slot number (1..MAX_WORKSPACES) and loads it. v2.0 uses REAPER's
  // native GetUserInputs prompt; v2.1 candidate replaces it with a
  // custom 600 ms timeout modal for full Logic parity (auto-load on
  // single-digit timeout). UX gain TODAY: 1 hotkey reaches all 32 slots
  // instead of 32 separate REAPER bindings.
  void OpenWorkspacePickup();

  HWND GetHwnd() const { return m_hwnd; }

  SplitTree& GetTree() { return m_tree; }
  const SplitTree& GetTree() const { return m_tree; }
  WindowManager& GetWinMgr() { return m_winMgr; }
  const WindowManager& GetWinMgr() const { return m_winMgr; }
  const WorkspaceManager& GetWsMgr() const { return *m_wsMgr; }
  // F4 (ADR-024 era) — Quick Switcher iterates favorites across the
  // shared favorites store; expose const accessor.
  const FavoritesManager& GetFavMgr() const { return *m_favMgr; }

  // Whole container empty + no other UI mode active = launcher hero (ADR-013).
  bool IsInLauncherMode() const;

  // ADR-026 — persistent navigation bar height, 0 when hidden. Used by the
  // tree to reserve vertical space at the top of the client area.
  int NavBarReservedHeight() const { return m_navBarVisible ? NavBar::NAV_BAR_HEIGHT : 0; }
  bool IsNavBarVisible() const { return m_navBarVisible; }
  void SetNavBarVisible(bool v);

  // ADR-026 — Home overlay state (non-destructive workspace picker over
  // current layout). When open, OnPaint renders the launcher hero on top
  // of the pane grid.
  bool IsHomeOverlayOpen() const { return m_homeOverlay; }
  void OpenHomeOverlay();
  void CloseHomeOverlay();

  // ADR-026 — drag-to-dock mode. Armed = user clicked [Drag] and we're
  // waiting for them to grab a REAPER window. Tracking = mid-drag with
  // live preview rendering.
  bool IsDragModeActive() const { return m_drag.mode != DragDock::IDLE; }
  void EnterDragMode();
  void ExitDragMode();

  // Keyboard navigation
  void NextTab();
  void PrevTab();
  void NextPane();
  void PrevPane();
  void SoloToggleFocused();

private:
  // Per-instance identity. m_instanceId is set in constructor; the three
  // string buffers are computed from it once (no further mutation).
  int m_instanceId;
  char m_extSection[32];    // "MaxPane_cpp"        or "MaxPane_cpp_N"
  char m_dockIdent[32];     // "MaxPane_container"  or "MaxPane_container_N"
  char m_rppChunkTag[32];   // "MAXPANE_STATE"      or "MAXPANE_STATE_N"

  HWND m_hwnd;
  SplitTree m_tree;
  WindowManager m_winMgr;
  bool m_visible;
  CaptureMode m_captureMode;
  DragState m_dragState;
  std::unique_ptr<CaptureQueue> m_captureQueue;
  std::unique_ptr<FavoritesManager> m_favMgr;
  std::unique_ptr<WorkspaceManager> m_wsMgr;
  int m_hoverSplitter;      // branch index of splitter under mouse, -1 when none
  int m_hoverPane;          // pane id of tab under mouse, -1 when none
  int m_hoverTab;           // tab index under mouse, -1 when none; -2 = menu button
  bool m_pendingRppLoad;     // true if waiting for RPP state to become available

  // Solo (maximize) pane state
  bool m_soloActive = false;
  int m_soloPaneId = -1;
  NodeSnapshot m_soloSnapshot[MAX_TREE_NODES];
  int m_soloSnapshotNodeCount = 0;
  bool m_soloTabVisibility[MAX_PANES][MAX_TABS_PER_PANE];  // which tabs were visible before solo

  int m_focusedPaneId = 0;  // pane with keyboard focus (for Next/Prev Tab/Pane)
  int m_launcherHover = -1;  // Launcher::HIT_NONE | HIT_CAPTURE_BUTTON | card index
  int m_launcherTooltipCard = -1;  // card index whose tooltip is currently shown, -1 = none

  // ADR-026 — persistent navigation bar.
  bool m_navBarVisible = true;        // read from ExtState in Create; default ON
  int  m_navHover = NavBar::BTN_NONE; // which nav button under cursor (-1 = none)
  int  m_navTooltipBtn = NavBar::BTN_NONE; // which nav button's tooltip is shown
  // ADR-026 — Home overlay state. When true, OnPaint paints workspace
  // cards (Launcher hero geometry) over the pane grid. Click on card =
  // load + close. Esc or click on empty area = close (no state change).
  bool m_homeOverlay = false;
  int  m_homeOverlayHover = -1;
  // ADR-026 — drag-to-dock state machine. Hot during ARMED/TRACKING; IDLE
  // outside. Owned here so the timer dispatch in DlgProc has direct access.
  DragDock::State m_drag = {};

  void LoadNavBarPref();
  void SaveNavBarPref();
  // Nav bar input dispatch — returns true if the click/move was consumed
  // (caller skips the pane-grid pathways).
  bool OnNavBarMouseMove(int x, int y);
  bool OnNavBarClick(int x, int y);
  // Resolve a nav button click to its action.
  void DispatchNavBar(int buttonId, int xClient, int yClient);
  // Show the workspace-load popup menu rooted near (xClient, yClient).
  void OpenLoadDropdown(int xClient, int yClient);

  // Home overlay input — returns true if consumed.
  bool OnHomeOverlayMouseMove(int x, int y);
  bool OnHomeOverlayClick(int x, int y);

  // Drag-to-dock — TIMER_ID_DRAG_DOCK tick.
  void DragModeTick();
  // Compute current target pane + zone given screen-coord cursor; updates
  // m_drag.targetPaneId + m_drag.zone in place. Returns dirty region the
  // caller should invalidate.
  void RefreshDragPreview();
  // Commit drop at current m_drag state. Resets state to IDLE.
  void CommitDrop();

  // F1a (ADR-024) — floating mode state. m_floating == false → container is
  // docked in REAPER via DockWindowAddEx (default, backward-compatible).
  // m_floating == true → container is a top-level OS window at (m_floatX,
  // m_floatY, m_floatW, m_floatH). All four ExtState keys are written under
  // m_extSection: float_enabled / float_x / float_y / float_w / float_h.
  bool m_floating = false;
  int m_floatX = 100;
  int m_floatY = 100;
  int m_floatW = 800;
  int m_floatH = 600;
  // C5 (ADR-027) — when floating, optionally keep window above other apps.
  // ExtState key `float_always_on_top`. No effect when m_floating == false.
  bool m_floatAlwaysOnTop = false;

  // C1 (ADR-027) — ring buffer of recently closed tabs. Cmd+Shift+T-style
  // reopen via `MaxPane: Reopen last closed tab` action. Lives in RAM only
  // (session-scoped; not persisted). Newest at (head-1+N)%N.
  struct ClosedTabSnapshot {
    char name[256];
    char searchTitle[256];
    char actionCmd[128];
    int  toggleAction;
    int  sourcePaneId;
    bool isArbitrary;
  };
  static const int RECENT_CLOSED_CAP = 16;
  ClosedTabSnapshot m_recentClosed[RECENT_CLOSED_CAP] = {};
  int m_recentClosedHead = 0;   // next write slot
  int m_recentClosedCount = 0;  // 0..RECENT_CLOSED_CAP

  // C1 helper — snapshot tab metadata into ring buffer before destructive
  // close. Called from every close path (menu Close Tab, C3 bulk close,
  // tab-bar close button, programmatic close).
  void RecordClosedTab(int paneId, int tabIndex);

  // Read float_* ExtState keys into m_floating + m_float{X,Y,W,H}. Called
  // from Create() before deciding dock vs float. Defaults preserved if keys
  // absent (backward compat with v1.5.x ExtState).
  void LoadFloatingState();
  // Write current m_floating + geometry to ExtState. Called on transition
  // (Detach/Redock) and on Shutdown if floating (geometry persistence).
  void SaveFloatingState();
  // Capture current top-level window rect into m_float{X,Y,W,H}. No-op if
  // not floating or HWND missing.
  void CaptureFloatGeometry();

  // Toast bar — transient feedback for non-fatal events (Sprint 3.2).
  // m_toastDeadlineMs holds the steady_clock monotonic deadline; 0 = no toast.
  char m_toastMessage[256] = {};
  long long m_toastDeadlineMs = 0;

  // GDI object cache (created once in constructor, destroyed in destructor)
  HBRUSH m_brushTabBarBg = nullptr;
  HBRUSH m_brushTabActive = nullptr;
  HBRUSH m_brushTabInactive = nullptr;
  HBRUSH m_brushEmptyHeader = nullptr;
  HBRUSH m_brushPaneBg = nullptr;
  HBRUSH m_brushSplitter = nullptr;
  HBRUSH m_brushSplitterHover = nullptr;
  HPEN   m_penTabSeparator = nullptr;
  HPEN   m_penGridLine = nullptr;

  static void SafeDeleteBrush(HBRUSH& brush) {
    if (brush) { DeleteObject(brush); brush = nullptr; }
  }
  static void SafeDeletePen(HPEN& pen) {
    if (pen) { DeleteObject(pen); pen = nullptr; }
  }

  void ApplyPaneState(const PaneSnapshot* panes, int maxPanes, bool deferActions);
  void RefreshLayout();
  void StartCaptureTimer();
  void StopCaptureTimerIfIdle();

  void OnSize(int cx, int cy);
  void OnPaint(HDC hdc);
  void OnMouseMove(int x, int y);
  void OnLButtonUp(int x, int y);
  void OnTimer();
  void OnContextMenu(int x, int y);
  void DrawTabBar(HDC hdc, int paneId, const RECT& paneRect);

  // Tab bar layout calculation (shared by draw + hit-test)
  struct TabBarLayout {
    int tabWidth;      // width of each tab
    int tabAreaLeft;   // left edge of tab area (= paneRect.left)
    int tabAreaRight;  // right edge of tab area (before menu button)
  };
  TabBarLayout CalcTabBarLayout(int paneId) const;
  RECT GetTabRect(int paneId, int tabIdx) const;

  // Tab hit testing
  int TabHitTest(int paneId, int x, int y) const;
  bool IsOnTabCloseButton(int paneId, int tabIndex, int x, int y) const;

  // Drag and drop
  void StartTabDrag(int paneId, int tabIndex, int x, int y);
  void UpdateTabDrag(int x, int y);
  void EndTabDrag(int x, int y);
  void CancelTabDrag();

  int PaneAtPoint(int x, int y) const;

  // Pane menu button (▼ in tab bar)
  void OnPaneMenuButtonClick(int paneId, int x, int y);

  // Launcher card right-click menu (Sprint 2.5). Called from OnContextMenu
  // when the user right-clicks a workspace card in launcher mode.
  void OpenLauncherCardMenu(int cardIdx, int x, int y);

  // Toast bar (Sprint 3.2). ShowToast copies the message and arms the
  // TIMER_ID_TOAST timer driving the fade animation. PaintToast renders
  // the bar overlaid on the bottom of the client area; OnPaint calls it
  // last so the toast sits above any other UI state.
  void ShowToast(const char* msg);
  void PaintToast(HDC hdc, const RECT& clientRect);

  // Context menu command dispatch
  void HandleTabMenuCommand(int cmd, int paneId, int tabIdx);
  void HandlePaneMenuCommand(int cmd, int paneId);

  static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
