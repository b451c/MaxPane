// launcher.cpp — Empty-state workspace launcher hero.
//
// Visual design (per user choice, 2026-05-21 evening session):
//   ┌─────────────────────────────────────────────┐
//   │  MaxPane                                    │
//   │  Workspace launcher                         │
//   │                                             │
//   │   [card] [card] [card]                      │
//   │   [card] [card] [card]                      │
//   │                                             │
//   │   [ +  Capture a window ]                   │
//   │                                             │
//   │   Right-click for more options              │
//   └─────────────────────────────────────────────┘
//
// Each card shows a mini layout preview computed from the workspace's saved
// NodeSnapshot tree (proportional leaf rectangles) plus the workspace name.
#include "launcher.h"
#include "workspace_manager.h"
#include "split_tree.h"
#include "config.h"
#include <cstdio>
#include <cstring>

namespace Launcher {
namespace {

// ---- Dimensions ----------------------------------------------------------

constexpr int CARD_W            = 124;
constexpr int CARD_H            = 96;
constexpr int CARD_GAP_X        = 14;
constexpr int CARD_GAP_Y        = 16;
constexpr int CARD_PREVIEW_H    = 64;   // preview area inside card
constexpr int CARD_PREVIEW_INSET = 10;  // inset of preview within card
constexpr int OUTER_MARGIN_X    = 36;
constexpr int OUTER_MARGIN_TOP  = 36;
constexpr int OUTER_MARGIN_BOT  = 28;
constexpr int HEADER_BAND_H     = 56;
constexpr int CARDS_GAP_TOP     = 22;
constexpr int CTA_W             = 240;
constexpr int CTA_H             = 42;
constexpr int CTA_GAP_TOP       = 28;
constexpr int TIP_GAP_TOP       = 18;
constexpr int TIP_H             = 18;
constexpr int OVERFLOW_GAP_TOP  = 8;
constexpr int OVERFLOW_H        = 18;
constexpr int MIN_SHOW_W        = 320;
constexpr int MIN_SHOW_H        = 220;
constexpr int MAX_CARDS_PER_ROW = 6;

// ---- Palette -------------------------------------------------------------

struct Palette {
  COLORREF panelBg;
  COLORREF wordmark;
  COLORREF subtitle;
  COLORREF cardBg;
  COLORREF cardBgHover;
  COLORREF cardBorder;
  COLORREF cardBorderHover;
  COLORREF previewLeaf;
  COLORREF previewLeafHover;
  COLORREF previewSplit;
  COLORREF cardName;
  COLORREF ctaBg;
  COLORREF ctaBgHover;
  COLORREF ctaText;
  COLORREF ctaBorder;
  COLORREF tip;
};

Palette GetPalette(bool dark)
{
  Palette p{};
  if (dark) {
    p.panelBg         = RGB( 34,  34,  34);
    p.wordmark        = RGB(238, 238, 238);
    p.subtitle        = RGB(150, 150, 150);
    p.cardBg          = RGB( 48,  48,  48);
    p.cardBgHover     = RGB( 62,  72,  86);
    p.cardBorder      = RGB( 68,  68,  68);
    p.cardBorderHover = RGB(120, 170, 220);
    p.previewLeaf     = RGB(110, 140, 175);
    p.previewLeafHover= RGB(150, 195, 240);
    p.previewSplit    = RGB( 30,  30,  30);
    p.cardName        = RGB(220, 220, 220);
    p.ctaBg           = RGB( 60, 110, 175);
    p.ctaBgHover      = RGB( 78, 135, 210);
    p.ctaText         = RGB(255, 255, 255);
    p.ctaBorder       = RGB( 96, 145, 200);
    p.tip             = RGB(125, 125, 125);
  } else {
    p.panelBg         = RGB(238, 238, 238);
    p.wordmark        = RGB( 30,  30,  30);
    p.subtitle        = RGB(100, 100, 100);
    p.cardBg          = RGB(252, 252, 252);
    p.cardBgHover     = RGB(230, 240, 252);
    p.cardBorder      = RGB(205, 205, 205);
    p.cardBorderHover = RGB( 65, 125, 195);
    p.previewLeaf     = RGB(140, 175, 215);
    p.previewLeafHover= RGB( 70, 130, 200);
    p.previewSplit    = RGB(225, 225, 225);
    p.cardName        = RGB( 30,  30,  30);
    p.ctaBg           = RGB( 70, 130, 200);
    p.ctaBgHover      = RGB( 50, 110, 180);
    p.ctaText         = RGB(255, 255, 255);
    p.ctaBorder       = RGB( 50, 105, 175);
    p.tip             = RGB(120, 120, 120);
  }
  return p;
}

// ---- Mini-preview --------------------------------------------------------

int FindRootIndex(const NodeSnapshot* nodes, int count)
{
  for (int i = 0; i < count; i++) {
    if (nodes[i].type != NODE_EMPTY && nodes[i].parent == -1) return i;
  }
  return -1;
}

// Walk the workspace's snapshot tree, drawing one filled rect per leaf inside
// `area`. Splits are 1px lines in the split-color.
void DrawPreviewRecursive(HDC hdc, const NodeSnapshot* nodes, int count, int idx,
                          RECT area, HBRUSH leafBrush, HBRUSH splitBrush)
{
  if (idx < 0 || idx >= count) return;
  const NodeSnapshot& n = nodes[idx];

  if (n.type == NODE_LEAF) {
    if (area.right - area.left < 2 || area.bottom - area.top < 2) return;
    FillRect(hdc, &area, leafBrush);
    return;
  }
  if (n.type != NODE_BRANCH) return;

  RECT a = area, b = area;
  if (n.orient == SPLIT_VERTICAL) {
    int splitX = area.left + (int)((float)(area.right - area.left) * n.ratio);
    if (splitX < area.left + 1) splitX = area.left + 1;
    if (splitX > area.right - 1) splitX = area.right - 1;
    a.right = splitX;
    b.left  = splitX + 1;
    RECT splitter = { splitX, area.top, splitX + 1, area.bottom };
    if (splitter.right - splitter.left >= 1)
      FillRect(hdc, &splitter, splitBrush);
  } else {
    int splitY = area.top + (int)((float)(area.bottom - area.top) * n.ratio);
    if (splitY < area.top + 1) splitY = area.top + 1;
    if (splitY > area.bottom - 1) splitY = area.bottom - 1;
    a.bottom = splitY;
    b.top    = splitY + 1;
    RECT splitter = { area.left, splitY, area.right, splitY + 1 };
    if (splitter.bottom - splitter.top >= 1)
      FillRect(hdc, &splitter, splitBrush);
  }
  DrawPreviewRecursive(hdc, nodes, count, n.childA, a, leafBrush, splitBrush);
  DrawPreviewRecursive(hdc, nodes, count, n.childB, b, leafBrush, splitBrush);
}

// ---- Slot badge ----------------------------------------------------------

// Draw the F6 slot number ("1".."10") in the top-left of a card. Hint to the
// user which `MaxPane_WsSlot_N` action this workspace responds to. Only valid
// for slotNumber in [1, MAX_WORKSPACES]; higher cards get no badge.
void DrawSlotBadge(HDC hdc, const RECT& cardRect, int slotNumber,
                    bool hover, const Palette& pal)
{
  if (slotNumber < 1 || slotNumber > MAX_WORKSPACES) return;

  const int BADGE_W = 22;
  const int BADGE_H = 16;
  const int BADGE_INSET = 6;

  RECT badge = {
    cardRect.left + BADGE_INSET,
    cardRect.top  + BADGE_INSET,
    cardRect.left + BADGE_INSET + BADGE_W,
    cardRect.top  + BADGE_INSET + BADGE_H
  };

  // Background slightly more saturated than card body so it reads as a chip.
  HBRUSH bg = CreateSolidBrush(hover ? pal.cardBorderHover : pal.cardBorder);
  FillRect(hdc, &badge, bg);
  DeleteObject(bg);

  // Text — white on hover (highlighted bg is dark accent), card-name color otherwise.
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, hover ? RGB(255, 255, 255) : pal.cardName);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", slotNumber);
  // Slightly bolder font for legibility at this size.
  HFONT badgeFont = CreateFont(11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH, "");
  HFONT oldFont = badgeFont ? (HFONT)SelectObject(hdc, badgeFont) : nullptr;
  DrawTextUtf8(hdc, buf, -1, &badge,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (oldFont) SelectObject(hdc, oldFont);
  if (badgeFont) DeleteObject(badgeFont);
}

// ---- Card draw -----------------------------------------------------------

void DrawCard(HDC hdc, const RECT& cardRect, const WorkspaceEntry& ws,
              int slotNumber, bool hover, const Palette& pal)
{
  // Background
  HBRUSH bgBrush = CreateSolidBrush(hover ? pal.cardBgHover : pal.cardBg);
  FillRect(hdc, &cardRect, bgBrush);
  DeleteObject(bgBrush);

  // Border (2px when hover for emphasis)
  int borderW = hover ? 2 : 1;
  HPEN borderPen = CreatePen(PS_SOLID, borderW,
                              hover ? pal.cardBorderHover : pal.cardBorder);
  HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
  HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, cardRect.left, cardRect.top, cardRect.right, cardRect.bottom);
  SelectObject(hdc, oldPen);
  SelectObject(hdc, oldBr);
  DeleteObject(borderPen);

  // Preview area
  RECT previewRect = {
    cardRect.left  + CARD_PREVIEW_INSET,
    cardRect.top   + CARD_PREVIEW_INSET,
    cardRect.right - CARD_PREVIEW_INSET,
    cardRect.top   + CARD_PREVIEW_H - 4
  };

  if (previewRect.right > previewRect.left && previewRect.bottom > previewRect.top) {
    HBRUSH leafBrush  = CreateSolidBrush(hover ? pal.previewLeafHover : pal.previewLeaf);
    HBRUSH splitBrush = CreateSolidBrush(pal.previewSplit);

    if (ws.treeVersion == 2 && ws.nodeCount > 0) {
      int root = FindRootIndex(ws.nodes, ws.nodeCount);
      if (root >= 0) {
        DrawPreviewRecursive(hdc, ws.nodes, ws.nodeCount, root,
                              previewRect, leafBrush, splitBrush);
      } else {
        FillRect(hdc, &previewRect, leafBrush);
      }
    } else {
      // Legacy workspace: render single leaf
      FillRect(hdc, &previewRect, leafBrush);
    }
    DeleteObject(leafBrush);
    DeleteObject(splitBrush);
  }

  // Name band
  RECT nameRect = {
    cardRect.left  + 6,
    cardRect.top   + CARD_PREVIEW_H,
    cardRect.right - 6,
    cardRect.bottom
  };
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, pal.cardName);
  DrawTextUtf8(hdc, ws.name, -1, &nameRect,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

  // F6 slot badge (top-left corner) — drawn last so it sits above preview.
  DrawSlotBadge(hdc, cardRect, slotNumber, hover, pal);
}

// ---- CTA button ----------------------------------------------------------

void DrawCaptureButton(HDC hdc, const RECT& btnRect, bool hover,
                        bool hasWorkspaces, bool armed, const Palette& pal,
                        const char* hoverName)
{
  // Armed = capture-by-click waiting for the user to click a window. Use the
  // hover background so the CTA reads as "active".
  HBRUSH bg = CreateSolidBrush((hover || armed) ? pal.ctaBgHover : pal.ctaBg);
  FillRect(hdc, &btnRect, bg);
  DeleteObject(bg);

  HPEN borderPen = CreatePen(PS_SOLID, 1, pal.ctaBorder);
  HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
  HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, btnRect.left, btnRect.top, btnRect.right, btnRect.bottom);
  SelectObject(hdc, oldPen);
  SelectObject(hdc, oldBr);
  DeleteObject(borderPen);

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, pal.ctaText);
  // ADR-048 — when armed and hovering a capturable window, show its name so
  // the user reads what a click will grab before committing (no blind dock).
  char armedBuf[280];
  const char* label;
  if (armed && hoverName && hoverName[0]) {
    snprintf(armedBuf, sizeof(armedBuf), "Capture: %s   (Esc to cancel)", hoverName);
    label = armedBuf;
  } else if (armed) {
    label = "Click a window to capture...  (Esc to cancel)";
  } else {
    label = hasWorkspaces ? "+  Capture a window"
                          : "+  Capture your first window";
  }
  RECT textRect = btnRect;
  DrawTextUtf8(hdc, label, -1, &textRect,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

} // namespace (anon)

// ============================================================================
// Compute
// ============================================================================

Layout Compute(const RECT& containerRect, const WorkspaceManager& wsMgr)
{
  Layout lay = {};
  lay.containerRect = containerRect;

  const int W = containerRect.right - containerRect.left;
  const int H = containerRect.bottom - containerRect.top;

  lay.totalWorkspaces = wsMgr.GetCount();
  lay.hasWorkspaces = (lay.totalWorkspaces > 0);

  if (W < MIN_SHOW_W || H < MIN_SHOW_H) {
    lay.visible = false;
    return lay;
  }
  lay.visible = true;

  // Header band
  lay.headerRect = {
    containerRect.left  + OUTER_MARGIN_X,
    containerRect.top   + OUTER_MARGIN_TOP,
    containerRect.right - OUTER_MARGIN_X,
    containerRect.top   + OUTER_MARGIN_TOP + HEADER_BAND_H
  };

  int cursorY = lay.headerRect.bottom + CARDS_GAP_TOP;
  bool overflow = false;
  int rowsDrawn = 0;
  int perRow = 0;

  if (lay.hasWorkspaces) {
    const int availW = W - OUTER_MARGIN_X * 2;
    perRow = (availW + CARD_GAP_X) / (CARD_W + CARD_GAP_X);
    if (perRow < 1) perRow = 1;
    if (perRow > MAX_CARDS_PER_ROW) perRow = MAX_CARDS_PER_ROW;

    // Reserve room for CTA + tip + bottom margin
    const int reservedBelow = CTA_GAP_TOP + CTA_H + TIP_GAP_TOP + TIP_H + OUTER_MARGIN_BOT;
    int availH = (containerRect.bottom - reservedBelow) - cursorY;
    if (availH < CARD_H) availH = CARD_H;
    int maxRows = (availH + CARD_GAP_Y) / (CARD_H + CARD_GAP_Y);
    if (maxRows < 1) maxRows = 1;

    int maxCards = perRow * maxRows;
    if (maxCards > MAX_CARDS_SHOWN) maxCards = MAX_CARDS_SHOWN;

    int shown = lay.totalWorkspaces < maxCards ? lay.totalWorkspaces : maxCards;
    lay.cardCount = shown;
    overflow = (shown < lay.totalWorkspaces);

    int rows = (shown + perRow - 1) / perRow;
    rowsDrawn = rows;

    for (int i = 0; i < shown; i++) {
      int row = i / perRow;
      int col = i % perRow;
      int cardsInThisRow = (row == rows - 1)
        ? (shown - row * perRow) : perRow;
      int rowGridW = cardsInThisRow * CARD_W + (cardsInThisRow - 1) * CARD_GAP_X;
      int rowLeft = containerRect.left + (W - rowGridW) / 2;
      int cx = rowLeft + col * (CARD_W + CARD_GAP_X);
      int cy = cursorY + row * (CARD_H + CARD_GAP_Y);
      lay.cards[i].left = cx;
      lay.cards[i].top = cy;
      lay.cards[i].right = cx + CARD_W;
      lay.cards[i].bottom = cy + CARD_H;
    }

    cursorY = cursorY + rows * CARD_H + (rows > 0 ? (rows - 1) * CARD_GAP_Y : 0);
  }

  // Overflow indicator
  if (overflow) {
    lay.overflowRect = {
      containerRect.left  + OUTER_MARGIN_X,
      cursorY + OVERFLOW_GAP_TOP,
      containerRect.right - OUTER_MARGIN_X,
      cursorY + OVERFLOW_GAP_TOP + OVERFLOW_H
    };
    cursorY = lay.overflowRect.bottom;
  } else {
    lay.overflowRect = {};
  }

  // CTA button (centered)
  int ctaY = cursorY + CTA_GAP_TOP;
  int ctaLeft = containerRect.left + (W - CTA_W) / 2;
  lay.captureBtn = { ctaLeft, ctaY, ctaLeft + CTA_W, ctaY + CTA_H };

  // Tip
  int tipY = lay.captureBtn.bottom + TIP_GAP_TOP;
  lay.tipRect = {
    containerRect.left  + OUTER_MARGIN_X,
    tipY,
    containerRect.right - OUTER_MARGIN_X,
    tipY + TIP_H
  };

  (void)rowsDrawn; (void)perRow;  // reserved for future debug overlay
  return lay;
}

// ============================================================================
// Paint
// ============================================================================

void Paint(HDC hdc, const Layout& lay, const WorkspaceManager& wsMgr,
            int hoverTarget, bool dark, bool captureArmed,
            const char* captureHoverName)
{
  Palette pal = GetPalette(dark);

  // Full panel background
  HBRUSH bg = CreateSolidBrush(pal.panelBg);
  FillRect(hdc, (RECT*)&lay.containerRect, bg);
  DeleteObject(bg);

  if (!lay.visible) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, pal.tip);
    DrawTextUtf8(hdc, "MaxPane — resize the panel for the launcher",
             -1, (RECT*)&lay.containerRect,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return;
  }

  // Header — wordmark (bold, larger) over subtitle.
  RECT wordRect = lay.headerRect;
  wordRect.bottom = wordRect.top + 32;
  RECT subRect = lay.headerRect;
  subRect.top = wordRect.bottom - 2;

  HFONT wordFont = CreateFont(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                               DEFAULT_PITCH, "");
  HFONT oldFont = wordFont ? (HFONT)SelectObject(hdc, wordFont) : nullptr;
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, pal.wordmark);
  DrawTextUtf8(hdc, "MaxPane", -1, &wordRect,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (oldFont) SelectObject(hdc, oldFont);
  if (wordFont) DeleteObject(wordFont);

  SetTextColor(hdc, pal.subtitle);
  DrawTextUtf8(hdc,
            lay.hasWorkspaces ? "Workspace launcher"
                              : "Get started — capture your first window",
            -1, &subRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // Cards — slot number = card index + 1 (1-based, matching F6 action names)
  for (int i = 0; i < lay.cardCount; i++) {
    bool h = (i == hoverTarget);
    const WorkspaceEntry& ws = wsMgr.Get(i);
    DrawCard(hdc, lay.cards[i], ws, i + 1, h, pal);
  }

  // Overflow hint
  if (lay.overflowRect.bottom > lay.overflowRect.top) {
    char msg[80];
    snprintf(msg, sizeof(msg),
             "+%d more — right-click for the full list",
             lay.totalWorkspaces - lay.cardCount);
    SetTextColor(hdc, pal.tip);
    DrawTextUtf8(hdc, msg, -1, (RECT*)&lay.overflowRect,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  }

  // CTA
  DrawCaptureButton(hdc, lay.captureBtn,
                     hoverTarget == HIT_CAPTURE_BUTTON,
                     lay.hasWorkspaces, captureArmed, pal, captureHoverName);

  // Tip
  SetTextColor(hdc, pal.tip);
  const char* tip = lay.hasWorkspaces
    ? "Right-click anywhere for more options"
    : "After capture, right-click → Save Workspace to make it one-click next time";
  DrawTextUtf8(hdc, tip, -1, (RECT*)&lay.tipRect,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

// ============================================================================
// PaintTooltip
// ============================================================================

namespace {

// Tooltip dimensions
constexpr int TIP_MAX_W      = 280;
constexpr int TIP_MIN_W      = 160;
constexpr int TIP_PAD_X      = 12;
constexpr int TIP_PAD_Y      = 10;
constexpr int TIP_LINE_H     = 18;
constexpr int TIP_HEADER_H   = 22;
constexpr int TIP_HEADER_GAP = 4;
constexpr int TIP_MAX_LINES  = 10;
constexpr int TIP_GAP_FROM_CARD = 8;

// Collect window names captured in the workspace, in stable pane/tab order.
// Returns the count actually written (clamped to maxLines+1, where the +1 is
// a "...+N more" overflow line). `linesOut` must hold maxLines+1 strings.
int CollectWorkspaceWindowNames(const WorkspaceEntry& ws,
                                 char linesOut[][128], int maxLines)
{
  int total = 0;
  for (int p = 0; p < MAX_PANES; p++) {
    if (ws.panes[p].tabCount == 0) continue;
    for (int t = 0; t < ws.panes[p].tabCount; t++) {
      const char* n = ws.panes[p].tabs[t].name;
      if (!n || !n[0]) continue;
      if (total < maxLines) {
        snprintf(linesOut[total], 128, "%s", n);
      }
      total++;
    }
  }
  if (total > maxLines) {
    snprintf(linesOut[maxLines], 128, "+%d more…", total - maxLines);
    return maxLines + 1;
  }
  return total;
}

} // namespace (anon)

void PaintTooltip(HDC hdc, const Layout& lay, const WorkspaceManager& wsMgr,
                   int cardIndex, bool dark)
{
  if (!lay.visible) return;
  if (cardIndex < 0 || cardIndex >= lay.cardCount) return;
  if (cardIndex >= wsMgr.GetCount()) return;

  const WorkspaceEntry& ws = wsMgr.Get(cardIndex);
  const RECT& cardRect = lay.cards[cardIndex];

  // Collect window names
  char lines[TIP_MAX_LINES + 1][128];
  int lineCount = CollectWorkspaceWindowNames(ws, lines, TIP_MAX_LINES);

  // Compute height
  int contentH;
  if (lineCount == 0) {
    contentH = TIP_HEADER_H + TIP_HEADER_GAP + TIP_LINE_H; // header + "(empty)"
  } else {
    contentH = TIP_HEADER_H + TIP_HEADER_GAP + lineCount * TIP_LINE_H;
  }
  int tipH = contentH + TIP_PAD_Y * 2;
  int tipW = TIP_MAX_W;
  if (tipW > lay.containerRect.right - lay.containerRect.left - 16)
    tipW = lay.containerRect.right - lay.containerRect.left - 16;
  if (tipW < TIP_MIN_W) tipW = TIP_MIN_W;

  // Position: prefer below card, anchored to card's left edge; clamp to
  // container so it stays visible. Fall back to above if no room below.
  int tipX = cardRect.left;
  int tipY = cardRect.bottom + TIP_GAP_FROM_CARD;

  if (tipY + tipH > lay.containerRect.bottom - 4) {
    int aboveY = cardRect.top - TIP_GAP_FROM_CARD - tipH;
    if (aboveY >= lay.containerRect.top + 4) {
      tipY = aboveY;
    } else {
      // Neither below nor above fits — pin at bottom of container, allow overlap
      tipY = lay.containerRect.bottom - tipH - 4;
    }
  }
  if (tipX + tipW > lay.containerRect.right - 4)
    tipX = lay.containerRect.right - tipW - 4;
  if (tipX < lay.containerRect.left + 4) tipX = lay.containerRect.left + 4;

  RECT tipRect = { tipX, tipY, tipX + tipW, tipY + tipH };

  // Subtle drop shadow (2px offset, dark for both modes)
  {
    RECT shadow = { tipRect.left + 2, tipRect.top + 3,
                    tipRect.right + 2, tipRect.bottom + 3 };
    HBRUSH sh = CreateSolidBrush(dark ? RGB(0,0,0) : RGB(170,170,170));
    FillRect(hdc, &shadow, sh);
    DeleteObject(sh);
  }

  // Background
  COLORREF tipBg     = dark ? RGB(46, 50, 58)   : RGB(255, 255, 255);
  COLORREF tipBorder = dark ? RGB(80, 86, 96)   : RGB(180, 180, 180);
  COLORREF tipHeader = dark ? RGB(240, 240, 240) : RGB(30, 30, 30);
  COLORREF tipBody   = dark ? RGB(200, 205, 215) : RGB(70, 70, 70);
  COLORREF tipDivider = dark ? RGB(65, 70, 80)  : RGB(220, 220, 220);
  COLORREF tipMuted  = dark ? RGB(140, 145, 155) : RGB(140, 140, 140);

  HBRUSH bg = CreateSolidBrush(tipBg);
  FillRect(hdc, &tipRect, bg);
  DeleteObject(bg);

  HPEN borderPen = CreatePen(PS_SOLID, 1, tipBorder);
  HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
  HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, tipRect.left, tipRect.top, tipRect.right, tipRect.bottom);
  SelectObject(hdc, oldPen);
  SelectObject(hdc, oldBr);
  DeleteObject(borderPen);

  // Header (workspace name, bold)
  SetBkMode(hdc, TRANSPARENT);

  RECT headRect = {
    tipRect.left + TIP_PAD_X,
    tipRect.top + TIP_PAD_Y,
    tipRect.right - TIP_PAD_X,
    tipRect.top + TIP_PAD_Y + TIP_HEADER_H
  };
  HFONT headerFont = CreateFont(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                 DEFAULT_PITCH, "");
  HFONT oldFont = headerFont ? (HFONT)SelectObject(hdc, headerFont) : nullptr;
  SetTextColor(hdc, tipHeader);
  DrawTextUtf8(hdc, ws.name, -1, &headRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  if (oldFont) SelectObject(hdc, oldFont);
  if (headerFont) DeleteObject(headerFont);

  // Divider line under header
  {
    int divY = headRect.bottom + 2;
    RECT divRect = { tipRect.left + TIP_PAD_X, divY,
                     tipRect.right - TIP_PAD_X, divY + 1 };
    HBRUSH dv = CreateSolidBrush(tipDivider);
    FillRect(hdc, &divRect, dv);
    DeleteObject(dv);
  }

  // Body lines
  int lineY = headRect.bottom + TIP_HEADER_GAP + 2;
  if (lineCount == 0) {
    RECT lr = { tipRect.left + TIP_PAD_X, lineY,
                tipRect.right - TIP_PAD_X, lineY + TIP_LINE_H };
    SetTextColor(hdc, tipMuted);
    DrawTextUtf8(hdc, "(no captures saved)", -1, &lr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  } else {
    for (int i = 0; i < lineCount; i++) {
      RECT lr = { tipRect.left + TIP_PAD_X, lineY,
                  tipRect.right - TIP_PAD_X, lineY + TIP_LINE_H };
      bool isOverflow = (i == TIP_MAX_LINES && lineCount == TIP_MAX_LINES + 1);
      SetTextColor(hdc, isOverflow ? tipMuted : tipBody);
      // Prefix with bullet
      char buf[160];
      snprintf(buf, sizeof(buf), "%s  %s",
                isOverflow ? " " : "\xe2\x80\xa2",  // • or space for overflow
                lines[i]);
      DrawTextUtf8(hdc, buf, -1, &lr,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
      lineY += TIP_LINE_H;
    }
  }
}

// ============================================================================
// HitTest
// ============================================================================

int HitTest(const Layout& lay, int x, int y)
{
  if (!lay.visible) return HIT_NONE;

  for (int i = 0; i < lay.cardCount; i++) {
    const RECT& r = lay.cards[i];
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
  }
  const RECT& b = lay.captureBtn;
  if (x >= b.left && x < b.right && y >= b.top && y < b.bottom)
    return HIT_CAPTURE_BUTTON;

  return HIT_NONE;
}

} // namespace Launcher
