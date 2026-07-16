#include "fx_capture.h"
#include "globals.h"
#include "debug.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace FxCapture {

namespace {

// Cap recFX scan to avoid runaway when GUID stays non-null past the last
// slot. No project in practice has more than a handful of recFX per track.
constexpr int kMaxRecFxScan = 64;

constexpr int kFlagRecFx = 0x1000000;
// constexpr int kFlagContainerFx = 0x2000000;  // not supported in v2.0.4

static bool ApiReady()
{
  return g_EnumProjects && g_CountTracks && g_GetTrack && g_GetMasterTrack &&
         g_TrackFX_GetCount && g_TrackFX_GetFloatingWindow &&
         g_TrackFX_GetFXGUID && g_TrackFX_Show && g_TrackFX_GetFXName &&
         g_TakeFX_GetCount && g_TakeFX_GetFloatingWindow &&
         g_TakeFX_GetFXGUID && g_TakeFX_Show && g_TakeFX_GetFXName &&
         g_GetSetMediaTrackInfo_String && g_GetSetMediaItemTakeInfo_String &&
         g_GetMediaItem && g_GetMediaItemTake && g_GetMediaItemNumTakes &&
         g_CountMediaItems && g_guidToString;
}

static void EmitTrackIdentity(MediaTrack* tr, int fxIndex, bool isMaster,
                              char* outIdentity, int outIdentitySize)
{
  char trackGuidStr[kGuidStrLen] = {};
  if (isMaster) {
    safe_strncpy(trackGuidStr, "master", sizeof(trackGuidStr));
  } else {
    g_GetSetMediaTrackInfo_String(tr, "GUID", trackGuidStr, false);
  }

  char fxGuidStr[kGuidStrLen] = {};
  GUID* fxGuid = g_TrackFX_GetFXGUID(tr, fxIndex);
  if (fxGuid) g_guidToString(fxGuid, fxGuidStr);

  int flags = fxIndex & 0xFF000000;  // preserve only the documented flag bits
  snprintf(outIdentity, outIdentitySize, "fx@%s@%s@%d",
           trackGuidStr, fxGuidStr, flags);
}

static void EmitTakeIdentity(MediaItem_Take* take, int fxIndex,
                             char* outIdentity, int outIdentitySize)
{
  char takeGuidStr[kGuidStrLen] = {};
  g_GetSetMediaItemTakeInfo_String(take, "GUID", takeGuidStr, false);

  char fxGuidStr[kGuidStrLen] = {};
  GUID* fxGuid = g_TakeFX_GetFXGUID(take, fxIndex);
  if (fxGuid) g_guidToString(fxGuid, fxGuidStr);

  int flags = fxIndex & 0xFF000000;
  snprintf(outIdentity, outIdentitySize, "takefx@%s@%s@%d",
           takeGuidStr, fxGuidStr, flags);
}

// Walk one track's main chain + recFX, calling `visit(track, idx)` for
// each FX. Returns true to abort iteration (match found by visitor).
template <typename V>
static bool WalkTrackFx(MediaTrack* tr, V&& visit)
{
  if (!tr) return false;
  int n = g_TrackFX_GetCount(tr);
  for (int i = 0; i < n; i++) {
    if (visit(i)) return true;
  }
  for (int i = 0; i < kMaxRecFxScan; i++) {
    int idx = kFlagRecFx | i;
    if (!g_TrackFX_GetFXGUID(tr, idx)) break;
    if (visit(idx)) return true;
  }
  return false;
}

}  // namespace

// IsFxIdentity moved to fx_capture.h as an inline (A5/D4) — the persistence
// writers (workspace_manager.cpp, project_state.cpp) need it in the pure-
// logic unit-test binary, which deliberately doesn't link this TU.

int ListTrackFxIdentities(MediaTrack* track,
                          char (*idsOut)[kIdentityMaxLen],
                          char (*namesOut)[256],
                          int maxCount)
{
  if (!track || !idsOut || !namesOut || maxCount <= 0) return 0;
  if (!ApiReady()) return 0;
  const bool isMaster =
      (g_GetMasterTrack &&
       track == g_GetMasterTrack(g_EnumProjects(-1, nullptr, 0)));
  const int total = g_TrackFX_GetCount(track);
  const int take = total < maxCount ? total : maxCount;
  for (int i = 0; i < take; i++) {
    EmitTrackIdentity(track, i, isMaster, idsOut[i], kIdentityMaxLen);
    namesOut[i][0] = '\0';
    if (!g_TrackFX_GetFXName(track, i, namesOut[i], 256) || !namesOut[i][0]) {
      snprintf(namesOut[i], 256, "FX %d", i + 1);
    }
  }
  return total;
}

bool DetectFxIdentityForHwnd(HWND hwnd, char* outIdentity, int outIdentitySize)
{
  if (!hwnd || !outIdentity || outIdentitySize <= 0) return false;
  if (!ApiReady()) return false;
  outIdentity[0] = '\0';

  for (int p = 0; ; p++) {
    ReaProject* proj = g_EnumProjects(p, nullptr, 0);
    if (!proj) break;

    // (a) Master track — main chain + hardware-output FX
    MediaTrack* master = g_GetMasterTrack(proj);
    if (master) {
      bool matched = WalkTrackFx(master, [&](int idx) -> bool {
        if (g_TrackFX_GetFloatingWindow(master, idx) == hwnd) {
          EmitTrackIdentity(master, idx, /*isMaster=*/true,
                            outIdentity, outIdentitySize);
          return true;
        }
        return false;
      });
      if (matched) {
        DBG("[MaxPane] FxCapture::Detect: master match identity='%s'\n", outIdentity);
        return true;
      }
    }

    // (b) Regular tracks — main chain + recFX
    int trackCount = g_CountTracks(proj);
    for (int t = 0; t < trackCount; t++) {
      MediaTrack* tr = g_GetTrack(proj, t);
      if (!tr) continue;
      bool matched = WalkTrackFx(tr, [&](int idx) -> bool {
        if (g_TrackFX_GetFloatingWindow(tr, idx) == hwnd) {
          EmitTrackIdentity(tr, idx, /*isMaster=*/false,
                            outIdentity, outIdentitySize);
          return true;
        }
        return false;
      });
      if (matched) {
        DBG("[MaxPane] FxCapture::Detect: track %d match identity='%s'\n",
            t, outIdentity);
        return true;
      }
    }

    // (c) Take FX — items × takes × takeFX
    int itemCount = g_CountMediaItems(proj);
    for (int it = 0; it < itemCount; it++) {
      MediaItem* item = g_GetMediaItem(proj, it);
      if (!item) continue;
      int takeCount = g_GetMediaItemNumTakes(item);
      for (int tk = 0; tk < takeCount; tk++) {
        MediaItem_Take* take = g_GetMediaItemTake(item, tk);
        if (!take) continue;
        int n = g_TakeFX_GetCount(take);
        for (int i = 0; i < n; i++) {
          if (g_TakeFX_GetFloatingWindow(take, i) == hwnd) {
            EmitTakeIdentity(take, i, outIdentity, outIdentitySize);
            DBG("[MaxPane] FxCapture::Detect: take match identity='%s'\n",
                outIdentity);
            return true;
          }
        }
      }
    }
  }

  return false;
}

bool ParseIdentity(const char* identity,
                   IdentityKind& outKind,
                   char* outOwnerGuid, int outOwnerGuidSize,
                   char* outFxGuid, int outFxGuidSize,
                   int& outFlags)
{
  outKind = IdentityKind::None;
  outFlags = 0;
  if (outOwnerGuid && outOwnerGuidSize > 0) outOwnerGuid[0] = '\0';
  if (outFxGuid && outFxGuidSize > 0) outFxGuid[0] = '\0';
  if (!identity || !identity[0]) return false;

  const char* p = identity;
  bool isTakeFx = false;
  if (strncmp(p, "fx@", 3) == 0) {
    p += 3;
  } else if (strncmp(p, "takefx@", 7) == 0) {
    p += 7;
    isTakeFx = true;
  } else {
    return false;
  }

  const char* at1 = strchr(p, '@');
  if (!at1) return false;
  int ownerLen = (int)(at1 - p);
  if (ownerLen < 1) return false;
  if (outOwnerGuid && outOwnerGuidSize > 0) {
    int copyLen = ownerLen < outOwnerGuidSize - 1 ? ownerLen : outOwnerGuidSize - 1;
    memcpy(outOwnerGuid, p, copyLen);
    outOwnerGuid[copyLen] = '\0';
  }

  p = at1 + 1;
  const char* at2 = strchr(p, '@');
  if (!at2) return false;
  int fxLen = (int)(at2 - p);
  if (fxLen < 1) return false;
  if (outFxGuid && outFxGuidSize > 0) {
    int copyLen = fxLen < outFxGuidSize - 1 ? fxLen : outFxGuidSize - 1;
    memcpy(outFxGuid, p, copyLen);
    outFxGuid[copyLen] = '\0';
  }

  p = at2 + 1;
  outFlags = (int)strtol(p, nullptr, 10);

  if (isTakeFx) {
    outKind = IdentityKind::TakeFx;
  } else if (outOwnerGuid && strcmp(outOwnerGuid, "master") == 0) {
    outKind = IdentityKind::MasterFx;
  } else {
    outKind = IdentityKind::TrackFx;
  }
  return true;
}

bool ResolveLocation(const char* identity, ResolvedLocation& out,
                     bool* outOwnerFound)
{
  out.kind = IdentityKind::None;
  out.track = nullptr;
  out.take = nullptr;
  out.fxIndex = -1;
  if (outOwnerFound) *outOwnerFound = false;
  if (!ApiReady()) return false;

  IdentityKind kind;
  char ownerGuid[kGuidStrLen] = {};
  char fxGuid[kGuidStrLen] = {};
  int flags = 0;
  if (!ParseIdentity(identity, kind,
                     ownerGuid, sizeof(ownerGuid),
                     fxGuid, sizeof(fxGuid),
                     flags)) {
    return false;
  }

  const bool wantRecFx = (flags & kFlagRecFx) != 0;

  for (int p = 0; ; p++) {
    ReaProject* proj = g_EnumProjects(p, nullptr, 0);
    if (!proj) break;

    if (kind == IdentityKind::MasterFx) {
      MediaTrack* master = g_GetMasterTrack(proj);
      if (!master) continue;
      if (outOwnerFound) *outOwnerFound = true;  // master always exists in every project
      bool found = false;
      WalkTrackFx(master, [&](int idx) -> bool {
        const bool isRec = (idx & kFlagRecFx) != 0;
        if (isRec != wantRecFx) return false;
        GUID* g = g_TrackFX_GetFXGUID(master, idx);
        if (!g) return false;
        char gs[kGuidStrLen] = {};
        g_guidToString(g, gs);
        if (strcmp(gs, fxGuid) == 0) {
          out.kind = kind;
          out.track = master;
          out.fxIndex = idx;
          found = true;
          return true;
        }
        return false;
      });
      if (found) return true;
    } else if (kind == IdentityKind::TrackFx) {
      int trackCount = g_CountTracks(proj);
      for (int t = 0; t < trackCount; t++) {
        MediaTrack* tr = g_GetTrack(proj, t);
        if (!tr) continue;
        char trGuid[kGuidStrLen] = {};
        if (!g_GetSetMediaTrackInfo_String(tr, "GUID", trGuid, false)) continue;
        if (strcmp(trGuid, ownerGuid) != 0) continue;
        if (outOwnerFound) *outOwnerFound = true;  // owner track exists

        bool found = false;
        WalkTrackFx(tr, [&](int idx) -> bool {
          const bool isRec = (idx & kFlagRecFx) != 0;
          if (isRec != wantRecFx) return false;
          GUID* g = g_TrackFX_GetFXGUID(tr, idx);
          if (!g) return false;
          char gs[kGuidStrLen] = {};
          g_guidToString(g, gs);
          if (strcmp(gs, fxGuid) == 0) {
            out.kind = kind;
            out.track = tr;
            out.fxIndex = idx;
            found = true;
            return true;
          }
          return false;
        });
        if (found) return true;
      }
    } else if (kind == IdentityKind::TakeFx) {
      int itemCount = g_CountMediaItems(proj);
      for (int it = 0; it < itemCount; it++) {
        MediaItem* item = g_GetMediaItem(proj, it);
        if (!item) continue;
        int takeCount = g_GetMediaItemNumTakes(item);
        for (int tk = 0; tk < takeCount; tk++) {
          MediaItem_Take* take = g_GetMediaItemTake(item, tk);
          if (!take) continue;
          char tkGuid[kGuidStrLen] = {};
          if (!g_GetSetMediaItemTakeInfo_String(take, "GUID", tkGuid, false)) continue;
          if (strcmp(tkGuid, ownerGuid) != 0) continue;
          if (outOwnerFound) *outOwnerFound = true;  // owner take exists

          int n = g_TakeFX_GetCount(take);
          for (int i = 0; i < n; i++) {
            GUID* g = g_TakeFX_GetFXGUID(take, i);
            if (!g) continue;
            char gs[kGuidStrLen] = {};
            g_guidToString(g, gs);
            if (strcmp(gs, fxGuid) == 0) {
              out.kind = kind;
              out.take = take;
              out.fxIndex = i;
              return true;
            }
          }
        }
      }
    }
  }

  DBG("[MaxPane] FxCapture::ResolveLocation: MISS identity='%s'\n", identity);
  return false;
}

bool ShowAndGetHwnd(const char* identity, HWND& outHwnd)
{
  outHwnd = nullptr;
  ResolvedLocation loc{};
  if (!ResolveLocation(identity, loc)) return false;

  if (loc.kind == IdentityKind::TrackFx || loc.kind == IdentityKind::MasterFx) {
    g_TrackFX_Show(loc.track, loc.fxIndex, 3);  // 3 = show floating
    outHwnd = g_TrackFX_GetFloatingWindow(loc.track, loc.fxIndex);
  } else if (loc.kind == IdentityKind::TakeFx) {
    g_TakeFX_Show(loc.take, loc.fxIndex, 3);
    outHwnd = g_TakeFX_GetFloatingWindow(loc.take, loc.fxIndex);
  }

  DBG("[MaxPane] FxCapture::ShowAndGetHwnd: identity='%s' hwnd=%p\n",
      identity, (void*)outHwnd);
  return outHwnd != nullptr;
}

bool GetFloatingHwnd(const char* identity, HWND& outHwnd)
{
  outHwnd = nullptr;
  ResolvedLocation loc{};
  if (!ResolveLocation(identity, loc)) return false;
  if (loc.kind == IdentityKind::TrackFx || loc.kind == IdentityKind::MasterFx) {
    outHwnd = g_TrackFX_GetFloatingWindow(loc.track, loc.fxIndex);
  } else if (loc.kind == IdentityKind::TakeFx) {
    outHwnd = g_TakeFX_GetFloatingWindow(loc.take, loc.fxIndex);
  }
  // No per-miss DBG: this runs on the 500 ms CheckAlive tick for waiting
  // tabs (backoff-gated); a closed float is the NORMAL state, not an event.
  return outHwnd != nullptr;
}

bool Hide(const char* identity)
{
  ResolvedLocation loc{};
  if (!ResolveLocation(identity, loc)) return false;
  if (loc.kind == IdentityKind::TrackFx || loc.kind == IdentityKind::MasterFx) {
    g_TrackFX_Show(loc.track, loc.fxIndex, 2);  // 2 = hide floating
  } else if (loc.kind == IdentityKind::TakeFx) {
    g_TakeFX_Show(loc.take, loc.fxIndex, 2);
  }
  return true;
}

bool GetDisplayName(const char* identity, char* out, int outSize)
{
  if (!out || outSize <= 0) return false;
  out[0] = '\0';
  ResolvedLocation loc{};
  if (!ResolveLocation(identity, loc)) return false;
  bool ok = false;
  if (loc.kind == IdentityKind::TrackFx || loc.kind == IdentityKind::MasterFx) {
    ok = g_TrackFX_GetFXName(loc.track, loc.fxIndex, out, outSize);
  } else if (loc.kind == IdentityKind::TakeFx) {
    ok = g_TakeFX_GetFXName(loc.take, loc.fxIndex, out, outSize);
  }
  return ok && out[0] != '\0';
}

}  // namespace FxCapture
