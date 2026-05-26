#pragma once
#include "platform.h"

// Forward declarations match globals.h (REAPER SDK types are class, not struct).
class MediaTrack;
class MediaItem_Take;

// v2.0.4 #1 — AU/VST plugin window save/restore via native track+FX GUID
// pair (ADR-037). The identity REAPER itself persists in RPP `FXID {…}`
// blocks: a (track GUID, FX GUID) tuple for track FX, or (take GUID,
// FX GUID) for take FX, with a sentinel literal `master` for master-track
// FX. Stable across project save/reload, REAPER restart, FX slot reorder.
//
// Workspace encoding (embedded in the existing `arb:<actionCmd>:<name>`
// envelope, using `@` as inner separator since GUIDs cannot contain `@`):
//   fx@{track_guid}@{fx_guid}@<flags>          — track FX
//   fx@master@{fx_guid}@<flags>                — master track FX
//   takefx@{take_guid}@{fx_guid}@<flags>       — take FX
//
// <flags> encodes REAPER's FX-index modifiers in decimal:
//   0          — regular FX
//   16777216   — input/record FX on normal track, hardware-output FX on master
//   33554432   — FX inside a container (NOT supported in v2.0.4; documented limitation)
//
// Cross-platform via REAPER SDK only — zero #ifdef in this module.

namespace FxCapture {

enum class IdentityKind {
  None,
  TrackFx,    // regular track FX or recFX (flags includes 0x1000000)
  MasterFx,   // master track FX (track_guid == "master" sentinel)
  TakeFx,     // take/item FX
};

constexpr int kIdentityMaxLen = 128;   // matches TabEntry::actionCmd
constexpr int kGuidStrLen = 64;        // guidToString destNeed64

// Resolved (track, fxIndex) tuple. For TakeFx, `take` is non-null and
// `track` is the take's parent track (filled for diagnostics; index uses
// `take`). For MasterFx, `track` is the master track. `fxIndex` already
// includes the flags bits (caller passes it directly to TrackFX_Show etc).
struct ResolvedLocation {
  IdentityKind kind = IdentityKind::None;
  MediaTrack* track = nullptr;
  MediaItem_Take* take = nullptr;
  int fxIndex = -1;
};

// Walk every loaded project's tracks (master + regular + recFX) and
// items/takes, calling TrackFX_GetFloatingWindow / TakeFX_GetFloatingWindow
// on each FX, matching against `hwnd`. On match, emit the identity string
// (always less than kIdentityMaxLen chars). Returns true on match.
// Cost: O(projects * tracks * fx + items * takes * takefx) — for a typical
// 100-track project ~1-2 ms. Safe to call from DoCapture hot path.
bool DetectFxIdentityForHwnd(HWND hwnd, char* outIdentity, int outIdentitySize);

// Reverse parser. Splits the identity into (kind, owner GUID,
// fx GUID, flags). owner GUID is the track or take GUID string (including
// braces), or the literal "master" for MasterFx.
bool ParseIdentity(const char* identity,
                   IdentityKind& outKind,
                   char* outOwnerGuid, int outOwnerGuidSize,
                   char* outFxGuid, int outFxGuidSize,
                   int& outFlags);

// Walk loaded projects to resolve identity → (MediaTrack*, fxIndex) or
// (MediaItem_Take*, fxIndex). FX GUID match is the authoritative test —
// FX slot order can shift between save and restore, but FX GUID is stable.
// Returns false if owner or FX GUID not found in any loaded project.
// `outOwnerFound` (optional) distinguishes the two failure modes for toast
// copy: true → owner track/take exists but FX GUID is not on it; false →
// owner itself wasn't found (track deleted, project unloaded).
bool ResolveLocation(const char* identity, ResolvedLocation& out,
                     bool* outOwnerFound = nullptr);

// Open the FX UI as a floating window (TrackFX_Show showFlag=3 or
// TakeFX_Show showFlag=3), then return the live HWND via
// TrackFX_GetFloatingWindow / TakeFX_GetFloatingWindow. Returns false if
// ResolveLocation fails or the SDK call doesn't produce a window.
bool ShowAndGetHwnd(const char* identity, HWND& outHwnd);

// Hide the floating FX window (TrackFX_Show showFlag=2). Idempotent.
bool Hide(const char* identity);

// Stable display label via TrackFX_GetFXName / TakeFX_GetFXName.
// Returns "VST3: Pro-Q 4 (FabFilter)"-style strings. Survives plugin
// renames at the project level (REAPER's name is the plugin-side label).
bool GetDisplayName(const char* identity, char* out, int outSize);

// True iff the actionCmd starts with one of the FX identity prefixes
// (`fx@` or `takefx@`). Cheap; safe to call from any branch.
bool IsFxIdentity(const char* actionCmd);

}  // namespace FxCapture
