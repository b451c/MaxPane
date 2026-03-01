#include "project_state.h"
#include "config.h"
#include "globals.h"
#include "workspace_manager.h"
#include "state_accessor.h"
#include "debug.h"
#include <cstring>
#include <cstdio>

PendingProjectState g_pendingProjectState = {};

// Forward declaration — defined in main.cpp
#include "container.h"
extern ReDockItContainer* GetContainer();

// =========================================================================
// BeginLoadProjectState — reset buffer before loading
// =========================================================================

void OnBeginLoadProjectState(bool isUndo, project_config_extension_t* /*reg*/)
{
  if (isUndo) return;  // we don't handle undo states

  g_pendingProjectState.valid = false;
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
  WorkspaceManager::WritePaneTabsStatic("", nullptr, MAX_PANES, &winMgr, writeAcc);

  // Now write the collected key-value pairs as RPP chunk lines
  ctx->AddLine("<REDOCKIT_STATE");
  for (int i = 0; i < writeAcc.GetCount(); i++) {
    ctx->AddLine("%s %s", writeAcc.GetKey(i), writeAcc.GetValue(i));
  }
  ctx->AddLine(">");

  DBG("[ReDockIt] SaveExtensionConfig: wrote %d lines\n", writeAcc.GetCount());
}
