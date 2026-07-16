#include "project_state.h"
#include "config.h"
#include "globals.h"
#include "workspace_manager.h"
#include "state_accessor.h"
#include "instance_manager.h"
#include "fx_capture.h"   // A5/D4 — IsFxIdentity (waiting fx@ tabs are session-only)
#include "debug.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

PendingProjectState g_pendingProjectState[MaxPaneContainer::MAX_INSTANCES] = {};

// Main.cpp hooks are declared in project_state.h (audit M3.4) so signature
// drift is a compile error, not a link-time surprise.

// =========================================================================
// BeginLoadProjectState — reset buffers before loading
// =========================================================================

void OnBeginLoadProjectState(bool isUndo, project_config_extension_t* /*reg*/)
{
  if (isUndo) return;

  // Don't wipe valid data that hasn't been consumed yet. REAPER calls this
  // multiple times during a single project load. Our chunks may be parsed
  // early; a later OnBeginLoadProjectState would wipe them before LoadState()
  // gets to consume them.
  for (int i = 0; i < MaxPaneContainer::MAX_INSTANCES; i++) {
    if (g_pendingProjectState[i].valid) {
      DBG("[MaxPane] OnBeginLoadProjectState: skipping reset for inst %d (%d lines pending)\n",
          i, g_pendingProjectState[i].lineCount);
      continue;
    }
    g_pendingProjectState[i].reading = false;
    g_pendingProjectState[i].lineCount = 0;
  }

  // F-39 — a project may carry MaxPane state in per-project ProjExtState rather
  // than a <MAXPANE_STATE> chunk (the chunk is only written when a container is
  // open at save time). That path never reaches OnRppStateReady, so without
  // this hook MaxPane would not auto-open and the captured windows would come
  // back floating. Arm a deferred poll that force-opens any instance whose
  // ProjExtState is present once the project finishes loading.
  OnProjectLoadMaybeOpen();
}

// =========================================================================
// ProcessExtensionLine — read <MAXPANE_STATE[_N]> chunks from RPP
// =========================================================================
//
// Returns instance id parsed from the chunk opener, or -1 if not our chunk.
static int InstanceIdFromChunkOpener(const char* line)
{
  // Legacy chunk for instance 0
  if (strcmp(line, "<MAXPANE_STATE") == 0) return 0;
  // Suffixed chunks: "<MAXPANE_STATE_N" for N >= 1
  const char* prefix = "<MAXPANE_STATE_";
  size_t plen = strlen(prefix);
  if (strncmp(line, prefix, plen) != 0) return -1;
  const char* tail = line + plen;
  if (*tail < '0' || *tail > '9') return -1;
  int n = atoi(tail);
  if (n < 1 || n >= MaxPaneContainer::MAX_INSTANCES) return -1;
  return n;
}

bool OnProcessExtensionLine(const char* line, ProjectStateContext* ctx,
                            bool isUndo, project_config_extension_t* /*reg*/)
{
  if (isUndo) return false;

  int instId = InstanceIdFromChunkOpener(line);
  if (instId < 0) return false;

  PendingProjectState& slot = g_pendingProjectState[instId];
  DBG("[MaxPane] ProcessExtensionLine: matched '%s' → instance %d\n", line, instId);
  slot.lineCount = 0;
  slot.reading = true;

  // Read subsequent lines until '>'
  char buf[RPP_MAX_LINE_LEN];
  while (ctx->GetLine(buf, sizeof(buf)) >= 0) {
    const char* p = buf;
    while (*p == ' ' || *p == '\t') p++;
    if (p[0] == '>') break;

    if (slot.lineCount < RPP_MAX_LINES) {
      safe_strncpy(slot.lines[slot.lineCount], p, RPP_MAX_LINE_LEN);
      slot.lineCount++;
    }
  }

  slot.reading = false;
  slot.valid = (slot.lineCount > 0);
  DBG("[MaxPane] ProcessExtensionLine: inst %d read %d lines, valid=%d\n",
      instId, slot.lineCount, slot.valid);

  if (slot.valid) OnRppStateReady();

  return true;  // we consumed these lines
}

// =========================================================================
// SaveExtensionConfig — write one <MAXPANE_STATE[_N]> chunk per live instance
// =========================================================================

// Shared serialization (declared in project_state.h). Writes tree_version +
// tree nodes + compact pane tabs into `acc`. Single source of truth for both
// the RPP <MAXPANE_STATE> chunk (below) and the screenset SAVE_STATE path
// (screenset.cpp, ADR-049). Returns false on a corrupt tree (write nothing).
bool WriteContainerState(MaxPaneContainer& container, StateAccessor& acc)
{
  // A3 (v2.4.0) — IsAlive, not GetHwnd: after a docker-X close the dangling
  // hwnd kept passing this guard, so a zombie instance wrote a layout-only
  // chunk at every project save — the unstoppable-resurrection source
  // (LorenzoB #79). Also kills the screenset SAVE_STATE zombie blob (same
  // writer, ADR-049).
  if (!container.IsAlive()) return false;

  const SplitTree& tree = container.GetTree();
  const WindowManager& winMgr = container.GetWinMgr();
  const char* section = container.ExtSection();

  acc.Set(section, "tree_version", "2", true);

  NodeSnapshot snap[MAX_TREE_NODES];
  int nodeCount = 0;
  tree.SaveSnapshot(snap, nodeCount);

  for (int i = 0; i < nodeCount; i++) {
    if (snap[i].type == NODE_BRANCH && snap[i].childA == snap[i].childB) {
      DBG("[MaxPane] WriteContainerState: inst %d corrupt tree node %d, skipping\n",
          container.InstanceId(), i);
      return false;
    }
  }

  WorkspaceManager::WriteTreeNodesStatic(section, "", snap, nodeCount, acc);

  // Write pane tabs compactly — skip empty panes and empty tab slots.
  // (Avoids the stale-clearing entries that WritePaneTabsStatic emits for ExtState.)
  char buf[256];
  char key[128];
  for (int p = 0; p < MAX_PANES; p++) {
    const PaneState* ps = winMgr.GetPaneState(p);
    if (!ps) continue;
    // F11 (ADR-078) — a follow-only pane typically has ZERO persistent tabs
    // (the slot tab is transient), but its ASSIGNMENT must still reach the
    // RPP chunk / screenset blob — emit it before any empty-pane skip.
    if (ps->followSlot >= 0) {
      snprintf(key, sizeof(key), "pane_%d_follow_slot", p);
      snprintf(buf, sizeof(buf), "%d", ps->followSlot);
      acc.Set(section, key, buf, true);
    }
    if (ps->tabCount == 0) continue;

    // U14 (ADR-070) — transient (follow-mode) tabs are never persisted.
    // A5/D4 (v2.4.0) — waiting fx@ tabs are session-only (see the identical
    // skip in WritePaneTabsStatic; restore would re-OPEN the closed float).
    int persistCount = 0;
    for (int t = 0; t < ps->tabCount; t++) {
      const TabEntry& te = ps->tabs[t];
      if (te.transient) continue;
      if (!te.captured && FxCapture::IsFxIdentity(te.actionCmd)) continue;
      persistCount++;
    }
    if (persistCount == 0) continue;

    snprintf(key, sizeof(key), "pane_%d_tab_count", p);
    snprintf(buf, sizeof(buf), "%d", persistCount);
    acc.Set(section, key, buf, true);

    snprintf(key, sizeof(key), "pane_%d_active_tab", p);
    snprintf(buf, sizeof(buf), "%d",
             (ps->activeTab >= 0 && ps->activeTab < persistCount) ? ps->activeTab : 0);
    acc.Set(section, key, buf, true);

    int outIdx = 0;
    for (int t = 0; t < ps->tabCount; t++) {
      const TabEntry& tab = ps->tabs[t];
      if (tab.transient) continue;
      if (!tab.captured && FxCapture::IsFxIdentity(tab.actionCmd)) continue;  // A5/D4
      snprintf(key, sizeof(key), "pane_%d_tab_%d", p, outIdx);
      if (tab.name[0]) {
        if (tab.isArbitrary) {
          char cmdStr[128] = "0";
          if (tab.actionCmd[0]) {
            safe_strncpy(cmdStr, tab.actionCmd, sizeof(cmdStr));
          } else if (tab.toggleAction > 0) {
            GetActionCommandString(tab.toggleAction, cmdStr, sizeof(cmdStr));
          }
          char val[512];
          snprintf(val, sizeof(val), "arb:%s:%s", cmdStr, tab.name);
          acc.Set(section, key, val, true);
        } else {
          acc.Set(section, key, tab.name, true);
        }
      }
      snprintf(key, sizeof(key), "pane_%d_tab_%d_color", p, outIdx);
      snprintf(buf, sizeof(buf), "%d", tab.colorIndex);
      acc.Set(section, key, buf, true);
      outIdx++;
    }
  }
  return true;
}

static void WriteOneInstanceChunk(ProjectStateContext* ctx, MaxPaneContainer& container)
{
  RppWriteAccessor writeAcc;
  if (!WriteContainerState(container, writeAcc)) return;

  ctx->AddLine("<%s", container.RppChunkTag());
  DBG("[MaxPane] SaveExtensionConfig: inst %d writing %d kv lines\n",
      container.InstanceId(), writeAcc.GetCount());
  for (int i = 0; i < writeAcc.GetCount(); i++) {
    ctx->AddLine("%s %s", writeAcc.GetKey(i), writeAcc.GetValue(i));
  }
  ctx->AddLine(">");
}

void OnSaveExtensionConfig(ProjectStateContext* ctx, bool isUndo,
                           project_config_extension_t* /*reg*/)
{
  if (isUndo) return;
  InstanceManager::Get().ForEach([&](int /*id*/, MaxPaneContainer& c) {
    WriteOneInstanceChunk(ctx, c);
  });
}
