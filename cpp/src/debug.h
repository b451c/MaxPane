#pragma once
#include <cstdio>
#include <cstdlib>

#ifdef MAXPANE_DEBUG
// inline (not static): a TU that includes debug.h but never calls DBG()
// trips clang's -Wunused-function on a static definition under the CI
// -Werror gate (caught on the gate's first real run, v2.2.0).
inline FILE* dbgFile()
{
  static FILE* f = nullptr;
  if (!f) {
#ifdef _WIN32
    // Windows has no /tmp. Resolve %TEMP% / %TMP% from environment, fall back
    // to C:\Windows\Temp so DBG() still works in stripped sandbox envs.
    char path[260];
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Windows\\Temp";
    snprintf(path, sizeof(path), "%s\\maxpane_debug.log", tmp);
    f = fopen(path, "a");
#else
    f = fopen("/tmp/maxpane_debug.log", "a");
#endif
    if (f) setbuf(f, nullptr);
  }
  return f;
}
#define DBG(...) do { FILE* _f = dbgFile(); if (_f) { fprintf(_f, __VA_ARGS__); } } while(0)
#else
#define DBG(...) ((void)0)
#endif
