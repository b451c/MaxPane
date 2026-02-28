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

  // Fire the toggle action to open the window
  if (g_Main_OnCommand) {
    g_Main_OnCommand(def.toggleActionId, 0);
  }

  m_count++;
  DBG("[ReDockIt] CaptureQueue: enqueued known '%s' for pane %d (count=%d)\n",
      def.name, paneId, m_count);
}

void CaptureQueue::EnqueueArbitrary(int paneId, const char* name, int toggleAction)
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

  // Fire the toggle action if we have one
  if (toggleAction > 0 && g_Main_OnCommand) {
    g_Main_OnCommand(toggleAction, 0);
  }

  m_count++;
  DBG("[ReDockIt] CaptureQueue: enqueued arbitrary '%s' action=%d for pane %d (count=%d)\n",
      name, toggleAction, paneId, m_count);
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
      bool captured = false;
      if (pc.isArbitrary) {
        captured = winMgr.CaptureArbitraryWindow(pc.paneId, found, pc.displayName, containerHwnd);
      } else {
        captured = winMgr.CaptureByIndex(pc.paneId, pc.knownWindowIndex, containerHwnd);
      }

      if (captured) {
        DBG("[ReDockIt] CaptureQueue: captured '%s' after %d retries\n",
            pc.displayName, pc.retryCount);
        anyCaptured = true;
      }
      Remove(i);
    } else if (pc.retryCount >= MAX_RETRIES) {
      DBG("[ReDockIt] CaptureQueue: FAILED '%s' after %d retries\n",
          pc.displayName, pc.retryCount);
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
