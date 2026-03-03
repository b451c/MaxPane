#include "capture_queue.h"
#include "config.h"
#include "globals.h"
#include "debug.h"
#include <cstring>
#include <cstdio>

CaptureQueue::CaptureQueue()
  : m_count(0)
{
  memset(m_queue, 0, sizeof(m_queue));
}

void CaptureQueue::EnqueueKnown(int paneId, int knownIdx, bool deferAction)
{
  if (m_count >= MAX_PENDING) return;
  if (knownIdx < 0 || knownIdx >= NUM_KNOWN_WINDOWS) return;

  const WindowDef& def = KNOWN_WINDOWS[knownIdx];

  PendingCapture& pc = m_queue[m_count];
  memset(&pc, 0, sizeof(PendingCapture));
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

  PendingCapture& pc = m_queue[m_count];
  memset(&pc, 0, sizeof(PendingCapture));
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

  // Fire the toggle action if we have one.
  // If deferAction is set (called from LoadState during startup), skip Main_OnCommand
  // to avoid deadlocking REAPER during project load. The action will fire on first Tick.
  if (!deferAction && toggleAction > 0 && g_Main_OnCommand) {
    g_Main_OnCommand(toggleAction, 0);
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
        snprintf(dockedTitle, sizeof(dockedTitle), "%s (docked)", pc.searchTitle);
        char foundTitle[512];
        GetWindowText(found, foundTitle, sizeof(foundTitle));

        bool foundIsDockFrame = (strstr(foundTitle, "(docked)") != nullptr);
        if (!foundIsDockFrame && pc.retryCount <= 8) {
          // We found the inner window but no dock frame yet — wait for it
          if (pc.retryCount == 1) {
            // Dump all windows once to see if dock frame exists anywhere
            WindowManager::DumpAllWindowTitles(pc.displayName);
          }
          DBG("[MaxPane] CaptureQueue: found inner '%s' hwnd=%p but no dock frame yet, waiting (retry=%d)\n",
              pc.displayName, (void*)found, pc.retryCount);
          continue;
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
      }
      Remove(i);
    } else if (pc.retryCount >= pc.maxRetries) {
      DBG("[MaxPane] CaptureQueue: FAILED '%s' after %d retries\n",
          pc.displayName, pc.retryCount);
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
  memset(m_queue, 0, sizeof(m_queue));
}

void CaptureQueue::Remove(int idx)
{
  if (idx < 0 || idx >= m_count) return;
  for (int i = idx; i < m_count - 1; i++) {
    m_queue[i] = m_queue[i + 1];
  }
  m_count--;
  memset(&m_queue[m_count], 0, sizeof(PendingCapture));
}
