#include "project_state.h"
#include "config.h"
#include "globals.h"
#include "workspace_manager.h"
#include "state_accessor.h"
#include "instance_manager.h"
#include "debug.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

PendingProjectState g_pendingProjectState[MaxPaneContainer::MAX_INSTANCES] = {};

// Forward declarations — defined in main.cpp
extern MaxPaneContainer* GetContainer();
extern void OnRppStateReady();

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

static void WriteOneInstanceChunk(ProjectStateContext* ctx, MaxPaneContainer& container)
{
  if (!container.GetHwnd()) return;

  RppWriteAccessor writeAcc;
  const SplitTree& tree = container.GetTree();
  const WindowManager& winMgr = container.GetWinMgr();
  const char* section = container.ExtSection();

  writeAcc.Set(section, "tree_version", "2", true);

  NodeSnapshot snap[MAX_TREE_NODES];
  int nodeCount = 0;
  tree.SaveSnapshot(snap, nodeCount);

  for (int i = 0; i < nodeCount; i++) {
    if (snap[i].type == NODE_BRANCH && snap[i].childA == snap[i].childB) {
      DBG("[MaxPane] SaveExtensionConfig: inst %d corrupt tree node %d, skipping chunk\n",
          container.InstanceId(), i);
      return;
    }
  }

  WorkspaceManager::WriteTreeNodesStatic(section, "", snap, nodeCount, writeAcc);

  // Write pane tabs compactly for RPP — skip empty panes and empty tab slots.
  // (Avoids the stale-clearing entries that WritePaneTabsStatic emits for ExtState.)
  char buf[256];
  char key[128];
  for (int p = 0; p < MAX_PANES; p++) {
    const PaneState* ps = winMgr.GetPaneState(p);
    if (!ps || ps->tabCount == 0) continue;

    snprintf(key, sizeof(key), "pane_%d_tab_count", p);
    snprintf(buf, sizeof(buf), "%d", ps->tabCount);
    writeAcc.Set(section, key, buf, true);

    snprintf(key, sizeof(key), "pane_%d_active_tab", p);
    snprintf(buf, sizeof(buf), "%d", ps->activeTab);
    writeAcc.Set(section, key, buf, true);

    for (int t = 0; t < ps->tabCount; t++) {
      const TabEntry& tab = ps->tabs[t];
      snprintf(key, sizeof(key), "pane_%d_tab_%d", p, t);
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
          writeAcc.Set(section, key, val, true);
        } else {
          writeAcc.Set(section, key, tab.name, true);
        }
      }
      snprintf(key, sizeof(key), "pane_%d_tab_%d_color", p, t);
      snprintf(buf, sizeof(buf), "%d", tab.colorIndex);
      writeAcc.Set(section, key, buf, true);
    }
  }

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
