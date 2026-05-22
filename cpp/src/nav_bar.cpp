// nav_bar.cpp — Persistent toolbar paint + hit-test (ADR-026).
//
// Design notes:
//  - Two visual groups separated by 1 px dividers. Left group: [Home] then
//    capture-related [Drag][Switch][Save][Load]. Right group, right-aligned:
//    [Settings][Support].
//  - Each button is a fixed BUTTON_SIZE square. Glyph drawn centered.
//    Hover: subtle background fill. Active (BTN_DRAG when armed, BTN_HOME
//    when overlay open): accent fill.
//  - Glyphs are single-character Unicode that ships in default macOS / Win
//    UI fonts (Tahoma/Segoe UI/SF Pro). No emoji — emoji rendering through
//    SWELL DrawText is inconsistent across platforms.
#include "nav_bar.h"
#include <cstring>

namespace NavBar {
namespace {

constexpr int BAR_PADDING_X    = 10;   // outer left/right pad inside bar
constexpr int BUTTON_GAP       = 2;    // gap between adjacent buttons in a group
constexpr int GROUP_GAP        = 14;   // gap on each side of divider
constexpr int DIVIDER_WIDTH    = 1;

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
  }
  return p;
}

// ---- Glyph icons (system font, per-icon size balanced) ------------------
//
// Procedural GDI drawing produced angular/blocky icons (smoke 2026-05-23).
// Reverted to Unicode glyphs which get anti-aliased smoothing from the
// system font rasterizer. Each glyph has a custom font size to balance
// intrinsic visual-weight variance (e.g. ⌂ renders small in most fonts,
// ⊕ renders bold). Per-icon tuning gives a near-uniform appearance.

struct GlyphSpec {
  const char* utf8;
  int fontPx;
  int yOffset;
};

GlyphSpec GlyphFor(int buttonId)
{
  switch (buttonId) {
    case BTN_HOME:     return { "\xe2\x8c\x82", 21,  0 };   // ⌂ small natively → bump
    case BTN_DRAG:     return { "\xe2\x8a\x95", 16,  0 };   // ⊕ already bold
    case BTN_SWITCH:   return { "\xe2\x8c\x95", 21,  0 };   // ⌕ small natively
    case BTN_SAVE:     return { "\xe2\xac\x87", 15,  0 };   // ⬇ bold
    case BTN_LOAD:     return { "\xe2\x89\xa1", 19, -1 };   // ≡ short
    case BTN_SETTINGS: return { "\xe2\x9a\x99", 17,  0 };   // ⚙ medium
    case BTN_SUPPORT:  return { "\xe2\x99\xa5", 15, -1 };   // ♥ medium
    default:           return { "?", 14, 0 };
  }
}

void DrawIconGlyph(HDC hdc, int buttonId, const RECT& r, COLORREF color)
{
  const GlyphSpec g = GlyphFor(buttonId);
  HFONT font = CreateFont(g.fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                          DEFAULT_PITCH, "");
  HFONT oldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, color);
  RECT tr = r;
  tr.top += g.yOffset;
  tr.bottom += g.yOffset;
  DrawTextUtf8(hdc,g.utf8, -1, &tr,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (oldFont) SelectObject(hdc, oldFont);
  if (font) DeleteObject(font);
}

// ---- Button helpers ---------------------------------------------------

void DrawButton(HDC hdc, const RECT& r, int buttonId, bool hover,
                bool active, bool armed, const Palette& pal)
{
  // Background fill if hover/active.
  if (active) {
    HBRUSH bg = CreateSolidBrush(armed ? pal.btnActiveBgArmed : pal.btnActiveBg);
    FillRect(hdc, &r, bg);
    DeleteObject(bg);
  } else if (hover) {
    HBRUSH bg = CreateSolidBrush(pal.btnHoverBg);
    FillRect(hdc, &r, bg);
    DeleteObject(bg);
  }

  // Glyph icon — system font + per-icon size balancing. Anti-aliased
  // smoothing from the font rasterizer reads cleaner than 2 px GDI
  // strokes at this scale.
  COLORREF iconColor = active ? pal.glyphActive
                      : hover ? pal.glyphHover
                      : pal.glyph;
  DrawIconGlyph(hdc, buttonId, r, iconColor);
}

} // namespace (anon)

// ============================================================================
// Compute
// ============================================================================

Layout Compute(const RECT& containerRect)
{
  Layout lay = {};

  const int W = containerRect.right - containerRect.left;
  if (W < MIN_VISIBLE_W) {
    lay.visible = false;
    return lay;
  }
  lay.visible = true;

  lay.barRect.left   = containerRect.left;
  lay.barRect.top    = containerRect.top;
  lay.barRect.right  = containerRect.right;
  lay.barRect.bottom = containerRect.top + NAV_BAR_HEIGHT;

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

  // Capture group: Drag, Switch, Save, Load — uniform width.
  const int captureBtns[] = { BTN_DRAG, BTN_SWITCH, BTN_SAVE, BTN_LOAD };
  for (int i = 0; i < (int)(sizeof(captureBtns) / sizeof(captureBtns[0])); i++) {
    int id = captureBtns[i];
    lay.buttons[id] = { x, btnY, x + BUTTON_SIZE, btnBottom };
    x += BUTTON_SIZE;
    if (i < (int)(sizeof(captureBtns) / sizeof(captureBtns[0])) - 1) x += BUTTON_GAP;
  }

  // Utility group right-aligned: Settings, Support
  int rightX = lay.barRect.right - BAR_PADDING_X;
  lay.buttons[BTN_SUPPORT] = { rightX - BUTTON_SIZE, btnY, rightX, btnBottom };
  rightX -= BUTTON_SIZE + BUTTON_GAP;
  lay.buttons[BTN_SETTINGS] = { rightX - BUTTON_SIZE, btnY, rightX, btnBottom };

  // Divider 2 pinned to the right edge of the capture group (was previously
  // centered in the empty space between groups, which read as a stray line
  // floating in the middle of the nav bar). Visually a continuation of the
  // capture group's boundary, not a free-standing separator.
  int settingsLeft = lay.buttons[BTN_SETTINGS].left;
  int loadRight    = lay.buttons[BTN_LOAD].right;
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

  return lay;
}

// ============================================================================
// Paint
// ============================================================================

void Paint(HDC hdc, const Layout& lay, const State& state, bool dark)
{
  if (!lay.visible) return;
  Palette pal = GetPalette(dark);

  // Bar background
  HBRUSH bg = CreateSolidBrush(pal.barBg);
  FillRect(hdc, (RECT*)&lay.barRect, bg);
  DeleteObject(bg);

  // 1px shadow line under the bar (visual separation from pane grid)
  RECT edge = lay.barRect;
  edge.top = edge.bottom - 1;
  HBRUSH edgeBrush = CreateSolidBrush(pal.barBottomEdge);
  FillRect(hdc, &edge, edgeBrush);
  DeleteObject(edgeBrush);

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
}

// ============================================================================
// PaintTooltip
// ============================================================================

void PaintTooltip(HDC hdc, const Layout& lay, int buttonId, bool dark)
{
  if (!lay.visible) return;
  if (buttonId < 0 || buttonId >= BUTTON_COUNT) return;
  const RECT& btn = lay.buttons[buttonId];
  if (btn.right <= btn.left) return;

  Palette pal = GetPalette(dark);

  // Note: dragArmed not passed in — caller could lift this to State if the
  // armed copy ever diverges per button. For now both share the same text.
  const char* text = ButtonTooltip(buttonId, false);
  if (!text || !text[0]) return;

  // Size: measure text + padding.
  HFONT tipFont = CreateFont(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              DEFAULT_PITCH, "");
  HFONT oldFont = tipFont ? (HFONT)SelectObject(hdc, tipFont) : nullptr;

  RECT measure = { 0, 0, 0, 0 };
  DrawTextUtf8(hdc,text, -1, &measure, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
  int tipW = (measure.right - measure.left) + 16;
  int tipH = 22;

  int cx = (btn.left + btn.right) / 2;
  int tipX = cx - tipW / 2;
  int tipY = lay.barRect.bottom + 6;

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
  RECT inner = { tipRect.left + 8, tipRect.top, tipRect.right - 8, tipRect.bottom };
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
    default:           return "";
  }
}

} // namespace NavBar
