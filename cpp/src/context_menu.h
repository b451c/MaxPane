#pragma once
#include "split_tree.h"
#include "window_manager.h"

class FavoritesManager;
class WorkspaceManager;

// Menu ID constants
namespace MenuIds {
  static const int RELEASE = 2000;
  static const int CAPTURE_BY_CLICK = 2001;
  static const int KNOWN_BASE = 1000;
  static const int LAYOUT_BASE = 3000;
  static const int OPEN_WINDOWS_BASE = 4000;
  static const int OPEN_WINDOWS_MAX = 4500;
  static const int TAB_CLOSE = 5000;
  static const int TAB_MOVE_BASE = 5100;
  static const int WS_LOAD_BASE = 6000;
  static const int WS_SAVE = 6100;
  static const int WS_DELETE_BASE = 6200;
  static const int AUTO_OPEN = 7000;
  static const int TAB_COLOR_BASE = 8000;
  static const int FAV_BASE = 9000;
  static const int FAV_ADD = 9100;
  static const int FAV_DELETE_BASE = 9200;
  static const int CLOSE_CONTAINER = 9300;
  static const int SPLIT_H = 10000;
  static const int SPLIT_V = 10001;
  static const int MERGE  = 10002;
  static const int DELETE_PANE = 10003;
  static const int SOLO = 10004;
}

// Open window entry (populated by BuildPaneContextMenu's enumeration)
struct OpenWindowEntry {
  HWND hwnd;
  char title[256];
};

// Access the open windows list populated during BuildPaneContextMenu
int GetOpenWindowCount();
const OpenWindowEntry& GetOpenWindow(int index);

// Build tab right-click menu. Caller owns returned HMENU.
HMENU BuildTabContextMenu(int paneId, int tabIndex,
                          const SplitTree& tree,
                          const WindowManager& winMgr,
                          const FavoritesManager& favMgr);

// Build pane right-click menu. Caller owns returned HMENU.
HMENU BuildPaneContextMenu(int paneId,
                           HWND containerHwnd,
                           const SplitTree& tree,
                           const WindowManager& winMgr,
                           const FavoritesManager& favMgr,
                           const WorkspaceManager& wsMgr,
                           bool soloActive = false);
