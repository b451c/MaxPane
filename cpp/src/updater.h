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

}  // namespace Updater
