#include "capture_queue.h"
#include "config.h"
#include "fx_capture.h"
#include "globals.h"
#include "debug.h"
#include <cstring>
#include <cstdio>

CaptureQueue::CaptureQueue()
  : m_count(0)
{
  // m_queue elements default-construct to their NSDMI values; no memset —
  // PendingCapture is non-trivial, so GCC's -Werror=class-memaccess flags
  // raw memory clears (ADR-060 session catch). Value-assignment is the
  // reset idiom throughout this TU now.
}

void CaptureQueue::EnqueueKnown(int paneId, int knownIdx, bool deferAction)
{
  if (m_count >= MAX_PENDING) return;
  if (knownIdx < 0 || knownIdx >= NUM_KNOWN_WINDOWS) return;

  const WindowDef& def = KNOWN_WINDOWS[knownIdx];

  PendingCapture& pc = m_queue[m_count];
  pc = PendingCapture{};
  pc.state = PendingCapture::WAITING;
  pc.paneId = paneId;
  pc.knownWindowIndex = knownIdx;
  safe_strncpy(pc.searchTitle, def.searchTitle, sizeof(pc.searchTitle));
  if (def.altSearchTitle) {
    safe_strncpy(pc.altSearchTitle, def.altSearchTitle, sizeof(pc.altSearchTitle));
  }
  safe_strncpy(pc.displayName, def.name, sizeof(pc.displayName));
  pc.toggleAction = def.toggleActionId;
  pc.isArbitrary = false;
  pc.tickCount = 0;
  pc.retryCount = 0;
  pc.maxRetries = MAX_RETRIES;
  pc.actionDeferred = deferAction;

  // Fire the toggle action to open the window — but only if it's currently closed.
  // If deferAction is set (called from LoadState during startup), skip Main_OnCommand
  // to avoid deadlocking REAPER during project load. The action will fire on first Tick.
  if (!deferAction && g_Main_OnCommand) {
    bool alreadyOpen = false;
    if (g_GetToggleCommandState) {
      int state = g_GetToggleCommandState(def.toggleActionId);
      alreadyOpen = (state == 1);
      DBG("[MaxPane] CaptureQueue: toggle state for '%s' (action %d) = %d\n",
          def.name, def.toggleActionId, state);
    }
    if (!alreadyOpen) {
      g_Main_OnCommand(def.toggleActionId, 0);
    }
  }

  m_count++;
  DBG("[MaxPane] CaptureQueue: enqueued known '%s' for pane %d (count=%d, deferred=%d)\n",
      def.name, paneId, m_count, deferAction);
}

void CaptureQueue::EnqueueArbitrary(int paneId, const char* name, int toggleAction, const char* actionCmd, bool deferAction)
{
  if (m_count >= MAX_PENDING) return;
  if (!name || !name[0]) return;

  // ADR-052 — Main toolbar is not capturable. A legacy workspace entry that
  // resolved to 41651 (saved before the exclusion, e.g. 'arb:41651:...')
  // would toggle the main toolbar open as a floating window and then burn
  // retries on a capture that CaptureArbitraryWindow rejects. Skip outright.
  if (toggleAction == MAIN_TOOLBAR_ACTION || strcmp(name, "Main toolbar") == 0) {
    DBG("[MaxPane] CaptureQueue: SKIP Main toolbar '%s' (action=%d) — not capturable (ADR-052)\n",
        name, toggleAction);
    return;
  }

  // v2.0.4 #1 (ADR-037) — FX identity short-circuits the FindReaperWindow
  // title-search path. Identity carries the (track, FX) GUID pair; restore
  // resolves to a live (track, slot) via REAPER SDK and calls TrackFX_Show
  // to obtain the HWND directly. No name-search retries needed.
  if (FxCapture::IsFxIdentity(actionCmd)) {
    PendingCapture& pc = m_queue[m_count];
    pc = PendingCapture{};
    pc.state = PendingCapture::WAITING;
    pc.paneId = paneId;
    pc.knownWindowIndex = -1;
    safe_strncpy(pc.searchTitle, name, sizeof(pc.searchTitle));
    safe_strncpy(pc.displayName, name, sizeof(pc.displayName));
    safe_strncpy(pc.fxIdentity, actionCmd, sizeof(pc.fxIdentity));
    safe_strncpy(pc.actionCommand, actionCmd, sizeof(pc.actionCommand));
    pc.toggleAction = 0;
    pc.isArbitrary = true;
    pc.tickCount = 0;
    pc.retryCount = 0;
    pc.maxRetries = MAX_RETRIES_ARBITRARY;
    pc.actionDeferred = deferAction;
    m_count++;
    DBG("[MaxPane] CaptureQueue: enqueued FX identity '%s' name='%s' for pane %d (count=%d)\n",
        actionCmd, name, paneId, m_count);
    return;
  }

  // B18: dead arbitrary tab — no toggle action AND no script command string
  // (legacy v1.5.x save format, or script that no longer exists). Without a
  // way to open the window, normal enqueue would burn MAX_RETRIES_ARBITRARY
  // (~10s) of OnTimer ticks per tab, freezing the UI and spamming the log.
  // One probe now: if the window happens to be open, fall through to capture;
  // otherwise skip and let the user re-add manually.
  if (toggleAction <= 0 && (!actionCmd || !actionCmd[0])) {
    if (!WindowManager::FindReaperWindow(name, nullptr)) {
      DBG("[MaxPane] CaptureQueue: SKIP dead arbitrary '%s' (no action, no cmd, not visible) for pane %d\n",
          name, paneId);
      return;
    }
    DBG("[MaxPane] CaptureQueue: dead arbitrary '%s' already visible — proceeding to capture\n", name);
  }

  PendingCapture& pc = m_queue[m_count];
  pc = PendingCapture{};
  pc.state = PendingCapture::WAITING;
  pc.paneId = paneId;
  pc.knownWindowIndex = -1;
  safe_strncpy(pc.searchTitle, name, sizeof(pc.searchTitle));
  safe_strncpy(pc.displayName, name, sizeof(pc.displayName));
  pc.toggleAction = toggleAction;
  pc.isArbitrary = true;
  pc.tickCount = 0;
  pc.retryCount = 0;
  pc.maxRetries = (toggleAction > 0) ? MAX_RETRIES : MAX_RETRIES_ARBITRARY;
  pc.actionDeferred = deferAction;
  if (actionCmd && actionCmd[0]) {
    safe_strncpy(pc.actionCommand, actionCmd, sizeof(pc.actionCommand));
  }

  // Fire the toggle action if we have one — but only if the window isn't
  // already open. An unconditional toggle on an already-open arbitrary window
  // (e.g. a toolbar the user has visible elsewhere) would CLOSE it, then we'd
  // spend MAX_RETRIES_ARBITRARY ticks failing to find what we just closed.
  // If deferAction is set (called from LoadState during startup), skip
  // Main_OnCommand to avoid deadlocking REAPER during project load. The
  // deferred action will fire on first Tick.
  if (!deferAction && toggleAction > 0 && g_Main_OnCommand) {
    bool alreadyOpen = false;
    if (g_GetToggleCommandState) {
      int state = g_GetToggleCommandState(toggleAction);
      alreadyOpen = (state == 1);
      DBG("[MaxPane] CaptureQueue: toggle state for arbitrary '%s' (action %d) = %d\n",
          name, toggleAction, state);
    }
    if (!alreadyOpen) {
      g_Main_OnCommand(toggleAction, 0);
    }
  }

  m_count++;
  DBG("[MaxPane] CaptureQueue: enqueued arbitrary '%s' action=%d cmd='%s' maxRetries=%d for pane %d (count=%d, deferred=%d)\n",
      name, toggleAction, pc.actionCommand, pc.maxRetries, paneId, m_count, deferAction);
}

bool CaptureQueue::Tick(HWND containerHwnd, WindowManager& winMgr)
{
  bool anyCaptured = false;

  for (int i = m_count - 1; i >= 0; i--) {
    PendingCapture& pc = m_queue[i];
    if (pc.state == PendingCapture::IDLE || pc.state == PendingCapture::DONE ||
        pc.state == PendingCapture::FAILED) {
      continue;
    }

    pc.tickCount++;

    if (pc.state == PendingCapture::WAITING) {
      if (pc.tickCount >= INITIAL_WAIT_TICKS) {
        pc.state = PendingCapture::RETRYING;
        pc.retryCount = 0;
      }
      continue;
    }

    // RETRYING state
    if (pc.tickCount % RETRY_INTERVAL != 0) continue;

    pc.retryCount++;

    // v2.0.4 #1 (ADR-037) — FX identity path: resolve (track, FX) by GUID,
    // open via TrackFX_Show, capture the returned HWND. No FindReaperWindow.
    // Retries cover the multi-project-tab race where the owning project
    // isn't loaded yet at the moment we begin retrying.
    if (pc.fxIdentity[0]) {
      HWND fxHwnd = nullptr;
      bool gotHwnd = FxCapture::ShowAndGetHwnd(pc.fxIdentity, fxHwnd);
      if (gotHwnd && fxHwnd) {
        // Refresh display name from REAPER for stability across renames.
        char freshName[256] = {};
        if (FxCapture::GetDisplayName(pc.fxIdentity, freshName, sizeof(freshName)) &&
            freshName[0]) {
          safe_strncpy(pc.displayName, freshName, sizeof(pc.displayName));
        }
        DBG("[MaxPane] CaptureQueue: FX resolve OK '%s' hwnd=%p — capturing\n",
            pc.fxIdentity, (void*)fxHwnd);
        bool captured = winMgr.CaptureArbitraryWindow(pc.paneId, fxHwnd,
                                                     pc.displayName, containerHwnd,
                                                     0, pc.fxIdentity);
        if (captured) {
          anyCaptured = true;
        } else {
          DBG("[MaxPane] CaptureQueue: FX capture failed (pane full or already captured)\n");
        }
        Remove(i);
        continue;
      }
      if (pc.retryCount >= pc.maxRetries) {
        DBG("[MaxPane] CaptureQueue: FX FAILED after %d retries identity='%s'\n",
            pc.retryCount, pc.fxIdentity);
        // Distinguish "owner missing" from "FX missing" for toast copy.
        FxCapture::ResolvedLocation diagLoc{};
        bool ownerFound = false;
        FxCapture::ResolveLocation(pc.fxIdentity, diagLoc, &ownerFound);
        const char* fallbackName = pc.displayName[0] ? pc.displayName : "(unknown)";
        // %.180s: displayName is 256 bytes — newer GCC's -Wformat-truncation
        // (ubuntu-24.04 CI gate) wants proof the suffix fits the toast buffer.
        if (ownerFound) {
          snprintf(m_lastFailureToast, sizeof(m_lastFailureToast),
                   "FX missing: %.180s — track no longer has this plugin.",
                   fallbackName);
        } else {
          snprintf(m_lastFailureToast, sizeof(m_lastFailureToast),
                   "FX missing: %.180s — owning track was deleted from the project.",
                   fallbackName);
        }
        Remove(i);
      }
      continue;
    }

    // Fire deferred action on first retry (REAPER is now past startup)
    if (pc.actionDeferred) {
      pc.actionDeferred = false;
      if (g_Main_OnCommand && pc.toggleAction > 0) {
        bool alreadyOpen = false;
        if (!pc.isArbitrary && g_GetToggleCommandState) {
          int state = g_GetToggleCommandState(pc.toggleAction);
          alreadyOpen = (state == 1);
        }
        if (!alreadyOpen) {
          DBG("[MaxPane] CaptureQueue: firing deferred action for '%s' (action %d)\n",
              pc.displayName, pc.toggleAction);
          g_Main_OnCommand(pc.toggleAction, 0);
        }
      }
    }

    // Try to find the window
    HWND found = WindowManager::FindReaperWindow(pc.searchTitle, containerHwnd);
    if (!found && pc.altSearchTitle[0]) {
      found = WindowManager::FindReaperWindow(pc.altSearchTitle, containerHwnd);
    }

    if (found) {
      // For arbitrary windows: check if the found window is the inner content
      // (not the dock frame). ReaImGui scripts have a dock frame "Title (docked)"
      // that contains the actual rendered UI. If we find "Title" but not
      // "Title (docked)", wait a few retries for the dock frame to appear.
      if (pc.isArbitrary) {
        char dockedTitle[512];
        snprintf(dockedTitle, sizeof(dockedTitle), "%s%s", pc.searchTitle, DOCKED_TITLE_SUFFIX);
        char foundTitle[512];
        GetWindowText(found, foundTitle, sizeof(foundTitle));

        bool foundIsDockFrame = (strstr(foundTitle, DOCKED_TITLE_SUFFIX) != nullptr);
        // Sprint 1 Entry 16 — skip dock-frame wait for window classes that
        // will never have one. Originally this also skipped Lua scripts
        // ("the wrapper never appears on Main_OnCommand restore"). On
        // macOS that assumption failed: REAPER 7.73 + a workspace that
        // auto-docked a ReaImGui script crashed REAPER inside
        // ReaImGui's Docker::moveTo with a null-deref at offset 0x20.
        // Root cause: when MaxPane reparents the INNER ReaImGui window
        // (because we skipped waiting for the dock-frame to materialize),
        // ReaImGui's internal Docker bookkeeping still holds a pointer
        // to the old hierarchy; the next heartbeat dereferences a stale
        // field and crashes. Per MEMORY.md "always capture the dock
        // frame, never the inner window". The Lua-script fast-path is
        // therefore reverted — scripts wait the full 8 retries for the
        // dock-frame wrapper before falling through to inner. The
        // empty-title plugin fast-path stays (ReaBeat / Reamix on
        // Win32 — they never have a dock-frame wrapper at all).
        const bool isPluginNoTitle = (foundTitle[0] == '\0');
        if (!foundIsDockFrame && !isPluginNoTitle && pc.retryCount <= 8) {
          // We found the inner window but no dock frame yet — wait for it
          if (pc.retryCount == 1) {
            WindowManager::DumpAllWindowTitles(pc.displayName);
          }
          DBG("[MaxPane] CaptureQueue: found inner '%s' hwnd=%p but no dock frame yet, waiting (retry=%d)\n",
              pc.displayName, (void*)found, pc.retryCount);
          continue;
        }
        if (isPluginNoTitle) {
          DBG("[MaxPane] CaptureQueue: empty-title plugin '%s' — skipping dock-frame wait (Entry 16)\n",
              pc.displayName);
        }
        if (!foundIsDockFrame) {
          DBG("[MaxPane] CaptureQueue: no dock frame appeared for '%s' after %d retries, using inner window\n",
              pc.displayName, pc.retryCount);
        }
      }

      DBG("[MaxPane] CaptureQueue: FOUND '%s' hwnd=%p retry=%d, attempting capture (arb=%d action=%d cmd='%s')\n",
          pc.displayName, (void*)found, pc.retryCount, pc.isArbitrary, pc.toggleAction, pc.actionCommand);
      bool captured = false;
      if (pc.isArbitrary) {
        captured = winMgr.CaptureArbitraryWindow(pc.paneId, found, pc.displayName, containerHwnd,
                                                    pc.toggleAction, pc.actionCommand);
      } else {
        captured = winMgr.CaptureByIndex(pc.paneId, pc.knownWindowIndex, containerHwnd);
      }

      if (captured) {
        DBG("[MaxPane] CaptureQueue: SUCCESS captured '%s' into pane %d after %d retries\n",
            pc.displayName, pc.paneId, pc.retryCount);
        anyCaptured = true;
      } else {
        DBG("[MaxPane] CaptureQueue: CAPTURE FAILED for '%s' hwnd=%p (already captured or pane full?)\n",
            pc.displayName, (void*)found);
        // Audit M1.2 — surface what used to be a Release-silent failure.
        snprintf(m_lastFailureToast, sizeof(m_lastFailureToast),
                 "Couldn't capture '%.180s' — pane is full or window is already captured.",
                 pc.displayName[0] ? pc.displayName : "(unknown)");
      }
      Remove(i);
    } else if (pc.retryCount >= pc.maxRetries) {
      DBG("[MaxPane] CaptureQueue: FAILED '%s' after %d retries\n",
          pc.displayName, pc.retryCount);
      // Audit M1.2 — the user's workspace tab silently never appeared here.
      snprintf(m_lastFailureToast, sizeof(m_lastFailureToast),
               "Couldn't restore '%.180s' — window not found.",
               pc.displayName[0] ? pc.displayName : "(unknown)");
      // Diagnostic: dump all window titles to help discover actual title
      WindowManager::DumpAllWindowTitles(pc.displayName);
      Remove(i);
    }
  }

  return anyCaptured;
}

bool CaptureQueue::HasPending() const
{
  for (int i = 0; i < m_count; i++) {
    if (m_queue[i].state == PendingCapture::WAITING ||
        m_queue[i].state == PendingCapture::RETRYING) {
      return true;
    }
  }
  return false;
}

void CaptureQueue::CancelAll()
{
  m_count = 0;
  for (int i = 0; i < MAX_PENDING; i++) m_queue[i] = PendingCapture{};
}

const char* CaptureQueue::PopFailureToast()
{
  if (!m_lastFailureToast[0]) return nullptr;
  // Return a stable pointer into the member buffer; caller copies or
  // displays before next Tick mutates the slot. We clear after one read so
  // the same failure doesn't toast on every OnTimer cycle.
  static char s_returnBuf[256];
  safe_strncpy(s_returnBuf, m_lastFailureToast, sizeof(s_returnBuf));
  m_lastFailureToast[0] = '\0';
  return s_returnBuf;
}

void CaptureQueue::Remove(int idx)
{
  if (idx < 0 || idx >= m_count) return;
  for (int i = idx; i < m_count - 1; i++) {
    m_queue[i] = m_queue[i + 1];
  }
  m_count--;
  m_queue[m_count] = PendingCapture{};
}
