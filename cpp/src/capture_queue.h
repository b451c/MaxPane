#pragma once
#include "window_manager.h"

struct PendingCapture {
  enum State { IDLE, WAITING, RETRYING, DONE, FAILED };
  State state = IDLE;
  int paneId = -1;
  int knownWindowIndex = -1;    // -1 for arbitrary
  char searchTitle[256] = {};
  char altSearchTitle[256] = {};
  char displayName[256] = {};
  int toggleAction = 0;
  bool isArbitrary = false;
  int tickCount = 0;
  int retryCount = 0;
  int colorIndex = 0;
};

class CaptureQueue {
public:
  static const int MAX_PENDING = 32;
  static const int INITIAL_WAIT_TICKS = 10;  // 500ms at 50ms/tick
  static const int RETRY_INTERVAL = 4;       // 200ms
  static const int MAX_RETRIES = 30;

  CaptureQueue();

  void EnqueueKnown(int paneId, int knownIdx);
  void EnqueueArbitrary(int paneId, const char* name, int toggleAction = 0);
  bool Tick(HWND containerHwnd, WindowManager& winMgr);  // returns true if any captured
  bool HasPending() const;
  void CancelAll();

private:
  PendingCapture m_queue[MAX_PENDING];
  int m_count;
  void Remove(int idx);
};
