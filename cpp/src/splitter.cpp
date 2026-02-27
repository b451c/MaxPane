#include "splitter.h"

static inline int clamp_i(int val, int lo, int hi) { return val < lo ? lo : (val > hi ? hi : val); }
static inline float clamp_f(float val, float lo, float hi) { return val < lo ? lo : (val > hi ? hi : val); }

SplitterLayout::SplitterLayout()
  : m_preset(PRESET_LEFT_RIGHT2V)
  , m_dragSplitter(-1)
  , m_containerW(0)
  , m_containerH(0)
{
  for (int i = 0; i < MAX_SPLITTERS; i++) m_ratios[i] = 0.5f;
  memset(m_panes, 0, sizeof(m_panes));
  memset(m_splitters, 0, sizeof(m_splitters));
  for (int i = 0; i < MAX_PANES; i++) m_panes[i].id = i;
}

void SplitterLayout::SetPreset(LayoutPreset preset)
{
  if (preset < 0 || preset >= PRESET_COUNT) return;
  m_preset = preset;
  // Reset ratios to defaults
  for (int i = 0; i < MAX_SPLITTERS; i++) m_ratios[i] = 0.5f;
  // Special defaults
  if (preset == PRESET_THREE_COLUMNS) {
    m_ratios[0] = 0.333f;
    m_ratios[1] = 0.666f;
  }
  Recalculate(m_containerW, m_containerH);
}

void SplitterLayout::SplitRectV(const RECT& parent, float ratio,
                                 RECT& outLeft, RECT& outSplitter, RECT& outRight)
{
  int w = parent.right - parent.left;
  int splitX = parent.left + (int)(w * ratio);
  splitX = clamp_i(splitX, parent.left + MIN_PANE_SIZE, parent.right - MIN_PANE_SIZE - SPLITTER_WIDTH);

  outLeft = parent;
  outLeft.right = splitX;

  outSplitter = parent;
  outSplitter.left = splitX;
  outSplitter.right = splitX + SPLITTER_WIDTH;

  outRight = parent;
  outRight.left = splitX + SPLITTER_WIDTH;
}

void SplitterLayout::SplitRectH(const RECT& parent, float ratio,
                                 RECT& outTop, RECT& outSplitter, RECT& outBottom)
{
  int h = parent.bottom - parent.top;
  int splitY = parent.top + (int)(h * ratio);
  splitY = clamp_i(splitY, parent.top + MIN_PANE_SIZE, parent.bottom - MIN_PANE_SIZE - SPLITTER_WIDTH);

  outTop = parent;
  outTop.bottom = splitY;

  outSplitter = parent;
  outSplitter.top = splitY;
  outSplitter.bottom = splitY + SPLITTER_WIDTH;

  outBottom = parent;
  outBottom.top = splitY + SPLITTER_WIDTH;
}

void SplitterLayout::Recalculate(int w, int h)
{
  m_containerW = w;
  m_containerH = h;
  if (w < 1 || h < 1) return;

  RECT full = {0, 0, w, h};

  switch (m_preset) {
    case PRESET_LEFT_RIGHT2V: {
      // [left] | [right-top / right-bottom]
      RECT leftR, splitV, rightR;
      SplitRectV(full, m_ratios[0], leftR, splitV, rightR);
      m_splitters[0].rect = splitV;
      m_splitters[0].referenceRegion = full;
      m_splitters[0].orient = SPLIT_VERTICAL;
      m_panes[0].rect = leftR;

      RECT topR, splitH, bottomR;
      SplitRectH(rightR, m_ratios[1], topR, splitH, bottomR);
      m_splitters[1].rect = splitH;
      m_splitters[1].referenceRegion = rightR;
      m_splitters[1].orient = SPLIT_HORIZONTAL;
      m_panes[1].rect = topR;
      m_panes[2].rect = bottomR;
      break;
    }

    case PRESET_TWO_COLUMNS: {
      // [left] | [right]
      RECT leftR, splitV, rightR;
      SplitRectV(full, m_ratios[0], leftR, splitV, rightR);
      m_splitters[0].rect = splitV;
      m_splitters[0].referenceRegion = full;
      m_splitters[0].orient = SPLIT_VERTICAL;
      m_panes[0].rect = leftR;
      m_panes[1].rect = rightR;
      break;
    }

    case PRESET_THREE_COLUMNS: {
      // [left] | [mid] | [right]
      // ratio[0] = position of first splitter (fraction of full width)
      // ratio[1] = position of second splitter (fraction of full width)
      int x1 = (int)(w * m_ratios[0]);
      x1 = clamp_i(x1, MIN_PANE_SIZE, w - 2 * MIN_PANE_SIZE - 2 * SPLITTER_WIDTH);
      int x2 = (int)(w * m_ratios[1]);
      x2 = clamp_i(x2, x1 + SPLITTER_WIDTH + MIN_PANE_SIZE, w - MIN_PANE_SIZE - SPLITTER_WIDTH);

      m_panes[0].rect = {0, 0, x1, h};

      m_splitters[0].rect = {x1, 0, x1 + SPLITTER_WIDTH, h};
      m_splitters[0].referenceRegion = full;
      m_splitters[0].orient = SPLIT_VERTICAL;

      m_panes[1].rect = {x1 + SPLITTER_WIDTH, 0, x2, h};

      m_splitters[1].rect = {x2, 0, x2 + SPLITTER_WIDTH, h};
      m_splitters[1].referenceRegion = full;
      m_splitters[1].orient = SPLIT_VERTICAL;

      m_panes[2].rect = {x2 + SPLITTER_WIDTH, 0, w, h};
      break;
    }

    case PRESET_GRID_2X2: {
      // [TL] [TR] / [BL] [BR]
      // ratio[0] = vertical splitter position (fraction of width)
      // ratio[1] = horizontal splitter position (fraction of height)
      RECT topR, splitH, bottomR;
      SplitRectH(full, m_ratios[1], topR, splitH, bottomR);
      m_splitters[1].rect = splitH;
      m_splitters[1].referenceRegion = full;
      m_splitters[1].orient = SPLIT_HORIZONTAL;

      // Split top row vertically
      RECT tl, splitV_top, tr;
      SplitRectV(topR, m_ratios[0], tl, splitV_top, tr);
      m_panes[0].rect = tl;
      m_panes[1].rect = tr;

      // Split bottom row vertically (same ratio for alignment)
      RECT bl, splitV_bot, br;
      SplitRectV(bottomR, m_ratios[0], bl, splitV_bot, br);
      m_panes[2].rect = bl;
      m_panes[3].rect = br;

      // The vertical splitter spans the full height
      m_splitters[0].rect = {splitV_top.left, 0, splitV_top.right, h};
      m_splitters[0].referenceRegion = full;
      m_splitters[0].orient = SPLIT_VERTICAL;
      break;
    }

    case PRESET_TOP_BOTTOM2H: {
      // [top] / [bottom-left | bottom-right]
      RECT topR, splitH, bottomR;
      SplitRectH(full, m_ratios[0], topR, splitH, bottomR);
      m_splitters[0].rect = splitH;
      m_splitters[0].referenceRegion = full;
      m_splitters[0].orient = SPLIT_HORIZONTAL;
      m_panes[0].rect = topR;

      RECT blR, splitV, brR;
      SplitRectV(bottomR, m_ratios[1], blR, splitV, brR);
      m_splitters[1].rect = splitV;
      m_splitters[1].referenceRegion = bottomR;
      m_splitters[1].orient = SPLIT_VERTICAL;
      m_panes[1].rect = blR;
      m_panes[2].rect = brR;
      break;
    }

    default:
      break;
  }
}

int SplitterLayout::HitTestSplitter(int x, int y) const
{
  // Use a wider grab margin (±5px beyond the actual splitter rect)
  // so it's easier to grab the splitter edge
  const int GRAB_MARGIN = 5;
  int count = GetSplitterCount();
  for (int i = 0; i < count; i++) {
    const RECT& r = m_splitters[i].rect;
    if (m_splitters[i].orient == SPLIT_VERTICAL) {
      if (x >= r.left - GRAB_MARGIN && x < r.right + GRAB_MARGIN &&
          y >= r.top && y < r.bottom)
        return i;
    } else {
      if (x >= r.left && x < r.right &&
          y >= r.top - GRAB_MARGIN && y < r.bottom + GRAB_MARGIN)
        return i;
    }
  }
  return -1;
}

void SplitterLayout::StartDrag(int splitterIdx)
{
  m_dragSplitter = splitterIdx;
}

void SplitterLayout::Drag(int x, int y, int containerW, int containerH)
{
  if (containerW < 1 || containerH < 1) return;
  if (m_dragSplitter < 0 || m_dragSplitter >= GetSplitterCount()) return;

  const SplitterInfo& si = m_splitters[m_dragSplitter];
  const RECT& ref = si.referenceRegion;

  if (si.orient == SPLIT_VERTICAL) {
    int refW = ref.right - ref.left;
    if (refW < 1) return;
    float ratio = (float)(x - ref.left) / (float)refW;
    float minR = (float)MIN_PANE_SIZE / (float)refW;
    float maxR = 1.0f - (float)(MIN_PANE_SIZE + SPLITTER_WIDTH) / (float)refW;

    // For THREE_COLUMNS, ensure splitters don't cross each other
    if (m_preset == PRESET_THREE_COLUMNS) {
      if (m_dragSplitter == 0) {
        // First splitter: can't go past second splitter
        float limit = m_ratios[1] - (float)(MIN_PANE_SIZE + SPLITTER_WIDTH) / (float)containerW;
        if (maxR > limit) maxR = limit;
      } else if (m_dragSplitter == 1) {
        // Second splitter: can't go before first splitter
        float limit = m_ratios[0] + (float)(MIN_PANE_SIZE + SPLITTER_WIDTH) / (float)containerW;
        if (minR < limit) minR = limit;
      }
    }

    m_ratios[m_dragSplitter] = clamp_f(ratio, minR, maxR);
  } else {
    int refH = ref.bottom - ref.top;
    if (refH < 1) return;
    float ratio = (float)(y - ref.top) / (float)refH;
    float minR = (float)MIN_PANE_SIZE / (float)refH;
    float maxR = 1.0f - (float)(MIN_PANE_SIZE + SPLITTER_WIDTH) / (float)refH;
    m_ratios[m_dragSplitter] = clamp_f(ratio, minR, maxR);
  }

  Recalculate(containerW, containerH);
}

void SplitterLayout::EndDrag()
{
  m_dragSplitter = -1;
}
