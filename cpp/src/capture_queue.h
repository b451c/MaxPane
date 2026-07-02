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
  int maxRetries = 30;
  char actionCommand[128] = {};  // stable command string for re-resolve after restart
  bool actionDeferred = false;   // true = Main_OnCommand not yet called (deferred from LoadState)
  // v2.0.4 #1 (ADR-037) — FX plugin identity. When non-empty Tick skips
  // FindReaperWindow and instead resolves via FxCapture::ResolveLocation +
  // ShowAndGetHwnd. Format: "fx@{track_guid}@{fx_guid}@<flags>" or
  // "takefx@{take_guid}@{fx_guid}@<flags>".
  char fxIdentity[128] = {};
  // U14 (ADR-070) — follow-mode tab: excluded from every persistence writer
  // (workspace slots, ProjExtState, RPP chunk); replaced on track switch.
  bool transient = false;
};

class CaptureQueue {
public:
  static const int MAX_PENDING = 32;
  static const int INITIAL_WAIT_TICKS = 10;  // 500ms at 50ms/tick
  static const int RETRY_INTERVAL = 4;       // 200ms
  static const int MAX_RETRIES = 30;          // ~1.5s for known windows with toggle
  static const int MAX_RETRIES_ARBITRARY = 200; // ~10s for arbitrary (user may reopen manually)

  CaptureQueue();

  void EnqueueKnown(int paneId, int knownIdx, bool deferAction = false);
  void EnqueueArbitrary(int paneId, const char* name, int toggleAction = 0, const char* actionCmd = nullptr, bool deferAction = false, bool transient = false);
  bool Tick(HWND containerHwnd, WindowManager& winMgr);  // returns true if any captured
  bool HasPending() const;
  void CancelAll();

  // Side-channel for capture-failure toasts (FX identity since v2.0.4 /
  // ADR-037; generalized to ALL capture failures in the 2026-06-10 audit —
  // retry exhaustion and pane-full used to be Release-silent). Tick fills
  // m_lastFailureToast with a user-readable string; Container drains it via
  // PopFailureToast() after each Tick. Cleared on pop (no re-toast loops).
  const char* PopFailureToast();

private:
  PendingCapture m_queue[MAX_PENDING];
  int m_count;
  char m_lastFailureToast[256] = {};
  void Remove(int idx);
};
