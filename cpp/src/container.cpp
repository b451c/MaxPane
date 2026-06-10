#include "container.h"
#include "hotkey_helper.h"
#include "config.h"
#include "globals.h"
#include "debug.h"
#include "capture_queue.h"
#include "favorites_manager.h"
#include "workspace_manager.h"
#include "context_menu.h"
#include "project_state.h"
#include "state_accessor.h"
#include "swell_cocoa_helpers.h"
#include "launcher.h"
#include "instance_manager.h"
#include "save_workspace_dialog.h"
#include "settings_dialog.h"
#include "updater.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// =========================================================================
// Constructor / lifecycle
// =========================================================================

MaxPaneContainer::MaxPaneContainer(int instanceId)
  : m_instanceId(instanceId)
  , m_hwnd(nullptr)
  , m_visible(false)
  , m_captureQueue(std::make_unique<CaptureQueue>())
  , m_favMgr(std::make_unique<FavoritesManager>())
  , m_wsMgr(std::make_unique<WorkspaceManager>())
  , m_hoverSplitter(-1)
  , m_hoverPane(-1)
  , m_hoverTab(-1)
  , m_pendingRppLoad(false)
{
  // Compute per-instance identifiers. Instance 0 keeps the v1.5.x legacy
  // names so existing user data continues to load without migration.
  if (m_instanceId <= 0) {
    safe_strncpy(m_extSection,   EXT_SECTION,       sizeof(m_extSection));
    safe_strncpy(m_dockIdent,    "MaxPane_container", sizeof(m_dockIdent));
    safe_strncpy(m_rppChunkTag,  "MAXPANE_STATE",     sizeof(m_rppChunkTag));
  } else {
    snprintf(m_extSection,   sizeof(m_extSection),   "MaxPane_cpp_%d",       m_instanceId);
    snprintf(m_dockIdent,    sizeof(m_dockIdent),    "MaxPane_container_%d", m_instanceId);
    snprintf(m_rppChunkTag,  sizeof(m_rppChunkTag),  "MAXPANE_STATE_%d",     m_instanceId);
  }

  m_captureMode.active = false;
  m_captureMode.targetPaneId = -1;
  m_captureMode.prevLmbDown = false;
  m_captureMode.hoverName[0] = '\0';
  m_captureMode.hoverCapturable = false;
  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  DragDock::Reset(m_drag);
  m_winMgr.Init();

  // Propagate section to managers that persist state.
  // Favorites are intentionally SHARED across instances (ADR-009) — leave
  // FavMgr on EXT_SECTION so adding a fav in any instance shows everywhere.
  // Workspaces are per-instance.
  m_wsMgr->SetSection(m_extSection);

  // Create cached GDI objects for static colors
  m_brushTabBarBg = CreateSolidBrush(COLOR_TAB_BAR_BG);
  m_brushTabActive = CreateSolidBrush(COLOR_TAB_ACTIVE_BG);
  m_brushTabInactive = CreateSolidBrush(COLOR_TAB_INACTIVE_BG);
  m_brushEmptyHeader = CreateSolidBrush(COLOR_EMPTY_HEADER_BG);
  m_brushPaneBg = CreateSolidBrush(COLOR_PANE_BG);
  m_brushSplitter = CreateSolidBrush(GetSysColor(COLOR_3DSHADOW));
  m_brushSplitterHover = CreateSolidBrush(COLOR_SPLITTER_HIGHLIGHT);
  m_penTabSeparator = CreatePen(PS_SOLID, 1, COLOR_TAB_SEPARATOR);
  m_penGridLine = CreatePen(PS_SOLID, 1, COLOR_PANE_GRID_LINE);
}

MaxPaneContainer::~MaxPaneContainer()
{
  Shutdown();
  SafeDeleteBrush(m_brushTabBarBg);
  SafeDeleteBrush(m_brushTabActive);
  SafeDeleteBrush(m_brushTabInactive);
  SafeDeleteBrush(m_brushEmptyHeader);
  SafeDeleteBrush(m_brushPaneBg);
  SafeDeleteBrush(m_brushSplitter);
  SafeDeleteBrush(m_brushSplitterHover);
  SafeDeletePen(m_penTabSeparator);
  SafeDeletePen(m_penGridLine);
}

bool MaxPaneContainer::Create()
{
  if (m_hwnd) return true;

  // F1a (ADR-024) — read floating-mode state BEFORE deciding dock vs float.
  // m_floating == true → skip DockWindowAddEx, make HWND top-level instead.
  LoadFloatingState();

  // ADR-026 — read nav bar visibility BEFORE the tree's first Recalculate so
  // pane rects start in the right place. Tree origin reserves the top strip
  // when nav bar is visible.
  LoadNavBarPref();
  m_tree.SetOrigin(0, NavBarReservedHeight());

  m_hwnd = CreateMaxPaneDialog(g_reaperMainHwnd, DlgProc, (LPARAM)this);
  if (!m_hwnd) return false;

  SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
  m_favMgr->Load();
  m_wsMgr->LoadList();   // launcher needs the workspace list available at first paint

  // Feature A — restore the persisted workspace name + dirty flag BEFORE
  // LoadState so the nav bar's first paint shows the label that matches
  // the layout we're about to apply. The pair is written under m_extSection
  // alongside was_visible; absent keys → no workspace loaded (empty label).
  if (g_GetExtState) {
    const char* persistedName  = g_GetExtState(m_extSection, "current_workspace_name");
    const char* persistedDirty = g_GetExtState(m_extSection, "workspace_dirty");
    if (persistedName && persistedName[0]) {
      safe_strncpy(m_currentWorkspaceName, persistedName, sizeof(m_currentWorkspaceName));
      m_workspaceDirty = (persistedDirty && persistedDirty[0] == '1');
    } else {
      m_currentWorkspaceName[0] = '\0';
      m_workspaceDirty = false;
    }
  }

  LoadState();

  if (m_floating) {
    // Floating path — make top-level + restore geometry + apply chrome.
    // SetParent(nullptr) on macOS SWELL recreates the NSWindow (Path B —
    // proven safe in DoRelease); on Win32 the dialog becomes top-level.
    SetParent(m_hwnd, nullptr);
    char title[64];
    if (m_instanceId <= 0) {
      safe_strncpy(title, "MaxPane", sizeof(title));
    } else {
      snprintf(title, sizeof(title), "MaxPane (%d)", m_instanceId + 1);
    }
    ApplyFloatingWindowChrome(m_hwnd, title);
    RECT r = { m_floatX, m_floatY, m_floatX + m_floatW, m_floatY + m_floatH };
    ClampRectToVisibleScreen(&r);
    m_floatX = r.left;
    m_floatY = r.top;
    m_floatW = r.right - r.left;
    m_floatH = r.bottom - r.top;
    SetWindowPos(m_hwnd, nullptr, m_floatX, m_floatY, m_floatW, m_floatH,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    // C5 (ADR-027) — restore persisted always-on-top.
    if (m_floatAlwaysOnTop) SetWindowAlwaysOnTop(m_hwnd, true);
  } else if (g_DockWindowAddEx) {
    g_DockWindowAddEx(m_hwnd, "MaxPane", m_dockIdent, true);
  }

  SetTimer(m_hwnd, TIMER_ID_CHECK, TIMER_INTERVAL, nullptr);
  ShowWindow(m_hwnd, SW_SHOW);
  m_visible = true;

  // F-H2 (forum v2.0.6) — keep tab-bar hover alive when a captured FX/AU plugin
  // tab grabs key-window status (Cocoa only sends mouseMoved to the key window).
  EnableContainerMouseTracking(m_hwnd);

  if (g_SetExtState) {
    g_SetExtState(m_extSection, "was_visible", "1", true);
  }

  // v2.0.4 #3 (ADR-039) — fire-and-forget async update check on first
  // container open. The detached worker thread runs HttpGetSync off the
  // main thread; OnTimer below picks up the result + shows the modal
  // from the main thread (Cocoa NSAlert is main-thread-only). Single-
  // fire guard inside Updater::StartAsyncCheck — additional instance
  // Create() calls are no-ops, so this is safe at every entry point.
  if (IsAutoUpdateEnabled()) {
    Updater::StartAsyncCheck();
  }

  return true;
}

// F1a (ADR-024) — Detach: deregister from REAPER docker, make HWND
// top-level, restore (or default) geometry, apply window chrome. Captured
// tabs follow because they're WS_CHILD of m_hwnd — only m_hwnd's parent
// changes (Path B reparent).
void MaxPaneContainer::DetachToFloating()
{
  if (!m_hwnd || m_floating) return;

  // Default geometry on first detach: position near REAPER main, sized to
  // sensible default. If we have previously-floated geometry persisted,
  // LoadFloatingState already populated m_float* — keep those.
  // Caller may have used the default (100, 100, 800, 600) or restored values.

  if (g_DockWindowRemove) {
    g_DockWindowRemove(m_hwnd);
  }
  SetParent(m_hwnd, nullptr);

  char title[64];
  if (m_instanceId <= 0) {
    safe_strncpy(title, "MaxPane", sizeof(title));
  } else {
    snprintf(title, sizeof(title), "MaxPane (%d)", m_instanceId + 1);
  }
  ApplyFloatingWindowChrome(m_hwnd, title);

  RECT r = { m_floatX, m_floatY, m_floatX + m_floatW, m_floatY + m_floatH };
  ClampRectToVisibleScreen(&r);
  m_floatX = r.left;
  m_floatY = r.top;
  m_floatW = r.right - r.left;
  m_floatH = r.bottom - r.top;
  SetWindowPos(m_hwnd, nullptr, m_floatX, m_floatY, m_floatW, m_floatH,
               SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  ShowWindow(m_hwnd, SW_SHOW);
  ForceViewLayoutAndDisplay(m_hwnd);

  m_floating = true;
  // C5 (ADR-027) — re-apply persisted always-on-top after the orphan
  // NSWindow exists. SetParent(nullptr) earlier recreates the NSWindow
  // with default level; we restore the user's preference.
  if (m_floatAlwaysOnTop) SetWindowAlwaysOnTop(m_hwnd, true);
  SaveFloatingState();
  DBG("[MaxPane] DetachToFloating[%s]: now at (%d,%d %dx%d)\n",
      m_extSection, m_floatX, m_floatY, m_floatW, m_floatH);
}

// F1a (ADR-024) — Re-dock: capture current floating geometry (for next
// detach), re-register in REAPER docker. DockWindowAddEx accepts the same
// HWND and reparents it back into the docker chain.
void MaxPaneContainer::RedockToContainer()
{
  if (!m_hwnd || !m_floating) return;

  CaptureFloatGeometry();  // remember current geometry for next detach

  m_floating = false;
  SaveFloatingState();

  if (g_DockWindowAddEx) {
    g_DockWindowAddEx(m_hwnd, "MaxPane", m_dockIdent, true);
  }
  ShowWindow(m_hwnd, SW_SHOW);
  ForceViewLayoutAndDisplay(m_hwnd);
  DBG("[MaxPane] RedockToContainer[%s]: returned to docker, saved geom (%d,%d %dx%d)\n",
      m_extSection, m_floatX, m_floatY, m_floatW, m_floatH);
}

// Defined in main.cpp — set by onAtExit to prevent Shutdown from overwriting with empty panes
extern "C" bool g_atexitSaved;

void MaxPaneContainer::Shutdown()
{
  if (!m_hwnd) return;

  DBG("[MaxPane] Shutdown: starting, hwnd=%p atexitSaved=%d\n", m_hwnd, g_atexitSaved);

  // Feature B — drain any pending async workspace save synchronously before
  // the host process can exit. The async path moves the ExtState freeze off
  // the click frame; this brings it back at shutdown (where the user is
  // already waiting on REAPER's quit) so no save is silently lost.
  if (m_wsMgr && (m_wsMgr->HasPendingSave() || m_wsMgr->HasPendingLoadList())) {
    DBG("[MaxPane] Shutdown: draining %d pending workspace save(s)\n",
        m_wsMgr->HasPendingSave() ? 1 : 0);
    KillTimer(m_hwnd, TIMER_ID_WORKSPACE_FLUSH);
    m_wsMgr->FlushAllPending();
    m_pendingWorkspaceName[0] = '\0';
  }

  if (!g_atexitSaved) {
    SaveState();
    // F1a (ADR-024) — persist final floating geometry so user's last
    // position is restored next session. Atexit path (Cmd+Q) also needs
    // this in case session ends via NSApplication terminate that bypasses
    // hookcommand 40004 — Toggle's Shutdown captures it inline; atexit's
    // MergeCapturesIntoStaleListForSection path runs on the same atexit
    // lambda that already calls SaveState equivalents.
    if (m_floating) {
      CaptureFloatGeometry();
      SaveFloatingState();
    }
  }

  if (m_captureMode.active) {
    ExitCaptureMode();                    // ADR-048 — pops the capture crosshair
    KillTimer(m_hwnd, TIMER_ID_CAPTURE);  // unconditional during teardown
  }
  m_captureQueue->CancelAll();

  // Log what we're about to release
  for (int p = 0; p < MAX_PANES; p++) {
    const PaneState* ps = m_winMgr.GetPaneState(p);
    if (ps) {
      for (int t = 0; t < ps->tabCount; t++) {
        if (ps->tabs[t].captured) {
          DBG("[MaxPane] Shutdown: releasing pane %d tab %d '%s' action=%d toggleState=%d\n",
              p, t, ps->tabs[t].name[0] ? ps->tabs[t].name : "(null)",
              ps->tabs[t].toggleAction,
              (g_GetToggleCommandState && ps->tabs[t].toggleAction > 0)
                ? g_GetToggleCommandState(ps->tabs[t].toggleAction) : -1);
        }
      }
    }
  }

  m_winMgr.ReleaseAll();
  KillTimer(m_hwnd, TIMER_ID_CHECK);
  DBG("[MaxPane] Shutdown: complete\n");

  if (g_DockWindowRemove) {
    g_DockWindowRemove(m_hwnd);
  }

  DestroyWindow(m_hwnd);
  m_hwnd = nullptr;
  m_visible = false;
}

void MaxPaneContainer::Show()
{
  if (!m_hwnd) { Create(); return; }
  ShowWindow(m_hwnd, SW_SHOW);
  m_visible = true;
}

void MaxPaneContainer::Toggle()
{
  if (!m_hwnd) { Create(); return; }
  DBG("[MaxPane] Toggle: closing\n");
  if (g_SetExtState) {
    g_SetExtState(m_extSection, "was_visible", "0", true);
  }
  // Feature A — closing the container loses any "currently loaded workspace"
  // context; clear the nav bar label so a subsequent re-open without an
  // explicit Load comes up with an empty label (premium auto-open into
  // launcher state — ADR-013).
  ClearCurrentWorkspace();
  // Record currently-captured action IDs so the next REAPER startup can close
  // any ghosts REAPER restores from its wnd_vis cache. A prior LoadWorkspace
  // may already have written entries here for pre-switch windows still
  // floating — the helper merges (doesn't overwrite) so those aren't lost.
  MergeCapturesIntoStaleListForSection(m_extSection, m_winMgr);
  Shutdown();
}

bool MaxPaneContainer::IsVisible() const
{
  return m_hwnd && IsWindowVisible(m_hwnd);
}

bool MaxPaneContainer::WasIntendedVisible() const
{
  // Sprint 1 Entry 10 — m_visible is set true in Create/Show and false in
  // Shutdown. Unlike IsVisible(), it survives Win32's quit-time parent-
  // chain teardown that would otherwise mask a still-open container as
  // "not visible" during the plugin unload write.
  return m_visible;
}

// Whole container empty + no other UI mode active = launcher hero state
// (ADR-013). When true, OnPaint replaces the generic empty-pane UI with
// the workspace launcher hero (cards + capture CTA).
bool MaxPaneContainer::IsInLauncherMode() const
{
  if (m_soloActive) return false;
  // v2.0.6 — keep the launcher visible while capture-by-click is ARMED (was:
  // hidden the instant the CTA was pressed, which read as "nothing happened").
  // It now stays until a window is actually captured (tabCount > 0 below), and
  // the CTA morphs to a "Click a window..." armed state for feedback.
  if (m_dragState.active) return false;
  if (m_tree.GetLeafCount() != 1) return false;
  int paneId = m_tree.GetPaneId(m_tree.GetLeafList()[0]);
  if (paneId < 0) return false;
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (ps && ps->tabCount > 0) return false;
  return true;
}

// =========================================================================
// RefreshLayout
// =========================================================================

void MaxPaneContainer::RefreshLayout()
{
  if (!m_hwnd) return;
  RECT rc;
  GetClientRect(m_hwnd, &rc);
  // ADR-026 — reserve nav bar height at the top. Tree origin already
  // points at NavBarReservedHeight() so the dimensions we pass here are
  // the *available* height for the pane grid.
  int navH = NavBarReservedHeight();
  int availH = (rc.bottom - rc.top) - navH;
  if (availH < 1) availH = 1;
  m_tree.Recalculate(rc.right - rc.left, availH);
  m_winMgr.RepositionAll(m_tree);
  InvalidateRect(m_hwnd, nullptr, FALSE);
}

// =========================================================================
// Capture timer helpers
// =========================================================================

void MaxPaneContainer::StartCaptureTimer()
{
  SetTimer(m_hwnd, TIMER_ID_CAPTURE, TIMER_CAPTURE_INTERVAL, nullptr);
}

void MaxPaneContainer::StopCaptureTimerIfIdle()
{
  if (!m_captureMode.active && !m_captureQueue->HasPending()) {
    KillTimer(m_hwnd, TIMER_ID_CAPTURE);
  }
}

void MaxPaneContainer::EnterCaptureMode(int paneId)
{
  if (paneId < 0) return;
  m_captureMode.active = true;
  m_captureMode.targetPaneId = paneId;
  // Sprint 1 Entry 13 — the arming click is "already down"; init true so the
  // first NEW down-stroke counts as the rising edge that commits the capture.
  m_captureMode.prevLmbDown = true;
  m_captureMode.hoverName[0] = '\0';
  m_captureMode.hoverCapturable = false;
  StartCaptureTimer();
  SetCaptureCursorActive(true);   // ADR-048 — crosshair while armed (macOS)
  InvalidateRect(m_hwnd, nullptr, FALSE);
  DBG("[MaxPane] EnterCaptureMode[%s]: pane=%d\n", m_extSection, paneId);
}

void MaxPaneContainer::ExitCaptureMode()
{
  bool was = m_captureMode.active;
  m_captureMode.active = false;
  m_captureMode.hoverName[0] = '\0';
  m_captureMode.hoverCapturable = false;
  SetCaptureCursorActive(false);  // ADR-048 — restore the normal cursor
  HideCaptureHighlight();         // ADR-048 — drop the hover outline
  StopCaptureTimerIfIdle();
  if (was) {
    InvalidateRect(m_hwnd, nullptr, FALSE);
    DBG("[MaxPane] ExitCaptureMode[%s]\n", m_extSection);
  }
}

// =========================================================================
// Layout preset / Split / Merge
// =========================================================================

void MaxPaneContainer::ApplyPreset(LayoutPreset preset)
{
  if (preset < 0 || preset >= PRESET_COUNT) return;

  // Exit solo before applying preset
  if (m_soloActive) ToggleSolo(m_soloPaneId);

  // Release all windows before rebuilding tree
  m_winMgr.ReleaseAll();

  m_tree.BuildPreset(preset);

  RefreshLayout();
  SaveState();
  // Feature A — applying a layout preset wipes the captured tabs (via
  // ReleaseAll) and rebuilds the tree from a fresh preset. The result no
  // longer represents the loaded workspace's stored layout, so drop the
  // active-workspace association entirely.
  ClearCurrentWorkspace();
}

void MaxPaneContainer::SplitPane(int paneId, SplitterOrientation orient)
{
  // Exit solo before splitting
  if (m_soloActive) ToggleSolo(m_soloPaneId);

  int nodeIdx = m_tree.NodeForPane(paneId);
  if (nodeIdx < 0) return;
  if (m_tree.GetLeafCount() >= MAX_LEAVES) return;

  m_tree.SplitLeaf(nodeIdx, orient, 0.5f);
  RefreshLayout();
  SaveState();
  MarkWorkspaceDirty();  // Feature A — layout no longer matches loaded ws
}

void MaxPaneContainer::MergePane(int paneId)
{
  // Exit solo before merge
  if (m_soloActive) ToggleSolo(m_soloPaneId);

  int nodeIdx = m_tree.NodeForPane(paneId);
  if (nodeIdx < 0) return;
  if (!m_tree.CanMerge(nodeIdx)) return;

  // Release the pane being merged — record action IDs for stale-cleanup
  // BEFORE the release wipes PaneState. Defense-in-depth against the
  // wnd_vis-cache race (B13/B16): even if mid-session toggle succeeds, the
  // startup verify pass is a no-op for state==0 windows; the win comes
  // from the failure case.
  {
    const PaneState* ps = m_winMgr.GetPaneState(paneId);
    if (ps) {
      for (int t = 0; t < ps->tabCount; t++) {
        if (ps->tabs[t].captured && ps->tabs[t].toggleAction > 0) {
          AppendActionToStaleListForSection(m_extSection, ps->tabs[t].toggleAction);
        }
      }
    }
  }
  m_winMgr.ReleaseWindow(paneId);

  m_tree.MergeNode(nodeIdx);
  RefreshLayout();
  SaveState();
  MarkWorkspaceDirty();  // Feature A — layout no longer matches loaded ws
}

void MaxPaneContainer::ToggleSolo(int paneId)
{
  if (m_soloActive) {
    // --- Exit solo ---
    DBG("[MaxPane] ToggleSolo: exiting solo (pane %d)\n", m_soloPaneId);

    // Restore tree from snapshot
    if (!m_tree.LoadSnapshot(m_soloSnapshot, m_soloSnapshotNodeCount)) {
      DBG("[MaxPane] ToggleSolo: snapshot restore failed, resetting\n");
      m_tree.Reset();
    }

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_tree.Recalculate(rc.right - rc.left, rc.bottom - rc.top);

    // Show tabs that were visible before solo
    for (int p = 0; p < MAX_PANES; p++) {
      const PaneState* ps = m_winMgr.GetPaneState(p);
      if (!ps || ps->tabCount == 0) continue;
      if (ps->activeTab >= 0 && ps->activeTab < ps->tabCount) {
        const TabEntry& tab = ps->tabs[ps->activeTab];
        if (tab.captured && tab.hwnd && IsWindow(tab.hwnd)) {
          ShowWindow(tab.hwnd, SW_SHOWNA);
        }
      }
    }

    m_soloActive = false;
    m_soloPaneId = -1;

    m_winMgr.RepositionAll(m_tree);
    InvalidateRect(m_hwnd, nullptr, FALSE);
  } else {
    // --- Enter solo ---
    if (paneId < 0 || !m_tree.IsPaneIdUsed(paneId)) return;

    DBG("[MaxPane] ToggleSolo: entering solo (pane %d)\n", paneId);

    // Save tree snapshot
    m_tree.SaveSnapshot(m_soloSnapshot, m_soloSnapshotNodeCount);

    // Hide all windows in other panes
    for (int p = 0; p < MAX_PANES; p++) {
      const PaneState* ps = m_winMgr.GetPaneState(p);
      if (!ps || ps->tabCount == 0) continue;
      if (p == paneId) continue;
      for (int t = 0; t < ps->tabCount; t++) {
        const TabEntry& tab = ps->tabs[t];
        if (tab.captured && tab.hwnd && IsWindow(tab.hwnd)) {
          ShowWindow(tab.hwnd, SW_HIDE);
        }
      }
    }

    // Reset tree to single leaf with solo pane
    m_tree.Reset();
    // The reset gives paneId 0 — we need to make it use our paneId
    // Instead: just recalculate to give all space to a single node
    // We'll modify the root node to point to the solo pane
    // Actually, simplest: save root paneId 0 and just remap in RepositionAll
    // Better approach: use LoadSnapshot with a minimal single-leaf tree
    {
      NodeSnapshot soloSnap[1];
      memset(soloSnap, 0, sizeof(soloSnap));
      soloSnap[0].type = NODE_LEAF;
      soloSnap[0].paneId = paneId;
      soloSnap[0].parent = -1;
      soloSnap[0].childA = -1;
      soloSnap[0].childB = -1;
      soloSnap[0].ratio = 0.5f;
      m_tree.LoadSnapshot(soloSnap, 1);
    }

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_tree.Recalculate(rc.right - rc.left, rc.bottom - rc.top);

    m_soloActive = true;
    m_soloPaneId = paneId;

    m_winMgr.RepositionAll(m_tree);
    InvalidateRect(m_hwnd, nullptr, FALSE);
  }
}

// =========================================================================
// Timer
// =========================================================================

void MaxPaneContainer::OnTimer()
{
  // v2.0.4 #3 (ADR-039) — drain the async update-check result. Atomic
  // read; HandleUpdateAvailable has its own single-fire guard so calling
  // every tick is cheap + safe. Only fires the modal once per session.
  if (Updater::PollAsyncResult() == Updater::AsyncResult::UpdateAvailable) {
    Updater::HandleUpdateAvailable(m_hwnd);
  }

  // Check if deferred RPP state has become available.
  // REAPER parses the RPP <MAXPANE_STATE> chunk asynchronously —
  // it may arrive after Create()/LoadState() already ran.
  if (m_pendingRppLoad && g_pendingProjectState[m_instanceId].valid) {
    DBG("[MaxPane] OnTimer: deferred RPP state now available (%d lines), applying\n",
        g_pendingProjectState[m_instanceId].lineCount);
    m_pendingRppLoad = false;

    RppReadAccessor rppAcc(g_pendingProjectState[m_instanceId].lines,
                           g_pendingProjectState[m_instanceId].lineCount);
    const char* treeVer = rppAcc.Get(m_extSection, "tree_version");
    bool hasTreeFormat = (treeVer && strcmp(treeVer, "2") == 0);
    if (hasTreeFormat) {
      NodeSnapshot snap[MAX_TREE_NODES];
      memset(snap, 0, sizeof(snap));
      int nodeCount = WorkspaceManager::ReadTreeNodesStatic(m_extSection, "", snap, rppAcc);
      if (nodeCount > 0) {
        PaneSnapshot panes[MAX_PANES];
        memset(panes, 0, sizeof(panes));
        WorkspaceManager::ReadPaneTabsStatic(m_extSection, "", panes, MAX_PANES, rppAcc);

        // Release any windows from default state before applying RPP state
        m_captureQueue->CancelAll();
        m_winMgr.ReleaseAll(false);

        if (!m_tree.LoadSnapshot(snap, nodeCount)) {
          DBG("[MaxPane] OnTimer: deferred RPP tree corrupt, resetting\n");
          m_tree.Reset();
        }

        RECT rc;
        GetClientRect(m_hwnd, &rc);
        m_tree.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
        ApplyPaneState(panes, MAX_PANES, true);
        m_winMgr.RepositionAll(m_tree);
        InvalidateRect(m_hwnd, nullptr, FALSE);

        DBG("[MaxPane] OnTimer: deferred RPP state applied (nodes=%d)\n", nodeCount);
      }
    }
    g_pendingProjectState[m_instanceId].valid = false;  // consumed
  }

  if (m_winMgr.CheckAlive()) {
    m_winMgr.RepositionAll(m_tree);
    InvalidateRect(m_hwnd, nullptr, FALSE);
    SaveState();
  }
}

// =========================================================================
// Context menu (builds menu via context_menu module, dispatches commands)
// =========================================================================

void MaxPaneContainer::OnContextMenu(int x, int y)
{
  // Sprint 2.5 — right-click on a launcher workspace card opens a card menu
  // (Load / Rename / Duplicate / Delete / Bind Hotkey). Right-click on the
  // launcher background still falls through to the standard pane menu.
  if (IsInLauncherMode()) {
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    cr.top += NavBarReservedHeight();  // ADR-048 — match Paint's launcher origin
    m_wsMgr->LoadList();
    Launcher::Layout lay = Launcher::Compute(cr, *m_wsMgr);
    const int hit = Launcher::HitTest(lay, x, y);
    if (hit >= 0) {
      OpenLauncherCardMenu(hit, x, y);
      return;
    }
  }

  // ADR-026 — Home overlay also paints workspace cards (reused launcher
  // primitives) over the current layout. Right-click on a card there must
  // open the same card menu, not the underlying pane menu. Without this
  // the overlay's right-click leaked through to whichever pane was under
  // the card. Geometry below nav bar matches OnHomeOverlayClick.
  if (m_homeOverlay) {
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    RECT below = cr;
    below.top += NavBarReservedHeight();
    m_wsMgr->LoadList();
    Launcher::Layout lay = Launcher::Compute(below, *m_wsMgr);
    const int hit = Launcher::HitTest(lay, x, y);
    if (hit >= 0) {
      OpenLauncherCardMenu(hit, x, y);
      return;
    }
    // Right-click outside any card on the overlay = cancel overlay
    // (mirrors Esc / left-click-outside behaviour). Eat the event so it
    // doesn't fall through to pane menu under the overlay backdrop.
    CloseHomeOverlay();
    return;
  }

  int paneId = PaneAtPoint(x, y);
  if (paneId < 0) return;

  // Check if click is on tab bar
  int tabIdx = TabHitTest(paneId, x, y);
  if (tabIdx >= 0) {
    HMENU menu = BuildTabContextMenu(paneId, tabIdx, m_tree, m_winMgr, *m_favMgr);
    if (!menu) return;

    POINT pt = {x, y};
    ClientToScreen(m_hwnd, &pt);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    // Clear stale hover state — mouse has moved during menu interaction
    m_hoverPane = -1;
    m_hoverTab = -1;
    m_hoverSplitter = -1;

    HandleTabMenuCommand(cmd, paneId, tabIdx);
    return;
  }

  // If in capture mode and user clicked inside the container, cancel
  if (m_captureMode.active) {
    ExitCaptureMode();  // ADR-048 — pops the capture crosshair
    return;
  }

  m_wsMgr->LoadList();
  HMENU menu = BuildPaneContextMenu(paneId, m_hwnd, m_tree, m_winMgr, *m_favMgr, *m_wsMgr, m_soloActive, m_floating, m_floatAlwaysOnTop);
  if (!menu) return;

  POINT pt = {x, y};
  ClientToScreen(m_hwnd, &pt);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
  DestroyMenu(menu);

  m_hoverPane = -1;
  m_hoverTab = -1;
  m_hoverSplitter = -1;

  HandlePaneMenuCommand(cmd, paneId);
}

void MaxPaneContainer::OnPaneMenuButtonClick(int paneId, int x, int y)
{
  m_wsMgr->LoadList();
  HMENU menu = BuildPaneContextMenu(paneId, m_hwnd, m_tree, m_winMgr, *m_favMgr, *m_wsMgr, m_soloActive, m_floating, m_floatAlwaysOnTop);
  if (!menu) return;

  POINT pt = {x, y};
  ClientToScreen(m_hwnd, &pt);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
  DestroyMenu(menu);

  m_hoverPane = -1;
  m_hoverTab = -1;
  m_hoverSplitter = -1;

  HandlePaneMenuCommand(cmd, paneId);
}

void MaxPaneContainer::OpenLauncherCardMenu(int cardIdx, int x, int y)
{
  if (cardIdx < 0 || cardIdx >= m_wsMgr->GetCount()) return;

  // Cache the workspace name BEFORE building the menu — Delete reshuffles
  // indices so we can't safely re-read via cardIdx after dispatch.
  const WorkspaceEntry srcEntry = m_wsMgr->Get(cardIdx);

  HMENU menu = CreatePopupMenu();
  if (!menu) return;

  int pos = 0;

  // Disabled header showing workspace name + slot number (for hotkey reference).
  char header[128];
  snprintf(header, sizeof(header), "%s  [Slot %d]", srcEntry.name, cardIdx + 1);
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.fState = MFS_DISABLED;
    mi.dwTypeData = header;
    InsertMenuItem(menu, pos++, TRUE, &mi);
  }
  {
    MENUITEMINFO sep = {};
    sep.cbSize = sizeof(sep);
    sep.fMask = MIIM_TYPE;
    sep.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, pos++, TRUE, &sep);
  }

  struct Item { int id; const char* label; };
  const Item items[] = {
    { MenuIds::LAUNCHER_CARD_LOAD,        "Load" },
    { MenuIds::LAUNCHER_CARD_RENAME,      "Rename..." },
    { MenuIds::LAUNCHER_CARD_DUPLICATE,   "Duplicate..." },
    { MenuIds::LAUNCHER_CARD_DELETE,      "Delete" },
    { MenuIds::LAUNCHER_CARD_BIND_HOTKEY, "Bind Hotkey..." },
  };
  for (const Item& it : items) {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = it.id;
    mi.dwTypeData = (char*)it.label;
    InsertMenuItem(menu, pos++, TRUE, &mi);
  }

  POINT pt = { x, y };
  ClientToScreen(m_hwnd, &pt);
  const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                 pt.x, pt.y, 0, m_hwnd, nullptr);
  DestroyMenu(menu);

  switch (cmd) {
    case MenuIds::LAUNCHER_CARD_LOAD:
      LoadWorkspace(srcEntry.name);
      break;

    case MenuIds::LAUNCHER_CARD_RENAME: {
      char buf[MAX_WORKSPACE_NAME];
      safe_strncpy(buf, srcEntry.name, sizeof(buf));
      if (g_GetUserInputs &&
          g_GetUserInputs("Rename Workspace", 1, "Name:,extrawidth=200",
                          buf, sizeof(buf)) &&
          buf[0]) {
        if (strcmp(buf, srcEntry.name) == 0) break;   // no-op
        char msg[160];
        if (m_wsMgr->Rename(cardIdx, buf)) {
          // Feature A — if the renamed workspace was the active one,
          // update the nav bar label to match.
          if (m_currentWorkspaceName[0] &&
              strcmp(m_currentWorkspaceName, srcEntry.name) == 0) {
            safe_strncpy(m_currentWorkspaceName, buf, sizeof(m_currentWorkspaceName));
            PersistWorkspaceLabel();
          }
          snprintf(msg, sizeof(msg), "Renamed to '%s'", buf);
          ShowToast(msg);
          InvalidateRect(m_hwnd, nullptr, FALSE);
        } else {
          snprintf(msg, sizeof(msg), "Workspace '%s' already exists", buf);
          ShowToast(msg);
        }
      }
      break;
    }

    case MenuIds::LAUNCHER_CARD_DUPLICATE: {
      char buf[MAX_WORKSPACE_NAME];
      // Bounded %.*s so GCC can prove " copy" always fits (the truncation is
      // intentional — buf is the workspace-name limit; -Werror=format-truncation).
      snprintf(buf, sizeof(buf), "%.*s copy", (int)(sizeof(buf) - 6),
               srcEntry.name);
      if (g_GetUserInputs &&
          g_GetUserInputs("Duplicate Workspace", 1, "Name:,extrawidth=200",
                          buf, sizeof(buf)) &&
          buf[0]) {
        char msg[160];
        if (m_wsMgr->Duplicate(cardIdx, buf)) {
          snprintf(msg, sizeof(msg), "Duplicated to '%s'", buf);
          ShowToast(msg);
          InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (m_wsMgr->Find(buf)) {
          snprintf(msg, sizeof(msg), "Workspace '%s' already exists", buf);
          ShowToast(msg);
        } else {
          snprintf(msg, sizeof(msg), "Workspace limit reached (%d)", MAX_WORKSPACES);
          ShowToast(msg);
        }
      }
      break;
    }

    case MenuIds::LAUNCHER_CARD_DELETE: {
      char msg[160];
      snprintf(msg, sizeof(msg), "Deleted '%s'", srcEntry.name);
      // Feature A — DeleteWorkspace clears m_currentWorkspaceName if the
      // deleted entry is the active one. Going through the wrapper keeps
      // that branch on a single path.
      DeleteWorkspace(srcEntry.name);
      ShowToast(msg);
      InvalidateRect(m_hwnd, nullptr, FALSE);
      break;
    }

    case MenuIds::LAUNCHER_CARD_BIND_HOTKEY:
      // v2.0.4 #2 (ADR-038) — REAPER's native shortcut-edit dialog opens
      // scoped to THIS workspace slot's command ID, replacing the legacy
      // "hunt MaxPane_WsSlot_<N+1> through 200k actions" UX.
      MaxPane_OpenHotkeyDialogForWsSlot(m_hwnd, cardIdx);
      break;
  }
}

// =========================================================================
// Toast bar — transient feedback for non-fatal events (Sprint 3.2)
// =========================================================================

void MaxPaneContainer::ShowToast(const char* msg)
{
  if (!msg) msg = "";
  safe_strncpy(m_toastMessage, msg, sizeof(m_toastMessage));
  m_toastDeadlineMs = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count() + TOAST_DURATION_MS;
  // Feature B — a fresh (non-sticky) ShowToast clears any prior sticky state.
  // This is the path the WM_TIMER fade-out runs through: when the flush
  // tick replaces "Saving…" with "Saved", we want normal fade behaviour
  // back, not the indefinite hold.
  m_toastSticky = false;
  if (m_hwnd) {
    SetTimer(m_hwnd, TIMER_ID_TOAST, TOAST_TICK_MS, nullptr);
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    RECT bar = cr;
    bar.top = cr.bottom - TOAST_BAR_HEIGHT;
    if (bar.top < cr.top) bar.top = cr.top;
    InvalidateRect(m_hwnd, &bar, FALSE);
  }
}

// Feature B — sticky variant for the "Saving '<X>'…" indicator. The toast
// sits at full opacity until the next ShowToast / ShowStickyToast call
// (typically the post-flush "Saved" message). No TIMER_ID_TOAST is armed
// — there's nothing to animate while sticky, and the next ShowToast
// replaces sticky with the normal fade-out path.
void MaxPaneContainer::ShowStickyToast(const char* msg)
{
  if (!msg) msg = "";
  safe_strncpy(m_toastMessage, msg, sizeof(m_toastMessage));
  // Use a far-future deadline so PaintToast's "now >= deadline" reap
  // branch never fires, and the fade-alpha math inside PaintToast keeps
  // alpha at 255 because msLeft is always far larger than TOAST_FADE_MS.
  m_toastDeadlineMs = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count() + 24LL * 60LL * 60LL * 1000LL;
  m_toastSticky = true;
  // No SetTimer — sticky toasts don't fade, so the per-tick repaint is
  // wasteful. The next ShowToast (or DismissToast) re-arms the timer.
  if (m_hwnd) {
    KillTimer(m_hwnd, TIMER_ID_TOAST);
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    RECT bar = cr;
    bar.top = cr.bottom - TOAST_BAR_HEIGHT;
    if (bar.top < cr.top) bar.top = cr.top;
    InvalidateRect(m_hwnd, &bar, FALSE);
  }
}

// Feature B — arm the one-shot flush timer. Called from SaveWorkspace
// (after EnqueueSave) and re-armed by OnWorkspaceFlushTick when the queue
// still has depth.
void MaxPaneContainer::ArmWorkspaceFlushTimer()
{
  if (!m_hwnd) return;
  // SetTimer with the same id is idempotent on both Win32 and SWELL
  // (replaces existing timer with a new period). This means a rapid burst
  // of Enqueue calls only ever drives one outstanding flush timer, which
  // is what we want — the timer doesn't need to fire N times for N
  // enqueues; one fire per Flush is enough, and Flush re-arms itself.
  SetTimer(m_hwnd, TIMER_ID_WORKSPACE_FLUSH, WORKSPACE_FLUSH_DELAY_MS, nullptr);
}

// Feature B — Phase 2. Pops one queue entry, writes ExtState, then either
// re-arms (queue still deeper than 1) or transitions the toast to the
// completion message ("Saved" / "Replaced") + clears pending flag.
void MaxPaneContainer::OnWorkspaceFlushTick()
{
  KillTimer(m_hwnd, TIMER_ID_WORKSPACE_FLUSH);

  // Drain any pending LoadList first so a fresh launcher repaint after
  // the flush sees the freshest list. EnqueueLoadList isn't currently
  // called by any synchronous path inside MaxPane, but the helper exists
  // so external (e.g. future inter-instance sync) paths can defer reads
  // the same way Save defers writes.
  m_wsMgr->FlushPendingLoadList();

  const bool flushed = m_wsMgr->FlushNextPendingSave();
  if (!flushed) {
    // Nothing was queued — defensive cleanup, shouldn't normally happen.
    m_pendingWorkspaceName[0] = '\0';
    return;
  }

  if (m_wsMgr->HasPendingSave()) {
    // Queue depth was 2 — drain on the next tick. Toast stays sticky.
    DBG("[MaxPane] OnWorkspaceFlushTick: queue still deep, re-arming\n");
    ArmWorkspaceFlushTimer();
    return;
  }

  // Queue drained — flip toast from sticky "Saving…" to timed completion.
  char msg[MAX_WORKSPACE_NAME + 64];
  snprintf(msg, sizeof(msg),
           m_pendingWorkspaceWasReplace ? "Replaced '%s'" : "Saved '%s'",
           m_pendingWorkspaceName[0] ? m_pendingWorkspaceName : "workspace");
  ShowToast(msg);
  m_pendingWorkspaceName[0] = '\0';
  // Launcher cards may need a repaint if the workspace count changed.
  // ShowToast already invalidated the toast strip; nuke the rest as well
  // since launcher cards live above the bar.
  InvalidateRect(m_hwnd, nullptr, FALSE);
  DBG("[MaxPane] OnWorkspaceFlushTick: flush complete, toast='%s'\n", msg);
}

void MaxPaneContainer::PaintToast(HDC hdc, const RECT& clientRect)
{
  if (m_toastDeadlineMs == 0 || m_toastMessage[0] == '\0') return;

  const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  if (now >= m_toastDeadlineMs) return;   // timer will reap and repaint

  const long long msLeft = m_toastDeadlineMs - now;

  // Fade alpha 0..255 — linear ramp during the trailing TOAST_FADE_MS window.
  // Sticky toasts (Feature B's "Saving…" indicator) skip the fade entirely:
  // we hold them at full opacity until the next ShowToast replaces them
  // with a timed message.
  int alpha = 255;
  if (!m_toastSticky && msLeft < TOAST_FADE_MS) {
    alpha = (int)((msLeft * 255) / TOAST_FADE_MS);
    if (alpha < 0) alpha = 0;
  }

  // SWELL FillRect has no per-pixel alpha; approximate the fade by lerping
  // the foreground text color toward the bar background as alpha → 0.
  RECT bar = clientRect;
  bar.top = clientRect.bottom - TOAST_BAR_HEIGHT;
  if (bar.top < clientRect.top) bar.top = clientRect.top;

  HBRUSH bg = CreateSolidBrush(COLOR_TAB_BAR_BG);
  if (bg) {
    FillRect(hdc, &bar, bg);
    DeleteObject(bg);
  }

  auto lerp = [&](int from, int to) -> int {
    return to + (from - to) * alpha / 255;
  };
  // Shift+mask instead of GetR/G/BValue: the macros' BYTE/WORD casts on a
  // compile-time COLORREF constant trip MSVC C4310 under the /WX gate.
  const int rF = (int)(COLOR_TAB_ACTIVE_TEXT & 0xFF);
  const int gF = (int)((COLOR_TAB_ACTIVE_TEXT >> 8) & 0xFF);
  const int bF = (int)((COLOR_TAB_ACTIVE_TEXT >> 16) & 0xFF);
  const int rB = (int)(COLOR_TAB_BAR_BG & 0xFF);
  const int gB = (int)((COLOR_TAB_BAR_BG >> 8) & 0xFF);
  const int bB = (int)((COLOR_TAB_BAR_BG >> 16) & 0xFF);
  SetTextColor(hdc, RGB(lerp(rF, rB), lerp(gF, gB), lerp(bF, bB)));
  SetBkMode(hdc, TRANSPARENT);

  RECT textRect = bar;
  textRect.left += 12;
  textRect.right -= 12;
  DrawTextUtf8(hdc, m_toastMessage, -1, &textRect,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void MaxPaneContainer::HandleTabMenuCommand(int cmd, int paneId, int tabIdx)
{
  if (cmd == MenuIds::TAB_CLOSE) {
    // Record action ID before close — see ReleaseWindow path above for why.
    if (const TabEntry* t = m_winMgr.GetTab(paneId, tabIdx)) {
      if (t->captured && t->toggleAction > 0) {
        AppendActionToStaleListForSection(m_extSection, t->toggleAction);
      }
    }
    RecordClosedTab(paneId, tabIdx);  // C1 — capture snapshot before destroy
    m_winMgr.CloseTab(paneId, tabIdx);
    RefreshLayout();
    SaveState();
    MarkWorkspaceDirty();  // Feature A
  } else if (cmd == MenuIds::TAB_RELEASE) {
    // v2.0.6 — Release: detach the captured window back to REAPER as a visible
    // floating window. Deliberately NO AppendActionToStaleListForSection and NO
    // RecordClosedTab: the window stays open under REAPER's own control, so it
    // must NOT be queued for the startup ghost-cleanup (B15/B17 would close it
    // next launch) and it is not a "closed tab" to reopen.
    m_winMgr.CloseTab(paneId, tabIdx, /*returnVisible=*/true);
    RefreshLayout();
    SaveState();
    MarkWorkspaceDirty();  // Feature A
  } else if (cmd == MenuIds::TAB_TOGGLE_PINNED) {
    // C2 (ADR-027) — flip pinned, then SetTabPinned re-sorts the pane.
    const TabEntry* t = m_winMgr.GetTab(paneId, tabIdx);
    if (t) {
      m_winMgr.SetTabPinned(paneId, tabIdx, !t->pinned);
      RefreshLayout();
      SaveState();
      MarkWorkspaceDirty();  // Feature A
    }
  } else if (cmd >= MenuIds::TAB_MOVE_BASE && cmd < MenuIds::TAB_MOVE_BASE + MAX_PANES) {
    int targetPane = cmd - MenuIds::TAB_MOVE_BASE;
    m_winMgr.MoveTab(paneId, tabIdx, targetPane);
    RefreshLayout();
    SaveState();
    MarkWorkspaceDirty();  // Feature A
  } else if (cmd >= MenuIds::TAB_COLOR_BASE && cmd < MenuIds::TAB_COLOR_BASE + TAB_COLOR_COUNT) {
    int newColor = cmd - MenuIds::TAB_COLOR_BASE;
    m_winMgr.SetTabColor(paneId, tabIdx, newColor);
    // Targeted: only invalidate the changed tab's rect
    RECT tr = GetTabRect(paneId, tabIdx);
    if (tr.right > tr.left) InvalidateRect(m_hwnd, &tr, FALSE);
    else InvalidateRect(m_hwnd, nullptr, FALSE);
    SaveState();
    MarkWorkspaceDirty();  // Feature A
  } else if (cmd == MenuIds::FAV_ADD) {
    const TabEntry* tab = m_winMgr.GetTab(paneId, tabIdx);
    if (tab && tab->captured && tab->name[0]) {
      char actionCmd[128] = "";
      if (tab->isArbitrary) {
        if (tab->actionCmd[0]) {
          safe_strncpy(actionCmd, tab->actionCmd, sizeof(actionCmd));
        } else if (g_GetUserInputs) {
          char inputBuf[128] = "";
          if (g_GetUserInputs("Add to Favorites", 1,
                              "Action ID or _command (for auto-reopen):",
                              inputBuf, sizeof(inputBuf))) {
            if (inputBuf[0]) {
              safe_strncpy(actionCmd, inputBuf, sizeof(actionCmd));
            }
          }
        }
      } else {
        GetActionCommandString(tab->toggleAction, actionCmd, sizeof(actionCmd));
      }
      m_favMgr->Add(tab->name,
                     tab->searchTitle[0] ? tab->searchTitle : tab->name,
                     actionCmd,
                     !tab->isArbitrary);
    }
  }
}

void MaxPaneContainer::HandlePaneMenuCommand(int cmd, int paneId)
{
  // Layout preset
  if (cmd >= MenuIds::LAYOUT_BASE && cmd < MenuIds::LAYOUT_BASE + PRESET_COUNT) {
    ApplyPreset((LayoutPreset)(cmd - MenuIds::LAYOUT_BASE));
    return;
  }

  // Known window selection — async capture
  if (cmd >= MenuIds::KNOWN_BASE && cmd < MenuIds::KNOWN_BASE + NUM_KNOWN_WINDOWS) {
    int idx = cmd - MenuIds::KNOWN_BASE;

    DBG("[MaxPane] Menu: selected '%s' for pane %d\n", KNOWN_WINDOWS[idx].name, paneId);

    HWND found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[idx].searchTitle, m_hwnd);
    if (!found && KNOWN_WINDOWS[idx].altSearchTitle) {
      found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[idx].altSearchTitle, m_hwnd);
    }

    if (found) {
      m_winMgr.CaptureByIndex(paneId, idx, m_hwnd);
      RefreshLayout();
      SaveState();
      MarkWorkspaceDirty();  // Feature A
    } else if (g_Main_OnCommand) {
      m_captureQueue->EnqueueKnown(paneId, idx);
      StartCaptureTimer();
      MarkWorkspaceDirty();  // Feature A
    }
    return;
  }

  // Open Windows selection
  if (cmd >= MenuIds::OPEN_WINDOWS_BASE && cmd < MenuIds::OPEN_WINDOWS_MAX) {
    int idx = cmd - MenuIds::OPEN_WINDOWS_BASE;
    if (idx >= 0 && idx < GetOpenWindowCount()) {
      const OpenWindowEntry& owe = GetOpenWindow(idx);
      if (!owe.hwnd || !IsWindow(owe.hwnd)) return;
      HWND targetHwnd = owe.hwnd;
      char title[256];
      safe_strncpy(title, owe.title, sizeof(title));

      HWND dockFrame = nullptr;
      targetHwnd = WindowManager::ResolveDockFrameChild(targetHwnd, title, &dockFrame);
      if (targetHwnd && IsWindow(targetHwnd)) {
        int toggleAction = LookupToggleAction(title);
        // Audit M1.3 — hide the dock frame ONLY when the capture committed.
        // The old unconditional hide made the user's window vanish (not in
        // REAPER, not in MaxPane) whenever the pane was full.
        bool captured = m_winMgr.CaptureArbitraryWindow(paneId, targetHwnd, title,
                                                        m_hwnd, toggleAction);
        if (captured && dockFrame && IsWindow(dockFrame)) {
          ShowWindow(dockFrame, SW_HIDE);
        }
        if (!captured) {
          // ADR-061 amendment — prefer the gate's specific reason.
          const char* reason = WindowManager::TakeCaptureRefusal();
          ShowToast(reason ? reason
            : "Couldn't capture — pane is full or window is already captured.");
        }

        RefreshLayout();
        SaveState();
        MarkWorkspaceDirty();  // Feature A
      }
    }
    return;
  }

  // Capture by Click
  if (cmd == MenuIds::CAPTURE_BY_CLICK) {
    EnterCaptureMode(paneId);  // ADR-048 — single arm path (crosshair + preview)
    return;
  }

  // Split Horizontal
  if (cmd == MenuIds::SPLIT_H) {
    SplitPane(paneId, SPLIT_HORIZONTAL);
    return;
  }

  // Split Vertical
  if (cmd == MenuIds::SPLIT_V) {
    SplitPane(paneId, SPLIT_VERTICAL);
    return;
  }

  // Merge with Sibling
  if (cmd == MenuIds::MERGE) {
    MergePane(paneId);
    return;
  }

  // Delete Pane (release all tabs + merge)
  if (cmd == MenuIds::DELETE_PANE) {
    MergePane(paneId);  // MergePane already calls ReleaseWindow which releases all tabs
    return;
  }

  // Solo Pane (maximize/restore)
  if (cmd == MenuIds::SOLO) {
    ToggleSolo(m_soloActive ? m_soloPaneId : paneId);
    return;
  }

  // Close (release active tab only)
  if (cmd == MenuIds::RELEASE) {
    // v2.0.6 — Release Window: detach the active tab's captured window back to
    // REAPER as a free-floating VISIBLE window. Deliberately NO
    // AppendActionToStaleListForSection (unlike the close paths) — the window
    // stays open under REAPER's own control, so queuing its action for the
    // startup ghost-cleanup (B15/B17) would silently close it on next launch.
    const PaneState* ps = m_winMgr.GetPaneState(paneId);
    if (ps && ps->activeTab >= 0) {
      m_winMgr.CloseTab(paneId, ps->activeTab, /*returnVisible=*/true);
    }
    RefreshLayout();
    SaveState();
    MarkWorkspaceDirty();  // Feature A
    return;
  }

  // Auto-open toggle (legacy menu id — kept for v1.5.x bindings that may
  // reference it; functionally moved to Settings dialog per ADR-019).
  if (cmd == MenuIds::AUTO_OPEN) {
    SetAutoOpenEnabled(!IsAutoOpenEnabled());
    return;
  }

  // Settings dialog
  if (cmd == MenuIds::SETTINGS) {
    OpenSettingsDialog(m_hwnd);
    InvalidateRect(m_hwnd, nullptr, FALSE);  // dark mode change repaint
    return;
  }

  // F1a (ADR-024) — Toggle floating mode for whole container.
  if (cmd == MenuIds::TOGGLE_FLOATING) {
    if (m_floating) RedockToContainer();
    else            DetachToFloating();
    return;
  }

  // C5 (ADR-027) — Always on top (only meaningful when floating).
  if (cmd == MenuIds::TOGGLE_FLOAT_ALWAYS_ON_TOP) {
    if (!m_floating) return;  // defensive: menu hides this when not floating
    m_floatAlwaysOnTop = !m_floatAlwaysOnTop;
    SetWindowAlwaysOnTop(m_hwnd, m_floatAlwaysOnTop);
    SaveFloatingState();
    return;
  }

  // Close MaxPane container
  if (cmd == MenuIds::CLOSE_CONTAINER) {
    Toggle();  // Toggle sets was_visible=0 before Shutdown
    return;
  }

  // Favorites — capture from favorite
  if (cmd >= MenuIds::FAV_BASE && cmd < MenuIds::FAV_BASE + MAX_FAVORITES) {
    ActivateFavorite(cmd - MenuIds::FAV_BASE, paneId);
    return;
  }

  // Favorites — delete
  if (cmd >= MenuIds::FAV_DELETE_BASE && cmd < MenuIds::FAV_DELETE_BASE + MAX_FAVORITES) {
    int favIdx = cmd - MenuIds::FAV_DELETE_BASE;
    m_favMgr->Remove(favIdx);
    return;
  }

  // Workspace load
  if (cmd >= MenuIds::WS_LOAD_BASE && cmd < MenuIds::WS_LOAD_BASE + MAX_WORKSPACES) {
    int idx = cmd - MenuIds::WS_LOAD_BASE;
    if (idx >= 0 && idx < m_wsMgr->GetCount()) {
      const WorkspaceEntry& ws = m_wsMgr->Get(idx);
      if (ws.used) {
        LoadWorkspace(ws.name);
      }
    }
    return;
  }

  // Workspace save — modal dialog (Sprint 3.3 / ADR-020).
  // Feature B — async path: SaveWorkspace enqueues + arms the deferred flush
  // timer. The completion toast ("Saved '<X>'" / "Replaced '<X>'") fires
  // from OnWorkspaceFlushTick once the ExtState write actually completes.
  // The sticky "Saving '<X>'…" indicator below makes the deferral visible
  // for users with large workspaces where the click→flush window is long
  // enough to perceive (>30 ms).
  if (cmd == MenuIds::WS_SAVE) {
    m_wsMgr->LoadList();  // freshen list before showing
    char wsName[MAX_WORKSPACE_NAME] = "";
    if (OpenSaveWorkspaceDialog(m_hwnd, *m_wsMgr, wsName, sizeof(wsName))) {
      char savingMsg[MAX_WORKSPACE_NAME + 64];
      snprintf(savingMsg, sizeof(savingMsg), "Saving '%s'\xe2\x80\xa6", wsName);
      ShowStickyToast(savingMsg);
      SaveWorkspace(wsName);
    }
    return;
  }

  // Workspace delete
  if (cmd >= MenuIds::WS_DELETE_BASE && cmd < MenuIds::WS_DELETE_BASE + MAX_WORKSPACES) {
    int idx = cmd - MenuIds::WS_DELETE_BASE;
    if (idx >= 0 && idx < m_wsMgr->GetCount()) {
      const WorkspaceEntry& ws = m_wsMgr->Get(idx);
      if (ws.used) {
        DeleteWorkspace(ws.name);
      }
    }
    return;
  }
}

// =========================================================================
// Dialog Procedure
// =========================================================================

// Audit M2.3 — the TIMER_ID_CAPTURE pipeline, extracted verbatim from the
// DlgProc WM_TIMER case (pattern: DragModeTick). The dispatcher was a
// 681-line god-function with this 9-level-deep state machine inlined;
// the order-sensitive safety gates (modal guard, overlay hide before
// WindowFromPoint, allow-list) are unchanged, byte-for-byte.
void MaxPaneContainer::OnCaptureTimerTick()
{
  // Handle capture-by-click mode
  if (m_captureMode.active) {
    // ADR-048 — capture-by-click safety: never grab a window while an
    // app-modal dialog (e.g. REAPER's "Save changes?" on quit) is up.
    // Reparenting the modal freezes REAPER's modal run loop → the dialog
    // dies and REAPER is bricked (forum v2.0.6: a user had to force-quit).
    // Disarm; the click reaches the modal normally (GetAsyncKeyState
    // polling doesn't consume it).
    if (IsAppModalActive()) {
      ExitCaptureMode();
      return;
    }
    RefreshCaptureCursor();  // ADR-048 — hold the crosshair (macOS)
    POINT pt;
    GetCursorPos(&pt);

    // ADR-048 — live hover preview: every tick, resolve the window under
    // the cursor and stash its display name + capturability so the armed
    // pane / launcher CTA can show "Capture: <name>" before the click.
    // Mirrors the rising-edge resolve below (kept separate because the
    // commit path also resolves the docked-frame child).
    {
      HWND uc = WindowManager::WindowFromPointForCapture(pt);
      // ADR-048 — skip ticks where the cursor sits on our own (click-
      // through) highlight overlay: re-resolving would find the overlay,
      // not the target, and the target can't have changed while we're
      // still over it. The committing click hides the overlay first.
      if (!IsCaptureHighlightWindow(uc)) {
        char hoverName[256] = {0};
        bool capturable = false;
        HWND tl = nullptr;
        if (uc && uc != m_hwnd && uc != g_reaperMainHwnd &&
            !m_winMgr.IsWindowCaptured(uc)) {
          tl = WindowManager::ResolveCaptureSourceForClick(uc);
          if (tl && tl != m_hwnd && tl != g_reaperMainHwnd &&
              IsWindowSafeToCapture(tl)) {
            char t[256];
            WindowManager::ResolveWindowDisplayName(tl, t, sizeof(t));
            // ADR-048 — allow-list (checked on the un-stripped title so the
            // DOCKED_TITLE_SUFFIX signal survives). Core main-window views fail it.
            if (t[0] && WindowManager::IsCapturableTarget(tl, t)) {
              char* dk = strstr(t, DOCKED_TITLE_SUFFIX);
              if (dk) *dk = '\0';
              safe_strncpy(hoverName, t, sizeof(hoverName));
              capturable = true;
            }
          }
        }
        if (capturable != m_captureMode.hoverCapturable ||
            strcmp(hoverName, m_captureMode.hoverName) != 0) {
          m_captureMode.hoverCapturable = capturable;
          safe_strncpy(m_captureMode.hoverName, hoverName,
                       sizeof(m_captureMode.hoverName));
          InvalidateRect(m_hwnd, nullptr, FALSE);
          // Logged only on transitions, so this stays low-volume. Vital for
          // diagnosing the silent-reject path on Win/Linux (audit follow-up).
          DBG("[MaxPane] capture hover: '%s' capturable=%d\n",
              hoverName[0] ? hoverName : "(none)", capturable ? 1 : 0);
        }
        // Outline the resolved target (or hide when not capturable).
        ShowCaptureHighlight(capturable ? tl : nullptr);
      }
    }

    short mouseState = GetAsyncKeyState(VK_LBUTTON);
    // Sprint 1 Entry 13 — rising-edge LMB detection. The previous
    // "if (currently-down)" branch had two bugs: (a) the button
    // click that activated capture mode triggered the first tick
    // immediately, (b) sub-tick LMB transitions slipped past the
    // 50ms poll. Rising-edge fires once on every true down-stroke.
    bool lmbDown = (mouseState & 0x8000) != 0;
    bool risingEdge = lmbDown && !m_captureMode.prevLmbDown;
#ifdef _WIN32
    // Win-VM live debug 2026-06-10 — a quick tap fits entirely between two
    // 50ms polls, so the rising edge never fired and armed capture looked
    // dead (drag-to-dock at 16ms ticks didn't suffer). Win32's
    // GetAsyncKeyState sets bit 0 ("pressed since the last call") exactly
    // for this; both SWELLs leave it unimplemented (always 0). prevLmbDown
    // must be false so the arming click itself can't self-commit on tick 1.
    if (!risingEdge && !m_captureMode.prevLmbDown && (mouseState & 0x0001)) {
      risingEdge = true;
      DBG("[MaxPane] capture click: tap caught via pressed-since-last bit\n");
    }
#endif
    m_captureMode.prevLmbDown = lmbDown;

    if (risingEdge) {
      // ADR-048 — drop the click-through overlay so WindowFromPoint
      // resolves the real target under the cursor, not our highlight.
      HideCaptureHighlight();
      HWND underCursor = WindowManager::WindowFromPointForCapture(pt);
      // Win-VM live debug 2026-06-10 — the commit gates were 100% silent, so
      // a click that died here was indistinguishable from no click at all.
      DBG("[MaxPane] capture click: under=%p self=%d main=%d captured=%d\n",
          (void*)underCursor,
          underCursor == m_hwnd ? 1 : 0,
          underCursor == g_reaperMainHwnd ? 1 : 0,
          (underCursor && m_winMgr.IsWindowCaptured(underCursor)) ? 1 : 0);
      if (underCursor && underCursor != m_hwnd &&
          underCursor != g_reaperMainHwnd &&
          !m_winMgr.IsWindowCaptured(underCursor)) {
        // Sprint 1 Entry 13 — walk up to the SHALLOWEST plugin-DLL
        // owned HWND. Handles docked plugins where the legacy
        // walk-to-top stopped at the dock frame's inner container.
        HWND topLevel = WindowManager::ResolveCaptureSourceForClick(underCursor);

        // Linux-VM live debug 2026-06-10 — clicking a child control of an
        // ALREADY-CAPTURED window slipped past the underCursor gate above
        // (it checks the raw hwnd, not the resolved one) and re-ran
        // CaptureArbitraryWindow on a window we own. The drag path has this
        // check; mirror it here.
        if (topLevel && m_winMgr.IsWindowCaptured(topLevel)) {
          DBG("[MaxPane] capture click: resolved %p is already captured — ignored\n",
              (void*)topLevel);
          topLevel = nullptr;
        }

        if (topLevel && topLevel != m_hwnd && topLevel != g_reaperMainHwnd) {
          char title[256];
          // Sprint 1 Entry 13 — empty-title plugins (ReaBeat, Reamix,
          // ReaImGui scripts) get a name via the module-DLL waterfall
          // so the capture flow doesn't drop them at the title gate.
          WindowManager::ResolveWindowDisplayName(topLevel, title, sizeof(title));
          // ADR-048 — allow-list gate. Rejects core REAPER main-window
          // views (arrange / ruler / TCP) so a stray click can't tear the
          // edit surface into a pane and blank the main window.
          if (title[0] && !WindowManager::IsCapturableTarget(topLevel, title)) {
            DBG("[MaxPane] capture rejected (core/unidentified main view): '%s'\n", title);
            // ADR-061 amendment — surface a gate-specific reason (e.g. the
            // Linux GL-ReaImGui forcecpu hint) instead of failing silently.
            if (const char* reason = WindowManager::TakeCaptureRefusal()) {
              ShowToast(reason);
            }
          }
          else if (title[0]) {
            HWND dockFrame = nullptr;
            HWND captureHwnd = WindowManager::ResolveDockFrameChild(
                topLevel, title, &dockFrame);
            int pId = m_captureMode.targetPaneId;
            int toggleAction = LookupToggleAction(title);
            // ADR-048 — per-window safety backstop: never reparent a
            // modal/sheet (catastrophic brick). The poll-level
            // IsAppModalActive() gate above already covers the common
            // case; this guards the resolved target itself.
            if (IsWindowSafeToCapture(captureHwnd)) {
              // Audit M1.3 — see the Open Windows path: hide the dock
              // frame only on a committed capture, toast on failure.
              bool captured = m_winMgr.CaptureArbitraryWindow(
                  pId, captureHwnd, title, m_hwnd, toggleAction);
              if (captured && dockFrame && IsWindow(dockFrame)) {
                ShowWindow(dockFrame, SW_HIDE);
              }
              if (!captured) {
                // ADR-061 amendment — prefer the gate's specific reason.
                const char* reason = WindowManager::TakeCaptureRefusal();
                ShowToast(reason ? reason
                  : "Couldn't capture — pane is full or window is already captured.");
              }

              RefreshLayout();
              SaveState();
            }
          }
        }

        ExitCaptureMode();  // ADR-048 — disarm + pop crosshair
      }
    }

    // ADR-048 — Esc cancel. SWELL's GetAsyncKeyState(VK_ESCAPE) always
    // returns 0 on macOS, so route through the CoreGraphics keycode probe.
    // Audit M1.6 — right-click also cancels: Linux has NO key probe at
    // all (GDK GetAsyncKeyState tracks only mouse buttons + modifiers),
    // so without this the only way out of armed capture there was to
    // capture something. VK_RBUTTON is tracked by both SWELLs + Win32.
    {
      bool escDown  = IsCaptureCancelKeyDown();
      bool rbtnDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
      if (escDown || rbtnDown) {
        DBG("[MaxPane] capture cancel: esc=%d rbtn=%d\n",
            escDown ? 1 : 0, rbtnDown ? 1 : 0);
        ExitCaptureMode();  // disarm + pop crosshair
      }
    }
  }
  // Handle async capture queue
  else if (m_captureQueue->HasPending()) {
    bool anyCaptured = m_captureQueue->Tick(m_hwnd, m_winMgr);
    // v2.0.4 #1 — drain FX-restore failure toast (fires on miss
    // regardless of whether any capture succeeded this tick).
    const char* fxToast = m_captureQueue->PopFailureToast();
    if (fxToast && fxToast[0]) {
      ShowToast(fxToast);
    }
    if (anyCaptured) {
      RefreshLayout();
    }
    if (!m_captureQueue->HasPending()) {
      StopCaptureTimerIfIdle();
      // F-40 — async captures landed in completion order; re-sort any
      // deferred pane back to its saved order before persisting.
      FinalizeRestoreOrder();
      SaveState();
      // F-H (forum v2.0.6) — force a real Cocoa layout+display once the
      // restore finishes. RefreshLayout's InvalidateRect doesn't repaint
      // the container's own NSView after children were reparented in
      // (SWELL doesn't cascade setNeedsDisplay), so the tab bar + pane
      // dividers stay blank until the user nudges a splitter to trigger a
      // real WM_SIZE relayout. A forced layout pass paints them now.
      RefreshLayout();
      ForceViewLayoutAndDisplay(m_hwnd);
    }
  }
  else {
    StopCaptureTimerIfIdle();
  }
}

INT_PTR CALLBACK MaxPaneContainer::DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  MaxPaneContainer* self = (MaxPaneContainer*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

  switch (msg) {
    case WM_INITDIALOG: {
      self = (MaxPaneContainer*)lParam;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
      if (self) self->m_hwnd = hwnd;
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;  // we paint everything in WM_PAINT

    case WM_GETMINMAXINFO: {
      // Bug I — when this container holds a captured ReaImGui / Lua-gfx window,
      // report a minimum size so REAPER won't shrink the dock (on the axis its
      // divider controls — REAPER honors ptMinTrackSize there) to the point
      // where the capture is squeezed below its content min and resizes REAPER's
      // own main window (collapse). SWELL's NSWindow minSize sends this to the
      // contentView (swell-dlg.mm:2462), so it also clamps a floating MaxPane on
      // both axes. Only when a ReaImGui host is captured, so MaxPanes holding
      // only toolbars / FX / native windows (legitimately small) still dock
      // arbitrarily small. The perpendicular axis (driven by REAPER's window,
      // which ignores this) is covered by the pane floor-hide in
      // WindowManager::RepositionAll.
      if (self && self->m_winMgr.HasCapturedReaImGui()) {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        if (mmi) {
          if (mmi->ptMinTrackSize.x < ARB_DOCK_MIN_W) mmi->ptMinTrackSize.x = ARB_DOCK_MIN_W;
          if (mmi->ptMinTrackSize.y < ARB_DOCK_MIN_H) mmi->ptMinTrackSize.y = ARB_DOCK_MIN_H;
        }
        return 0;
      }
      break;
    }

    case WM_SIZE: {
      if (self) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int cx = rc.right - rc.left;
        int cy = rc.bottom - rc.top;
        if (cx > 0 && cy > 0) {
          self->OnSize(cx, cy);
        }
        // F1a (ADR-024) — track floating window size so we can persist on
        // Shutdown. CaptureFloatGeometry reads window rect (outer frame),
        // matching what SetWindowPos expects on re-restore.
        if (self->m_floating) {
          self->CaptureFloatGeometry();
        }
      }
      return 0;
    }

    case WM_MOVE: {
      // F1a (ADR-024) — track floating window position. SWELL fires WM_MOVE
      // for top-level windows after user drags titlebar.
      if (self && self->m_floating) {
        self->CaptureFloatGeometry();
      }
      return 0;
    }

    case WM_PAINT: {
      if (self) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        self->OnPaint(hdc);
        EndPaint(hwnd, &ps);
        // After parent paint, tell captured children to redraw.
        // SWELL doesn't implement WS_CLIPCHILDREN, so the Cocoa drawRect:
        // cycle on the parent view may clear areas under child views.
        //
        // ADR-026 — when an overlay/menu is active we paint UI ON TOP of
        // the pane grid. If we invalidate child windows here they paint
        // OVER our overlay (their NSViews don't know the menu exists).
        // Skip the propagation in those states; the next regular tick
        // (drag/hover/etc.) will re-invalidate naturally.
        bool overlayActive = self->m_homeOverlay ||
                             self->m_drag.mode == DragDock::TRACKING;
        if (!overlayActive) {
          for (int i = 0; i < self->m_tree.GetLeafCount(); i++) {
            int pid = self->m_tree.GetPaneId(self->m_tree.GetLeafList()[i]);
            if (pid < 0 || pid >= MAX_PANES) continue;
            const PaneState* pps = self->m_winMgr.GetPaneState(pid);
            if (pps && pps->activeTab >= 0 && pps->activeTab < pps->tabCount) {
              const TabEntry* tab = &pps->tabs[pps->activeTab];
              if (tab->captured && tab->hwnd && IsWindow(tab->hwnd)) {
                InvalidateRect(tab->hwnd, nullptr, FALSE);
              }
            }
          }
        }
      }
      return 0;
    }

    case WM_LBUTTONDOWN: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);

        // F6 — mark this instance as focused for slot-action routing.
        InstanceManager::Get().SetFocused(self->m_instanceId);

        // ADR-026 — nav bar consumes clicks on its strip first. If
        // visible and the click hit the bar (button or empty area),
        // the handler returns true and we stop here.
        if (self->OnNavBarClick(x, y)) return 0;

        // ADR-026 — Home overlay consumes the next click. Card click
        // loads workspace + closes; click outside cards closes overlay.
        if (self->m_homeOverlay) {
          self->OnHomeOverlayClick(x, y);
          return 0;
        }

        // Launcher hero (ADR-013): cards load workspaces, CTA enters capture.
        if (self->IsInLauncherMode()) {
          RECT rc;
          GetClientRect(hwnd, &rc);
          // ADR-048 — match Paint's launcher origin (container_paint.cpp shifts
          // the layout down by NavBarReservedHeight()). Without this offset the
          // CTA/card hit rects sit NAV_BAR_HEIGHT px above the painted buttons,
          // so the CTA only "worked" along its top edge and above it.
          rc.top += self->NavBarReservedHeight();
          Launcher::Layout lay = Launcher::Compute(rc, *self->m_wsMgr);
          int hit = Launcher::HitTest(lay, x, y);
          if (hit == Launcher::HIT_CAPTURE_BUTTON) {
            int paneId = self->m_tree.GetPaneId(self->m_tree.GetLeafList()[0]);
            DBG("[MaxPane] Launcher capture CTA clicked → arm capture-by-click pane=%d\n", paneId);
            self->m_launcherHover = Launcher::HIT_NONE;
            self->EnterCaptureMode(paneId);  // ADR-048 — guards paneId<0
            return 0;
          }
          if (hit >= 0 && hit < self->m_wsMgr->GetCount()) {
            const WorkspaceEntry& ws = self->m_wsMgr->Get(hit);
            self->m_launcherHover = Launcher::HIT_NONE;
            self->LoadWorkspace(ws.name);
            return 0;
          }
          // Click on empty launcher area: fall through to standard handlers
          // (right-click context menu is processed via WM_CONTEXTMENU separately).
        }

        int splitter = self->m_tree.HitTestSplitter(x, y);
        if (splitter >= 0) {
          self->m_tree.StartDrag(splitter);
          SetCapture(hwnd);
          return 0;
        }

        // Check tab clicks across all leaf panes
        for (int li = 0; li < self->m_tree.GetLeafCount(); li++) {
          int paneId = self->m_tree.GetPaneId(self->m_tree.GetLeafList()[li]);
          if (paneId < 0) continue;

          int tabIdx = self->TabHitTest(paneId, x, y);
          if (tabIdx == -2) {
            // Pane menu button ▼
            self->OnPaneMenuButtonClick(paneId, x, y);
            return 0;
          } else if (tabIdx >= 0) {
            self->m_focusedPaneId = paneId;
            if (self->IsOnTabCloseButton(paneId, tabIdx, x, y)) {
              // Record action ID for stale-cleanup before close (see
              // HandleTabMenuCommand). This is the X-button click path —
              // the most common close action and the one that's been
              // leaking ghosts on REAPER restart for windows with B13/B16
              // racing wnd_vis behaviour (Actions, FX Browser, etc.).
              if (const TabEntry* t = self->m_winMgr.GetTab(paneId, tabIdx)) {
                if (t->captured && t->toggleAction > 0) {
                  AppendActionToStaleListForSection(self->m_extSection, t->toggleAction);
                }
              }
              self->RecordClosedTab(paneId, tabIdx);  // C1
              self->m_winMgr.CloseTab(paneId, tabIdx);
              self->RefreshLayout();
              self->SaveState();
            } else {
              self->m_winMgr.SetActiveTab(paneId, tabIdx);
              self->RefreshLayout();
              self->StartTabDrag(paneId, tabIdx, x, y);
            }
            return 0;
          }
        }

        // Check if click is on an empty pane's header bar
        for (int li = 0; li < self->m_tree.GetLeafCount(); li++) {
          int paneId = self->m_tree.GetPaneId(self->m_tree.GetLeafList()[li]);
          if (paneId < 0) continue;

          const PaneState* ps = self->m_winMgr.GetPaneState(paneId);
          if (!ps || ps->tabCount == 0) {
            const RECT& r = self->m_tree.GetPaneRect(paneId);
            if (x >= r.left && x < r.right && y >= r.top && y < r.top + TAB_BAR_HEIGHT) {
              self->OnContextMenu(x, y);
              return 0;
            }
          }
        }
      }
      return 0;
    }

    case WM_MOUSEMOVE: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnMouseMove(x, y);
      }
      return 0;
    }


    case WM_LBUTTONUP: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnLButtonUp(x, y);
      }
      return 0;
    }

    case WM_NCHITTEST:
#ifdef _WIN32
      // Sprint 1 Entry 2 — native Win32 DialogProc treats the return value
      // as BOOL. WM_NCHITTEST is not in MSDN's BOOL-exception list, so the
      // result must be published via SetWindowLongPtr(DWLP_MSGRESULT, …)
      // and the proc must return TRUE. The previous "return HTCLIENT;" was
      // read as TRUE-handled, then the dialog manager sampled DWLP_MSGRESULT
      // which defaults to 0 (HTNOWHERE) — so every mouse button event got
      // routed past our DlgProc. SWELL DialogProc honours the direct return.
      // Sprint 1 Entry 6 — when floating, defer to default handler so Win32
      // resolves resize-border / caption hit-test areas correctly.
      if (self && self->IsFloating()) return FALSE;
      SetWindowLongPtr(hwnd, DWLP_MSGRESULT, (LONG_PTR)HTCLIENT);
      return TRUE;
#else
      return HTCLIENT;
#endif

    case WM_SETCURSOR: {
      if (self && (HWND)wParam == hwnd) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        int splitter = self->m_tree.HitTestSplitter(pt.x, pt.y);
        if (splitter >= 0) {
          if (self->m_tree.GetSplitterOrientation(splitter) == SPLIT_VERTICAL) {
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
          } else {
            SetCursor(LoadCursor(nullptr, IDC_SIZENS));
          }
          return 1;
        }
      }
      break;
    }

    case WM_LBUTTONDBLCLK: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        int splitter = self->m_tree.HitTestSplitter(x, y);
        if (splitter >= 0) {
          self->m_tree.ResetRatio(splitter);
          self->m_winMgr.RepositionAll(self->m_tree);
          InvalidateRect(hwnd, nullptr, FALSE);
          self->SaveState();
          return 0;
        }
        self->OnContextMenu(x, y);
      }
      return 0;
    }

    case WM_RBUTTONUP: {
#ifdef _WIN32
      // Sprint 1 Entry 17 — on Win32 the default dialog handler synthesizes
      // WM_CONTEXTMENU from WM_RBUTTONUP. Calling OnContextMenu here AND in
      // the WM_CONTEXTMENU case below produced a double-fire — the second
      // pass interpreted as "right-click in container during capture mode"
      // and cancelled the freshly-entered Capture-by-Click. Let WM_CONTEXTMENU
      // be the single canonical trigger on Win32. SWELL macOS/Linux do not
      // synthesize WM_CONTEXTMENU from WM_RBUTTONUP, so the original path
      // is preserved there.
      (void)self;
#else
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnContextMenu(x, y);
      }
#endif
      return 0;
    }

    case WM_RBUTTONDOWN: {
      return 0;
    }

    case WM_CONTEXTMENU: {
      if (self) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        self->OnContextMenu(pt.x, pt.y);
      }
      return 0;
    }

    case WM_TIMER: {
      if (self && wParam == TIMER_ID_CHECK) {
        self->OnTimer();
      }
      else if (self && wParam == TIMER_ID_DRAG_DOCK) {
        // ADR-026 — drag-to-dock polling. State machine in DragModeTick
        // handles ARMED → TRACKING → IDLE transitions + commit/cancel.
        self->DragModeTick();
      }
      else if (self && wParam == TIMER_ID_NAVBAR_TIP) {
        KillTimer(hwnd, TIMER_ID_NAVBAR_TIP);
        // Show tooltip only if hover is still on a nav button.
        if (self->m_navHover != NavBar::BTN_NONE) {
          self->m_navTooltipBtn = self->m_navHover;
          RECT rc; GetClientRect(hwnd, &rc);
          NavBar::Layout lay = self->ComputeNavBarLayout(rc);
          RECT dirty = lay.barRect;
          dirty.bottom += 36;
          InvalidateRect(hwnd, &dirty, FALSE);
        }
      }
      else if (self && wParam == TIMER_ID_LAUNCHER_TIP) {
        KillTimer(hwnd, TIMER_ID_LAUNCHER_TIP);
        // Sprint 1 Entry 18 — also fire tooltip when hover is on a home-
        // overlay card (overlay over an active workspace). The previous
        // gate on IsInLauncherMode() returned false once any capture was
        // active, suppressing overlay tooltips.
        int hoverCard = -1;
        if (self->IsInLauncherMode())  hoverCard = self->m_launcherHover;
        else if (self->m_homeOverlay)  hoverCard = self->m_homeOverlayHover;
        if (hoverCard >= 0) {
          self->m_launcherTooltipCard = hoverCard;
          InvalidateRect(hwnd, nullptr, FALSE);
        }
      }
      else if (self && wParam == TIMER_ID_TOAST) {
        // Sticky toasts (Feature B "Saving…") skip the fade tick entirely
        // — ShowStickyToast killed the timer when it set the message, so
        // we should never see this branch with m_toastSticky=true. Guard
        // defensively in case a stale tick races between sticky→ShowToast.
        if (self->m_toastSticky) {
          KillTimer(hwnd, TIMER_ID_TOAST);
          return 0;
        }
        // Drive fade animation + reap when deadline passes.
        const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
        if (self->m_toastDeadlineMs == 0 || now >= self->m_toastDeadlineMs) {
          KillTimer(hwnd, TIMER_ID_TOAST);
          self->m_toastDeadlineMs = 0;
          self->m_toastMessage[0] = '\0';
          InvalidateRect(hwnd, nullptr, FALSE);
        } else {
          // Only repaint the bottom toast strip to keep the animation cheap.
          RECT cr;
          GetClientRect(hwnd, &cr);
          RECT bar = cr;
          bar.top = cr.bottom - TOAST_BAR_HEIGHT;
          if (bar.top < cr.top) bar.top = cr.top;
          InvalidateRect(hwnd, &bar, FALSE);
        }
      }
      else if (self && wParam == TIMER_ID_WORKSPACE_FLUSH) {
        // Feature B — drives the deferred ExtState write off the click frame.
        self->OnWorkspaceFlushTick();
      }
      else if (self && wParam == TIMER_ID_HOVER) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        bool outside = (pt.x < rc.left || pt.x >= rc.right || pt.y < rc.top || pt.y >= rc.bottom);

        // Compute current hover targets from cursor position (handles
        // mouse moving onto captured child windows where we stop getting
        // WM_MOUSEMOVE but cursor is still inside the container rect).
        int curSplitter = outside ? -1 : self->m_tree.HitTestSplitter(pt.x, pt.y);
        int curPane = -1, curTab = -1;
        if (!outside) {
          for (int i = 0; i < self->m_tree.GetLeafCount(); i++) {
            int pid = self->m_tree.GetPaneId(self->m_tree.GetLeafList()[i]);
            if (pid < 0) continue;
            int t = self->TabHitTest(pid, pt.x, pt.y);
            if (t != -1) { curPane = pid; curTab = t; break; }
          }
        }

        if (curSplitter != self->m_hoverSplitter ||
            curPane != self->m_hoverPane || curTab != self->m_hoverTab) {
          int oldSplitter = self->m_hoverSplitter;
          int oldPane     = self->m_hoverPane;
          int oldTab      = self->m_hoverTab;
          self->m_hoverSplitter = curSplitter;
          self->m_hoverPane = curPane;
          self->m_hoverTab = curTab;

          if (curSplitter < 0 && curTab == -1)
            KillTimer(hwnd, TIMER_ID_HOVER);

          RECT dirty = {};
          if (oldSplitter >= 0)
            ExpandRect(dirty, self->m_tree.GetNode(oldSplitter).splitterRect);
          if (curSplitter >= 0)
            ExpandRect(dirty, self->m_tree.GetNode(curSplitter).splitterRect);
          if (oldPane >= 0 && oldTab >= 0)
            ExpandRect(dirty, self->GetTabRect(oldPane, oldTab));
          else if (oldPane >= 0 && oldTab == -2) {
            const RECT& pr = self->m_tree.GetPaneRect(oldPane);
            RECT btnR = { pr.right - PANE_MENU_BTN_WIDTH, pr.top,
                          pr.right, pr.top + TAB_BAR_HEIGHT };
            ExpandRect(dirty, btnR);
          }
          if (curPane >= 0 && curTab >= 0)
            ExpandRect(dirty, self->GetTabRect(curPane, curTab));
          else if (curPane >= 0 && curTab == -2) {
            const RECT& pr = self->m_tree.GetPaneRect(curPane);
            RECT btnR = { pr.right - PANE_MENU_BTN_WIDTH, pr.top,
                          pr.right, pr.top + TAB_BAR_HEIGHT };
            ExpandRect(dirty, btnR);
          }

          if (dirty.right > dirty.left) InvalidateRect(hwnd, &dirty, FALSE);
          else InvalidateRect(hwnd, nullptr, FALSE);
        }
      }
      else if (self && wParam == TIMER_ID_CAPTURE) {
        // Audit M2.3 — capture-by-click poll + async-queue drain live in
        // OnCaptureTimerTick() now; this case is dispatch only.
        self->OnCaptureTimerTick();
      }
      return 0;
    }

    case WM_COMMAND: {
      if (self && LOWORD(wParam) == IDCANCEL) {
        self->Toggle();  // Toggle handles was_visible + captured_actions + Shutdown
        return 0;
      }
      break;
    }

    case WM_CLOSE: {
      if (self) {
        self->Toggle();  // Toggle handles was_visible + captured_actions + Shutdown
      }
      return 0;
    }

    case WM_DESTROY: {
      if (self) {
        // F-B (forum v2.0.6) — the docker close button reaches us via
        // WM_DESTROY (not Toggle→Shutdown), so persist floating geometry here
        // too. Skip when atexit already saved it (quit path) to avoid reading
        // a rect mid-teardown; CaptureFloatGeometry rejects pathologic rects.
        if (!g_atexitSaved) self->PersistFloatingGeometry();
        // B4: REAPER's docker close button (or any external DestroyWindow)
        // bypasses Shutdown(). Captured children must be reparented out
        // before we disappear, or they become frameless ghosts owned by a
        // dead parent. ReleaseAll(false) reparents + hides without toggling
        // the REAPER action — atexit / next-open reconciles state.
        int captured = 0;
        for (int p = 0; p < MAX_PANES; p++) {
          const PaneState* ps = self->m_winMgr.GetPaneState(p);
          if (!ps) continue;
          for (int t = 0; t < ps->tabCount; t++) {
            if (ps->tabs[t].captured) captured++;
          }
        }
        if (captured > 0) {
          DBG("[MaxPane] WM_DESTROY: %d captured tab(s) — emergency ReleaseAll(false)\n", captured);
          self->m_winMgr.ReleaseAll(false);
        }
        KillTimer(hwnd, TIMER_ID_CHECK);
        KillTimer(hwnd, TIMER_ID_CAPTURE);
        // B26: do NOT write was_visible from WM_DESTROY. On macOS REAPER
        // destroys docker windows asynchronously during quit — order between
        // these destroys and our atexit callback is undefined, so any "0"
        // written here can clobber legitimate "1" state for instances that
        // were open at quit time. The legit close paths are Toggle()
        // (line 169 — explicit user close, writes "0") and Create()
        // (line 103 — writes "1"). Edge case "user clicked X on dock
        // without Toggle" → was_visible stays "1" → MaxPane auto-restores
        // next session; acceptable (user can Toggle off if undesired).
      }
      return 0;
    }
  }

  return 0;
}
