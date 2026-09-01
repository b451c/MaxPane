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

// Registration `param` per instance. It MUST be a real pointer: REAPER keeps
// only the registration whose param is 0 when the params are small integers
// (0..7 as (void*)i left instances 2-8 unregistered — no SAVE/LOAD callbacks
// ever, "container 2 ignores window sets", LorenzoB #90; verified
// 2026-09-02 with reaper-screensets.ini: 101 -> 108 entries once the params
// became pointers). The callback maps the pointer back to the instance id.
static int s_ssParam[MaxPaneContainer::MAX_INSTANCES];

static int InstFromParam(const void* param)
{
  for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++)
    if (param == (const void*)&s_ssParam[i]) return i;
  return -1;
}

static void BuildScreensetId(int i, char* out, size_t n)
{
  if (i == 0) snprintf(out, n, "MaxPane");
  else        snprintf(out, n, "MaxPane_%d", i);
}

// =========================================================================
// A1 (v2.4.0) — deferred screenset LOAD replay.
//
// LOAD_STATE used to silently DROP the blob whenever it arrived inside the
// project-restore window (guard in the callback below). A screenset recalled
// from a REAPER startup action always lands inside that window — the timer
// arms on EVERY project load and stays up the full 20-tick poll for
// stateless projects — so "MaxPane doesn't load at startup, but the same
// shortcut works a second later" (LorenzoB #76). actionParm is only valid
// during the callback, so the blob is heap-stashed per instance and replayed
// once the restore settles. Semantics: replay-always (owner decision D3,
// 2026-07-09) — after settling, the screenset applies exactly like a manual
// press (screenset-wins; in a state-carrying project that is a brief,
// honest layout swap).
// =========================================================================
struct ScreensetStash {
  char* blob = nullptr;
  int   len = 0;
  int   settleTicks = 0;  // consecutive guard-clear ticks observed
  int   ageTicks = 0;     // total ticks since stashed (drop cap)
};
static ScreensetStash s_stash[MaxPaneContainer::MAX_INSTANCES];
static bool s_replayTimerArmed = false;

static void ApplyScreensetBlob(int instId, const char* blob, int blobLen);

static void FreeStash(int i)
{
  delete[] s_stash[i].blob;
  s_stash[i] = ScreensetStash();
}

static void screensetReplayTimerFunc()
{
  bool anyLeft = false;
  for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++) {
    ScreensetStash& st = s_stash[i];
    if (!st.blob) continue;
    st.ageTicks++;
    const bool guardUp = g_pendingProjectState[i].valid || g_projOpenTimerActive;
    if (guardUp) {
      st.settleTicks = 0;
      if (st.ageTicks > 100) {  // ~3 s — something is genuinely mid-flight;
        // with the rppReadyTimer consumption fix the pending arm can no
        // longer stick, so expiry means dropping is the conservative choice.
        DBG("[MaxPane] screenset replay inst %d: cap expired, dropping\n", i);
        FreeStash(i);
      } else {
        anyLeft = true;
      }
      continue;
    }
    // Settle margin: let the project restore's deferred capture queue drain
    // before we swap the layout out from under it.
    if (++st.settleTicks < 5) { anyLeft = true; continue; }
    DBG("[MaxPane] screenset replay inst %d: applying stashed blob (%d bytes)\n",
        i, st.len);
    char* blob = st.blob;
    int len = st.len;
    st.blob = nullptr;      // detach before Apply (Apply may take time)
    FreeStash(i);           // resets counters; blob already detached
    ApplyScreensetBlob(i, blob, len);
    delete[] blob;
  }
  if (!anyLeft && s_replayTimerArmed && g_plugin_register) {
    g_plugin_register("-timer", (void*)(void(*)())screensetReplayTimerFunc);
    s_replayTimerArmed = false;
  }
}

static void StashScreensetBlob(int instId, const char* blob, int blobLen)
{
  FreeStash(instId);  // last-write-wins
  s_stash[instId].blob = new char[(size_t)blobLen + 1];
  memcpy(s_stash[instId].blob, blob, (size_t)blobLen);
  s_stash[instId].blob[blobLen] = '\0';
  s_stash[instId].len = blobLen;
  if (!s_replayTimerArmed && g_plugin_register) {
    g_plugin_register("timer", (void*)(void(*)())screensetReplayTimerFunc);
    s_replayTimerArmed = true;
  }
}

// Parse a "KEY VALUE\n" blob into the shared pending buffer and drive the
// SAME restore funnel the RPP/ProjExtState paths use (LoadState consumes
// it). Shared by the immediate LOAD_STATE path and the deferred replay.
static void ApplyScreensetBlob(int instId, const char* blob, int blobLen)
{
  MaxPaneContainer* c = InstanceManager::Get().GetOrCreate(instId);
  if (!c) return;

  PendingProjectState& slot = g_pendingProjectState[instId];
  slot.reading = false;
  slot.lineCount = 0;
  const char* p = blob;
  const size_t bl = strnlen(p, (size_t)blobLen);
  const char* end = p + bl;
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
  if (!slot.valid) return;

  if (c->IsAlive()) {          // A3 — a docker-X zombie must rebuild, not reload
    c->ReloadProjectState();   // CancelAll + ReleaseAll + LoadState + reposition
  } else {
    c->Create();               // Create → LoadState consumes the pending blob
  }
  // Ensure visible — a recalled window set means "show MaxPane with this layout".
  MaxPaneContainer* live = InstanceManager::Get().GetExisting(instId);
  if (live && live->IsAlive() && !live->IsVisible()) live->Show();
}

// =========================================================================
// Screenset callback — REAPER invokes this for save/load/query of one slot.
// param carries the instance id (set at registration).
// =========================================================================
static LRESULT MaxPaneScreensetCallback(int action, const char* /*id*/, void* param,
                                        void* actionParm, int actionParmSize)
{
  const int instId = InstFromParam(param);
  if (instId < 0) return 0;

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
        FreeStash(instId);  // A1 — a hide supersedes any pending replay
        if (c->IsAlive() && c->IsVisible()) {
          DBG("[MaxPane] screenset LOAD inst %d: hide\n", instId);
          c->Toggle();  // visible → hidden
        }
        return 0;
      }

      // Reconciliation with the project-load restore (F-39): if a project is
      // mid-load — an RPP <MAXPANE_STATE> chunk is parsed-but-not-yet-consumed,
      // OR the ProjExtState open-timer is in flight — the project path is
      // authoritative NOW. A1 (v2.4.0): do NOT drop the blob — stash it and
      // replay once the restore settles (D3 replay-always), or a screenset
      // recalled from a startup action silently never applies.
      if (g_pendingProjectState[instId].valid || g_projOpenTimerActive) {
        DBG("[MaxPane] screenset LOAD inst %d: deferring to in-flight project restore (stashed)\n",
            instId);
        StashScreensetBlob(instId, (const char*)actionParm, actionParmSize);
        return 0;
      }

      ApplyScreensetBlob(instId, (const char*)actionParm, actionParmSize);
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
    s_ssParam[i] = i;
    g_screenset_registerNew(s_ssId[i], (void*)MaxPaneScreensetCallback, (void*)&s_ssParam[i]);
    DBG("[MaxPane] screenset register id='%s' inst %d\n", s_ssId[i], i);
  }
}

void MaxPaneScreenset::UnregisterAll()
{
  // A1 (v2.4.0) — kill the replay timer and free stashes BEFORE the
  // screenset ids go away (atexit path: no replay may fire mid-teardown).
  if (s_replayTimerArmed && g_plugin_register) {
    g_plugin_register("-timer", (void*)(void(*)())screensetReplayTimerFunc);
    s_replayTimerArmed = false;
  }
  for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++) FreeStash(i);

  if (!g_screenset_unregister) return;
  for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++) {
    if (s_ssId[i][0]) g_screenset_unregister(s_ssId[i]);
  }
}
