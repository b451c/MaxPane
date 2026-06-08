// launcher.h — Empty-state workspace launcher hero.
//
// Per ADR-013, MaxPane v2.0 opens empty at startup. When the container has
// exactly one empty pane and no other UI mode is active, we paint a launcher
// hero instead of the generic empty-pane UI: branded header, grid of clickable
// workspace cards (with mini layout previews), and a "Capture a window" CTA.
//
// The launcher disappears as soon as any pane has content (workspace loaded,
// window captured) and reappears whenever the container returns to empty state.
#pragma once
#include "platform.h"

class WorkspaceManager;

namespace Launcher {

// Hit-test targets returned by HitTest.
enum : int {
  HIT_NONE = -1,
  HIT_CAPTURE_BUTTON = -2,
  // 0..N-1 are workspace card indices into WorkspaceManager
};

// Maximum cards laid out per frame. Excess workspaces collapse into an
// overflow hint ("+N more — right-click for full list").
static constexpr int MAX_CARDS_SHOWN = 16;

struct Layout {
  bool visible;              // false when the container is too small for the hero
  RECT containerRect;        // full client area
  RECT headerRect;           // wordmark + subtitle band
  bool hasWorkspaces;        // false → first-launch / onboarding tone
  int  cardCount;            // cards actually laid out (≤ totalWorkspaces)
  RECT cards[MAX_CARDS_SHOWN];
  RECT overflowRect;         // shown only when cardCount < totalWorkspaces
  int  totalWorkspaces;
  RECT captureBtn;           // primary CTA
  RECT tipRect;              // bottom hint text
};

// Plan the hero layout for the given client rect and workspace list.
Layout Compute(const RECT& containerRect, const WorkspaceManager& wsMgr);

// Render the hero into hdc. `hoverTarget` should be HIT_NONE, HIT_CAPTURE_BUTTON,
// or a card index in [0, cardCount). `captureArmed` (v2.0.6) morphs the CTA into
// a "Click a window to capture..." armed state while capture-by-click is waiting.
// `captureHoverName` (ADR-048), when armed and non-empty, shows the resolved
// target name ("Capture: <name>") so the user reads what a click will grab.
void Paint(HDC hdc, const Layout& lay, const WorkspaceManager& wsMgr,
           int hoverTarget, bool darkMode, bool captureArmed = false,
           const char* captureHoverName = nullptr);

// Render a tooltip for the given card showing the windows captured in that
// workspace. Called after Paint so it draws above cards.
void PaintTooltip(HDC hdc, const Layout& lay, const WorkspaceManager& wsMgr,
                  int cardIndex, bool darkMode);

// Returns HIT_NONE | HIT_CAPTURE_BUTTON | card index.
int HitTest(const Layout& lay, int x, int y);

} // namespace Launcher
