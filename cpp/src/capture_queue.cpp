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

void CaptureQueue::EnqueueKnown(int paneId, int knownIdx)
{
  if (m_count >= MAX_PENDING) return;
  if (knownIdx < 0 || knownIdx >= NUM_KNOWN_WINDOWS) return;

  const WindowDef& def = KNOWN_WINDOWS[knownIdx];

  PendingCapture& pc = m_queue[m_count];
  memset(&pc, 0, sizeof(PendingCapture));
  pc.state = PendingCapture::WAITING;
  pc.paneId = paneId;
  pc.knownWindowIndex = knownIdx;
  strncpy(pc.searchTitle, def.searchTitle, sizeof(pc.searchTitle) - 1);
  if (def.altSearchTitle) {
    strncpy(pc.altSearchTitle, def.altSearchTitle, sizeof(pc.altSearchTitle) - 1);
  }
  strncpy(pc.displayName, def.name, sizeof(pc.displayName) - 1);
  pc.toggleAction = def.toggleActionId;
  pc.isArbitrary = false;
  pc.tickCount = 0;
  pc.retryCount = 0;
  pc.maxRetries = MAX_RETRIES;

  // Fire the toggle action to open the window — but only if it's currently closed.
  // If already open (state=1), don't toggle or we'd close it.
  if (g_Main_OnCommand) {
    bool alreadyOpen = false;
    if (g_GetToggleCommandState) {
      int state = g_GetToggleCommandState(def.toggleActionId);
      alreadyOpen = (state == 1);
      DBG("[ReDockIt] CaptureQueue: toggle state for '%s' (action %d) = %d\n",
          def.name, def.toggleActionId, state);
    }
    if (!alreadyOpen) {
      g_Main_OnCommand(def.toggleActionId, 0);
    }
  }

  m_count++;
  DBG("[ReDockIt] CaptureQueue: enqueued known '%s' for pane %d (count=%d)\n",
      def.name, paneId, m_count);
}

void CaptureQueue::EnqueueArbitrary(int paneId, const char* name, int toggleAction, const char* actionCmd)
{
  if (m_count >= MAX_PENDING) return;
  if (!name || !name[0]) return;

  PendingCapture& pc = m_queue[m_count];
  memset(&pc, 0, sizeof(PendingCapture));
  pc.state = PendingCapture::WAITING;
  pc.paneId = paneId;
  pc.knownWindowIndex = -1;
  strncpy(pc.searchTitle, name, sizeof(pc.searchTitle) - 1);
  strncpy(pc.displayName, name, sizeof(pc.displayName) - 1);
  pc.toggleAction = toggleAction;
  pc.isArbitrary = true;
  pc.tickCount = 0;
  pc.retryCount = 0;
  pc.maxRetries = (toggleAction > 0) ? MAX_RETRIES : MAX_RETRIES_ARBITRARY;
  if (actionCmd && actionCmd[0]) {
    strncpy(pc.actionCommand, actionCmd, sizeof(pc.actionCommand) - 1);
  }

  // Fire the toggle action if we have one
  if (toggleAction > 0 && g_Main_OnCommand) {
    g_Main_OnCommand(toggleAction, 0);
  }

  m_count++;
  DBG("[ReDockIt] CaptureQueue: enqueued arbitrary '%s' action=%d cmd='%s' maxRetries=%d for pane %d (count=%d)\n",
      name, toggleAction, pc.actionCommand, pc.maxRetries, paneId, m_count);
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
          DBG("[ReDockIt] CaptureQueue: found inner '%s' hwnd=%p but no dock frame yet, waiting (retry=%d)\n",
              pc.displayName, (void*)found, pc.retryCount);
          continue;
        }
        if (!foundIsDockFrame) {
          DBG("[ReDockIt] CaptureQueue: no dock frame appeared for '%s' after %d retries, using inner window\n",
              pc.displayName, pc.retryCount);
        }
      }

      DBG("[ReDockIt] CaptureQueue: FOUND '%s' hwnd=%p retry=%d, attempting capture (arb=%d action=%d cmd='%s')\n",
          pc.displayName, (void*)found, pc.retryCount, pc.isArbitrary, pc.toggleAction, pc.actionCommand);
      bool captured = false;
      if (pc.isArbitrary) {
        captured = winMgr.CaptureArbitraryWindow(pc.paneId, found, pc.displayName, containerHwnd,
                                                    pc.toggleAction, pc.actionCommand);
      } else {
        captured = winMgr.CaptureByIndex(pc.paneId, pc.knownWindowIndex, containerHwnd);
      }

      if (captured) {
        DBG("[ReDockIt] CaptureQueue: SUCCESS captured '%s' into pane %d after %d retries\n",
            pc.displayName, pc.paneId, pc.retryCount);
        anyCaptured = true;
      } else {
        DBG("[ReDockIt] CaptureQueue: CAPTURE FAILED for '%s' hwnd=%p (already captured or pane full?)\n",
            pc.displayName, (void*)found);
      }
      Remove(i);
    } else if (pc.retryCount >= pc.maxRetries) {
      DBG("[ReDockIt] CaptureQueue: FAILED '%s' after %d retries\n",
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
