#pragma once
#include <cstdio>

#ifdef MAXPANE_DEBUG
static FILE* dbgFile()
{
  static FILE* f = nullptr;
  if (!f) {
    f = fopen("/tmp/maxpane_debug.log", "a");
    if (f) setbuf(f, nullptr);
  }
  return f;
}
#define DBG(...) do { FILE* _f = dbgFile(); if (_f) { fprintf(_f, __VA_ARGS__); } } while(0)
#else
#define DBG(...) ((void)0)
#endif
