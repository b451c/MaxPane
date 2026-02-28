#pragma once
#include "split_tree.h"
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

class CaptureQueue;
class FavoritesManager;
class WorkspaceManager;

class ReDockItContainer {
public:
  ReDockItContainer();
  ~ReDockItContainer();

  bool Create();
  void Shutdown();
  void Show();
  void Toggle();
  bool IsVisible() const;

  void ApplyPreset(LayoutPreset preset);
  void SplitPane(int paneId, SplitterOrientation orient);
  void MergePane(int paneId);
  int NodeForPane(int paneId) const { return m_tree.NodeForPane(paneId); }

  void SaveState();
  void LoadState();

  // Workspace management
  void SaveWorkspace(const char* name);
  void LoadWorkspace(const char* name);
  void DeleteWorkspace(const char* name);

  HWND GetHwnd() const { return m_hwnd; }

  SplitTree& GetTree() { return m_tree; }
  WindowManager& GetWinMgr() { return m_winMgr; }

private:
  HWND m_hwnd;
  SplitTree m_tree;
  WindowManager m_winMgr;
  bool m_visible;
  CaptureMode m_captureMode;
  DragState m_dragState;
  CaptureQueue* m_captureQueue;
  FavoritesManager* m_favMgr;
  WorkspaceManager* m_wsMgr;
  int m_shutdownGraceTicks;  // countdown after capture completes before allowing shutdown

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

  // Context menu command dispatch
  void HandleTabMenuCommand(int cmd, int paneId, int tabIdx);
  void HandlePaneMenuCommand(int cmd, int paneId);

  static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
