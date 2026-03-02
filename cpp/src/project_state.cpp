#include "project_state.h"
#include "config.h"
#include "globals.h"
#include "workspace_manager.h"
#include "state_accessor.h"
#include "debug.h"
#include <cstring>
#include <cstdio>

PendingProjectState g_pendingProjectState = {};

// Forward declarations — defined in main.cpp
#include "container.h"
extern ReDockItContainer* GetContainer();
extern void OnRppStateReady();

// =========================================================================
// BeginLoadProjectState — reset buffer before loading
// =========================================================================

void OnBeginLoadProjectState(bool isUndo, project_config_extension_t* /*reg*/)
{
  if (isUndo) return;  // we don't handle undo states

  // Don't wipe valid data that hasn't been consumed yet.
  // REAPER calls OnBeginLoadProjectState multiple times during a single project load.
  // Our chunk may be parsed early, then a later OnBeginLoadProjectState would wipe it
  // before LoadState() gets a chance to consume it.
  if (g_pendingProjectState.valid) {
    DBG("[ReDockIt] OnBeginLoadProjectState: skipping reset — valid data not yet consumed (%d lines)\n",
        g_pendingProjectState.lineCount);
    return;
  }

  g_pendingProjectState.reading = false;
  g_pendingProjectState.lineCount = 0;
  DBG("[ReDockIt] OnBeginLoadProjectState: buffer reset\n");
}

// =========================================================================
// ProcessExtensionLine — read <REDOCKIT_STATE> chunk from RPP
// =========================================================================

bool OnProcessExtensionLine(const char* line, ProjectStateContext* ctx,
                            bool isUndo, project_config_extension_t* /*reg*/)
{
  if (isUndo) return false;

  // Check if this is our chunk opener
  if (strcmp(line, "<REDOCKIT_STATE") == 0) {
    DBG("[ReDockIt] ProcessExtensionLine: found <REDOCKIT_STATE>\n");
    g_pendingProjectState.lineCount = 0;
    g_pendingProjectState.reading = true;

    // Read subsequent lines until '>'
    char buf[RPP_MAX_LINE_LEN];
    while (ctx->GetLine(buf, sizeof(buf)) >= 0) {
      // Trim leading whitespace
      const char* p = buf;
      while (*p == ' ' || *p == '\t') p++;

      if (p[0] == '>') {
        // End of chunk
        break;
      }

      if (g_pendingProjectState.lineCount < RPP_MAX_LINES) {
        safe_strncpy(g_pendingProjectState.lines[g_pendingProjectState.lineCount],
                     p, RPP_MAX_LINE_LEN);
        g_pendingProjectState.lineCount++;
      }
    }

    g_pendingProjectState.reading = false;
    g_pendingProjectState.valid = (g_pendingProjectState.lineCount > 0);
    DBG("[ReDockIt] ProcessExtensionLine: read %d lines, valid=%d\n",
        g_pendingProjectState.lineCount, g_pendingProjectState.valid);

    // Notify main.cpp — it will create the container on the next main-loop tick
    // if one doesn't exist yet (e.g. was_visible was "0" from a previous session).
    if (g_pendingProjectState.valid) {
      OnRppStateReady();
    }

    return true;  // we consumed these lines
  }

  return false;  // not our chunk
}

// =========================================================================
// SaveExtensionConfig — write <REDOCKIT_STATE> chunk to RPP
// =========================================================================

void OnSaveExtensionConfig(ProjectStateContext* ctx, bool isUndo,
                           project_config_extension_t* /*reg*/)
{
  if (isUndo) return;

  // Get current container state via RppWriteAccessor
  // We need to serialize the tree and pane tabs into RPP lines

  // Use a temporary RppWriteAccessor to collect key-value pairs,
  // then write them as lines in the RPP chunk
  RppWriteAccessor writeAcc;

  // Get the current container — we need its tree and winMgr
  // Access via the extern function
  ReDockItContainer* container = GetContainer();
  if (!container || !container->GetHwnd()) return;

  const SplitTree& tree = container->GetTree();
  const WindowManager& winMgr = container->GetWinMgr();

  // Write tree version
  writeAcc.Set(EXT_SECTION, "tree_version", "2", true);

  // Save tree nodes
  NodeSnapshot snap[MAX_TREE_NODES];
  int nodeCount = 0;
  tree.SaveSnapshot(snap, nodeCount);

  // Validate
  bool corrupt = false;
  for (int i = 0; i < nodeCount; i++) {
    if (snap[i].type == NODE_BRANCH && snap[i].childA == snap[i].childB) {
      DBG("[ReDockIt] SaveExtensionConfig: corrupt tree node %d, skipping\n", i);
      corrupt = true;
    }
  }
  if (corrupt) return;

  WorkspaceManager::WriteTreeNodesStatic("", snap, nodeCount, writeAcc);

  // Write pane tabs compactly for RPP — skip empty panes and empty tab slots.
  // WritePaneTabsStatic writes stale-clearing entries for ExtState, which is
  // wasteful for RPP (self-contained chunk, no stale data). This reduces
  // line count from ~325 to ~50 for a typical 3-pane setup.
  {
    char buf[256];
    char key[128];
    for (int p = 0; p < MAX_PANES; p++) {
      const PaneState* ps = winMgr.GetPaneState(p);
      if (!ps || ps->tabCount == 0) continue;  // skip empty panes

      snprintf(key, sizeof(key), "pane_%d_tab_count", p);
      snprintf(buf, sizeof(buf), "%d", ps->tabCount);
      writeAcc.Set(EXT_SECTION, key, buf, true);

      snprintf(key, sizeof(key), "pane_%d_active_tab", p);
      snprintf(buf, sizeof(buf), "%d", ps->activeTab);
      writeAcc.Set(EXT_SECTION, key, buf, true);

      for (int t = 0; t < ps->tabCount; t++) {
        const TabEntry& tab = ps->tabs[t];
        snprintf(key, sizeof(key), "pane_%d_tab_%d", p, t);
        if (tab.name && tab.name[0]) {
          if (tab.isArbitrary) {
            char cmdStr[128] = "0";
            if (tab.arbitraryActionCmd[0]) {
              safe_strncpy(cmdStr, tab.arbitraryActionCmd, sizeof(cmdStr));
            } else if (tab.toggleAction > 0) {
              GetActionCommandString(tab.toggleAction, cmdStr, sizeof(cmdStr));
            }
            char val[512];
            snprintf(val, sizeof(val), "arb:%s:%s", cmdStr, tab.name);
            writeAcc.Set(EXT_SECTION, key, val, true);
          } else {
            writeAcc.Set(EXT_SECTION, key, tab.name, true);
          }
        }
        snprintf(key, sizeof(key), "pane_%d_tab_%d_color", p, t);
        snprintf(buf, sizeof(buf), "%d", tab.colorIndex);
        writeAcc.Set(EXT_SECTION, key, buf, true);
      }
    }
  }

  // Now write the collected key-value pairs as RPP chunk lines
  ctx->AddLine("<REDOCKIT_STATE");
  DBG("[ReDockIt] SaveExtensionConfig: writing %d key-value lines\n", writeAcc.GetCount());
  for (int i = 0; i < writeAcc.GetCount(); i++) {
    ctx->AddLine("%s %s", writeAcc.GetKey(i), writeAcc.GetValue(i));
  }
  ctx->AddLine(">");

  DBG("[ReDockIt] SaveExtensionConfig: wrote %d lines\n", writeAcc.GetCount());
}
