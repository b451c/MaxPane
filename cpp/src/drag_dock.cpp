// drag_dock.cpp — Drag-to-dock zone math + preview rendering (ADR-026).
#include "drag_dock.h"
#include "config.h"          // TAB_BAR_HEIGHT
#include <cstdio>
#include <cstring>

namespace DragDock {
namespace {

// Edge band width (as fraction of pane's dimension) that triggers a split.
// Inside this band on left → ZONE_SPLIT_LEFT, etc. Center remains tab-append.
// 0.25 = 25 % from each edge.
constexpr float EDGE_FRACTION = 0.25f;
// Below this absolute pixel cutoff a band shrinks proportionally to avoid
// the whole pane being one big "split" zone in narrow panes.
constexpr int EDGE_MIN_PX = 24;
// Maximum band size in absolute pixels — large panes don't deserve a 200 px
// hot zone, that would crowd out the body-center "tab" target.
constexpr int EDGE_MAX_PX = 96;

int EdgeBand(int dim)
{
  int band = (int)((float)dim * EDGE_FRACTION);
  if (band < EDGE_MIN_PX) band = EDGE_MIN_PX;
  if (band > EDGE_MAX_PX) band = EDGE_MAX_PX;
  // Never let two bands collide — leave at least half the dim for body.
  if (band * 2 > dim / 2) band = dim / 4;
  return band;
}

// Palette — fewer colors than nav_bar/launcher; the preview overlays the
// existing pane render so we mostly use translucent-feeling accents.
struct Palette {
  COLORREF zoneFill;
  COLORREF zoneBorder;
  COLORREF replaceFill;
  COLORREF replaceBorder;
  COLORREF hintBg;
  COLORREF hintText;
};

Palette GetPalette(bool dark)
{
  Palette p{};
  if (dark) {
    p.zoneFill      = RGB( 70, 130, 200);
    p.zoneBorder    = RGB(140, 200, 250);
    p.replaceFill   = RGB(200, 110,  80);
    p.replaceBorder = RGB(250, 170, 140);
    p.hintBg        = RGB( 28,  32,  40);
    p.hintText      = RGB(250, 250, 250);
  } else {
    p.zoneFill      = RGB(180, 215, 250);
    p.zoneBorder    = RGB( 65, 125, 195);
    p.replaceFill   = RGB(250, 200, 180);
    p.replaceBorder = RGB(210, 100,  60);
    p.hintBg        = RGB( 30,  34,  42);
    p.hintText      = RGB(245, 245, 245);
  }
  return p;
}

} // namespace (anon)

void Reset(State& s)
{
  s.mode = IDLE;
  s.lastBtnDown = false;
  s.mouseDownScreenPos.x = 0;
  s.mouseDownScreenPos.y = 0;
  s.sourceHwnd = nullptr;
  s.sourceToggleAction = 0;
  s.sourceTitle[0] = '\0';
  s.cursorScreenPos.x = 0;
  s.cursorScreenPos.y = 0;
  s.targetPaneId = -1;
  s.zone = ZONE_NONE;
  s.shiftHeld = false;
}

DropZone ComputeZone(const RECT& paneRect, int x, int y, bool shiftHeld)
{
  if (x < paneRect.left || x >= paneRect.right ||
      y < paneRect.top  || y >= paneRect.bottom) {
    return ZONE_NONE;
  }

  // Tab bar strip at the top of the pane — explicit "add as tab" target.
  if (y < paneRect.top + TAB_BAR_HEIGHT) {
    return ZONE_TAB_BAR;
  }

  const int W = paneRect.right - paneRect.left;
  // Body area is paneRect minus the tab bar. Compute edge bands relative
  // to the BODY rect so the tab bar doesn't steal the top split zone.
  const int bodyTop = paneRect.top + TAB_BAR_HEIGHT;
  const int H = paneRect.bottom - bodyTop;
  if (H <= 0) return ZONE_TAB_BAR;  // degenerate — fall back to tab bar

  const int relX = x - paneRect.left;
  const int relY = y - bodyTop;

  const int bandH = EdgeBand(W);
  const int bandV = EdgeBand(H);

  // Determine which (if any) edge band the cursor is in. We test the
  // closest edge: if cursor is in both left and top bands (corner), pick
  // whichever has a smaller relative distance — gives intuitive UX in
  // corners (closer edge wins).
  int distLeft   = relX;
  int distRight  = W - relX - 1;
  int distTop    = relY;
  int distBottom = H - relY - 1;

  bool inLeft   = (distLeft   < bandH);
  bool inRight  = (distRight  < bandH);
  bool inTop    = (distTop    < bandV);
  bool inBottom = (distBottom < bandV);

  if (inLeft || inRight || inTop || inBottom) {
    // Pick smallest distance.
    int minD = distLeft;
    DropZone z = ZONE_SPLIT_LEFT;
    if (inRight  && distRight  < minD) { minD = distRight;  z = ZONE_SPLIT_RIGHT; }
    if (inTop    && distTop    < minD) { minD = distTop;    z = ZONE_SPLIT_TOP; }
    if (inBottom && distBottom < minD) { minD = distBottom; z = ZONE_SPLIT_BOTTOM; }
    // ZONE_SPLIT_LEFT was the default — only valid if inLeft actually true.
    if (!inLeft && z == ZONE_SPLIT_LEFT) {
      // First valid hit determines starting point; reseed if needed.
      if (inRight)       { z = ZONE_SPLIT_RIGHT;  minD = distRight; }
      else if (inTop)    { z = ZONE_SPLIT_TOP;    minD = distTop; }
      else if (inBottom) { z = ZONE_SPLIT_BOTTOM; minD = distBottom; }
    }
    return z;
  }

  // Interior — Shift modifier flips intent to Replace.
  return shiftHeld ? ZONE_REPLACE : ZONE_BODY_CENTER;
}

void PaintPreview(HDC hdc, const RECT& paneRect, DropZone zone, bool dark)
{
  if (zone == ZONE_NONE) return;
  Palette pal = GetPalette(dark);

  const int W = paneRect.right - paneRect.left;
  const int bodyTop = paneRect.top + TAB_BAR_HEIGHT;
  const int H = paneRect.bottom - bodyTop;
  if (W <= 0 || H <= 0) return;
  const int bandH = EdgeBand(W);
  const int bandV = EdgeBand(H);

  RECT z{};
  COLORREF fill   = pal.zoneFill;
  COLORREF border = pal.zoneBorder;

  switch (zone) {
    case ZONE_TAB_BAR:
      z = { paneRect.left, paneRect.top, paneRect.right, paneRect.top + TAB_BAR_HEIGHT };
      break;
    case ZONE_BODY_CENTER:
      z = { paneRect.left, bodyTop, paneRect.right, paneRect.bottom };
      break;
    case ZONE_SPLIT_LEFT:
      z = { paneRect.left, bodyTop, paneRect.left + bandH, paneRect.bottom };
      break;
    case ZONE_SPLIT_RIGHT:
      z = { paneRect.right - bandH, bodyTop, paneRect.right, paneRect.bottom };
      break;
    case ZONE_SPLIT_TOP:
      z = { paneRect.left, bodyTop, paneRect.right, bodyTop + bandV };
      break;
    case ZONE_SPLIT_BOTTOM:
      z = { paneRect.left, paneRect.bottom - bandV, paneRect.right, paneRect.bottom };
      break;
    case ZONE_REPLACE:
      z = { paneRect.left, bodyTop, paneRect.right, paneRect.bottom };
      fill = pal.replaceFill;
      border = pal.replaceBorder;
      break;
    default:
      return;
  }

  // Fill (SWELL FillRect has no per-pixel alpha — we use a saturated tint;
  // the underlying captured window's pixels still show through gaps near
  // the zone edges because of WS_CLIPCHILDREN not being implemented; for
  // the body-center / replace zones the pane is typically empty during
  // a drop preview so the fill reads as a solid hint).
  HBRUSH bg = CreateSolidBrush(fill);
  FillRect(hdc, &z, bg);
  DeleteObject(bg);

  // 2 px border for emphasis.
  HPEN bp = CreatePen(PS_SOLID, 2, border);
  HPEN op = (HPEN)SelectObject(hdc, bp);
  HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, z.left, z.top, z.right, z.bottom);
  SelectObject(hdc, op);
  SelectObject(hdc, ob);
  DeleteObject(bp);
}

void PaintIndicator(HDC hdc, const RECT& paneRect, DropZone zone, bool dark,
                    const char* sourceTitle)
{
  if (zone == ZONE_NONE) return;
  Palette pal = GetPalette(dark);

  // Compose the hint: "Add as tab — Mixer" / "Split right — Mixer" etc.
  char hint[320];
  const char* what = ZoneLabel(zone);
  if (sourceTitle && sourceTitle[0]) {
    snprintf(hint, sizeof(hint), "%s\xe2\x80\x83\xe2\x80\xa2\xe2\x80\x83%s", what, sourceTitle);
  } else {
    snprintf(hint, sizeof(hint), "%s", what);
  }

  // Measure with a slightly-larger font for legibility.
  HFONT font = CreateFont(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                          DEFAULT_PITCH, "");
  HFONT oldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;

  RECT measure = {};
  DrawTextUtf8(hdc, hint, -1, &measure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
  int textW = measure.right - measure.left;
  int hintW = textW + 24;
  int hintH = 30;

  // Centered horizontally in pane; vertically ~1/3 from top for "above-center".
  int cx = (paneRect.left + paneRect.right) / 2;
  int cy = paneRect.top + (paneRect.bottom - paneRect.top) / 3;
  RECT hr = { cx - hintW / 2, cy - hintH / 2,
              cx + hintW / 2, cy + hintH / 2 };

  // Shadow
  RECT sh = { hr.left + 2, hr.top + 2, hr.right + 2, hr.bottom + 2 };
  HBRUSH shB = CreateSolidBrush(RGB(0,0,0));
  FillRect(hdc, &sh, shB);
  DeleteObject(shB);

  // Pill background
  HBRUSH bg = CreateSolidBrush(pal.hintBg);
  FillRect(hdc, &hr, bg);
  DeleteObject(bg);

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, pal.hintText);
  DrawTextUtf8(hdc, hint, -1, &hr,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

  if (oldFont) SelectObject(hdc, oldFont);
  if (font) DeleteObject(font);
}

const char* ZoneLabel(DropZone zone)
{
  switch (zone) {
    case ZONE_TAB_BAR:      return "Add as tab";
    case ZONE_BODY_CENTER:  return "Add as tab";
    case ZONE_SPLIT_LEFT:   return "Split left";
    case ZONE_SPLIT_RIGHT:  return "Split right";
    case ZONE_SPLIT_TOP:    return "Split top";
    case ZONE_SPLIT_BOTTOM: return "Split bottom";
    case ZONE_REPLACE:      return "Replace tab";
    default:                return "";
  }
}

} // namespace DragDock
