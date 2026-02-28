#pragma once
#include "splitter.h"
#include "window_manager.h"

// Capture mode: user clicks any window to grab it into a pane
struct CaptureMode {
  bool active;
  int targetPaneId;
};

// Drag state: dragging a tab between panes
struct DragState {
  bool active;
  int sourcePaneId;
  int sourceTabIndex;
  POINT startPt;
  int highlightPaneId;
  bool dragStarted;
};

// Workspace preset
struct WorkspaceEntry {
  char name[MAX_WORKSPACE_NAME];
  bool used;
  int layoutPreset;
  float ratios[MAX_SPLITTERS];
  int paneCount;
  struct PaneSnapshot {
    int tabCount;
    int activeTab;
    struct TabSnapshot {
      bool isArbitrary;
      char name[256];
      int toggleAction;
    } tabs[MAX_TABS_PER_PANE];
  } panes[MAX_PANES];
};

class CaptureQueue;
class FavoritesManager;

class ReDockItContainer {
public:
  ReDockItContainer();
  ~ReDockItContainer();

  bool Create();
  void Shutdown();
  void Show();
  void Toggle();
  bool IsVisible() const;

  void SetLayoutPreset(LayoutPreset preset);

  void SaveState();
  void LoadState();

  // Workspace management
  void SaveWorkspace(const char* name);
  void LoadWorkspace(const char* name);
  void DeleteWorkspace(const char* name);
  void LoadWorkspaceList();
  void SaveWorkspaceList();

  HWND GetHwnd() const { return m_hwnd; }

  SplitterLayout& GetLayout() { return m_layout; }
  WindowManager& GetWinMgr() { return m_winMgr; }

private:
  HWND m_hwnd;
  SplitterLayout m_layout;
  WindowManager m_winMgr;
  bool m_visible;
  CaptureMode m_captureMode;
  DragState m_dragState;
  WorkspaceEntry m_workspaces[MAX_WORKSPACES];
  int m_workspaceCount;
  CaptureQueue* m_captureQueue;
  FavoritesManager* m_favMgr;

  void RefreshLayout();
  void StartCaptureTimer();
  void StopCaptureTimerIfIdle();

  void OnSize(int cx, int cy);
  void OnPaint(HDC hdc);
  void OnMouseMove(int x, int y);
  void OnLButtonUp(int x, int y);
  void OnTimer();
  void OnContextMenu(int x, int y);
  void DrawTabBar(HDC hdc, int paneId, const RECT& paneRect);

  // Tab hit testing
  int TabHitTest(int paneId, int x, int y) const;
  bool IsOnTabCloseButton(int paneId, int tabIndex, int x, int y) const;

  // Drag and drop
  void StartTabDrag(int paneId, int tabIndex, int x, int y);
  void UpdateTabDrag(int x, int y);
  void EndTabDrag(int x, int y);
  void CancelTabDrag();

  int PaneAtPoint(int x, int y) const;
  void BuildOpenWindowsSubmenu(HMENU submenu, int baseId);

  static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
