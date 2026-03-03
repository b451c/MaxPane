#pragma once
#include "config.h"
#include "split_tree.h"

struct TabEntry {
  char name[256];                // always owned (display name)
  char searchTitle[256];         // always owned (window search title, or prefix for dynamicTitle)
  int toggleAction;
  HWND hwnd;
  HWND originalParent;
  bool captured;
  bool isArbitrary;
  bool dynamicTitle;             // title changes at runtime (e.g. MIDI Editor) — use searchTitle as prefix
  int colorIndex;  // 0 = default (no color), 1-8 = palette color
  char actionCmd[128];           // stable command string ("_RSxxx" or "12345")
};

// Returns the stable search prefix for known dynamic-title windows,
// or nullptr if the window has a static title.
const char* GetDynamicTitlePrefix(const char* title);

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
  void ReorderTab(int paneId, int fromIndex, int toIndex);
  void SetTabColor(int paneId, int tabIndex, int colorIndex);

  // Release all tabs in a pane
  void ReleaseWindow(int paneId, bool toggleOff = true);
  void ReleaseAll(bool toggleOff = true);
  void RepositionAll(const SplitTree& tree);
  bool CheckAlive();  // returns true if any tabs were removed or recaptured

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
  HWND m_containerHwnd;  // stored for CheckAlive recapture
  bool DoCapture(TabEntry& tab, HWND targetHwnd, HWND containerHwnd);
  void DoRelease(TabEntry& tab, bool toggleOff = true);
};
