#pragma once
#include "config.h"

#ifdef _WIN32
#include <windows.h>
#else
#ifndef SWELL_PROVIDED_BY_APP
#define SWELL_PROVIDED_BY_APP
#endif
#include "swell/swell.h"
#endif

struct Pane {
  RECT rect;
  int id;
};

struct SplitterInfo {
  RECT rect;            // Splitter bar position
  RECT referenceRegion; // Region this splitter subdivides (for drag ratio calc)
  SplitterOrientation orient;
};

class SplitterLayout {
public:
  SplitterLayout();

  void SetPreset(LayoutPreset preset);
  LayoutPreset GetPreset() const { return m_preset; }
  int GetPaneCount() const { return PRESET_PANE_COUNT[m_preset]; }
  int GetSplitterCount() const { return PRESET_SPLITTER_COUNT[m_preset]; }

  void Recalculate(int w, int h);
  int HitTestSplitter(int x, int y) const;
  void StartDrag(int splitterIdx);
  void Drag(int x, int y, int containerW, int containerH);
  void EndDrag();
  bool IsDragging() const { return m_dragSplitter >= 0; }
  int DraggingSplitter() const { return m_dragSplitter; }

  const Pane& GetPane(int id) const { return m_panes[id]; }
  const SplitterInfo& GetSplitter(int id) const { return m_splitters[id]; }

  float GetRatio(int idx) const { return m_ratios[idx]; }
  void SetRatio(int idx, float r) { if (idx >= 0 && idx < MAX_SPLITTERS) m_ratios[idx] = r; }

private:
  LayoutPreset m_preset;
  float m_ratios[MAX_SPLITTERS];
  int m_dragSplitter;
  Pane m_panes[MAX_PANES];
  SplitterInfo m_splitters[MAX_SPLITTERS];
  int m_containerW, m_containerH;

  // Helpers: split a rect into two parts plus a splitter bar
  static void SplitRectV(const RECT& parent, float ratio,
                          RECT& outLeft, RECT& outSplitter, RECT& outRight);
  static void SplitRectH(const RECT& parent, float ratio,
                          RECT& outTop, RECT& outSplitter, RECT& outBottom);
};
