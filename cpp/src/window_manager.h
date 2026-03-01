#pragma once
#include "config.h"
#include "split_tree.h"

struct TabEntry {
  const char* name;
  const char* searchTitle;
  int toggleAction;
  HWND hwnd;
  HWND originalParent;
  LONG_PTR originalStyle;
  LONG_PTR originalExStyle;
  bool captured;
  bool isArbitrary;
  int colorIndex;  // 0 = default (no color), 1-8 = palette color
  char arbitraryName[256];
  char arbitrarySearchTitle[256];
  char arbitraryActionCmd[128];   // stable command string ("_RSxxx" or "12345")
};

struct PaneState {
  TabEntry tabs[MAX_TABS_PER_PANE];
  int tabCount;   // 0 = empty pane
  int activeTab;  // -1 if none
};

class WindowManager {
public:
  WindowManager();

  void Init();

  // Capture appends a new tab (returns false if MAX_TABS reached)
  bool CaptureByIndex(int paneId, int knownWindowIndex, HWND containerHwnd);
  bool CaptureArbitraryWindow(int paneId, HWND targetHwnd, const char* displayName, HWND containerHwnd, int toggleAction = 0, const char* actionCmd = nullptr);

  // Tab management
  void SetActiveTab(int paneId, int tabIndex);
  void CloseTab(int paneId, int tabIndex);
  void MoveTab(int srcPane, int srcTab, int dstPane);
  void SetTabColor(int paneId, int tabIndex, int colorIndex);

  // Release all tabs in a pane
  void ReleaseWindow(int paneId, bool toggleOff = true);
  void ReleaseAll(bool toggleOff = true);
  void RepositionAll(const SplitTree& tree);
  void CheckAlive(HWND containerHwnd);

  // Accessors
  const PaneState* GetPaneState(int paneId) const;
  const TabEntry* GetActiveTabEntry(int paneId) const;
  const TabEntry* GetTab(int paneId, int tabIndex) const;
  int GetTabCount(int paneId) const;
  bool IsWindowCaptured(HWND hwnd) const;

  static HWND FindReaperWindow(const char* title, HWND skipContainer = nullptr);
  static HWND FindChildInParent(HWND parent, const char* title);
  static void DumpAllWindowTitles(const char* context = nullptr);

private:
  PaneState m_panes[MAX_PANES];
  bool DoCapture(TabEntry& tab, HWND targetHwnd, HWND containerHwnd);
  void DoRelease(TabEntry& tab, bool toggleOff = true);
};
