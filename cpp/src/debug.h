#pragma once
#ifdef _MSC_VER
// getenv / fopen / setbuf below: the plugin target defines
// _CRT_SECURE_NO_WARNINGS, the unit-test target does not (C4996 noise).
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include <cstdio>
#include <cstdlib>
#include <chrono>

// v2.5.0 — the debug log is a RUNTIME switch in every build (forum #90
// LorenzoB: "a 2.4.1 with logging enabled for everyone ... many of us are
// still debugging MaxPane with different setups"). Debug builds log
// unconditionally (g_dbgEnabled defaults true); Release builds log only
// while Settings → "Write debug log" is on (ExtState `debug_log`, read at
// plugin entry + flipped live on Settings OK). Heavier probes that dump
// window trees stay behind `#ifdef MAXPANE_DEBUG`.
extern bool g_dbgEnabled;

// inline (not static): a TU that includes debug.h but never calls DBG()
// trips clang's -Wunused-function on a static definition under the CI
// -Werror gate (caught on the gate's first real run, v2.2.0).
// Full path of the debug log — shared by dbgFile() and the Settings "Show
// log file..." button (v2.5.0, owner request), so both can never disagree.
inline void MaxPaneDebugLogPath(char* buf, int bufSize)
{
  if (!buf || bufSize <= 0) return;
#ifdef _WIN32
  // Windows has no /tmp. Resolve %TEMP% / %TMP% from environment, fall back
  // to C:\Windows\Temp so DBG() still works in stripped sandbox envs.
  const char* tmp = getenv("TEMP");
  if (!tmp) tmp = getenv("TMP");
  if (!tmp) tmp = "C:\\Windows\\Temp";
  snprintf(buf, (size_t)bufSize, "%s\\maxpane_debug.log", tmp);
#else
  snprintf(buf, (size_t)bufSize, "%s", "/tmp/maxpane_debug.log");
#endif
}

inline FILE* dbgFile()
{
  static FILE* f = nullptr;
  if (!f) {
    char path[512];
    MaxPaneDebugLogPath(path, sizeof(path));
    f = fopen(path, "a");
    // v2.5.0 perf (ADR-093 #8): the log used to be fully unbuffered — two
    // syscalls per line, and the per-tab [DRIFT*] lines fire at 20 Hz during
    // a splitter drag (Windows AV real-time scanning made each write
    // measurable). Line-buffered on POSIX (a line lands as it is written);
    // the MS CRT treats _IOLBF as full buffering, so Windows gets an 8 KB
    // buffer + the periodic MaxPaneDebugLogFlush() from the 500 ms tick.
#ifdef _WIN32
    if (f) setvbuf(f, nullptr, _IOFBF, 8192);
#else
    if (f) setvbuf(f, nullptr, _IOLBF, 4096);
#endif
  }
  return f;
}

// Called from the container's 500 ms OnTimer (and the startup timer) so a
// buffered log on Windows never lags more than a tick; no-op when off.
inline void MaxPaneDebugLogFlush()
{
  if (!g_dbgEnabled) return;
  FILE* f = dbgFile();
  if (f) fflush(f);
}
// Milliseconds since first DBG call — timing bugs (startup lag, capture
// latency, watchdog cadence) are undiagnosable from an untimed log.
inline long dbgElapsedMs()
{
  static const auto t0 = std::chrono::steady_clock::now();
  return (long)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
}
#define DBG(...) do { if (g_dbgEnabled) { FILE* _f = dbgFile(); if (_f) { \
    fprintf(_f, "[%6ld] ", dbgElapsedMs()); \
    fprintf(_f, __VA_ARGS__); } } } while(0)
#ifdef _MSC_VER
#pragma warning(pop)
#endif
