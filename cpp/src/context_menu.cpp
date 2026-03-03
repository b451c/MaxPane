#include "context_menu.h"
#include "config.h"
#include "globals.h"
#include "favorites_manager.h"
#include "workspace_manager.h"
#include <cstdio>
#include <cstring>

// =========================================================================
// Open windows enumeration (module-static, accessed via GetOpenWindow*)
// =========================================================================

static OpenWindowEntry g_openWindows[256];
static int g_openWindowCount = 0;

int GetOpenWindowCount() { return g_openWindowCount; }
const OpenWindowEntry& GetOpenWindow(int index)
{
  static OpenWindowEntry empty = {};
  if (index < 0 || index >= g_openWindowCount) return empty;
  return g_openWindows[index];
}

struct EnumOpenWindowsData {
  HWND containerHwnd;
  const WindowManager* winMgr;
};

static BOOL CALLBACK EnumOpenWindowsProc(HWND hwnd, LPARAM lParam)
{
  EnumOpenWindowsData* data = (EnumOpenWindowsData*)lParam;
  if (g_openWindowCount >= 256) return FALSE;

  if (!IsWindowVisible(hwnd)) return TRUE;

  char buf[256];
  GetWindowText(hwnd, buf, sizeof(buf));
  if (!buf[0]) return TRUE;

  if (strstr(buf, "toolbar") || strstr(buf, "Toolbar")) return TRUE;
  if (hwnd == data->containerHwnd) return TRUE;
  if (hwnd == g_reaperMainHwnd) return TRUE;
  if (data->winMgr->IsWindowCaptured(hwnd)) return TRUE;
  if (strlen(buf) < 3) return TRUE;

  g_openWindows[g_openWindowCount].hwnd = hwnd;
  safe_strncpy(g_openWindows[g_openWindowCount].title, buf, sizeof(g_openWindows[g_openWindowCount].title));
  g_openWindowCount++;

  return TRUE;
}

static void BuildOpenWindowsSubmenu(HMENU submenu, int baseId,
                                    HWND containerHwnd, const WindowManager& winMgr)
{
  g_openWindowCount = 0;
  EnumOpenWindowsData data = { containerHwnd, &winMgr };
  EnumWindows(EnumOpenWindowsProc, (LPARAM)&data);

  if (g_openWindowCount == 0) {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.fState = MFS_GRAYED;
    mi.wID = baseId;
    mi.dwTypeData = (char*)"(No windows found)";
    InsertMenuItem(submenu, 0, TRUE, &mi);
    return;
  }

  int maxItems = g_openWindowCount;
  if (maxItems > (MenuIds::OPEN_WINDOWS_MAX - baseId)) maxItems = MenuIds::OPEN_WINDOWS_MAX - baseId;
  for (int i = 0; i < maxItems; i++) {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = baseId + i;
    mi.dwTypeData = g_openWindows[i].title;
    InsertMenuItem(submenu, i, TRUE, &mi);
  }
}

// =========================================================================
// Tab context menu
// =========================================================================

HMENU BuildTabContextMenu(int paneId, int tabIndex,
                          const SplitTree& tree,
                          const WindowManager& winMgr,
                          const FavoritesManager& favMgr)
{
  HMENU menu = CreatePopupMenu();
  if (!menu) return nullptr;

  int insertPos = 0;

  // Close Tab
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::TAB_CLOSE;
    mi.dwTypeData = (char*)"Close Tab";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // Add to Favorites
  {
    const TabEntry* tab = winMgr.GetTab(paneId, tabIndex);
    bool canAdd = (tab && tab->captured && tab->name);
    bool alreadyFav = false;
    if (canAdd) {
      for (int i = 0; i < favMgr.GetCount(); i++) {
        if (strcmp(favMgr.Get(i).name, tab->name) == 0) {
          alreadyFav = true;
          break;
        }
      }
    }

    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::FAV_ADD;
    mi.dwTypeData = alreadyFav ? (char*)"Already in Favorites" : (char*)"Add to Favorites";
    mi.fState = (canAdd && !alreadyFav) ? 0 : MFS_GRAYED;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // Color submenu
  {
    MENUITEMINFO sep = {};
    sep.cbSize = sizeof(sep);
    sep.fMask = MIIM_TYPE;
    sep.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &sep);

    HMENU colorMenu = CreatePopupMenu();
    if (colorMenu) {
      const TabEntry* tab = winMgr.GetTab(paneId, tabIndex);
      int currentColor = tab ? tab->colorIndex : 0;

      for (int c = 0; c < TAB_COLOR_COUNT; c++) {
        MENUITEMINFO cmi = {};
        cmi.cbSize = sizeof(cmi);
        cmi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
        cmi.fType = MFT_STRING;
        cmi.wID = MenuIds::TAB_COLOR_BASE + c;
        cmi.dwTypeData = (char*)TAB_COLORS[c].name;
        cmi.fState = (c == currentColor) ? MFS_CHECKED : 0;
        InsertMenuItem(colorMenu, c, TRUE, &cmi);
      }

      MENUITEMINFO colorItem = {};
      colorItem.cbSize = sizeof(colorItem);
      colorItem.fMask = MIIM_SUBMENU | MIIM_TYPE;
      colorItem.fType = MFT_STRING;
      colorItem.hSubMenu = colorMenu;
      colorItem.dwTypeData = (char*)"Color";
      InsertMenuItem(menu, insertPos++, TRUE, &colorItem);
    }
  }

  // Move to other panes
  int leafCount = tree.GetLeafCount();
  if (leafCount > 1) {
    MENUITEMINFO sep2 = {};
    sep2.cbSize = sizeof(sep2);
    sep2.fMask = MIIM_TYPE;
    sep2.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &sep2);

    for (int li = 0; li < leafCount; li++) {
      int otherPaneId = tree.GetPaneId(tree.GetLeafList()[li]);
      if (otherPaneId == paneId) continue;
      if (winMgr.GetTabCount(otherPaneId) >= MAX_TABS_PER_PANE) continue;

      char label[64];
      snprintf(label, sizeof(label), "Move to Pane %d", li + 1);
      MENUITEMINFO mi = {};
      mi.cbSize = sizeof(mi);
      mi.fMask = MIIM_ID | MIIM_TYPE;
      mi.fType = MFT_STRING;
      mi.wID = MenuIds::TAB_MOVE_BASE + otherPaneId;
      mi.dwTypeData = label;
      InsertMenuItem(menu, insertPos++, TRUE, &mi);
    }
  }

  return menu;
}

// =========================================================================
// Pane context menu
// =========================================================================

HMENU BuildPaneContextMenu(int paneId,
                           HWND containerHwnd,
                           const SplitTree& tree,
                           const WindowManager& winMgr,
                           const FavoritesManager& favMgr,
                           const WorkspaceManager& wsMgr)
{
  HMENU menu = CreatePopupMenu();
  if (!menu) return nullptr;

  int insertPos = 0;

  // --- Layout submenu ---
  HMENU layoutMenu = CreatePopupMenu();
  if (layoutMenu) {
    for (int i = 0; i < PRESET_COUNT; i++) {
      MENUITEMINFO mi = {};
      mi.cbSize = sizeof(mi);
      mi.fMask = MIIM_ID | MIIM_TYPE;
      mi.fType = MFT_STRING;
      mi.wID = MenuIds::LAYOUT_BASE + i;
      mi.dwTypeData = (char*)PRESET_NAMES[i];
      InsertMenuItem(layoutMenu, i, TRUE, &mi);
    }

    MENUITEMINFO layoutMi = {};
    layoutMi.cbSize = sizeof(layoutMi);
    layoutMi.fMask = MIIM_SUBMENU | MIIM_TYPE;
    layoutMi.fType = MFT_STRING;
    layoutMi.hSubMenu = layoutMenu;
    layoutMi.dwTypeData = (char*)"Layout";
    InsertMenuItem(menu, insertPos++, TRUE, &layoutMi);
  }

  // --- Workspaces submenu ---
  HMENU wsMenu = CreatePopupMenu();
  if (wsMenu) {
    int wsPos = 0;
    int wsCount = wsMgr.GetCount();

    for (int i = 0; i < wsCount; i++) {
      const WorkspaceEntry& ws = wsMgr.Get(i);
      if (!ws.used) continue;
      MENUITEMINFO mi = {};
      mi.cbSize = sizeof(mi);
      mi.fMask = MIIM_ID | MIIM_TYPE;
      mi.fType = MFT_STRING;
      mi.wID = MenuIds::WS_LOAD_BASE + i;
      mi.dwTypeData = (char*)ws.name;
      InsertMenuItem(wsMenu, wsPos++, TRUE, &mi);
    }

    if (wsCount > 0) {
      MENUITEMINFO sep = {};
      sep.cbSize = sizeof(sep);
      sep.fMask = MIIM_TYPE;
      sep.fType = MFT_SEPARATOR;
      InsertMenuItem(wsMenu, wsPos++, TRUE, &sep);
    }

    {
      MENUITEMINFO mi = {};
      mi.cbSize = sizeof(mi);
      mi.fMask = MIIM_ID | MIIM_TYPE;
      mi.fType = MFT_STRING;
      mi.wID = MenuIds::WS_SAVE;
      mi.dwTypeData = (char*)"Save Current...";
      InsertMenuItem(wsMenu, wsPos++, TRUE, &mi);
    }

    if (wsCount > 0) {
      MENUITEMINFO sep = {};
      sep.cbSize = sizeof(sep);
      sep.fMask = MIIM_TYPE;
      sep.fType = MFT_SEPARATOR;
      InsertMenuItem(wsMenu, wsPos++, TRUE, &sep);

      HMENU delMenu = CreatePopupMenu();
      if (delMenu) {
        for (int i = 0; i < wsCount; i++) {
          const WorkspaceEntry& ws = wsMgr.Get(i);
          if (!ws.used) continue;
          MENUITEMINFO mi = {};
          mi.cbSize = sizeof(mi);
          mi.fMask = MIIM_ID | MIIM_TYPE;
          mi.fType = MFT_STRING;
          mi.wID = MenuIds::WS_DELETE_BASE + i;
          mi.dwTypeData = (char*)ws.name;
          InsertMenuItem(delMenu, i, TRUE, &mi);
        }

        MENUITEMINFO delMi = {};
        delMi.cbSize = sizeof(delMi);
        delMi.fMask = MIIM_SUBMENU | MIIM_TYPE;
        delMi.fType = MFT_STRING;
        delMi.hSubMenu = delMenu;
        delMi.dwTypeData = (char*)"Delete";
        InsertMenuItem(wsMenu, wsPos++, TRUE, &delMi);
      }
    }

    MENUITEMINFO wsMi = {};
    wsMi.cbSize = sizeof(wsMi);
    wsMi.fMask = MIIM_SUBMENU | MIIM_TYPE;
    wsMi.fType = MFT_STRING;
    wsMi.hSubMenu = wsMenu;
    wsMi.dwTypeData = (char*)"Workspaces";
    InsertMenuItem(menu, insertPos++, TRUE, &wsMi);
  }

  // --- Favorites submenu ---
  {
    HMENU favMenu = CreatePopupMenu();
    if (favMenu) {
      int favPos = 0;
      int favCount = favMgr.GetCount();

      for (int i = 0; i < favCount; i++) {
        const FavoriteEntry& fav = favMgr.Get(i);
        MENUITEMINFO mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_ID | MIIM_TYPE;
        mi.fType = MFT_STRING;
        mi.wID = MenuIds::FAV_BASE + i;
        mi.dwTypeData = (char*)fav.name;
        InsertMenuItem(favMenu, favPos++, TRUE, &mi);
      }

      // Delete submenu
      if (favCount > 0) {
        MENUITEMINFO sep = {};
        sep.cbSize = sizeof(sep);
        sep.fMask = MIIM_TYPE;
        sep.fType = MFT_SEPARATOR;
        InsertMenuItem(favMenu, favPos++, TRUE, &sep);

        HMENU favDelMenu = CreatePopupMenu();
        if (favDelMenu) {
          for (int i = 0; i < favCount; i++) {
            const FavoriteEntry& fav = favMgr.Get(i);
            MENUITEMINFO mi = {};
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_ID | MIIM_TYPE;
            mi.fType = MFT_STRING;
            mi.wID = MenuIds::FAV_DELETE_BASE + i;
            mi.dwTypeData = (char*)fav.name;
            InsertMenuItem(favDelMenu, i, TRUE, &mi);
          }

          MENUITEMINFO delMi = {};
          delMi.cbSize = sizeof(delMi);
          delMi.fMask = MIIM_SUBMENU | MIIM_TYPE;
          delMi.fType = MFT_STRING;
          delMi.hSubMenu = favDelMenu;
          delMi.dwTypeData = (char*)"Delete";
          InsertMenuItem(favMenu, favPos++, TRUE, &delMi);
        }
      }

      MENUITEMINFO favMi = {};
      favMi.cbSize = sizeof(favMi);
      favMi.fMask = MIIM_SUBMENU | MIIM_TYPE;
      favMi.fType = MFT_STRING;
      favMi.hSubMenu = favMenu;
      favMi.dwTypeData = (char*)"Favorites";
      InsertMenuItem(menu, insertPos++, TRUE, &favMi);
    }
  }

  // --- Separator ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_TYPE;
    mi.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Known windows submenu ---
  {
    HMENU knownMenu = CreatePopupMenu();
    if (knownMenu) {
      for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
        MENUITEMINFO mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_ID | MIIM_TYPE;
        mi.fType = MFT_STRING;
        mi.wID = MenuIds::KNOWN_BASE + i;
        mi.dwTypeData = (char*)KNOWN_WINDOWS[i].name;
        InsertMenuItem(knownMenu, i, TRUE, &mi);
      }

      MENUITEMINFO knownMi = {};
      knownMi.cbSize = sizeof(knownMi);
      knownMi.fMask = MIIM_SUBMENU | MIIM_TYPE;
      knownMi.fType = MFT_STRING;
      knownMi.hSubMenu = knownMenu;
      knownMi.dwTypeData = (char*)"Known Windows";
      InsertMenuItem(menu, insertPos++, TRUE, &knownMi);
    }
  }

  // --- Separator ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_TYPE;
    mi.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Open Windows submenu ---
  HMENU openWinMenu = CreatePopupMenu();
  if (openWinMenu) {
    BuildOpenWindowsSubmenu(openWinMenu, MenuIds::OPEN_WINDOWS_BASE, containerHwnd, winMgr);

    MENUITEMINFO owMi = {};
    owMi.cbSize = sizeof(owMi);
    owMi.fMask = MIIM_SUBMENU | MIIM_TYPE;
    owMi.fType = MFT_STRING;
    owMi.hSubMenu = openWinMenu;
    owMi.dwTypeData = (char*)"Open Windows";
    InsertMenuItem(menu, insertPos++, TRUE, &owMi);
  }

  // --- Capture by Click ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::CAPTURE_BY_CLICK;
    mi.dwTypeData = (char*)"Capture by Click";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Separator + Split/Merge ---
  {
    MENUITEMINFO sep = {};
    sep.cbSize = sizeof(sep);
    sep.fMask = MIIM_TYPE;
    sep.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &sep);
  }

  // Split Horizontal
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::SPLIT_H;
    mi.dwTypeData = (char*)"Split Top / Bottom";
    mi.fState = (tree.GetLeafCount() < MAX_LEAVES) ? 0 : MFS_GRAYED;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // Split Vertical
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::SPLIT_V;
    mi.dwTypeData = (char*)"Split Left / Right";
    mi.fState = (tree.GetLeafCount() < MAX_LEAVES) ? 0 : MFS_GRAYED;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // Merge with Sibling
  {
    int nodeIdx = tree.NodeForPane(paneId);
    bool canMerge = (nodeIdx >= 0 && tree.CanMerge(nodeIdx));
    const PaneState* ps = winMgr.GetPaneState(paneId);
    bool isEmpty = (!ps || ps->tabCount == 0);

    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::MERGE;
    mi.dwTypeData = (char*)"Merge with Sibling";
    mi.fState = (canMerge && isEmpty) ? 0 : MFS_GRAYED;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // Delete Pane (release all tabs + merge)
  {
    int nodeIdx = tree.NodeForPane(paneId);
    bool canMerge = (nodeIdx >= 0 && tree.CanMerge(nodeIdx));

    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::DELETE_PANE;
    mi.dwTypeData = (char*)"Delete Pane";
    mi.fState = canMerge ? 0 : MFS_GRAYED;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Separator + Close ---
  {
    const TabEntry* activeTab = winMgr.GetActiveTabEntry(paneId);
    if (activeTab && activeTab->captured) {
      MENUITEMINFO sep = {};
      sep.cbSize = sizeof(sep);
      sep.fMask = MIIM_TYPE;
      sep.fType = MFT_SEPARATOR;
      InsertMenuItem(menu, insertPos++, TRUE, &sep);

      MENUITEMINFO mi = {};
      mi.cbSize = sizeof(mi);
      mi.fMask = MIIM_ID | MIIM_TYPE;
      mi.fType = MFT_STRING;
      mi.wID = MenuIds::RELEASE;
      mi.dwTypeData = (char*)"Close";
      InsertMenuItem(menu, insertPos++, TRUE, &mi);
    }
  }

  // --- Separator + Auto-open toggle ---
  {
    MENUITEMINFO sep = {};
    sep.cbSize = sizeof(sep);
    sep.fMask = MIIM_TYPE;
    sep.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &sep);
  }
  {
    bool autoOpen = IsAutoOpenEnabled();

    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::AUTO_OPEN;
    mi.dwTypeData = (char*)"Auto-open on startup";
    mi.fState = autoOpen ? MFS_CHECKED : 0;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Close MaxPane ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::CLOSE_CONTAINER;
    mi.dwTypeData = (char*)"Close MaxPane";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  return menu;
}
