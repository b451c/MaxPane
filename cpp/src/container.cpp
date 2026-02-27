#include "container.h"
#include "config.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

static FILE* dbgFile()
{
  static FILE* f = nullptr;
  if (!f) {
    f = fopen("/tmp/redockit_debug.log", "a");
    if (f) setbuf(f, nullptr);
  }
  return f;
}
#define DBG(...) do { FILE* _f = dbgFile(); if (_f) { fprintf(_f, __VA_ARGS__); } } while(0)

// Globals (defined in main.cpp)
extern void (*g_DockWindowAddEx)(HWND, const char*, const char*, bool);
extern void (*g_DockWindowRemove)(HWND);
extern void (*g_ShowConsoleMsg)(const char*);
extern void (*g_Main_OnCommand)(int, int);
extern const char* (*g_GetExtState)(const char*, const char*);
extern void (*g_SetExtState)(const char*, const char*, const char*, bool);
extern HWND g_reaperMainHwnd;
extern int (*g_plugin_register)(const char*, void*);
extern bool (*g_GetUserInputs)(const char*, int, const char*, char*, int);

#define TIMER_ID_CHECK 1
#define TIMER_INTERVAL 500
#define TIMER_ID_CAPTURE 2
#define TIMER_CAPTURE_INTERVAL 50

// Menu IDs
#define MENU_RELEASE 2000
#define MENU_CAPTURE_BY_CLICK 2001
#define MENU_KNOWN_BASE 1000
#define MENU_LAYOUT_BASE 3000
#define MENU_OPEN_WINDOWS_BASE 4000
#define MENU_OPEN_WINDOWS_MAX 4500
#define MENU_TAB_CLOSE 5000
#define MENU_TAB_MOVE_BASE 5100
#define MENU_WS_LOAD_BASE 6000
#define MENU_WS_SAVE 6100
#define MENU_WS_DELETE_BASE 6200
#define MENU_AUTO_OPEN 7000
#define MENU_TAB_COLOR_BASE 8000

// Storage for open windows enumeration
struct OpenWindowEntry {
  HWND hwnd;
  char title[256];
};

static OpenWindowEntry g_openWindows[256];
static int g_openWindowCount = 0;

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
  strncpy(g_openWindows[g_openWindowCount].title, buf, sizeof(g_openWindows[g_openWindowCount].title) - 1);
  g_openWindows[g_openWindowCount].title[sizeof(g_openWindows[g_openWindowCount].title) - 1] = '\0';
  g_openWindowCount++;

  return TRUE;
}

// =========================================================================
// Constructor / lifecycle
// =========================================================================

ReDockItContainer::ReDockItContainer()
  : m_hwnd(nullptr)
  , m_visible(false)
  , m_workspaceCount(0)
{
  m_captureMode.active = false;
  m_captureMode.targetPaneId = -1;
  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  memset(m_workspaces, 0, sizeof(m_workspaces));
  m_winMgr.Init();
}

ReDockItContainer::~ReDockItContainer()
{
  Shutdown();
}

bool ReDockItContainer::Create()
{
  if (m_hwnd) return true;

  m_hwnd = SWELL_CreateDialog(nullptr, nullptr, g_reaperMainHwnd, DlgProc, (LPARAM)this);
  if (!m_hwnd) return false;

  SetWindowLong(m_hwnd, GWL_USERDATA, (LONG_PTR)this);
  LoadState();

  if (g_DockWindowAddEx) {
    g_DockWindowAddEx(m_hwnd, "ReDockIt", "ReDockIt_container", true);
  }

  SetTimer(m_hwnd, TIMER_ID_CHECK, TIMER_INTERVAL, nullptr);
  ShowWindow(m_hwnd, SW_SHOW);
  m_visible = true;

  return true;
}

void ReDockItContainer::Shutdown()
{
  if (!m_hwnd) return;

  SaveState();

  if (m_captureMode.active) {
    KillTimer(m_hwnd, TIMER_ID_CAPTURE);
    m_captureMode.active = false;
  }

  m_winMgr.ReleaseAll();
  KillTimer(m_hwnd, TIMER_ID_CHECK);

  if (g_DockWindowRemove) {
    g_DockWindowRemove(m_hwnd);
  }

  DestroyWindow(m_hwnd);
  m_hwnd = nullptr;
  m_visible = false;
}

void ReDockItContainer::Show()
{
  if (!m_hwnd) { Create(); return; }
  ShowWindow(m_hwnd, SW_SHOW);
  m_visible = true;
}

void ReDockItContainer::Toggle()
{
  if (!m_hwnd) { Create(); return; }
  if (m_visible) {
    ShowWindow(m_hwnd, SW_HIDE);
    m_visible = false;
  } else {
    ShowWindow(m_hwnd, SW_SHOW);
    m_visible = true;
  }
}

bool ReDockItContainer::IsVisible() const
{
  return m_visible && m_hwnd && IsWindowVisible(m_hwnd);
}

void ReDockItContainer::SetLayoutPreset(LayoutPreset preset)
{
  if (preset < 0 || preset >= PRESET_COUNT) return;

  int oldCount = m_layout.GetPaneCount();
  int newCount = PRESET_PANE_COUNT[preset];

  for (int i = newCount; i < oldCount; i++) {
    m_winMgr.ReleaseWindow(i);
  }

  m_layout.SetPreset(preset);
  m_winMgr.SetActivePaneCount(newCount);

  RECT rc;
  GetClientRect(m_hwnd, &rc);
  m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
  m_winMgr.RepositionAll(m_layout);
  InvalidateRect(m_hwnd, nullptr, TRUE);
  SaveState();
}

void ReDockItContainer::CaptureDefaultWindows()
{
  if (!m_hwnd) return;

  struct { int pane; const char* name; int action; } defaults[] = {
    {0, "Media Explorer", 50124},
    {1, "FX Browser",     40271},
    {2, "Actions",        40605},
  };

  int paneCount = m_layout.GetPaneCount();
  int count = 3;
  if (count > paneCount) count = paneCount;

  for (int d = 0; d < count; d++) {
    for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
      if (strcmp(KNOWN_WINDOWS[i].name, defaults[d].name) == 0) {
        HWND found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[i].searchTitle);
        if (!found && KNOWN_WINDOWS[i].altSearchTitle) {
          found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[i].altSearchTitle);
        }
        if (!found && g_Main_OnCommand) {
          g_Main_OnCommand(defaults[d].action, 0);
          #ifdef __APPLE__
          usleep(500000);
          #endif
          for (int attempt = 0; attempt < 30 && !found; attempt++) {
            #ifdef __APPLE__
            usleep(200000);
            #endif
            found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[i].searchTitle);
            if (!found && KNOWN_WINDOWS[i].altSearchTitle) {
              found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[i].altSearchTitle);
            }
          }
        }
        m_winMgr.CaptureByIndex(defaults[d].pane, i, m_hwnd);
        break;
      }
    }
  }

  RECT rc;
  GetClientRect(m_hwnd, &rc);
  m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
  m_winMgr.RepositionAll(m_layout);
}

void ReDockItContainer::CaptureWindowToPane(int paneId, const char* windowName)
{
  if (!m_hwnd) return;

  for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
    if (strcmp(KNOWN_WINDOWS[i].name, windowName) == 0) {
      HWND found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[i].searchTitle);
      if (!found && KNOWN_WINDOWS[i].altSearchTitle) {
        found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[i].altSearchTitle);
      }
      if (!found && g_Main_OnCommand) {
        g_Main_OnCommand(KNOWN_WINDOWS[i].toggleActionId, 0);
        #ifdef __APPLE__
        usleep(500000);
        #endif
        for (int attempt = 0; attempt < 30 && !found; attempt++) {
          #ifdef __APPLE__
          usleep(200000);
          #endif
          found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[i].searchTitle);
          if (!found && KNOWN_WINDOWS[i].altSearchTitle) {
            found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[i].altSearchTitle);
          }
        }
      }
      m_winMgr.CaptureByIndex(paneId, i, m_hwnd);
      break;
    }
  }

  RECT rc;
  GetClientRect(m_hwnd, &rc);
  m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
  m_winMgr.RepositionAll(m_layout);
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

// =========================================================================
// Save / Load state
// =========================================================================

void ReDockItContainer::SaveState()
{
  if (!g_SetExtState) return;

  char buf[256];

  // Save preset
  snprintf(buf, sizeof(buf), "%d", (int)m_layout.GetPreset());
  g_SetExtState(EXT_SECTION, "layout_preset", buf, true);

  // Save ratios
  int splitterCount = m_layout.GetSplitterCount();
  for (int i = 0; i < splitterCount; i++) {
    char key[32];
    snprintf(key, sizeof(key), "split_ratio_%d", i);
    snprintf(buf, sizeof(buf), "%.4f", m_layout.GetRatio(i));
    g_SetExtState(EXT_SECTION, key, buf, true);
  }

  // Save pane tab assignments
  int paneCount = m_layout.GetPaneCount();
  for (int i = 0; i < paneCount; i++) {
    const PaneState* ps = m_winMgr.GetPaneState(i);
    if (!ps) continue;

    char key[64];
    snprintf(key, sizeof(key), "pane_%d_tab_count", i);
    snprintf(buf, sizeof(buf), "%d", ps->tabCount);
    g_SetExtState(EXT_SECTION, key, buf, true);

    snprintf(key, sizeof(key), "pane_%d_active_tab", i);
    snprintf(buf, sizeof(buf), "%d", ps->activeTab);
    g_SetExtState(EXT_SECTION, key, buf, true);

    for (int t = 0; t < ps->tabCount; t++) {
      snprintf(key, sizeof(key), "pane_%d_tab_%d", i, t);
      const TabEntry& tab = ps->tabs[t];
      if (tab.captured && tab.name) {
        if (tab.isArbitrary) {
          char val[280];
          snprintf(val, sizeof(val), "arb:%s", tab.name);
          g_SetExtState(EXT_SECTION, key, val, true);
        } else {
          g_SetExtState(EXT_SECTION, key, tab.name, true);
        }
      } else {
        g_SetExtState(EXT_SECTION, key, "", true);
      }
      // Save tab color
      snprintf(key, sizeof(key), "pane_%d_tab_%d_color", i, t);
      snprintf(buf, sizeof(buf), "%d", tab.colorIndex);
      g_SetExtState(EXT_SECTION, key, buf, true);
    }
    // Clear any leftover tabs from previous state
    for (int t = ps->tabCount; t < MAX_TABS_PER_PANE; t++) {
      snprintf(key, sizeof(key), "pane_%d_tab_%d", i, t);
      g_SetExtState(EXT_SECTION, key, "", true);
      snprintf(key, sizeof(key), "pane_%d_tab_%d_color", i, t);
      g_SetExtState(EXT_SECTION, key, "", true);
    }
  }
}

void ReDockItContainer::LoadState()
{
  if (!g_GetExtState) return;

  // Load preset
  const char* presetStr = g_GetExtState(EXT_SECTION, "layout_preset");
  if (presetStr && presetStr[0]) {
    int p = atoi(presetStr);
    if (p >= 0 && p < PRESET_COUNT) {
      m_layout.SetPreset((LayoutPreset)p);
      m_winMgr.SetActivePaneCount(PRESET_PANE_COUNT[p]);
    }
  }

  // Load ratios
  int splitterCount = m_layout.GetSplitterCount();
  for (int i = 0; i < splitterCount; i++) {
    char key[32];
    snprintf(key, sizeof(key), "split_ratio_%d", i);
    const char* val = g_GetExtState(EXT_SECTION, key);
    if (val && val[0]) {
      m_layout.SetRatio(i, (float)atof(val));
    }
  }

  // Load pane tabs
  int paneCount = m_layout.GetPaneCount();
  for (int i = 0; i < paneCount; i++) {
    char key[64];

    // Check for new tab format
    snprintf(key, sizeof(key), "pane_%d_tab_count", i);
    const char* tabCountStr = g_GetExtState(EXT_SECTION, key);

    if (tabCountStr && tabCountStr[0]) {
      // New format: multiple tabs
      int tabCount = atoi(tabCountStr);
      if (tabCount < 0) tabCount = 0;
      if (tabCount > MAX_TABS_PER_PANE) tabCount = MAX_TABS_PER_PANE;

      for (int t = 0; t < tabCount; t++) {
        snprintf(key, sizeof(key), "pane_%d_tab_%d", i, t);
        const char* winName = g_GetExtState(EXT_SECTION, key);
        if (!winName || !winName[0]) continue;

        if (strncmp(winName, "arb:", 4) == 0) {
          const char* arbName = winName + 4;
          HWND h = WindowManager::FindReaperWindow(arbName);
          if (h) {
            m_winMgr.CaptureArbitraryWindow(i, h, arbName, m_hwnd);
          }
        } else {
          for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
            if (strcmp(KNOWN_WINDOWS[j].name, winName) == 0) {
              HWND h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].searchTitle);
              if (!h && KNOWN_WINDOWS[j].altSearchTitle) {
                h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].altSearchTitle);
              }
              if (!h && g_Main_OnCommand) {
                g_Main_OnCommand(KNOWN_WINDOWS[j].toggleActionId, 0);
                #ifdef __APPLE__
                usleep(500000);
                #endif
                // Retry loop for docked child windows
                for (int attempt = 0; attempt < 30 && !h; attempt++) {
                  #ifdef __APPLE__
                  usleep(200000);
                  #endif
                  h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].searchTitle);
                  if (!h && KNOWN_WINDOWS[j].altSearchTitle) {
                    h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].altSearchTitle);
                  }
                }
              }
              m_winMgr.CaptureByIndex(i, j, m_hwnd);
              break;
            }
          }
        }
      }

      // Restore tab colors
      int loadedTabs = m_winMgr.GetTabCount(i);
      for (int t = 0; t < loadedTabs; t++) {
        snprintf(key, sizeof(key), "pane_%d_tab_%d_color", i, t);
        const char* colorStr = g_GetExtState(EXT_SECTION, key);
        if (colorStr && colorStr[0]) {
          int ci = atoi(colorStr);
          if (ci >= 0 && ci < TAB_COLOR_COUNT) {
            m_winMgr.SetTabColor(i, t, ci);
          }
        }
      }

      // Set active tab
      snprintf(key, sizeof(key), "pane_%d_active_tab", i);
      const char* activeStr = g_GetExtState(EXT_SECTION, key);
      if (activeStr && activeStr[0]) {
        int at = atoi(activeStr);
        if (at >= 0 && at < m_winMgr.GetTabCount(i)) {
          m_winMgr.SetActiveTab(i, at);
        }
      }
    } else {
      // Legacy format: single window per pane
      snprintf(key, sizeof(key), "pane_%d_window", i);
      const char* winName = g_GetExtState(EXT_SECTION, key);
      if (winName && winName[0]) {
        if (strncmp(winName, "arb:", 4) == 0) {
          const char* arbName = winName + 4;
          HWND h = WindowManager::FindReaperWindow(arbName);
          if (h) {
            m_winMgr.CaptureArbitraryWindow(i, h, arbName, m_hwnd);
          }
        } else {
          for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
            if (strcmp(KNOWN_WINDOWS[j].name, winName) == 0) {
              m_winMgr.CaptureByIndex(i, j, m_hwnd);
              break;
            }
          }
        }
      }
    }
  }
}

// =========================================================================
// Workspace management
// =========================================================================

void ReDockItContainer::LoadWorkspaceList()
{
  m_workspaceCount = 0;
  memset(m_workspaces, 0, sizeof(m_workspaces));

  if (!g_GetExtState) return;

  const char* countStr = g_GetExtState(EXT_SECTION, "ws_count");
  if (!countStr || !countStr[0]) return;

  int count = atoi(countStr);
  if (count < 0) count = 0;
  if (count > MAX_WORKSPACES) count = MAX_WORKSPACES;

  char key[128];
  char buf[256];

  for (int w = 0; w < count; w++) {
    snprintf(key, sizeof(key), "ws_%d_name", w);
    const char* name = g_GetExtState(EXT_SECTION, key);
    if (!name || !name[0]) continue;

    WorkspaceEntry& ws = m_workspaces[m_workspaceCount];
    strncpy(ws.name, name, MAX_WORKSPACE_NAME - 1);
    ws.used = true;

    snprintf(key, sizeof(key), "ws_%d_preset", w);
    const char* val = g_GetExtState(EXT_SECTION, key);
    ws.layoutPreset = (val && val[0]) ? atoi(val) : 0;

    for (int r = 0; r < MAX_SPLITTERS; r++) {
      snprintf(key, sizeof(key), "ws_%d_ratio_%d", w, r);
      val = g_GetExtState(EXT_SECTION, key);
      ws.ratios[r] = (val && val[0]) ? (float)atof(val) : 0.5f;
    }

    snprintf(key, sizeof(key), "ws_%d_pane_count", w);
    val = g_GetExtState(EXT_SECTION, key);
    ws.paneCount = (val && val[0]) ? atoi(val) : 0;

    for (int p = 0; p < ws.paneCount && p < MAX_PANES; p++) {
      snprintf(key, sizeof(key), "ws_%d_pane_%d_tab_count", w, p);
      val = g_GetExtState(EXT_SECTION, key);
      ws.panes[p].tabCount = (val && val[0]) ? atoi(val) : 0;

      snprintf(key, sizeof(key), "ws_%d_pane_%d_active_tab", w, p);
      val = g_GetExtState(EXT_SECTION, key);
      ws.panes[p].activeTab = (val && val[0]) ? atoi(val) : 0;

      for (int t = 0; t < ws.panes[p].tabCount && t < MAX_TABS_PER_PANE; t++) {
        snprintf(key, sizeof(key), "ws_%d_pane_%d_tab_%d", w, p, t);
        val = g_GetExtState(EXT_SECTION, key);
        if (val && val[0]) {
          if (strncmp(val, "arb:", 4) == 0) {
            ws.panes[p].tabs[t].isArbitrary = true;
            strncpy(ws.panes[p].tabs[t].name, val + 4, sizeof(ws.panes[p].tabs[t].name) - 1);
          } else {
            ws.panes[p].tabs[t].isArbitrary = false;
            strncpy(ws.panes[p].tabs[t].name, val, sizeof(ws.panes[p].tabs[t].name) - 1);
            // Find toggle action
            for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
              if (strcmp(KNOWN_WINDOWS[j].name, val) == 0) {
                ws.panes[p].tabs[t].toggleAction = KNOWN_WINDOWS[j].toggleActionId;
                break;
              }
            }
          }
        }
      }
    }
    m_workspaceCount++;
  }
}

void ReDockItContainer::SaveWorkspaceList()
{
  if (!g_SetExtState) return;

  char buf[256];
  char key[128];

  snprintf(buf, sizeof(buf), "%d", m_workspaceCount);
  g_SetExtState(EXT_SECTION, "ws_count", buf, true);

  for (int w = 0; w < m_workspaceCount; w++) {
    WorkspaceEntry& ws = m_workspaces[w];

    snprintf(key, sizeof(key), "ws_%d_name", w);
    g_SetExtState(EXT_SECTION, key, ws.name, true);

    snprintf(key, sizeof(key), "ws_%d_preset", w);
    snprintf(buf, sizeof(buf), "%d", ws.layoutPreset);
    g_SetExtState(EXT_SECTION, key, buf, true);

    for (int r = 0; r < MAX_SPLITTERS; r++) {
      snprintf(key, sizeof(key), "ws_%d_ratio_%d", w, r);
      snprintf(buf, sizeof(buf), "%.4f", ws.ratios[r]);
      g_SetExtState(EXT_SECTION, key, buf, true);
    }

    snprintf(key, sizeof(key), "ws_%d_pane_count", w);
    snprintf(buf, sizeof(buf), "%d", ws.paneCount);
    g_SetExtState(EXT_SECTION, key, buf, true);

    for (int p = 0; p < ws.paneCount && p < MAX_PANES; p++) {
      snprintf(key, sizeof(key), "ws_%d_pane_%d_tab_count", w, p);
      snprintf(buf, sizeof(buf), "%d", ws.panes[p].tabCount);
      g_SetExtState(EXT_SECTION, key, buf, true);

      snprintf(key, sizeof(key), "ws_%d_pane_%d_active_tab", w, p);
      snprintf(buf, sizeof(buf), "%d", ws.panes[p].activeTab);
      g_SetExtState(EXT_SECTION, key, buf, true);

      for (int t = 0; t < ws.panes[p].tabCount && t < MAX_TABS_PER_PANE; t++) {
        snprintf(key, sizeof(key), "ws_%d_pane_%d_tab_%d", w, p, t);
        if (ws.panes[p].tabs[t].isArbitrary) {
          char val[280];
          snprintf(val, sizeof(val), "arb:%s", ws.panes[p].tabs[t].name);
          g_SetExtState(EXT_SECTION, key, val, true);
        } else {
          g_SetExtState(EXT_SECTION, key, ws.panes[p].tabs[t].name, true);
        }
      }
    }
  }
}

void ReDockItContainer::SaveWorkspace(const char* name)
{
  if (!name || !name[0]) return;

  // Find existing slot or first empty
  int slot = -1;
  for (int i = 0; i < m_workspaceCount; i++) {
    if (m_workspaces[i].used && strcmp(m_workspaces[i].name, name) == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    if (m_workspaceCount >= MAX_WORKSPACES) return;
    slot = m_workspaceCount;
    m_workspaceCount++;
  }

  WorkspaceEntry& ws = m_workspaces[slot];
  memset(&ws, 0, sizeof(WorkspaceEntry));
  strncpy(ws.name, name, MAX_WORKSPACE_NAME - 1);
  ws.used = true;
  ws.layoutPreset = (int)m_layout.GetPreset();
  for (int r = 0; r < MAX_SPLITTERS; r++) {
    ws.ratios[r] = m_layout.GetRatio(r);
  }
  ws.paneCount = m_layout.GetPaneCount();

  for (int p = 0; p < ws.paneCount && p < MAX_PANES; p++) {
    const PaneState* ps = m_winMgr.GetPaneState(p);
    if (!ps) continue;
    ws.panes[p].tabCount = ps->tabCount;
    ws.panes[p].activeTab = ps->activeTab;
    for (int t = 0; t < ps->tabCount && t < MAX_TABS_PER_PANE; t++) {
      const TabEntry& tab = ps->tabs[t];
      ws.panes[p].tabs[t].isArbitrary = tab.isArbitrary;
      ws.panes[p].tabs[t].toggleAction = tab.toggleAction;
      if (tab.name) {
        strncpy(ws.panes[p].tabs[t].name, tab.name, sizeof(ws.panes[p].tabs[t].name) - 1);
      }
    }
  }

  SaveWorkspaceList();
}

void ReDockItContainer::LoadWorkspace(const char* name)
{
  if (!name || !name[0]) return;

  int slot = -1;
  for (int i = 0; i < m_workspaceCount; i++) {
    if (m_workspaces[i].used && strcmp(m_workspaces[i].name, name) == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return;

  WorkspaceEntry& ws = m_workspaces[slot];

  // Release everything
  m_winMgr.ReleaseAll();

  // Set layout
  int lp = ws.layoutPreset;
  if (lp < 0 || lp >= PRESET_COUNT) lp = 0;
  m_layout.SetPreset((LayoutPreset)lp);
  m_winMgr.SetActivePaneCount(PRESET_PANE_COUNT[lp]);
  for (int r = 0; r < MAX_SPLITTERS; r++) {
    m_layout.SetRatio(r, ws.ratios[r]);
  }

  RECT rc;
  GetClientRect(m_hwnd, &rc);
  m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);

  // Re-capture windows
  for (int p = 0; p < ws.paneCount && p < MAX_PANES; p++) {
    for (int t = 0; t < ws.panes[p].tabCount && t < MAX_TABS_PER_PANE; t++) {
      const char* wname = ws.panes[p].tabs[t].name;
      if (!wname[0]) continue;

      if (ws.panes[p].tabs[t].isArbitrary) {
        HWND h = WindowManager::FindReaperWindow(wname);
        if (h) {
          m_winMgr.CaptureArbitraryWindow(p, h, wname, m_hwnd);
        }
      } else {
        for (int j = 0; j < NUM_KNOWN_WINDOWS; j++) {
          if (strcmp(KNOWN_WINDOWS[j].name, wname) == 0) {
            HWND h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].searchTitle);
            if (!h && KNOWN_WINDOWS[j].altSearchTitle) {
              h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].altSearchTitle);
            }
            if (!h && g_Main_OnCommand) {
              g_Main_OnCommand(KNOWN_WINDOWS[j].toggleActionId, 0);
              #ifdef __APPLE__
              usleep(500000);
              #endif
              for (int attempt = 0; attempt < 30 && !h; attempt++) {
                #ifdef __APPLE__
                usleep(200000);
                #endif
                h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].searchTitle);
                if (!h && KNOWN_WINDOWS[j].altSearchTitle) {
                  h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[j].altSearchTitle);
                }
              }
            }
            m_winMgr.CaptureByIndex(p, j, m_hwnd);
            break;
          }
        }
      }
    }
    // Set active tab
    int at = ws.panes[p].activeTab;
    if (at >= 0 && at < m_winMgr.GetTabCount(p)) {
      m_winMgr.SetActiveTab(p, at);
    }
  }

  m_winMgr.RepositionAll(m_layout);
  InvalidateRect(m_hwnd, nullptr, TRUE);
  SaveState();
}

void ReDockItContainer::DeleteWorkspace(const char* name)
{
  if (!name || !name[0]) return;

  for (int i = 0; i < m_workspaceCount; i++) {
    if (m_workspaces[i].used && strcmp(m_workspaces[i].name, name) == 0) {
      // Shift remaining
      for (int j = i; j < m_workspaceCount - 1; j++) {
        m_workspaces[j] = m_workspaces[j + 1];
      }
      m_workspaceCount--;
      memset(&m_workspaces[m_workspaceCount], 0, sizeof(WorkspaceEntry));
      SaveWorkspaceList();
      return;
    }
  }
}

// =========================================================================
// Helpers
// =========================================================================

int ReDockItContainer::PaneAtPoint(int x, int y) const
{
  int count = m_layout.GetPaneCount();
  for (int i = 0; i < count; i++) {
    const RECT& r = m_layout.GetPane(i).rect;
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
      return i;
  }
  return -1;
}

void ReDockItContainer::BuildOpenWindowsSubmenu(HMENU submenu, int baseId)
{
  g_openWindowCount = 0;
  EnumOpenWindowsData data = { m_hwnd, &m_winMgr };
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
  if (maxItems > (MENU_OPEN_WINDOWS_MAX - baseId)) maxItems = MENU_OPEN_WINDOWS_MAX - baseId;
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
// Tab hit testing
// =========================================================================

int ReDockItContainer::TabHitTest(int paneId, int x, int y) const
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || ps->tabCount == 0) return -1;

  const RECT& paneRect = m_layout.GetPane(paneId).rect;
  int tabBarTop = paneRect.top;  // Tab bar at pane top
  int tabBarBottom = tabBarTop + TAB_BAR_HEIGHT;

  if (y < tabBarTop || y >= tabBarBottom) return -1;

  int paneWidth = paneRect.right - paneRect.left;
  int tabWidth = paneWidth / ps->tabCount;
  if (tabWidth < TAB_MIN_WIDTH) tabWidth = TAB_MIN_WIDTH;
  if (tabWidth > TAB_MAX_WIDTH) tabWidth = TAB_MAX_WIDTH;

  int relX = x - paneRect.left;
  int tabIdx = relX / tabWidth;
  if (tabIdx < 0 || tabIdx >= ps->tabCount) return -1;
  return tabIdx;
}

bool ReDockItContainer::IsOnTabCloseButton(int paneId, int tabIndex, int x, int y) const
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || tabIndex < 0 || tabIndex >= ps->tabCount) return false;

  const RECT& paneRect = m_layout.GetPane(paneId).rect;
  int tabBarTop = paneRect.top;  // Tab bar at pane top

  int paneWidth = paneRect.right - paneRect.left;
  int tabWidth = paneWidth / ps->tabCount;
  if (tabWidth < TAB_MIN_WIDTH) tabWidth = TAB_MIN_WIDTH;
  if (tabWidth > TAB_MAX_WIDTH) tabWidth = TAB_MAX_WIDTH;

  int tabRight = paneRect.left + (tabIndex + 1) * tabWidth;
  if (tabRight > paneRect.right) tabRight = paneRect.right;

  int closeRight = tabRight - 4;
  int closeLeft = closeRight - 12;
  int closeTop = tabBarTop + 3;
  int closeBottom = tabBarTop + TAB_BAR_HEIGHT - 3;

  return (x >= closeLeft && x <= closeRight && y >= closeTop && y <= closeBottom);
}

// =========================================================================
// Drag and drop
// =========================================================================

void ReDockItContainer::StartTabDrag(int paneId, int tabIndex, int x, int y)
{
  m_dragState.active = false;
  m_dragState.sourcePaneId = paneId;
  m_dragState.sourceTabIndex = tabIndex;
  m_dragState.startPt.x = x;
  m_dragState.startPt.y = y;
  m_dragState.highlightPaneId = -1;
  m_dragState.dragStarted = false;
  SetCapture(m_hwnd);
}

void ReDockItContainer::UpdateTabDrag(int x, int y)
{
  if (!m_dragState.dragStarted) {
    int dx = x - m_dragState.startPt.x;
    int dy = y - m_dragState.startPt.y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx < 15 && ady < 15) return;
    m_dragState.dragStarted = true;
    m_dragState.active = true;
  }

  int oldHighlight = m_dragState.highlightPaneId;
  m_dragState.highlightPaneId = PaneAtPoint(x, y);

  // Don't highlight source pane
  if (m_dragState.highlightPaneId == m_dragState.sourcePaneId) {
    m_dragState.highlightPaneId = -1;
  }

  // Don't highlight pane at max tabs
  if (m_dragState.highlightPaneId >= 0) {
    if (m_winMgr.GetTabCount(m_dragState.highlightPaneId) >= MAX_TABS_PER_PANE) {
      m_dragState.highlightPaneId = -1;
    }
  }

  if (m_dragState.highlightPaneId != oldHighlight) {
    InvalidateRect(m_hwnd, nullptr, TRUE);
  }

  // ESC to cancel
  short escState = GetAsyncKeyState(VK_ESCAPE);
  if (escState & 0x8000) {
    CancelTabDrag();
  }
}

void ReDockItContainer::EndTabDrag(int x, int y)
{
  if (!m_dragState.active || !m_dragState.dragStarted) {
    memset(&m_dragState, 0, sizeof(m_dragState));
    m_dragState.sourcePaneId = -1;
    m_dragState.highlightPaneId = -1;
    ReleaseCapture();
    return;
  }

  int targetPane = PaneAtPoint(x, y);
  if (targetPane >= 0 && targetPane != m_dragState.sourcePaneId) {
    if (m_winMgr.GetTabCount(targetPane) < MAX_TABS_PER_PANE) {
      m_winMgr.MoveTab(m_dragState.sourcePaneId, m_dragState.sourceTabIndex, targetPane);

      RECT rc;
      GetClientRect(m_hwnd, &rc);
      m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
      m_winMgr.RepositionAll(m_layout);
      SaveState();
    }
  }

  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  ReleaseCapture();
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ReDockItContainer::CancelTabDrag()
{
  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  ReleaseCapture();
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

// =========================================================================
// Event handlers
// =========================================================================

void ReDockItContainer::OnSize(int cx, int cy)
{
  m_layout.Recalculate(cx, cy);
  m_winMgr.RepositionAll(m_layout);
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ReDockItContainer::OnPaint(HDC hdc)
{
  RECT rc;
  GetClientRect(m_hwnd, &rc);

  // Background
  HBRUSH bgBrush = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
  FillRect(hdc, &rc, bgBrush);
  DeleteObject(bgBrush);

  // Draw splitter bars
  HBRUSH splitterBrush = CreateSolidBrush(GetSysColor(COLOR_3DSHADOW));
  int splitterCount = m_layout.GetSplitterCount();
  for (int i = 0; i < splitterCount; i++) {
    RECT sr = m_layout.GetSplitter(i).rect;
    FillRect(hdc, &sr, splitterBrush);
  }
  DeleteObject(splitterBrush);

  // Draw pane tab bars or empty headers
  int paneCount = m_layout.GetPaneCount();
  for (int i = 0; i < paneCount; i++) {
    const Pane& pane = m_layout.GetPane(i);
    const PaneState* ps = m_winMgr.GetPaneState(i);

    if (ps && ps->tabCount > 0) {
      // Pane has tabs — draw tab bar directly at pane top (no separate header)
      DrawTabBar(hdc, i, pane.rect);
    } else {
      // Empty pane — draw a compact header bar
      RECT headerRect = pane.rect;
      headerRect.bottom = headerRect.top + TAB_BAR_HEIGHT;

      HBRUSH headerBrush = CreateSolidBrush(RGB(50, 50, 50));
      FillRect(hdc, &headerRect, headerBrush);
      DeleteObject(headerBrush);

      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, RGB(180, 180, 180));
      char headerText[128];
      if (m_captureMode.active && m_captureMode.targetPaneId == i) {
        snprintf(headerText, sizeof(headerText), " Click a window to capture...");
      } else {
        snprintf(headerText, sizeof(headerText), " Pane %d (click to assign)", i + 1);
      }
      RECT headerTextRect = headerRect;
      headerTextRect.left += 4;
      DrawText(hdc, headerText, -1, &headerTextRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // Empty label
      RECT contentRect = pane.rect;
      contentRect.top += TAB_BAR_HEIGHT;
      SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
      DrawText(hdc, "Click header to assign a window", -1, &contentRect,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
  }

  // Drag highlight
  if (m_dragState.active && m_dragState.dragStarted && m_dragState.highlightPaneId >= 0) {
    const RECT& r = m_layout.GetPane(m_dragState.highlightPaneId).rect;
    HPEN highlightPen = CreatePen(PS_SOLID, 3, RGB(80, 140, 255));
    HPEN oldPen = (HPEN)SelectObject(hdc, highlightPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(highlightPen);
  }
}

void ReDockItContainer::DrawTabBar(HDC hdc, int paneId, const RECT& paneRect)
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || ps->tabCount == 0) return;

  int tabBarTop = paneRect.top;  // Tab bar starts at pane top (no separate header)
  int tabBarBottom = tabBarTop + TAB_BAR_HEIGHT;
  int paneWidth = paneRect.right - paneRect.left;

  // Tab bar background
  RECT barRect = { paneRect.left, tabBarTop, paneRect.right, tabBarBottom };
  HBRUSH barBg = CreateSolidBrush(RGB(40, 40, 40));
  FillRect(hdc, &barRect, barBg);
  DeleteObject(barBg);

  int tabWidth = paneWidth / ps->tabCount;
  if (tabWidth < TAB_MIN_WIDTH) tabWidth = TAB_MIN_WIDTH;
  if (tabWidth > TAB_MAX_WIDTH) tabWidth = TAB_MAX_WIDTH;

  for (int t = 0; t < ps->tabCount; t++) {
    int tabLeft = paneRect.left + t * tabWidth;
    int tabRight = tabLeft + tabWidth;
    if (tabRight > paneRect.right) tabRight = paneRect.right;

    RECT tabRect = { tabLeft, tabBarTop, tabRight, tabBarBottom };

    // Tab background with optional color
    {
      int ci = ps->tabs[t].colorIndex;
      COLORREF bgColor;
      if (ci > 0 && ci < TAB_COLOR_COUNT) {
        const TabColor& tc = TAB_COLORS[ci];
        if (t == ps->activeTab) {
          bgColor = RGB(tc.r, tc.g, tc.b);
        } else {
          // Darken for inactive
          bgColor = RGB(tc.r / 2, tc.g / 2, tc.b / 2);
        }
      } else {
        bgColor = (t == ps->activeTab) ? RGB(80, 80, 80) : RGB(50, 50, 50);
      }
      HBRUSH tabBrush = CreateSolidBrush(bgColor);
      FillRect(hdc, &tabRect, tabBrush);
      DeleteObject(tabBrush);
    }

    // Tab name
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, (t == ps->activeTab) ? RGB(220, 220, 220) : RGB(160, 160, 160));
    RECT textRect = tabRect;
    textRect.left += 4;
    textRect.right -= 16; // room for close button
    const char* tabName = ps->tabs[t].name ? ps->tabs[t].name : "?";
    DrawText(hdc, tabName, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Close button "x"
    SetTextColor(hdc, RGB(150, 150, 150));
    RECT closeRect = { tabRight - 16, tabBarTop + 2, tabRight - 2, tabBarBottom - 2 };
    DrawText(hdc, "x", 1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Separator between tabs
    if (t < ps->tabCount - 1) {
      HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(30, 30, 30));
      HPEN oldPen = (HPEN)SelectObject(hdc, sepPen);
      MoveToEx(hdc, tabRight, tabBarTop + 2, nullptr);
      LineTo(hdc, tabRight, tabBarBottom - 2);
      SelectObject(hdc, oldPen);
      DeleteObject(sepPen);
    }
  }
}

void ReDockItContainer::DrawPaneLabel(HDC hdc, const Pane& pane, const char* label)
{
  RECT r = pane.rect;
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
  DrawText(hdc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void ReDockItContainer::OnLButtonDown(int x, int y)
{
  // Splitter drag is handled in WM_LBUTTONDOWN before this is called
  // This handles any remaining click logic (currently none)
}

void ReDockItContainer::OnMouseMove(int x, int y)
{
  if (m_dragState.sourcePaneId >= 0) {
    UpdateTabDrag(x, y);
    return;
  }

  if (m_layout.IsDragging()) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_layout.Drag(x, y, rc.right - rc.left, rc.bottom - rc.top);
    m_winMgr.RepositionAll(m_layout);
    InvalidateRect(m_hwnd, nullptr, TRUE);
  }
}

void ReDockItContainer::OnLButtonUp(int x, int y)
{
  if (m_dragState.sourcePaneId >= 0) {
    EndTabDrag(x, y);
    return;
  }

  if (m_layout.IsDragging()) {
    m_layout.EndDrag();
    ReleaseCapture();
    SaveState();
  }
}

void ReDockItContainer::OnTimer()
{
  m_winMgr.CheckAlive(m_hwnd);
}

void ReDockItContainer::OnContextMenu(int x, int y)
{
  int paneId = PaneAtPoint(x, y);
  if (paneId < 0) return;

  // Check if click is on tab bar
  int tabIdx = TabHitTest(paneId, x, y);
  if (tabIdx >= 0) {
    // Tab-specific right-click menu
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    int insertPos = 0;

    // Close Tab
    {
      MENUITEMINFO mi = {};
      mi.cbSize = sizeof(mi);
      mi.fMask = MIIM_ID | MIIM_TYPE;
      mi.fType = MFT_STRING;
      mi.wID = MENU_TAB_CLOSE;
      mi.dwTypeData = (char*)"Close Tab";
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
        const TabEntry* tab = m_winMgr.GetTab(paneId, tabIdx);
        int currentColor = tab ? tab->colorIndex : 0;

        for (int c = 0; c < TAB_COLOR_COUNT; c++) {
          MENUITEMINFO cmi = {};
          cmi.cbSize = sizeof(cmi);
          cmi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
          cmi.fType = MFT_STRING;
          cmi.wID = MENU_TAB_COLOR_BASE + c;
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
    int paneCount = m_layout.GetPaneCount();
    if (paneCount > 1) {
      MENUITEMINFO sep2 = {};
      sep2.cbSize = sizeof(sep2);
      sep2.fMask = MIIM_TYPE;
      sep2.fType = MFT_SEPARATOR;
      InsertMenuItem(menu, insertPos++, TRUE, &sep2);

      for (int p = 0; p < paneCount; p++) {
        if (p == paneId) continue;
        if (m_winMgr.GetTabCount(p) >= MAX_TABS_PER_PANE) continue;

        char label[64];
        snprintf(label, sizeof(label), "Move to Pane %d", p + 1);
        MENUITEMINFO mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_ID | MIIM_TYPE;
        mi.fType = MFT_STRING;
        mi.wID = MENU_TAB_MOVE_BASE + p;
        mi.dwTypeData = label;
        InsertMenuItem(menu, insertPos++, TRUE, &mi);
      }
    }

    POINT pt = {x, y};
    ClientToScreen(m_hwnd, &pt);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == MENU_TAB_CLOSE) {
      m_winMgr.CloseTab(paneId, tabIdx);
      RECT rc;
      GetClientRect(m_hwnd, &rc);
      m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
      m_winMgr.RepositionAll(m_layout);
      InvalidateRect(m_hwnd, nullptr, TRUE);
      SaveState();
    } else if (cmd >= MENU_TAB_MOVE_BASE && cmd < MENU_TAB_MOVE_BASE + MAX_PANES) {
      int targetPane = cmd - MENU_TAB_MOVE_BASE;
      m_winMgr.MoveTab(paneId, tabIdx, targetPane);
      RECT rc;
      GetClientRect(m_hwnd, &rc);
      m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
      m_winMgr.RepositionAll(m_layout);
      InvalidateRect(m_hwnd, nullptr, TRUE);
      SaveState();
    } else if (cmd >= MENU_TAB_COLOR_BASE && cmd < MENU_TAB_COLOR_BASE + TAB_COLOR_COUNT) {
      int newColor = cmd - MENU_TAB_COLOR_BASE;
      m_winMgr.SetTabColor(paneId, tabIdx, newColor);
      InvalidateRect(m_hwnd, nullptr, TRUE);
      SaveState();
    }
    return;
  }

  // If in capture mode and user clicked inside the container, cancel
  if (m_captureMode.active) {
    KillTimer(m_hwnd, TIMER_ID_CAPTURE);
    m_captureMode.active = false;
    InvalidateRect(m_hwnd, nullptr, TRUE);
    return;
  }

  HMENU menu = CreatePopupMenu();
  if (!menu) return;

  int insertPos = 0;

  // --- Layout submenu ---
  HMENU layoutMenu = CreatePopupMenu();
  if (layoutMenu) {
    for (int i = 0; i < PRESET_COUNT; i++) {
      MENUITEMINFO mi = {};
      mi.cbSize = sizeof(mi);
      mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
      mi.fType = MFT_STRING;
      mi.wID = MENU_LAYOUT_BASE + i;
      mi.dwTypeData = (char*)PRESET_NAMES[i];
      mi.fState = (i == (int)m_layout.GetPreset()) ? MFS_CHECKED : 0;
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
  LoadWorkspaceList();
  HMENU wsMenu = CreatePopupMenu();
  if (wsMenu) {
    int wsPos = 0;

    for (int i = 0; i < m_workspaceCount; i++) {
      if (!m_workspaces[i].used) continue;
      MENUITEMINFO mi = {};
      mi.cbSize = sizeof(mi);
      mi.fMask = MIIM_ID | MIIM_TYPE;
      mi.fType = MFT_STRING;
      mi.wID = MENU_WS_LOAD_BASE + i;
      mi.dwTypeData = m_workspaces[i].name;
      InsertMenuItem(wsMenu, wsPos++, TRUE, &mi);
    }

    if (m_workspaceCount > 0) {
      MENUITEMINFO sep = {};
      sep.cbSize = sizeof(sep);
      sep.fMask = MIIM_TYPE;
      sep.fType = MFT_SEPARATOR;
      InsertMenuItem(wsMenu, wsPos++, TRUE, &sep);
    }

    // Save Current...
    {
      MENUITEMINFO mi = {};
      mi.cbSize = sizeof(mi);
      mi.fMask = MIIM_ID | MIIM_TYPE;
      mi.fType = MFT_STRING;
      mi.wID = MENU_WS_SAVE;
      mi.dwTypeData = (char*)"Save Current...";
      InsertMenuItem(wsMenu, wsPos++, TRUE, &mi);
    }

    // Delete submenu
    if (m_workspaceCount > 0) {
      MENUITEMINFO sep = {};
      sep.cbSize = sizeof(sep);
      sep.fMask = MIIM_TYPE;
      sep.fType = MFT_SEPARATOR;
      InsertMenuItem(wsMenu, wsPos++, TRUE, &sep);

      HMENU delMenu = CreatePopupMenu();
      if (delMenu) {
        for (int i = 0; i < m_workspaceCount; i++) {
          if (!m_workspaces[i].used) continue;
          MENUITEMINFO mi = {};
          mi.cbSize = sizeof(mi);
          mi.fMask = MIIM_ID | MIIM_TYPE;
          mi.fType = MFT_STRING;
          mi.wID = MENU_WS_DELETE_BASE + i;
          mi.dwTypeData = m_workspaces[i].name;
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

  // --- Separator ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_TYPE;
    mi.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Known windows ---
  for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MENU_KNOWN_BASE + i;
    mi.dwTypeData = (char*)KNOWN_WINDOWS[i].name;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
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
    BuildOpenWindowsSubmenu(openWinMenu, MENU_OPEN_WINDOWS_BASE);

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
    mi.wID = MENU_CAPTURE_BY_CLICK;
    mi.dwTypeData = (char*)"Capture by Click";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Separator + Close ---
  const TabEntry* activeTab = m_winMgr.GetActiveTabEntry(paneId);
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
    mi.wID = MENU_RELEASE;
    mi.dwTypeData = (char*)"Close";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
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
    const char* autoVal = g_GetExtState ? g_GetExtState(EXT_SECTION, "auto_open") : "";
    bool autoOpen = (autoVal && autoVal[0] == '1');

    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.wID = MENU_AUTO_OPEN;
    mi.dwTypeData = (char*)"Auto-open on startup";
    mi.fState = autoOpen ? MFS_CHECKED : 0;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // Show menu
  POINT pt = {x, y};
  ClientToScreen(m_hwnd, &pt);

  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
  DestroyMenu(menu);

  // --- Handle commands ---

  // Layout preset
  if (cmd >= MENU_LAYOUT_BASE && cmd < MENU_LAYOUT_BASE + PRESET_COUNT) {
    SetLayoutPreset((LayoutPreset)(cmd - MENU_LAYOUT_BASE));
    return;
  }

  // Known window selection
  if (cmd >= MENU_KNOWN_BASE && cmd < MENU_KNOWN_BASE + NUM_KNOWN_WINDOWS) {
    int idx = cmd - MENU_KNOWN_BASE;

    auto findWindow = [&](int widx) -> HWND {
      HWND h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[widx].searchTitle);
      if (!h && KNOWN_WINDOWS[widx].altSearchTitle) {
        h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[widx].altSearchTitle);
      }
      return h;
    };

    DBG("[ReDockIt] Menu: selected '%s' for pane %d\n", KNOWN_WINDOWS[idx].name, paneId);

    HWND found = findWindow(idx);
    if (!found && g_Main_OnCommand) {
      g_Main_OnCommand(KNOWN_WINDOWS[idx].toggleActionId, 0);

      // Wait for REAPER to create the window — docked child windows
      // can take significant time to appear (especially Actions)
      #ifdef __APPLE__
      usleep(500000);
      #endif

      for (int attempt = 0; attempt < 30 && !found; attempt++) {
        #ifdef __APPLE__
        usleep(200000);
        #endif
        found = findWindow(idx);
        DBG("[ReDockIt] Retry %d for '%s': %s\n", attempt, KNOWN_WINDOWS[idx].name,
            found ? "FOUND" : "not found");
      }
    }

    if (found) {
      m_winMgr.CaptureByIndex(paneId, idx, m_hwnd);

      RECT rc;
      GetClientRect(m_hwnd, &rc);
      m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
      m_winMgr.RepositionAll(m_layout);
      InvalidateRect(m_hwnd, nullptr, TRUE);
      SaveState();
    }
    return;
  }

  // Open Windows selection
  if (cmd >= MENU_OPEN_WINDOWS_BASE && cmd < MENU_OPEN_WINDOWS_MAX) {
    int idx = cmd - MENU_OPEN_WINDOWS_BASE;
    if (idx >= 0 && idx < g_openWindowCount) {
      HWND targetHwnd = g_openWindows[idx].hwnd;
      char title[256];
      strncpy(title, g_openWindows[idx].title, sizeof(title) - 1);
      title[sizeof(title) - 1] = '\0';
      // If window is a "(docked)" frame, find the actual child window inside
      HWND dockFrame = nullptr;
      char* docked = strstr(title, " (docked)");
      if (docked) {
        *docked = '\0'; // Strip suffix for search and display
        dockFrame = targetHwnd; // Remember the dock frame to close later
        // Search for the real child window inside the dock frame
        HWND child = WindowManager::FindChildInParent(targetHwnd, title);
        if (child) {
          targetHwnd = child;
        } else {
          dockFrame = nullptr; // No child found, don't close frame
        }
      }
      if (targetHwnd && IsWindow(targetHwnd)) {
        m_winMgr.CaptureArbitraryWindow(paneId, targetHwnd, title, m_hwnd);

        // Hide the empty dock frame
        if (dockFrame && IsWindow(dockFrame)) {
          ShowWindow(dockFrame, SW_HIDE);
        }

        RECT rc;
        GetClientRect(m_hwnd, &rc);
        m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
        m_winMgr.RepositionAll(m_layout);
        InvalidateRect(m_hwnd, nullptr, TRUE);
        SaveState();
      }
    }
    return;
  }

  // Capture by Click
  if (cmd == MENU_CAPTURE_BY_CLICK) {
    m_captureMode.active = true;
    m_captureMode.targetPaneId = paneId;
    InvalidateRect(m_hwnd, nullptr, TRUE);
    SetTimer(m_hwnd, TIMER_ID_CAPTURE, TIMER_CAPTURE_INTERVAL, nullptr);
    return;
  }

  // Close (release active tab only)
  if (cmd == MENU_RELEASE) {
    const PaneState* ps = m_winMgr.GetPaneState(paneId);
    if (ps && ps->activeTab >= 0) {
      m_winMgr.CloseTab(paneId, ps->activeTab);
    }
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
    m_winMgr.RepositionAll(m_layout);
    InvalidateRect(m_hwnd, nullptr, TRUE);
    SaveState();
    return;
  }

  // Auto-open toggle
  if (cmd == MENU_AUTO_OPEN) {
    const char* cur = g_GetExtState ? g_GetExtState(EXT_SECTION, "auto_open") : "";
    bool isOn = (cur && cur[0] == '1');
    if (g_SetExtState) {
      g_SetExtState(EXT_SECTION, "auto_open", isOn ? "0" : "1", true);
    }
    return;
  }

  // Workspace load
  if (cmd >= MENU_WS_LOAD_BASE && cmd < MENU_WS_LOAD_BASE + MAX_WORKSPACES) {
    int idx = cmd - MENU_WS_LOAD_BASE;
    if (idx >= 0 && idx < m_workspaceCount && m_workspaces[idx].used) {
      LoadWorkspace(m_workspaces[idx].name);
    }
    return;
  }

  // Workspace save
  if (cmd == MENU_WS_SAVE) {
    if (g_GetUserInputs) {
      char name[MAX_WORKSPACE_NAME] = "";
      if (g_GetUserInputs("Save Workspace", 1, "Name:", name, sizeof(name))) {
        if (name[0]) {
          SaveWorkspace(name);
        }
      }
    }
    return;
  }

  // Workspace delete
  if (cmd >= MENU_WS_DELETE_BASE && cmd < MENU_WS_DELETE_BASE + MAX_WORKSPACES) {
    int idx = cmd - MENU_WS_DELETE_BASE;
    if (idx >= 0 && idx < m_workspaceCount && m_workspaces[idx].used) {
      DeleteWorkspace(m_workspaces[idx].name);
    }
    return;
  }
}

// =========================================================================
// Dialog Procedure
// =========================================================================

INT_PTR CALLBACK ReDockItContainer::DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  ReDockItContainer* self = (ReDockItContainer*)(LONG_PTR)GetWindowLong(hwnd, GWL_USERDATA);

  switch (msg) {
    case WM_INITDIALOG: {
      self = (ReDockItContainer*)lParam;
      SetWindowLong(hwnd, GWL_USERDATA, (LONG_PTR)self);
      if (self) self->m_hwnd = hwnd;
      return 0;
    }

    case WM_SIZE: {
      if (self) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int cx = rc.right - rc.left;
        int cy = rc.bottom - rc.top;
        if (cx > 0 && cy > 0) {
          self->OnSize(cx, cy);
        }
      }
      return 0;
    }

    case WM_PAINT: {
      if (self) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        self->OnPaint(hdc);
        EndPaint(hwnd, &ps);
      }
      return 0;
    }

    case WM_LBUTTONDOWN: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);

        // Check splitter FIRST — so resizing always takes priority
        int splitter = self->m_layout.HitTestSplitter(x, y);
        if (splitter >= 0) {
          self->m_layout.StartDrag(splitter);
          SetCapture(hwnd);
          return 0;
        }

        // Check for tab bar click
        int paneCount = self->m_layout.GetPaneCount();
        for (int i = 0; i < paneCount; i++) {
          int tabIdx = self->TabHitTest(i, x, y);
          if (tabIdx >= 0) {
            if (self->IsOnTabCloseButton(i, tabIdx, x, y)) {
              self->m_winMgr.CloseTab(i, tabIdx);
              RECT rc;
              GetClientRect(hwnd, &rc);
              self->m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
              self->m_winMgr.RepositionAll(self->m_layout);
              InvalidateRect(hwnd, nullptr, TRUE);
              self->SaveState();
            } else {
              self->m_winMgr.SetActiveTab(i, tabIdx);
              RECT rc;
              GetClientRect(hwnd, &rc);
              self->m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
              self->m_winMgr.RepositionAll(self->m_layout);
              InvalidateRect(hwnd, nullptr, TRUE);
              // Start potential drag
              self->StartTabDrag(i, tabIdx, x, y);
            }
            return 0;
          }
        }

        // Check if click is on an empty pane's header bar
        for (int i = 0; i < paneCount; i++) {
          const PaneState* ps = self->m_winMgr.GetPaneState(i);
          if (!ps || ps->tabCount == 0) {
            const RECT& r = self->m_layout.GetPane(i).rect;
            if (x >= r.left && x < r.right && y >= r.top && y < r.top + TAB_BAR_HEIGHT) {
              self->OnContextMenu(x, y);
              return 0;
            }
          }
        }

        self->OnLButtonDown(x, y);
      }
      return 0;
    }

    case WM_MOUSEMOVE: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnMouseMove(x, y);
      }
      return 0;
    }

    case WM_LBUTTONUP: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnLButtonUp(x, y);
      }
      return 0;
    }

    case WM_NCHITTEST:
      return HTCLIENT;

    case WM_SETCURSOR: {
      if (self && (HWND)wParam == hwnd) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        int splitter = self->m_layout.HitTestSplitter(pt.x, pt.y);
        if (splitter >= 0) {
          const SplitterInfo& si = self->m_layout.GetSplitter(splitter);
          if (si.orient == SPLIT_VERTICAL) {
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
          } else {
            SetCursor(LoadCursor(nullptr, IDC_SIZENS));
          }
          return 1;
        }
      }
      break;
    }

    case WM_LBUTTONDBLCLK: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnContextMenu(x, y);
      }
      return 0;
    }

    case WM_RBUTTONUP: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnContextMenu(x, y);
      }
      return 0;
    }

    case WM_RBUTTONDOWN: {
      return 0;
    }

    case WM_CONTEXTMENU: {
      if (self) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        self->OnContextMenu(pt.x, pt.y);
      }
      return 0;
    }

    case WM_TIMER: {
      if (self && wParam == TIMER_ID_CHECK) {
        self->OnTimer();
      }
      else if (self && wParam == TIMER_ID_CAPTURE && self->m_captureMode.active) {
        POINT pt;
        GetCursorPos(&pt);
        short mouseState = GetAsyncKeyState(VK_LBUTTON);
        if (mouseState & 0x8000) {
          HWND underCursor = WindowFromPoint(pt);
          if (underCursor && underCursor != self->m_hwnd &&
              underCursor != g_reaperMainHwnd &&
              !self->m_winMgr.IsWindowCaptured(underCursor)) {
            HWND topLevel = underCursor;
            HWND parent = GetParent(topLevel);
            while (parent && parent != g_reaperMainHwnd) {
              topLevel = parent;
              parent = GetParent(topLevel);
            }

            if (topLevel && topLevel != self->m_hwnd && topLevel != g_reaperMainHwnd) {
              char title[256];
              GetWindowText(topLevel, title, sizeof(title));
              if (title[0]) {
                HWND captureHwnd = topLevel;
                HWND dockFrame = nullptr;
                // If "(docked)" frame, find actual child window
                char* dockedSuffix = strstr(title, " (docked)");
                if (dockedSuffix) {
                  *dockedSuffix = '\0';
                  HWND child = WindowManager::FindChildInParent(topLevel, title);
                  if (child) {
                    captureHwnd = child;
                    dockFrame = topLevel;
                  }
                }
                int pId = self->m_captureMode.targetPaneId;
                self->m_winMgr.CaptureArbitraryWindow(pId, captureHwnd, title, self->m_hwnd);

                // Hide the empty dock frame
                if (dockFrame && IsWindow(dockFrame)) {
                  ShowWindow(dockFrame, SW_HIDE);
                }

                RECT rc;
                GetClientRect(self->m_hwnd, &rc);
                self->m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
                self->m_winMgr.RepositionAll(self->m_layout);
                self->SaveState();
              }
            }

            KillTimer(hwnd, TIMER_ID_CAPTURE);
            self->m_captureMode.active = false;
            InvalidateRect(self->m_hwnd, nullptr, TRUE);
          }
        }

        short escState = GetAsyncKeyState(VK_ESCAPE);
        if (escState & 0x8000) {
          KillTimer(hwnd, TIMER_ID_CAPTURE);
          self->m_captureMode.active = false;
          InvalidateRect(self->m_hwnd, nullptr, TRUE);
        }
      }
      return 0;
    }

    case WM_DESTROY: {
      if (self) {
        KillTimer(hwnd, TIMER_ID_CHECK);
        KillTimer(hwnd, TIMER_ID_CAPTURE);
      }
      return 0;
    }
  }

  return 0;
}
