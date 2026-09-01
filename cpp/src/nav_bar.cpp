// nav_bar.cpp — Persistent toolbar paint + hit-test (ADR-026).
//
// Design notes:
//  - Two visual groups separated by 1 px dividers. Left group: [Home] then
//    capture-related [Drag][Switch][Save][Load]. Right group, right-aligned:
//    [Settings][Support].
//  - Each button is a fixed BUTTON_SIZE square. Icon drawn centered with
//    a 3 px inset. Hover: subtle background fill. Active (BTN_DRAG when
//    armed, BTN_HOME when overlay open): accent fill.
//  - Icons are Phosphor Icons (MIT) embedded as alpha masks at 1x/2x and
//    composited per-pixel against the requested bg + tint color. See
//    nav_icons.{h,cpp}. Unified across macOS / Win / Linux in v2.0.2.
#include "nav_bar.h"
#include "config.h"   // MAX_WORKSPACE_NAME (label-buffer sizing)
#include "nav_icons.h"
#include "swell_cocoa_helpers.h"   // U9 — MaxPaneDpiScaleForDC (ADR-068)
#include <cstring>
#include <cstdio>

namespace NavBar {
namespace {

constexpr int BAR_PADDING_X    = 10;   // outer left/right pad inside bar
constexpr int BUTTON_GAP       = 2;    // gap between adjacent buttons in a group
constexpr int GROUP_GAP        = 14;   // gap on each side of divider
constexpr int DIVIDER_WIDTH    = 1;

// Feature A — workspace-name label.
constexpr int WS_NAME_FONT_PX     = 13;
constexpr int WS_NAME_INNER_PAD   = 8;   // horizontal pad on each side of label
constexpr int WS_NAME_MIN_W       = 40;  // below this we drop the label entirely
// U+2022 BULLET (UTF-8: e2 80 a2) — trailing marker for unsaved changes.
constexpr const char* WS_DIRTY_GLYPH = " \xe2\x80\xa2";

// ---- Palette ----------------------------------------------------------

struct Palette {
  COLORREF barBg;
  COLORREF barBottomEdge;
  COLORREF btnHoverBg;
  COLORREF btnActiveBg;
  COLORREF btnActiveBgArmed;   // strong accent when drag mode armed
  COLORREF glyph;
  COLORREF glyphHover;
  COLORREF glyphActive;
  COLORREF divider;
  COLORREF tooltipBg;
  COLORREF tooltipText;
  COLORREF tooltipBorder;
  // Feature A — workspace label colors.
  COLORREF wsName;            // primary label color
  COLORREF wsNameDirty;       // accent for the trailing "•" glyph
};

Palette GetPalette(bool dark)
{
  Palette p{};
  if (dark) {
    p.barBg            = RGB( 28,  28,  28);
    p.barBottomEdge    = RGB( 12,  12,  12);
    p.btnHoverBg       = RGB( 56,  60,  68);
    p.btnActiveBg      = RGB( 70,  90, 130);
    p.btnActiveBgArmed = RGB( 80, 140, 220);
    p.glyph            = RGB(200, 200, 200);
    p.glyphHover       = RGB(250, 250, 250);
    p.glyphActive      = RGB(255, 255, 255);
    p.divider          = RGB( 56,  56,  56);
    p.tooltipBg        = RGB( 50,  54,  62);
    p.tooltipText      = RGB(238, 238, 238);
    p.tooltipBorder    = RGB( 82,  88,  98);
    p.wsName           = RGB(210, 210, 210);
    p.wsNameDirty      = RGB(255, 175,  70);
  } else {
    p.barBg            = RGB(245, 245, 245);
    p.barBottomEdge    = RGB(210, 210, 210);
    p.btnHoverBg       = RGB(225, 232, 245);
    p.btnActiveBg      = RGB(195, 215, 245);
    p.btnActiveBgArmed = RGB( 80, 140, 220);
    p.glyph            = RGB( 60,  60,  60);
    p.glyphHover       = RGB( 20,  20,  20);
    p.glyphActive      = RGB(255, 255, 255);
    p.divider          = RGB(210, 210, 210);
    p.tooltipBg        = RGB( 50,  54,  62);
    p.tooltipText      = RGB(245, 245, 245);
    p.tooltipBorder    = RGB( 30,  34,  42);
    p.wsName           = RGB( 55,  55,  55);
    p.wsNameDirty      = RGB(200, 100,   0);
  }
  return p;
}

// Build the display string ("<name>" or "<name> •") into outBuf. Returns
// length written (excluding NUL).
int FormatWorkspaceLabel(const char* name, bool dirty, char* outBuf, int outCap)
{
  if (!outBuf || outCap <= 0) return 0;
  outBuf[0] = '\0';
  if (!name || !name[0]) return 0;
  int n = snprintf(outBuf, (size_t)outCap, "%s%s",
                   name, dirty ? WS_DIRTY_GLYPH : "");
  if (n < 0) { outBuf[0] = '\0'; return 0; }
  if (n >= outCap) n = outCap - 1;
  return n;
}

// Approximate rendered width of `text` in the workspace-label font.
//
// When a real HDC is provided (paint path), uses DT_CALCRECT for accurate
// measurement. When no HDC is available (hit-test path —
// MaxPaneContainer::ComputeNavBarLayout passes nullptr), falls back to a
// char-count heuristic — close enough for sizing the hit-test rectangle.
// The label is re-measured during WM_PAINT anyway, so the rendered output
// is always pixel-accurate; the heuristic only sets the hover region.
//
// Heuristic: assume average advance ~0.55 * WS_NAME_FONT_PX for proportional
// Latin text + 1.6 px slack per char. Treats UTF-8 multi-byte sequences as
// single chars by counting leading bytes (skips continuation bytes 10xxxxxx).
int MeasureWorkspaceLabel(const char* text, HDC measureHdc)
{
  if (!text || !text[0]) return 0;

  if (measureHdc) {
    HFONT font = CreateFont(WS_NAME_FONT_PX, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                            DEFAULT_PITCH, "");
    HFONT oldFont = font ? (HFONT)SelectObject(measureHdc, font) : nullptr;
    RECT measure = { 0, 0, 0, 0 };
    DrawTextUtf8(measureHdc, text, -1, &measure,
                 DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
    int w = measure.right - measure.left;
    if (oldFont) SelectObject(measureHdc, oldFont);
    if (font) DeleteObject(font);
    return w;
  }

  // No HDC available — approximate from UTF-8 codepoint count.
  int chars = 0;
  for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
    if ((*p & 0xC0) != 0x80) chars++;   // skip UTF-8 continuation bytes
  }
  // Empirical: 13px label averages ~7.2 px/char including small inter-char
  // spacing. Round up to favor including the full label in the hover rect.
  return (chars * (WS_NAME_FONT_PX * 11 / 20)) + (chars * 2);
}

// ---- Button helpers ---------------------------------------------------

void DrawButton(HDC hdc, const RECT& r, int buttonId, bool hover,
                bool active, bool armed, const Palette& pal)
{
  // bgColor mirrors the bar background unless the button is hover/active.
  // NavIcons::DrawIcon blends the pre-multiplied icon alpha against this
  // color in software, so the rendered alpha needs a real backdrop —
  // not a brushed-from-thin-air pal.barBg surprise.
  COLORREF bgColor = pal.barBg;
  if (active)      bgColor = (armed ? pal.btnActiveBgArmed : pal.btnActiveBg);
  else if (hover)  bgColor = pal.btnHoverBg;

  if (active || hover) {
    HBRUSH bg = CreateSolidBrush(bgColor);
    FillRect(hdc, &r, bg);
    DeleteObject(bg);
  }

  COLORREF iconColor = active ? pal.glyphActive
                      : hover ? pal.glyphHover
                      : pal.glyph;

  // 3 px inset inside the 26 px button reads as visually lighter than a
  // glyph drawn corner-to-corner; hit-test rect is unchanged.
  constexpr int kIconInset = 3;
  RECT iconRect = {
    r.left + kIconInset, r.top + kIconInset,
    r.right - kIconInset, r.bottom - kIconInset,
  };
  NavIcons::DrawIcon(hdc, buttonId, iconRect, bgColor, iconColor);
}

// Collapse/expand chevron, centered in r. Pen lines instead of a ▾/▴ font
// glyph — DejaVu Sans on Linux renders those as tofu (v2.0.0 R7 lesson).
void DrawChevron(HDC hdc, const RECT& r, bool pointUp, COLORREF color)
{
  const int cx = (r.left + r.right) / 2;
  const int cy = (r.top + r.bottom) / 2;
  const int halfW = 4;
  const int halfH = 2;
  const int apexY  = pointUp ? cy - halfH : cy + halfH;
  const int baseY  = pointUp ? cy + halfH : cy - halfH;

  HPEN pen = CreatePen(PS_SOLID, 2, color);
  HPEN old = (HPEN)SelectObject(hdc, pen);
  MoveToEx(hdc, cx - halfW, baseY, nullptr);
  LineTo(hdc, cx, apexY);
  LineTo(hdc, cx + halfW + 1, baseY + (pointUp ? 1 : -1));
  SelectObject(hdc, old);
  DeleteObject(pen);
}

} // namespace (anon)

// ============================================================================
// Compute
// ============================================================================

Layout Compute(const RECT& containerRect, const State& state, HDC measureHdc)
{
  Layout lay = {};

  // v2.0.6 #6 — the strip rect is always derivable from the container width and
  // must be set even when we hide the bar below MIN_VISIBLE_W. The caller keeps
  // reserving NAV_BAR_HEIGHT whenever the bar is toggled on (NavBarReservedHeight
  // keys off the on/off pref, not the width), so Paint needs a valid barRect to
  // fill the reserved strip; otherwise a narrow container shows a grey unpainted
  // band where the bar would be.
  lay.barRect.left   = containerRect.left;
  lay.barRect.top    = containerRect.top;
  lay.barRect.right  = containerRect.right;
  lay.barRect.bottom = containerRect.top + NAV_BAR_HEIGHT;

  // Collapsed mode (user request 2026-06-10): the bar is a thin sliver whose
  // entire surface is the expand chevron. No width gate — the sliver fits any
  // container, and it must stay clickable or the user can't get the bar back
  // (no overlay above captured panes is possible on macOS, ADR-026).
  if (state.collapsed) {
    lay.visible = true;
    lay.collapsed = true;
    lay.barRect.bottom = lay.barRect.top + NAV_BAR_COLLAPSED_HEIGHT;
    lay.buttons[BTN_COLLAPSE] = lay.barRect;
    return lay;
  }

  const int W = containerRect.right - containerRect.left;
  if (W < MIN_VISIBLE_W) {
    lay.visible = false;
    return lay;
  }
  lay.visible = true;

  const int btnY = lay.barRect.top + (NAV_BAR_HEIGHT - BUTTON_SIZE) / 2;
  const int btnBottom = btnY + BUTTON_SIZE;

  // Left group: Home | [Drag Switch Save Load(with caret)]
  int x = lay.barRect.left + BAR_PADDING_X;

  // Home
  lay.buttons[BTN_HOME] = { x, btnY, x + BUTTON_SIZE, btnBottom };
  x += BUTTON_SIZE;

  // Divider 1
  x += GROUP_GAP / 2;
  lay.divider1 = { x, btnY + 4, x + DIVIDER_WIDTH, btnBottom - 4 };
  x += DIVIDER_WIDTH + GROUP_GAP / 2;

  // Tool group, in workflow order (v2.5.0 menu/nav pass, owner request):
  // open a workspace, save it, add a window, arrange the panes, find things.
  //   Load ▾ | Save | Drag-to-dock | Edit layout | Quick Switcher
  // Home stays alone on the left (workspace dashboard); Settings / Support
  // / collapse stay pinned right. Uniform width.
  const int captureBtns[] = { BTN_LOAD, BTN_SAVE, BTN_DRAG, BTN_EDIT_LAYOUT, BTN_SWITCH };
  for (int i = 0; i < (int)(sizeof(captureBtns) / sizeof(captureBtns[0])); i++) {
    int id = captureBtns[i];
    lay.buttons[id] = { x, btnY, x + BUTTON_SIZE, btnBottom };
    x += BUTTON_SIZE;
    if (i < (int)(sizeof(captureBtns) / sizeof(captureBtns[0])) - 1) x += BUTTON_GAP;
  }

  // Utility group right-aligned: Settings, Support, then the slim collapse
  // chevron pinned to the very edge.
  int rightX = lay.barRect.right - BAR_PADDING_X;
  lay.buttons[BTN_COLLAPSE] = { rightX - COLLAPSE_BTN_W, btnY, rightX, btnBottom };
  rightX -= COLLAPSE_BTN_W + BUTTON_GAP;
  lay.buttons[BTN_SUPPORT] = { rightX - BUTTON_SIZE, btnY, rightX, btnBottom };
  rightX -= BUTTON_SIZE + BUTTON_GAP;
  lay.buttons[BTN_SETTINGS] = { rightX - BUTTON_SIZE, btnY, rightX, btnBottom };

  // Divider 2 pinned to the right edge of the capture group (was previously
  // centered in the empty space between groups, which read as a stray line
  // floating in the middle of the nav bar). Visually a continuation of the
  // capture group's boundary, not a free-standing separator.
  int settingsLeft = lay.buttons[BTN_SETTINGS].left;
  int loadRight    = lay.buttons[BTN_SWITCH].right;  // tool group's last button (v2.5.0 order)
  if (settingsLeft > loadRight + GROUP_GAP + DIVIDER_WIDTH) {
    int dx = loadRight + GROUP_GAP / 2;
    lay.divider2 = { dx, btnY + 4, dx + DIVIDER_WIDTH, btnBottom - 4 };
  } else {
    // Not enough room — hide divider2 (zeroed rect won't paint).
    lay.divider2 = {};
    // If the capture group ran into the utility group, mark bar invisible
    // — the caller should fall back to no-bar mode. Defensive; should be
    // prevented earlier by MIN_VISIBLE_W.
    if (settingsLeft <= loadRight) {
      lay.visible = false;
    }
  }

  // Feature A — workspace-name label. Only laid out when state carries a
  // non-empty name AND there is meaningful horizontal room between the two
  // groups. When the available region is below WS_NAME_MIN_W we drop the
  // label entirely (no truncated-to-ellipsis-only artifacts).
  if (lay.visible && state.workspaceName && state.workspaceName[0]) {
    int regionLeft  = (lay.divider2.right > lay.divider2.left ? lay.divider2.right
                                                              : loadRight)
                      + GROUP_GAP;
    int regionRight = settingsLeft - GROUP_GAP;
    int regionW     = regionRight - regionLeft;
    if (regionW >= WS_NAME_MIN_W) {
      char labelBuf[MAX_WORKSPACE_NAME + 8];
      FormatWorkspaceLabel(state.workspaceName, state.workspaceDirty,
                           labelBuf, (int)sizeof(labelBuf));
      int textW = MeasureWorkspaceLabel(labelBuf, measureHdc);
      int usableW = regionW - 2 * WS_NAME_INNER_PAD;
      if (usableW < 1) usableW = 1;

      bool truncated = false;
      int rectW = textW;
      if (rectW > usableW) {
        rectW = usableW;
        truncated = true;
      }
      // Center the rect within the available region. Render rect is the
      // actual draw area (without the inner padding); hit-test rect spans
      // the full vertical strip for forgiving hover.
      int cx = (regionLeft + regionRight) / 2;
      int rl = cx - rectW / 2;
      int rr = rl + rectW;
      lay.workspaceNameRect = { rl, lay.barRect.top, rr, lay.barRect.bottom };
      lay.workspaceNameTruncated = truncated;
    }
  }

  return lay;
}

// ============================================================================
// Paint
// ============================================================================

void Paint(HDC hdc, const Layout& lay, const State& state, bool dark)
{
  Palette pal = GetPalette(dark);

  // Bar background — always fill the reserved strip first, even when the bar is
  // too narrow to lay out its buttons (lay.visible == false). The caller reserves
  // NAV_BAR_HEIGHT whenever the bar is toggled on regardless of width, so an
  // early !visible return left a grey unpainted band on narrow containers
  // (v2.0.6 #6). barRect is valid in both cases (set before Compute's width gate).
  HBRUSH bg = CreateSolidBrush(pal.barBg);
  FillRect(hdc, (RECT*)&lay.barRect, bg);
  DeleteObject(bg);

  // 1px shadow line under the bar (visual separation from pane grid)
  RECT edge = lay.barRect;
  edge.top = edge.bottom - 1;
  HBRUSH edgeBrush = CreateSolidBrush(pal.barBottomEdge);
  FillRect(hdc, &edge, edgeBrush);
  DeleteObject(edgeBrush);

  // Collapsed sliver: hover feedback over the whole strip + a centered
  // downward chevron (the expand affordance). Drawn with pen lines, not a
  // font glyph — Linux fonts render U+25BE as tofu (v2.0.0 R7 lesson).
  if (lay.collapsed) {
    if (state.hoverButton == BTN_COLLAPSE) {
      HBRUSH hov = CreateSolidBrush(pal.btnHoverBg);
      RECT hr = lay.barRect;
      hr.bottom -= 1;  // keep the shadow line visible
      FillRect(hdc, &hr, hov);
      DeleteObject(hov);
    }
    DrawChevron(hdc, lay.barRect, /*pointUp=*/false,
                state.hoverButton == BTN_COLLAPSE ? pal.glyphHover : pal.glyph);
    return;
  }

  // Too narrow to lay out buttons / dividers / workspace label — the filled
  // strip is the whole bar in this state. Done.
  if (!lay.visible) return;

  // Dividers
  auto paintDivider = [&](const RECT& d) {
    if (d.right <= d.left) return;
    HBRUSH db = CreateSolidBrush(pal.divider);
    FillRect(hdc, (RECT*)&d, db);
    DeleteObject(db);
  };
  paintDivider(lay.divider1);
  paintDivider(lay.divider2);

  // Buttons
  for (int i = 0; i < BUTTON_COUNT; i++) {
    const RECT& r = lay.buttons[i];
    if (r.right <= r.left) continue;
    // The collapse chevron and the layout-edit grid have no Phosphor icon
    // (NavIcons is indexed by the pre-collapse button ids) — drawn
    // vector-style below.
    if (i == BTN_COLLAPSE || i == BTN_EDIT_LAYOUT) continue;
    bool hover = (state.hoverButton == i);
    bool active = false;
    bool armed = false;
    if (i == BTN_DRAG && state.dragModeArmed) {
      active = true;
      armed = true;
    } else if (i == BTN_HOME && state.homeActive) {
      active = true;
    }
    DrawButton(hdc, r, i, hover, active, armed, pal);
  }

  // v2.5.0 — layout-edit grid button: 2x2 window grid drawn with 1 px
  // strips (no font glyph — Linux tofu lesson). Lit like Home/Drag while
  // the mode is active.
  {
    const RECT& r = lay.buttons[BTN_EDIT_LAYOUT];
    if (r.right > r.left) {
      const bool hover  = (state.hoverButton == BTN_EDIT_LAYOUT);
      const bool active = state.layoutEditActive;
      if (active || hover) {
        HBRUSH bgb = CreateSolidBrush(active ? pal.btnActiveBg : pal.btnHoverBg);
        FillRect(hdc, (RECT*)&r, bgb);
        DeleteObject(bgb);
      }
      const COLORREF gc = active ? pal.glyphActive : hover ? pal.glyphHover : pal.glyph;
      HBRUSH gb = CreateSolidBrush(gc);
      const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
      const int half = 6;  // 13 px box
      RECT top    = { cx - half, cy - half, cx + half + 1, cy - half + 1 };
      RECT bottom = { cx - half, cy + half, cx + half + 1, cy + half + 1 };
      RECT left   = { cx - half, cy - half, cx - half + 1, cy + half + 1 };
      RECT right  = { cx + half, cy - half, cx + half + 1, cy + half + 1 };
      RECT vmid   = { cx, cy - half, cx + 1, cy + half + 1 };
      RECT hmid   = { cx - half, cy, cx + half + 1, cy + 1 };
      FillRect(hdc, &top, gb);    FillRect(hdc, &bottom, gb);
      FillRect(hdc, &left, gb);   FillRect(hdc, &right, gb);
      FillRect(hdc, &vmid, gb);   FillRect(hdc, &hmid, gb);
      DeleteObject(gb);
    }
  }

  // Collapse chevron (points up = "fold the bar away").
  {
    const RECT& r = lay.buttons[BTN_COLLAPSE];
    if (r.right > r.left) {
      bool hover = (state.hoverButton == BTN_COLLAPSE);
      if (hover) {
        HBRUSH hov = CreateSolidBrush(pal.btnHoverBg);
        FillRect(hdc, (RECT*)&r, hov);
        DeleteObject(hov);
      }
      DrawChevron(hdc, r, /*pointUp=*/true, hover ? pal.glyphHover : pal.glyph);
    }
  }

  // Feature A — workspace-name label. Drawn last (after dividers + buttons)
  // so the centered text sits on the same baseline as the icons. The bullet
  // glyph stays attached to the name and shares its truncation — if the
  // rect is too narrow, DT_END_ELLIPSIS may eat the bullet first; that is
  // acceptable degraded fallback (tooltip still shows the full untruncated
  // string with the dirty marker).
  if (lay.workspaceNameRect.right > lay.workspaceNameRect.left &&
      state.workspaceName && state.workspaceName[0]) {
    char labelBuf[MAX_WORKSPACE_NAME + 8];
    FormatWorkspaceLabel(state.workspaceName, state.workspaceDirty,
                         labelBuf, (int)sizeof(labelBuf));

    HFONT font = CreateFont(WS_NAME_FONT_PX, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                            DEFAULT_PITCH, "");
    HFONT oldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, state.workspaceDirty ? pal.wsNameDirty : pal.wsName);

    UINT flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    if (lay.workspaceNameTruncated) flags |= DT_END_ELLIPSIS;
    RECT tr = lay.workspaceNameRect;
    DrawTextUtf8(hdc, labelBuf, -1, &tr, flags);

    if (oldFont) SelectObject(hdc, oldFont);
    if (font) DeleteObject(font);
  }
}

// ============================================================================
// PaintTooltip
// ============================================================================

void PaintTooltip(HDC hdc, const Layout& lay, int buttonId, const State& state,
                  bool dark)
{
  if (!lay.visible) return;

  // Resolve anchor rect + tooltip text. Standard buttons use lay.buttons[]
  // and ButtonTooltip(). The workspace-name pseudo-button uses
  // lay.workspaceNameRect and the live name from State.
  RECT anchor;
  const char* text = nullptr;
  if (buttonId == BTN_WORKSPACE_NAME) {
    anchor = lay.workspaceNameRect;
    text = (state.workspaceName && state.workspaceName[0]) ? state.workspaceName
                                                           : nullptr;
  } else if (buttonId >= 0 && buttonId < BUTTON_COUNT) {
    anchor = lay.buttons[buttonId];
    text = ButtonTooltip(buttonId, false);
    if (buttonId == BTN_COLLAPSE && state.collapsed) {
      text = "Expand navigation bar";
    }
  } else {
    return;
  }
  if (anchor.right <= anchor.left) return;
  if (!text || !text[0]) return;

  Palette pal = GetPalette(dark);

  // Tooltip for the workspace-name pseudo-button always shows the full
  // untruncated name (with bullet if dirty) — that is the entire point of
  // making it tooltip-able. Other tooltips use the static ButtonTooltip
  // strings unchanged.
  char wsTipBuf[MAX_WORKSPACE_NAME + 8] = {};
  if (buttonId == BTN_WORKSPACE_NAME) {
    FormatWorkspaceLabel(state.workspaceName, state.workspaceDirty,
                         wsTipBuf, (int)sizeof(wsTipBuf));
    text = wsTipBuf;
  }

  // U9 (ADR-068) — scale font + box metrics by the window DPI: REAPER runs
  // DPI-aware, so at 125%+ Windows scaling GDI does not magnify our output
  // and the fixed 12px font read as "~5px tooltips" (X-Raym #67).
  const double dpiScale = MaxPaneDpiScaleForDC(hdc);
  auto ds = [dpiScale](int v) { return (int)(v * dpiScale + 0.5); };

  // Size: measure text + padding.
  HFONT tipFont = CreateFont(ds(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              DEFAULT_PITCH, "");
  HFONT oldFont = tipFont ? (HFONT)SelectObject(hdc, tipFont) : nullptr;

  RECT measure = { 0, 0, 0, 0 };
  DrawTextUtf8(hdc,text, -1, &measure, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
  int tipW = (measure.right - measure.left) + ds(16);
  int tipH = ds(22);
  // Clamp tipW to bar width so very long workspace names don't blow past
  // the container edges; the tooltip inner draw uses DT_SINGLELINE without
  // ellipsis, so a tip wider than the bar would visually clip — clamping
  // gives the user the prefix at least.
  int maxTipW = (lay.barRect.right - lay.barRect.left) - 8;
  if (maxTipW > 0 && tipW > maxTipW) tipW = maxTipW;

  int cx = (anchor.left + anchor.right) / 2;
  int tipX = cx - tipW / 2;
  int tipY = lay.barRect.bottom + ds(6);

  // Clamp X to bar rect (always-visible-inside-container guarantee)
  if (tipX < lay.barRect.left + 4) tipX = lay.barRect.left + 4;
  if (tipX + tipW > lay.barRect.right - 4) tipX = lay.barRect.right - tipW - 4;

  RECT tipRect = { tipX, tipY, tipX + tipW, tipY + tipH };

  // Shadow
  RECT sh = { tipRect.left + 2, tipRect.top + 2,
              tipRect.right + 2, tipRect.bottom + 2 };
  HBRUSH shB = CreateSolidBrush(dark ? RGB(0,0,0) : RGB(180,180,180));
  FillRect(hdc, &sh, shB);
  DeleteObject(shB);

  HBRUSH tb = CreateSolidBrush(pal.tooltipBg);
  FillRect(hdc, &tipRect, tb);
  DeleteObject(tb);

  HPEN bp = CreatePen(PS_SOLID, 1, pal.tooltipBorder);
  HPEN op = (HPEN)SelectObject(hdc, bp);
  HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, tipRect.left, tipRect.top, tipRect.right, tipRect.bottom);
  SelectObject(hdc, op);
  SelectObject(hdc, ob);
  DeleteObject(bp);

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, pal.tooltipText);
  RECT inner = { tipRect.left + ds(8), tipRect.top, tipRect.right - ds(8), tipRect.bottom };
  DrawTextUtf8(hdc,text, -1, &inner,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  if (oldFont) SelectObject(hdc, oldFont);
  if (tipFont) DeleteObject(tipFont);
}

// ============================================================================
// HitTest
// ============================================================================

int HitTest(const Layout& lay, int x, int y)
{
  if (!lay.visible) return BTN_NONE;
  // Bail early if outside the bar rect entirely.
  if (y < lay.barRect.top || y >= lay.barRect.bottom) return BTN_NONE;
  for (int i = 0; i < BUTTON_COUNT; i++) {
    const RECT& r = lay.buttons[i];
    if (r.right <= r.left) continue;
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
  }
  // Feature A — workspace-name pseudo-button. Only returned when the label
  // has a rect (i.e. there is a loaded workspace AND the bar had room).
  const RECT& wr = lay.workspaceNameRect;
  if (wr.right > wr.left && x >= wr.left && x < wr.right &&
      y >= wr.top && y < wr.bottom) {
    return BTN_WORKSPACE_NAME;
  }
  return BTN_NONE;
}

// ============================================================================
// ButtonTooltip
// ============================================================================

const char* ButtonTooltip(int buttonId, bool dragArmed)
{
  switch (buttonId) {
    case BTN_HOME:     return "Workspaces";
    case BTN_DRAG:     return dragArmed ? "Click a window to drag in"
                                        : "Drag a window in";
    case BTN_SWITCH:   return "Quick Switcher";
    case BTN_SAVE:     return "Save workspace";
    case BTN_LOAD:     return "Load workspace";
    case BTN_SETTINGS: return "Settings";
    case BTN_SUPPORT:  return "Support development";
    case BTN_COLLAPSE: return "Collapse navigation bar";
    case BTN_EDIT_LAYOUT: return "Edit layout: hide windows, drag panes to swap (click again to finish)";
    default:           return "";
  }
}

} // namespace NavBar
