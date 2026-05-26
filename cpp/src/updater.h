// updater.h — manual "check for updates" trigger for v2.0.3.
//
// Synchronous HTTPS GET against the project's ReaPack index.xml on
// raw.githubusercontent.com. Extracts the first <version name="vX.Y.Z">
// tag and compares against MAXPANE_VERSION_STRING. If newer, shows a
// SWELL MessageBox with "Yes" / "No" — Yes opens the GitHub Releases
// page in the user's browser via OpenUrlPlatform.
//
// Synchronous on purpose: avoids the Cocoa main-thread-only rule for
// NSAlert / NSAlertWindow. The hit blocks the UI for ~1-3 seconds
// depending on connection; acceptable for a user-explicit Settings
// button press. v2.0.4+ should add a proper async path with timer-
// polled main-thread dispatch for the at-startup auto-check case.
#pragma once

#include "platform.h"

namespace Updater {

// Synchronous check. Blocks the calling thread (~1-3 s typical) for
// the HTTPS round-trip + XML parse. Posts the result modal under
// `parent`. If `showIfUpToDate` is true, also shows a confirmation
// modal when no newer version is found (manual-trigger UX from the
// Settings dialog). If false, silent on no-update.
void CheckForUpdatesNow(HWND parent, bool showIfUpToDate);

// v2.0.4 #3 — async auto-on-startup path. The HTTP layer and modal
// already exist; this adds a detached worker thread + atomic state +
// container OnTimer pickup so REAPER startup is not blocked by the
// ~1-3 s synchronous check. Cocoa NSAlert is main-thread-only, so the
// worker thread can't display the modal directly — it stages the
// result in an atomic enum and the main thread (container OnTimer)
// fires the modal once the result lands.

enum class AsyncResult {
  NotStarted,        // StartAsyncCheck never called
  InProgress,        // worker thread running
  NoUpdate,          // current build is latest
  UpdateAvailable,   // newer build found; GetLatestVersion() is valid
  NetworkError,      // HttpGetSync returned empty (offline, github down, …)
  ParseError,        // got XML but couldn't extract / parse the version tag
};

// Fire-and-forget. Spawns a detached worker thread on the first call
// per session; subsequent calls are no-ops (single-fire guard). Safe
// to call from anywhere on the main thread; non-blocking.
void StartAsyncCheck();

// Snapshot of the worker's current state. Safe to call from the main
// thread at any frequency. Container OnTimer polls this once per tick.
AsyncResult PollAsyncResult();

// Returns the latest version tag (e.g. "v2.0.4"); valid only after
// PollAsyncResult() returns UpdateAvailable. Pointer is stable for the
// rest of the session.
const char* GetLatestVersion();

// Show the "newer version available" modal under `parent`. Idempotent
// within a session: subsequent calls are no-ops (user dismissed once,
// don't pester). Container OnTimer calls this once when result lands.
void HandleUpdateAvailable(HWND parent);

}  // namespace Updater
