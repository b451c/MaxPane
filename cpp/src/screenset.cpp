#include "screenset.h"
#include "globals.h"
#include "container.h"
#include "instance_manager.h"
#include "project_state.h"     // g_pendingProjectState, g_projOpenTimerActive, WriteContainerState
#include "state_accessor.h"
#include "debug.h"
#include "reaper_plugin.h"     // SCREENSET_ACTION_*
#include <cstring>
#include <cstdio>
#include <cstdint>

// Stable per-instance screenset ids. REAPER stores each instance's blob keyed
// by this string, so it must be stable across runs.
static char s_ssId[MaxPaneContainer::MAX_INSTANCES][32];

static void BuildScreensetId(int i, char* out, size_t n)
{
  if (i == 0) snprintf(out, n, "MaxPane");
  else        snprintf(out, n, "MaxPane_%d", i);
}

// =========================================================================
// Screenset callback — REAPER invokes this for save/load/query of one slot.
// param carries the instance id (set at registration).
// =========================================================================
static LRESULT MaxPaneScreensetCallback(int action, const char* /*id*/, void* param,
                                        void* actionParm, int actionParmSize)
{
  const int instId = (int)(intptr_t)param;
  if (instId < 0 || instId >= MaxPaneContainer::MAX_INSTANCES) return 0;

  DBG("[MaxPane] screenset cb: action=0x%x inst=%d parm=%p size=%d\n",
      action, instId, actionParm, actionParmSize);

  switch (action) {
    case SCREENSET_ACTION_GETHWND: {
      MaxPaneContainer* c = InstanceManager::Get().GetExisting(instId);
      HWND h = c ? c->GetHwnd() : nullptr;
      DBG("[MaxPane] screenset GETHWND inst %d -> %p\n", instId, (void*)h);
      return (LRESULT)(intptr_t)h;
    }

    case SCREENSET_ACTION_IS_DOCKED: {
      MaxPaneContainer* c = InstanceManager::Get().GetExisting(instId);
      if (!c || !c->GetHwnd()) return 0;
      if (g_DockIsChildOfDock) {
        bool isFloat = false;  // SDK out-param — never pass nullptr (review major)
        return g_DockIsChildOfDock(c->GetHwnd(), &isFloat) >= 0 ? 1 : 0;
      }
      // Fallback: MaxPane's own floating mode means "not docked".
      return c->IsFloating() ? 0 : 1;
    }

    case SCREENSET_ACTION_SWITCH_DOCK: {
      MaxPaneContainer* c = InstanceManager::Get().GetExisting(instId);
      if (!c || !c->GetHwnd()) return 0;
      if (c->IsFloating()) c->RedockToContainer();
      else                 c->DetachToFloating();
      return 0;
    }

    case SCREENSET_ACTION_WANT_STATE_SIZE:
      // Generous — a deep tree with many captured tabs can exceed the 4096
      // default. REAPER "may or may not" honour this; SAVE_STATE bounds-checks.
      return 65536;

    case SCREENSET_ACTION_SAVE_STATE: {
      MaxPaneContainer* c = InstanceManager::Get().GetExisting(instId);
      if (!c || !c->GetHwnd() || !actionParm || actionParmSize <= 0) {
        DBG("[MaxPane] screenset SAVE inst %d: bail (c=%p hwnd=%p parm=%p size=%d)\n",
            instId, (void*)c, (void*)(c ? c->GetHwnd() : nullptr), actionParm, actionParmSize);
        return 0;
      }

      RppWriteAccessor acc;
      if (!WriteContainerState(*c, acc)) {
        DBG("[MaxPane] screenset SAVE inst %d: WriteContainerState=false (empty/corrupt)\n", instId);
        return 0;  // corrupt tree → save nothing
      }

      // Flatten "KEY VALUE\n" lines into the buffer (same format RppReadAccessor
      // parses). Never write a partial line if we'd overflow.
      char* out = (char*)actionParm;
      int used = 0;
      for (int i = 0; i < acc.GetCount(); i++) {
        int n = snprintf(out + used, (size_t)(actionParmSize - used), "%s %s\n",
                         acc.GetKey(i), acc.GetValue(i));
        if (n < 0 || used + n >= actionParmSize) { out[used] = '\0'; break; }
        used += n;
      }
      DBG("[MaxPane] screenset SAVE inst %d: %d bytes, %d kv\n", instId, used, acc.GetCount());
      return used;
    }

    case SCREENSET_ACTION_LOAD_STATE: {
      MaxPaneContainer* c = InstanceManager::Get().GetOrCreate(instId);
      if (!c) return 0;

      // NULL/NULL → hide.
      if (!actionParm || actionParmSize <= 0) {
        if (c->GetHwnd() && c->IsVisible()) {
          DBG("[MaxPane] screenset LOAD inst %d: hide\n", instId);
          c->Toggle();  // visible → hidden
        }
        return 0;
      }

      // Reconciliation with the project-load restore (F-39): if a project is
      // mid-load — an RPP <MAXPANE_STATE> chunk is parsed-but-not-yet-consumed,
      // OR the ProjExtState open-timer is in flight — the project path is
      // authoritative. Don't clobber the shared pending buffer or double-apply.
      if (g_pendingProjectState[instId].valid || g_projOpenTimerActive) {
        DBG("[MaxPane] screenset LOAD inst %d: deferring to in-flight project restore\n", instId);
        return 0;
      }

      // Parse the blob into the shared pending buffer, then drive the SAME
      // restore funnel the RPP/ProjExtState paths use (LoadState consumes it).
      PendingProjectState& slot = g_pendingProjectState[instId];
      slot.reading = false;
      slot.lineCount = 0;
      const char* p = (const char*)actionParm;
      const size_t blobLen = strnlen(p, (size_t)actionParmSize);
      const char* end = p + blobLen;
      while (p < end && slot.lineCount < RPP_MAX_LINES) {
        const char* nl = (const char*)memchr(p, '\n', (size_t)(end - p));
        const char* lineEnd = nl ? nl : end;
        int len = (int)(lineEnd - p);
        if (len > 0) {
          if (len >= RPP_MAX_LINE_LEN) len = RPP_MAX_LINE_LEN - 1;
          memcpy(slot.lines[slot.lineCount], p, (size_t)len);
          slot.lines[slot.lineCount][len] = '\0';
          slot.lineCount++;
        }
        if (!nl) break;
        p = nl + 1;
      }
      if (p < end)  // exited because we hit the 512-line RPP ceiling (review minor)
        DBG("[MaxPane] screenset LOAD inst %d: WARN blob truncated at %d lines (RPP_MAX_LINES)\n",
            instId, slot.lineCount);
      slot.valid = (slot.lineCount > 0);
      DBG("[MaxPane] screenset LOAD inst %d: %d lines, valid=%d\n",
          instId, slot.lineCount, slot.valid);
      if (!slot.valid) return 0;

      if (c->GetHwnd()) {
        c->ReloadProjectState();  // CancelAll + ReleaseAll + LoadState + reposition
      } else {
        c->Create();              // Create → LoadState consumes the pending blob
      }
      // Ensure visible — a recalled window set means "show MaxPane with this layout".
      MaxPaneContainer* live = InstanceManager::Get().GetExisting(instId);
      if (live && live->GetHwnd() && !live->IsVisible()) live->Show();
      return 0;
    }
  }
  return 0;
}

void MaxPaneScreenset::RegisterAll()
{
  if (!g_screenset_registerNew) return;
  for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++) {
    BuildScreensetId(i, s_ssId[i], sizeof(s_ssId[i]));
    g_screenset_registerNew(s_ssId[i], (void*)MaxPaneScreensetCallback, (void*)(intptr_t)i);
    DBG("[MaxPane] screenset register id='%s' inst %d\n", s_ssId[i], i);
  }
}

void MaxPaneScreenset::UnregisterAll()
{
  if (!g_screenset_unregister) return;
  for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++) {
    if (s_ssId[i][0]) g_screenset_unregister(s_ssId[i]);
  }
}
