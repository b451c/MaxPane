#include "context_menu.h"
#include "config.h"
#include "globals.h"
#include "favorites_manager.h"
#include "workspace_manager.h"
#include "debug.h"
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
  if (hwnd == data->containerHwnd) return TRUE;
  if (hwnd == g_reaperMainHwnd) return TRUE;
  if (data->winMgr->IsWindowCaptured(hwnd)) return TRUE;
  if (data->containerHwnd && IsChild(hwnd, data->containerHwnd)) return TRUE;

#ifdef _WIN32
  // Sprint 1 Entry 9 — process filter. EnumWindows is system-wide; without
  // a PID check, Firefox / Program Manager / any visible OS window with a
  // non-empty title landed in the menu and clicking attempted a
  // cross-process SetParent (undefined behaviour).
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != GetCurrentProcessId()) return TRUE;
#endif

  char buf[256];
  GetWindowText(hwnd, buf, sizeof(buf));

#ifdef _WIN32
  // Sprint 1 Entry 9 — empty-title plugins (ReaBeat, Reamix, Direct2D)
  // get a name via the module-DLL waterfall (Entry 12). Plus a tightened
  // acceptance criterion to reject REAPER internal helpers added to the
  // menu by the EnumChildWindows pass below (track view children, custom
  // toolbars at REAPER main's child level).
  const LONG style = GetWindowLong(hwnd, GWL_STYLE);
  const bool hasCaption = (style & WS_CAPTION) != 0;
  const bool isPopup    = (style & WS_POPUP)   != 0;
  char clsName[64] = {};
  GetClassNameA(hwnd, clsName, sizeof(clsName));

  if (!buf[0]) {
    if (!hasCaption) return TRUE;
    if (!WindowManager::TryGetAppNameFromModule(hwnd, buf, sizeof(buf)) &&
        !WindowManager::TryExtractAppNameFromChildren(hwnd, buf, sizeof(buf))) {
      snprintf(buf, sizeof(buf), "#%s (untitled)", clsName[0] ? clsName : "?");
    }
  }
  if (strlen(buf) < 3) return TRUE;

  // Acceptance: WS_POPUP (real floating) OR known REAPER toggle action OR
  // plugin-DLL-owned. REAPER internal UI children (track view, timeline,
  // custom toolbars at the dock-frame level) fail all three and drop.
  bool accept = isPopup;
  if (!accept && LookupToggleAction(buf) > 0) accept = true;
  if (!accept) {
    char modCheck[128] = {};
    if (WindowManager::TryGetAppNameFromModule(hwnd, modCheck, sizeof(modCheck))) {
      accept = true;
    }
  }
  if (!accept) return TRUE;
#else
  // v2.0.6 #5 — empty-title windows on macOS (ReaImGui / Lua-gfx scripts such
  // as TK Patchbay, whose GetWindowText returns "") were silently dropped here,
  // so they never showed up in the "Open windows" capture menu — the user could
  // only grab them via Capture by Click. Mirror the click path: synthesize a
  // label via ResolveWindowDisplayName (yields "#untitled" on macOS, the same
  // name the click path already produces) instead of dropping the window. Gate
  // the new path behind the 50×50 floor FindWindowEnumProc uses so tiny internal
  // helper NSWindows don't flood the menu now that empty titles are admitted.
  if (!buf[0]) {
    RECT wr; GetClientRect(hwnd, &wr);
    if ((wr.right - wr.left) < 50 || (wr.bottom - wr.top) < 50) return TRUE;
    WindowManager::ResolveWindowDisplayName(hwnd, buf, sizeof(buf));
    DBG("[MaxPane] OpenWindowsMenu: admit empty-title hwnd=%p as '%s' (size=%ldx%ld)\n",
        (void*)hwnd, buf, (long)(wr.right - wr.left), (long)(wr.bottom - wr.top));
  }
  if (strlen(buf) < 3) return TRUE;
#endif

  // ADR-052 — Main toolbar (main-window top chrome) is not capturable;
  // don't offer it in the menu (CaptureArbitraryWindow would reject it).
  if (strcmp(buf, "Main toolbar") == 0) return TRUE;

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
#ifdef _WIN32
  // Sprint 1 Entry 9 — second pass over REAPER main's children. EnumWindows
  // only walks top-level; docked plugins live as children of the REAPER
  // dock frame and would otherwise be invisible to the menu. The Entry 9
  // acceptance criterion (WS_POPUP / known action / plugin DLL) keeps
  // internal REAPER UI children filtered out.
  if (g_reaperMainHwnd) {
    EnumChildWindows(g_reaperMainHwnd, EnumOpenWindowsProc, (LPARAM)&data);
  }
#endif

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

  // C2 (ADR-027) — Pin/Unpin Tab on top: pinning is a promotion action, not
  // a close-related one. Label flips, check-mark reflects state.
  {
    const TabEntry* tab = winMgr.GetTab(paneId, tabIndex);
    bool pinned = tab && tab->pinned;
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::TAB_TOGGLE_PINNED;
    mi.dwTypeData = pinned ? (char*)"Unpin Tab" : (char*)"Pin Tab";
    mi.fState = pinned ? MFS_CHECKED : 0;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  {
    MENUITEMINFO sep = {};
    sep.cbSize = sizeof(sep);
    sep.fMask = MIIM_TYPE;
    sep.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &sep);
  }

  // Close Tab — primary close action, top-level (Chrome/VS Code pattern).
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::TAB_CLOSE;
    mi.dwTypeData = (char*)"Close Tab";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // v2.0.6 — bulk-close family ("Close Others / to Right / All") removed: a
  // MaxPane pane holds a handful of windows, not a browser's worth of tabs, so
  // the submenu was over-engineering. "Close Tab" (above) + "Delete Pane" (pane
  // menu, releases all) cover the real need.

  // v2.0.6 — "Release Window": detach the captured window back to REAPER as a
  // free-floating VISIBLE window (distinct from "Close Tab" above, which hides
  // it). Shown only for tab types that can safely float (CanReturnVisible):
  // known/toggle windows, FX, toolbars — hidden for ReaImGui (ADR-035 crash).
  if (winMgr.CanReturnVisible(winMgr.GetTab(paneId, tabIndex))) {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::TAB_RELEASE;
    mi.dwTypeData = (char*)"Release Window";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // Separator between close-block and the rest.
  {
    MENUITEMINFO sep = {};
    sep.cbSize = sizeof(sep);
    sep.fMask = MIIM_TYPE;
    sep.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &sep);
  }

  // Add to Favorites
  {
    const TabEntry* tab = winMgr.GetTab(paneId, tabIndex);
    bool canAdd = (tab && tab->captured && tab->name[0]);
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
                           const WorkspaceManager& wsMgr,
                           bool soloActive,
                           bool isFloating,
                           bool floatAlwaysOnTop)
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

  // --- Capture window submenu (Q2 — 2026-05-22 UX revisit).
  // Sprint 2 (ADR-020) had flattened these 15 known windows into the
  // top-level menu — 1 click but long menu. Returned to a submenu so the
  // pane menu's primary surface stays under ~7 verbs; capture-everything
  // now lives under one consistent "Capture …" submenu group together
  // with Open Windows and Capture by Click below.
  {
    HMENU captureMenu = CreatePopupMenu();
    if (captureMenu) {
      for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
        MENUITEMINFO mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_ID | MIIM_TYPE;
        mi.fType = MFT_STRING;
        mi.wID = MenuIds::KNOWN_BASE + i;
        mi.dwTypeData = (char*)KNOWN_WINDOWS[i].name;
        InsertMenuItem(captureMenu, i, TRUE, &mi);
      }
      MENUITEMINFO submi = {};
      submi.cbSize = sizeof(submi);
      submi.fMask = MIIM_SUBMENU | MIIM_TYPE;
      submi.fType = MFT_STRING;
      submi.hSubMenu = captureMenu;
      submi.dwTypeData = (char*)"Capture window";
      InsertMenuItem(menu, insertPos++, TRUE, &submi);
    }
  }

  // --- Open Windows submenu (kept alongside Capture window for grouping) ---
  HMENU openWinMenu = CreatePopupMenu();
  if (openWinMenu) {
    BuildOpenWindowsSubmenu(openWinMenu, MenuIds::OPEN_WINDOWS_BASE, containerHwnd, winMgr);

    MENUITEMINFO owMi = {};
    owMi.cbSize = sizeof(owMi);
    owMi.fMask = MIIM_SUBMENU | MIIM_TYPE;
    owMi.fType = MFT_STRING;
    owMi.hSubMenu = openWinMenu;
    owMi.dwTypeData = (char*)"Open windows";
    InsertMenuItem(menu, insertPos++, TRUE, &owMi);
  }

  // --- Capture by Click ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::CAPTURE_BY_CLICK;
    mi.dwTypeData = (char*)"Capture by click";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Capture track FX chain (U14, ADR-070) ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::CAPTURE_FX_CHAIN;
    mi.dwTypeData = (char*)"Capture track FX chain";
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

  // Solo Pane (maximize/restore)
  {
    const PaneState* ps = winMgr.GetPaneState(paneId);
    bool hasTabs = (ps && ps->tabCount > 0);

    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::SOLO;
    mi.dwTypeData = soloActive ? (char*)"Exit Solo" : (char*)"Solo Pane";
    mi.fState = (hasTabs || soloActive) ? 0 : MFS_GRAYED;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Separator before pane-state group (Settings / Detach / AlwaysOnTop) ---
  {
    MENUITEMINFO sep = {};
    sep.cbSize = sizeof(sep);
    sep.fMask = MIIM_TYPE;
    sep.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &sep);
  }

  // --- Settings... (ADR-019) ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::SETTINGS;
    mi.dwTypeData = (char*)"Settings...";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Detach to Floating / Re-dock (F1a — ADR-024) ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::TOGGLE_FLOATING;
    mi.dwTypeData = isFloating ? (char*)"Re-dock to REAPER"
                               : (char*)"Detach to Floating";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Always on top (C5 — ADR-027). Only when floating. ---
  if (isFloating) {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.wID = MenuIds::TOGGLE_FLOAT_ALWAYS_ON_TOP;
    mi.dwTypeData = (char*)"Always on top";
    mi.fState = floatAlwaysOnTop ? MFS_CHECKED : 0;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Destructive group on the bottom (Q4 — 2026-05-22 UX revisit):
  // "Release Window" was previously labelled "Close" which read ambiguously
  // in a pane menu ('close what — the tab? the pane? MaxPane?'). Renamed
  // and grouped with Close MaxPane so destructive actions cluster together
  // at the end, matching premium-OSS menu order (VS Code, JetBrains, Logic).
  {
    MENUITEMINFO sep = {};
    sep.cbSize = sizeof(sep);
    sep.fMask = MIIM_TYPE;
    sep.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &sep);
  }

  // Release Window — detaches the active tab's captured window back to REAPER as
  // a free-floating VISIBLE window (v2.0.6). Shown only for types that can
  // safely float (CanReturnVisible: known/toggle, FX, toolbar — not ReaImGui).
  {
    const TabEntry* activeTab = winMgr.GetActiveTabEntry(paneId);
    if (winMgr.CanReturnVisible(activeTab)) {
      MENUITEMINFO mi = {};
      mi.cbSize = sizeof(mi);
      mi.fMask = MIIM_ID | MIIM_TYPE;
      mi.fType = MFT_STRING;
      mi.wID = MenuIds::RELEASE;
      mi.dwTypeData = (char*)"Release Window";
      InsertMenuItem(menu, insertPos++, TRUE, &mi);
    }
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
