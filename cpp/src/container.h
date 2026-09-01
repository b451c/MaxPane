#pragma once
#include "split_tree.h"
#include "window_manager.h"
#include "nav_bar.h"
#include "drag_dock.h"
#include "config.h"      // MAX_WORKSPACE_NAME (Feature A nav-bar workspace label)
#include <memory>

// Capture mode: user clicks any window to grab it into a pane
struct CaptureMode {
  bool active;
  int targetPaneId;
  // Sprint 1 Entry 13 — rising-edge LMB tracking. Polling "currently down"
  // misses sub-tick LMB transitions and the button click that entered
  // capture mode would self-trigger the timer immediately. Init to true at
  // every mode entry so the first real click after entry counts as rising.
  bool prevLmbDown;
  // ADR-048 — live hover preview. The poll resolves the window under the
  // cursor each tick so the armed pane can show "Capture: <name>" (green) or
  // a "can't capture" hint before the user commits the click — no more blind
  // grab of whatever happens to be under the pointer.
  char hoverName[256];
  bool hoverCapturable;
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

// F-40 — saved per-tab restore order. When a pane's restore routes captures
// through the async CaptureQueue, tabs land in capture-completion order rather
// than the saved order (the saved ordinal is dropped at enqueue time and both
// capture functions append). ApplyPaneState stashes the saved order here for
// such panes; FinalizeRestoreOrder() re-sorts them back once the queue drains.
// Self-contained (no PaneSnapshot dependency) so it can live in the header.
struct RestoreOrderSlot {
  char name[256];
  char actionCmd[128];   // stable command / FX identity — preferred match key
  int  colorIndex;
  bool pinned;
};
struct RestoreOrderPane {
  int tabCount;
  int activeTab;
  RestoreOrderSlot tabs[MAX_TABS_PER_PANE];
};

// Process stale_toggle_actions ExtState entries for the given section.
// Closes any windows REAPER restored at startup that the user had captured
// before quit but Shutdown's toggle didn't reliably switch off (REAPER caches
// wnd_vis BEFORE atexit, so any DoRelease toggle that races with the cache
// is lost). Called from startupTimerFunc for all instance sections before
// any MaxPane container is created.
// deepProbe: use the full FindReaperWindow tree walk for not-yet-visible
// entries. The walk costs ~10-20ms PER ENTRY even on an idle machine (U1:
// EnumWindows + 4 recursive child walks + Win32 module lookups), and the
// startup poll re-probes every entry every ~50ms tick — with a toolbar-heavy
// stale list this stalled REAPER launch for minutes on Windows. Per-tick
// polling now uses a cheap top-level exact-title probe (ghosts REAPER
// restores from wnd_vis are top-level floats, so this catches the classic
// case with the same one-tick latency); the deep walk runs only on the few
// designated sweep ticks to cover dock-frame children and fuzzy titles.
// Returns true if a non-empty stale list was found and processed.
bool ProcessStaleActionsForSection(const char* section, bool deepProbe);

// Remove one action ID from stale_toggle_actions ExtState in EVERY instance
// section. Called when a capture path deliberately opens that action during
// the startup window (workspace restore / favorites recall): an action being
// intentionally opened is by definition not stale, and without this strip the
// startup ghost-cleanup races the recall — CaptureQueue toggles the toolbar
// open, the next cleanup tick sees state==1 and toggles it straight back off,
// so the toolbar is "not recalled if it was not already open" (forum U7).
void RemoveActionFromStaleLists(int action);

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
  // U5-B (ADR-065) — re-run DockWindowAddEx for a docked-mode container that
  // isn't actually inside a dock (an SWS startup action created it before
  // REAPER's docker layout existed → bare 400x300 WS_CHILD behind main).
  // Called once from startupTimerFunc STEP-2 after the settle delay; no-op
  // for floating instances and properly-docked ones.
  void RedockIfDetachedFromDock();
  // U13 (ADR-068) — (re)create splitter brushes from the persisted color
  // preset; called from the ctor and after Settings closes (live apply).
  void RefreshChromeBrushes();
  // U15 (ADR-069) — always-on-top toggle shared by the context menu and the
  // bindable action; floating-only (no-op when docked).
  void ToggleFloatAlwaysOnTop();
  // v2.5.0 (quar_edm #91: "is there an action to get to the settings?") —
  // one entry point for the nav button, the pane menu and the bindable
  // MaxPane_OpenSettings action: runs the modal dialog, then re-applies
  // every pref that can change live (nav bar, tab bars, chrome colors,
  // floating chrome) exactly like the nav button always did.
  void OpenSettings();
  // U15 (ADR-069) — re-apply floating chrome (title + taskbar visibility)
  // after the hide_from_taskbar pref changes; WS_EX_TOOLWINDOW only takes
  // cleanly across a hide/show cycle.
  void RefreshFloatingChrome();
  // U14 (ADR-070) — enqueue the selected track's whole FX chain into paneId.
  // transient=true marks the tabs as follow-mode (never persisted).
  void CaptureTrackFxChain(int paneId, bool transient);
  // F11 (ADR-078) — slot mode: one FX slot per marked pane (see FollowTick).
  void CaptureTrackFxSlots();
  // U14 (ADR-070) — experimental follow-selected-track poll (500ms OnTimer
  // tick): when the last-touched track changes and stays stable for 2 ticks,
  // replace pane 0's transient tabs with the new track's FX chain.
  void FollowTick();
  void Shutdown();
  void Show();
  void Toggle();
  bool IsVisible() const;
  // Sprint 1 Entry 10 — intent-level visibility independent of Win32 parent
  // chain. IsVisible() = IsWindowVisible which on Win32 quit walks the
  // parent chain; the docker is hidden before plugin unload, so a still-
  // open container reports false → was_visible persisted as "0" → next
  // start fails to auto-open. WasIntendedVisible returns m_visible
  // (true between Create/Show and Shutdown). Use at unload-persistence
  // sites only; live action routing still wants OS truth.
  bool WasIntendedVisible() const;

  // F1a (ADR-024) — per-container floating mode. Detach turns the whole
  // container into a top-level OS window with own chrome (title bar, close,
  // resize). Re-dock reattaches to REAPER's docker. Geometry persists in
  // ExtState across sessions and float/dock transitions.
  bool IsFloating() const { return m_floating; }
  void DetachToFloating();
  void RedockToContainer();
  // F-B (forum v2.0.6) — persist current floating geometry to ExtState now.
  // No-op when docked. Public so the atexit handler (Cmd+Q / close-session)
  // and the WM_DESTROY path (docker close button) can save the last on-screen
  // position/size — both bypass Shutdown(), which was the only saver before.
  void PersistFloatingGeometry();

  int InstanceId() const { return m_instanceId; }
  // Per-instance identifiers for ExtState section, REAPER dock ident, and
  // RPP chunk tag. Computed once in constructor — stable lifetime.
  const char* ExtSection()  const { return m_extSection; }
  const char* DockIdent()   const { return m_dockIdent; }
  const char* RppChunkTag() const { return m_rppChunkTag; }

  void ApplyPreset(LayoutPreset preset);
  void SplitPane(int paneId, SplitterOrientation orient);
  // F9 (v2.4.0) — releaseTabs=true is the DELETE_PANE path (today's
  // behavior: stale-list append + release). releaseTabs=false is the new
  // MERGE path: tabs RELOCATE into the nearest sibling leaf (all-or-
  // nothing; grayed in the menu when they don't fit).
  void MergePane(int paneId, bool releaseTabs = true);
  // F9 (v2.4.0) — swap two panes' contents wholesale (works when both are
  // full — the one case Move to Pane can never handle).
  void SwapPanes(int paneA, int paneB);
  // v2.5.0 (LorenzoB #90 "merge sibling up/down/left/right") — relocate this
  // pane's tabs into the ADJACENT pane in that direction (largest one when
  // several touch; SplitTree::NeighborPane) and remove the pane from the
  // tree. dir = SplitTree::PaneDirection. All-or-nothing like Merge.
  void MergePaneToward(int paneId, int dir);
  void MergeFocusedPaneToward(int dir);   // bindable-action entry
  // v2.5.0 (LorenzoB #90 "container lock mode") — LAYOUT EDIT MODE: every
  // captured window is hidden and each pane paints as a labelled card, so
  // the pane structure can be edited "as if the windows weren't there":
  // drag a card onto another pane = swap contents, right-click = pane menu,
  // splitters drag as usual. Session-only UI state; toggled from the nav
  // bar button, the pane menu or MaxPane_ToggleLayoutEdit.
  void ToggleLayoutEditMode();
  bool IsLayoutEditMode() const { return m_layoutEdit; }
  // F12 (ADR-079) — pref-gated focus of the active fx@/takefx@ tab's window
  // after a USER-initiated tab switch (tab click, NextTab/PrevTab). Never
  // called from programmatic switches (restore, follow-mode) — SW_SHOWNA
  // stays the baseline everywhere else.
  void FocusActiveFxTab(int paneId);
  // v2.4.0 "Fit Pane to Window" (owner smoke feedback) — resize the
  // adjacent splitters so this pane matches its active captured window's
  // natural (capture-time) size + header. Kills the host's white filler
  // around fixed-size plugin UIs without touching how windows stretch.
  void FitPaneToWindow(int paneId);
  void ToggleSolo(int paneId);
  bool IsSoloActive() const { return m_soloActive; }
  int NodeForPane(int paneId) const { return m_tree.NodeForPane(paneId); }

  void SaveState();
  void LoadState();

  // Workspace management
  void SaveWorkspace(const char* name);
  // F7 (v2.4.0) — startupAutoLoad=true is the STEP-2.5 startup-workspace
  // fire: it mutes project persistence (see m_projectPersistMuted) so an
  // untouched project is never dirtied by MaxPane alone.
  void LoadWorkspace(const char* name, bool startupAutoLoad = false);
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
  // A3 (v2.4.0) — GetHwnd() can dangle after a docker-X close: WM_DESTROY
  // bypasses Shutdown and deliberately never nulls m_hwnd (B26 mac quit
  // ordering). IsAlive() is the truth test for "instance is open". SWELL
  // IsWindow walks window lists — keep this OFF per-tick/paint paths.
  bool IsAlive() const { return m_hwnd && IsWindow(m_hwnd); }
  // F7 (v2.4.0) — cleared by the first user-initiated action after the
  // startup-workspace auto-load (mouse in container, menu command, Toggle,
  // MaxPane hotkey); until then SaveState skips the project channel so the
  // auto-load never dirties an untouched project (critic finding: the
  // trailing SaveState → MarkProjectDirty would prompt "Save changes?"
  // every session and self-cannibalize the feature after the first save).
  void UnmuteProjectPersist() { m_projectPersistMuted = false; }

  // F-39 — swap an already-open container's contents to the current project's
  // saved state. The force-open timer only Creates a CLOSED container; when
  // MaxPane is already open (e.g. showing the launcher) and a project with
  // saved captures loads, this drops the current captures and reloads.
  void ReloadProjectState();

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
  int NavBarReservedHeight() const {
    if (!m_navBarVisible) return 0;
    // Collapsed (user request 2026-06-10): the chevron sliver still reserves
    // its strip — it must stay clickable (no overlay above captured panes is
    // possible on macOS, ADR-026), but panes reclaim the other 20 px.
    return m_navBarCollapsed ? NavBar::NAV_BAR_COLLAPSED_HEIGHT
                             : NavBar::NAV_BAR_HEIGHT;
  }
  bool IsNavBarVisible() const { return m_navBarVisible; }
  void ToggleNavBarCollapsed();
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
  bool m_userClosing;    // true only inside Toggle-off — see SaveState (U3/ADR-065)
  bool m_floatMaximized; // U2/ADR-066 — floating window was OS-maximized (Win32)
  void* m_followTrack = nullptr;    // U14/ADR-070 — track currently followed
  void* m_followPending = nullptr;  // U14 — candidate awaiting debounce
  int m_followTicks = 0;            // U14 — debounce counter
  bool m_inCreate;       // U2/ADR-066 — Create() in progress: WM_SIZE/WM_MOVE
                         // fired during Win32 dialog construction must not
                         // CaptureFloatGeometry-clobber the geometry that
                         // LoadFloatingState just read (the floating branch
                         // applies it only at the END of Create)
  bool m_needsSettleRefresh = false;  // A2 (v2.4.0) — armed by Create();
                         // first OnTimer tick re-runs RefreshLayout to catch
                         // a docker resize delivered without a real WM_SIZE
  bool m_redockChecked = false;  // A3 (v2.4.0) — deferred dock self-heal ran
  int  m_redockTicks = 0;        // A3 — OnTimer ticks since Create()
  bool m_projectPersistMuted = false;  // F7 — see UnmuteProjectPersist()
  DWORD m_lastDragReposTick = 0;  // v2.4.0 — splitter-drag reposition throttle
  // v2.5.0 — layout edit mode (see ToggleLayoutEditMode). Drag state for
  // the card-to-pane swap gesture: source pane, press point, whether the
  // drag threshold was crossed, and the pane currently under the cursor.
  bool  m_layoutEdit = false;
  int   m_editDragPane = -1;
  POINT m_editDragStart = {};
  bool  m_editDragging = false;
  int   m_editHoverPane = -1;
  CaptureMode m_captureMode;
  DragState m_dragState;
  std::unique_ptr<CaptureQueue> m_captureQueue;
  std::unique_ptr<FavoritesManager> m_favMgr;
  std::unique_ptr<WorkspaceManager> m_wsMgr;
  // F-40 — restore-order deferral (see RestoreOrderPane). One slot per pane;
  // m_paneAwaitingReorder[p] gates whether m_restoreOrder[p] is live.
  RestoreOrderPane m_restoreOrder[MAX_PANES];
  bool m_paneAwaitingReorder[MAX_PANES] = {};
  int m_hoverSplitter;      // branch index of splitter under mouse, -1 when none
  int m_hoverPane;          // pane id of tab under mouse, -1 when none
  int m_hoverTab;           // tab index under mouse, -1 when none; -2 = menu button

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
  bool m_navBarCollapsed = false;     // chevron sliver mode; persisted globally
  int  m_navHover = NavBar::BTN_NONE; // which nav button under cursor (-1 = none)
  int  m_navTooltipBtn = NavBar::BTN_NONE; // which nav button's tooltip is shown
  // Feature A — name of the most recently loaded workspace (empty = none).
  // Rendered centered in the nav bar between the two button groups. Persists
  // to per-instance ExtState as `current_workspace_name` alongside
  // `was_visible` so re-opening into a workspace restores the label.
  // Cleared by: Toggle close, ApplyPreset, deletion of the active workspace
  // (rename updates in-place). Truncated to MAX_WORKSPACE_NAME-1 chars.
  char m_currentWorkspaceName[MAX_WORKSPACE_NAME] = {};
  // Feature A — true when the live tree/captures have been modified since
  // the workspace was loaded or saved. Drawn as a trailing "•" next to the
  // name. Cleared on Save / next Load. Persisted as `workspace_dirty`.
  bool m_workspaceDirty = false;
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

  // Feature A — workspace-name state helpers.
  //
  // SetCurrentWorkspace: success path from LoadWorkspace — copies the name
  // into m_currentWorkspaceName, clears m_workspaceDirty, persists, and
  // repaints the bar strip so the new label shows.
  // ClearCurrentWorkspace: name unset (e.g. Toggle close, workspace
  // delete, ApplyPreset). Persists empty strings.
  // MarkWorkspaceDirty: any mutation that changes the captured/layout
  // state vs. the loaded workspace (capture, release, split, merge, tab
  // move, color change, pin/unpin). No-op if no workspace is loaded —
  // dirty has no meaning when there's no name to compare against.
  // PersistWorkspaceLabel: writes current_workspace_name + workspace_dirty
  // ExtState. Called from the other helpers + load/save paths.
  void SetCurrentWorkspace(const char* name);
  void ClearCurrentWorkspace();
  void MarkWorkspaceDirty();
  void PersistWorkspaceLabel();

  // Hit-test/layout-side NavBar::Compute wrapper (audit M3.6). Builds the
  // NavBar::State from this instance's m_currentWorkspaceName +
  // m_workspaceDirty — the same label source the paint path uses — and
  // passes no HDC, so the workspace-label width comes from the char-count
  // heuristic (the label region is re-measured pixel-accurately during
  // WM_PAINT anyway). Replaces the former module-level label cache in
  // nav_bar.cpp + the legacy single-arg Compute(rect) shim.
  NavBar::Layout ComputeNavBarLayout(const RECT& rc) const;

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
  // m_toastSticky: when true, PaintToast ignores the deadline and the toast
  // sits indefinitely until the next ShowToast / DismissToast call. Feature B
  // uses this for the "Saving '<name>'…" phase whose lifetime is bounded by
  // the actual flush completion rather than a fixed timeout — we don't know
  // a priori whether ExtState will take 50 ms or 500 ms.
  char m_toastMessage[256] = {};
  long long m_toastDeadlineMs = 0;
  bool m_toastSticky = false;

  // Feature B — latest enqueued save name. Held so the post-flush toast
  // can name it ("Saved '<name>'") even after the queue was popped.
  // With 2-deep queuing, the last Enqueue's name is what the user sees
  // in the final confirmation toast (final-save-wins; intermediate names
  // collapse). m_wsMgr->HasPendingSave() is the canonical "is a flush
  // in transit?" predicate — we don't shadow it here.
  char m_pendingWorkspaceName[MAX_WORKSPACE_NAME] = {};
  // Distinguish "new save" vs "replace existing" so the final toast can
  // mirror the synchronous wording from before Feature B (Saved vs Replaced).
  bool m_pendingWorkspaceWasReplace = false;

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
  // F-40 — re-sort any pane flagged in m_paneAwaitingReorder back to its saved
  // order and re-derive color/pinned/active. Called once the capture queue
  // drains, before the post-drain SaveState.
  void FinalizeRestoreOrder();
  void RefreshLayout();
  void StartCaptureTimer();
  void StopCaptureTimerIfIdle();
  // ADR-048 — single arm/disarm entry points so the capture-mode crosshair
  // (SetCaptureCursorActive) is pushed/popped exactly once regardless of which
  // of the three arm sites or several disarm sites fires. EnterCaptureMode
  // also resets the hover-preview state.
  void EnterCaptureMode(int paneId);
  void ExitCaptureMode();

  void OnSize(int cx, int cy);
  // paintRect (ADR-093 #5): the dirty rect from BeginPaint — sections that
  // do not intersect it (nav bar, pane headers, splitters) are skipped.
  void OnPaint(HDC hdc, const RECT* paintRect = nullptr);
  void OnMouseMove(int x, int y);
  void OnLButtonUp(int x, int y);
  void OnTimer();
  // Audit M2.3 — TIMER_ID_CAPTURE pipeline (capture-by-click poll + async
  // queue drain), extracted from the DlgProc WM_TIMER case.
  void OnCaptureTimerTick();
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
  // Feature B — sticky toast variant for the "Saving '<name>'…" indicator:
  // m_toastSticky=true means the deadline-based dismiss is skipped, the
  // bar paints at full opacity, and only the next ShowToast (or a manual
  // DismissToast) replaces it. The TIMER_ID_TOAST fade tick is suppressed
  // while sticky so we don't burn CPU on no-op repaints.
  void ShowToast(const char* msg);
  void ShowStickyToast(const char* msg);
  void PaintToast(HDC hdc, const RECT& clientRect);

  // Feature B — schedule the deferred workspace ExtState write. Called by
  // SaveWorkspace after EnqueueSave has captured the snapshot + toasted
  // "Saving…". Arms a one-shot TIMER_ID_WORKSPACE_FLUSH that fires the
  // FlushWorkspaceTick handler. If a flush is already armed, this is a
  // no-op — the existing tick will drain whatever's in the queue.
  void ArmWorkspaceFlushTimer();
  // Feature B — timer callback. Pops one queue entry via
  // m_wsMgr->FlushNextPendingSave, then either re-arms (queue still has
  // depth) or transitions the toast to "Saved '<name>'".
  void OnWorkspaceFlushTick();

  // Context menu command dispatch
  void HandleTabMenuCommand(int cmd, int paneId, int tabIdx);
  void HandlePaneMenuCommand(int cmd, int paneId);

  // v2.5.0 — shared by Merge into Sibling and the directional merges: move
  // every non-transient tab of src into dst, all-or-nothing (toasts on no
  // room / mid-move fill). Returns true when src ended up empty.
  bool RelocateAllTabs(int srcPane, int dstPane);
  // v2.5.0 — layout edit mode paint + input (see ToggleLayoutEditMode).
  void PaintLayoutEditCards(HDC hdc);
  bool OnLayoutEditLButtonDown(int x, int y);   // true = consumed
  // Drop a held card without acting (menu opened / bound action fired /
  // mode left while the mouse was down) — releases capture, clears drag.
  void CancelLayoutEditDrag();

  static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
