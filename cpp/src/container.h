#pragma once
#include "split_tree.h"
#include "window_manager.h"
#include <memory>

// Capture mode: user clicks any window to grab it into a pane
struct CaptureMode {
  bool active;
  int targetPaneId;
};

// Drag state: dragging a tab between panes or reordering within a pane
struct DragState {
  bool active;
  int sourcePaneId;
  int sourceTabIndex;
  POINT startPt;
  int highlightPaneId;
  bool dragStarted;
  int insertTabIndex;   // intra-pane reorder: insertion position, -1 = none
};

// Expand dirty rect to include src (in-place union, handles empty dst/src)
inline void ExpandRect(RECT& dst, const RECT& src)
{
  if (src.right <= src.left || src.bottom <= src.top) return;
  if (dst.right <= dst.left || dst.bottom <= dst.top) { dst = src; return; }
  if (src.left   < dst.left)   dst.left   = src.left;
  if (src.top    < dst.top)    dst.top    = src.top;
  if (src.right  > dst.right)  dst.right  = src.right;
  if (src.bottom > dst.bottom) dst.bottom = src.bottom;
}

struct PaneSnapshot;
class CaptureQueue;
class FavoritesManager;
class WorkspaceManager;

class MaxPaneContainer {
public:
  MaxPaneContainer();
  ~MaxPaneContainer();

  bool Create();
  void Shutdown();
  void Show();
  void Toggle();
  bool IsVisible() const;

  void ApplyPreset(LayoutPreset preset);
  void SplitPane(int paneId, SplitterOrientation orient);
  void MergePane(int paneId);
  void ToggleSolo(int paneId);
  bool IsSoloActive() const { return m_soloActive; }
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

  // Keyboard navigation
  void NextTab();
  void PrevTab();
  void NextPane();
  void PrevPane();
  void SoloToggleFocused();

private:
  HWND m_hwnd;
  SplitTree m_tree;
  WindowManager m_winMgr;
  bool m_visible;
  CaptureMode m_captureMode;
  DragState m_dragState;
  std::unique_ptr<CaptureQueue> m_captureQueue;
  std::unique_ptr<FavoritesManager> m_favMgr;
  std::unique_ptr<WorkspaceManager> m_wsMgr;
  int m_hoverSplitter;      // branch index of splitter under mouse, -1 when none
  int m_hoverPane;          // pane id of tab under mouse, -1 when none
  int m_hoverTab;           // tab index under mouse, -1 when none; -2 = menu button
  bool m_pendingRppLoad;     // true if waiting for RPP state to become available

  // Solo (maximize) pane state
  bool m_soloActive = false;
  int m_soloPaneId = -1;
  NodeSnapshot m_soloSnapshot[MAX_TREE_NODES];
  int m_soloSnapshotNodeCount = 0;
  bool m_soloTabVisibility[MAX_PANES][MAX_TABS_PER_PANE];  // which tabs were visible before solo

  int m_focusedPaneId = 0;  // pane with keyboard focus (for Next/Prev Tab/Pane)
  int m_staleCleanupCountdown = 0;  // ticks until deferred stale action cleanup

  // GDI object cache (created once in constructor, destroyed in destructor)
  HBRUSH m_brushTabBarBg = nullptr;
  HBRUSH m_brushTabActive = nullptr;
  HBRUSH m_brushTabInactive = nullptr;
  HBRUSH m_brushEmptyHeader = nullptr;
  HBRUSH m_brushPaneBg = nullptr;
  HBRUSH m_brushSplitter = nullptr;
  HBRUSH m_brushSplitterHover = nullptr;
  HPEN   m_penTabSeparator = nullptr;
  HPEN   m_penGridLine = nullptr;

  static void SafeDeleteBrush(HBRUSH& brush) {
    if (brush) { DeleteObject(brush); brush = nullptr; }
  }
  static void SafeDeletePen(HPEN& pen) {
    if (pen) { DeleteObject(pen); pen = nullptr; }
  }

  void ApplyPaneState(const PaneSnapshot* panes, int maxPanes, bool deferActions);
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

  // Tab bar layout calculation (shared by draw + hit-test)
  struct TabBarLayout {
    int tabWidth;      // width of each tab
    int tabAreaLeft;   // left edge of tab area (= paneRect.left)
    int tabAreaRight;  // right edge of tab area (before menu button)
  };
  TabBarLayout CalcTabBarLayout(int paneId) const;
  RECT GetTabRect(int paneId, int tabIdx) const;

  // Tab hit testing
  int TabHitTest(int paneId, int x, int y) const;
  bool IsOnTabCloseButton(int paneId, int tabIndex, int x, int y) const;

  // Drag and drop
  void StartTabDrag(int paneId, int tabIndex, int x, int y);
  void UpdateTabDrag(int x, int y);
  void EndTabDrag(int x, int y);
  void CancelTabDrag();

  int PaneAtPoint(int x, int y) const;

  // Pane menu button (▼ in tab bar)
  void OnPaneMenuButtonClick(int paneId, int x, int y);

  // Context menu command dispatch
  void HandleTabMenuCommand(int cmd, int paneId, int tabIdx);
  void HandlePaneMenuCommand(int cmd, int paneId);

  static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
