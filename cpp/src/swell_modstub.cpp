// Legacy — kept for potential Linux build. Not used on macOS (see CMakeLists.txt).
// Custom SWELL modstub wrapper for modern C++ toolchains.
// The original swell-modstub-generic.cpp wraps swell.h inside extern "C",
// which breaks with modern libc++ headers that use C++ templates in <cstddef>.
// Solution: include swell-types.h OUTSIDE extern "C", then only wrap function
// pointer declarations inside extern "C".

#ifdef SWELL_PROVIDED_BY_APP

#define WDL_NO_DEFINE_MINMAX
// Include types first (outside extern "C") — this pulls in <cstddef> etc.
#include "swell/swell-types.h"

// Define function pointer variables (not inside extern "C" — they're C++ globals)
#define SWELL_API_DEFPARM(x)
#define SWELL_API_DEFINE(ret,func,parms) ret (*func) parms ;
#include "swell/swell-functions.h"

// Exported symbols for REAPER
struct SWELL_CursorResourceIndex *SWELL_curmodule_cursorresource_head;
struct SWELL_DialogResourceIndex *SWELL_curmodule_dialogresource_head;
struct SWELL_MenuResourceIndex *SWELL_curmodule_menuresource_head;

// Build lookup table for loading functions at runtime
static struct {
  const char *name;
  void **func;
} api_tab[] = {

#undef _WDL_SWELL_H_API_DEFINED_
#undef SWELL_API_DEFINE
#define SWELL_API_DEFINE(ret, func, parms) {#func, (void **)&func },
#include "swell/swell-functions.h"

};

static int dummyFunc() { return 0; }

// This is the entry point REAPER calls to provide SWELL function pointers
extern "C" __attribute__ ((visibility ("default")))
int SWELL_dllMain(HINSTANCE hInst, DWORD callMode, LPVOID _GetFunc)
{
  if (callMode == DLL_PROCESS_ATTACH)
  {
    if (!_GetFunc) return 0;
    void *(*getFunc)(const char *) = (void *(*)(const char *))_GetFunc;
    for (int x = 0; x < (int)(sizeof(api_tab)/sizeof(api_tab[0])); x++)
    {
      *api_tab[x].func = getFunc(api_tab[x].name);
      if (!*api_tab[x].func)
      {
        *api_tab[x].func = (void*)&dummyFunc;
      }
    }
  }
  // Return 1 to indicate DllMain should be called if available
  return 1;
}

#endif // SWELL_PROVIDED_BY_APP
