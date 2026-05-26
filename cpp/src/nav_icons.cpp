// nav_icons.cpp — cross-platform nav-bar icon rendering via embedded
// Phosphor alpha masks. See nav_icons.h for the per-platform rationale.
//
// For each DrawIcon call:
//  1. Look up the alpha mask + source dimensions from nav_icons.gen.cpp.
//  2. Compose a tightly-packed BGRA pixel buffer where each pixel is the
//     button background lerped toward `iconColor` by the mask alpha. The
//     result is opaque — alpha was consumed at compose time.
//  3. Blit into `hdc` via the natural per-platform path:
//       Linux SWELL    — StretchBltFromMem (takes raw bytes directly).
//       Win32          — StretchDIBits     (takes raw bytes directly).
//       macOS Cocoa    — BlitBGRABitmapMacOS in swell_cocoa_helpers.mm,
//                        which wraps the buffer in a CGImage and draws
//                        through the HDC's CGContextRef.
//
// Each path uses native API; the API-level entry point (NavIcons::DrawIcon)
// is identical across platforms. Source RGB from the SVG rasterization is
// ignored — only alpha matters.

#include "nav_icons.h"

#include <cstdint>
#include <cstddef>
#include <vector>

#ifdef __APPLE__
#include "swell_cocoa_helpers.h"  // BlitBGRABitmapMacOS
#endif

// Pixel layout: B=bits[0..7], G=bits[8..15], R=bits[16..23], A=bits[24..31].
// On little-endian machines that's byte order B, G, R, A in memory — matches
// what every platform's blit path expects (Linux LICE_pixel, Win32 DIB top-
// down BI_RGB, macOS kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little).
static inline uint32_t PackBGRA(int r, int g, int b, int a)
{
  return (uint32_t)((b & 0xff)
                  | ((g & 0xff) << 8)
                  | ((r & 0xff) << 16)
                  | ((a & 0xff) << 24));
}

namespace NavIcons {

void DrawIcon(HDC hdc, int buttonId, const RECT& r,
              COLORREF bgColor, COLORREF iconColor)
{
  const int dstW = r.right - r.left;
  const int dstH = r.bottom - r.top;
  if (dstW <= 0 || dstH <= 0) return;

  // 1x source = 26 px; pick 2x once the target rect exceeds halfway to 52.
  const int scale2x = (dstW > 38 || dstH > 38) ? 1 : 0;
  int sw = 0, sh = 0;
  const unsigned char* alpha = GetIconAlpha(buttonId, scale2x, &sw, &sh);
  if (!alpha || sw <= 0 || sh <= 0) return;

  const int br = GetRValue(bgColor);
  const int bg = GetGValue(bgColor);
  const int bb = GetBValue(bgColor);
  const int ir = GetRValue(iconColor);
  const int ig = GetGValue(iconColor);
  const int ib = GetBValue(iconColor);

  // Compose at source resolution; the blit path handles the resize.
  // thread_local keeps the buffer alive across calls without contention.
  thread_local std::vector<uint32_t> buf;
  buf.resize((std::size_t)sw * (std::size_t)sh);

  for (int y = 0; y < sh; y++) {
    const unsigned char* arow = alpha + (std::size_t)y * sw;
    uint32_t* drow = buf.data() + (std::size_t)y * sw;
    for (int x = 0; x < sw; x++) {
      const unsigned int a  = arow[x];
      const unsigned int ia = 255u - a;
      // Linear blend, +127 rounds-to-nearest before /255.
      const int rr = (int)(((unsigned int)ir * a + (unsigned int)br * ia + 127u) / 255u);
      const int gg = (int)(((unsigned int)ig * a + (unsigned int)bg * ia + 127u) / 255u);
      const int bbp = (int)(((unsigned int)ib * a + (unsigned int)bb * ia + 127u) / 255u);
      drow[x] = PackBGRA(rr, gg, bbp, 255);
    }
  }

#if defined(__linux__) || defined(__FreeBSD__)
  // srcspan is in 32-bit-pixel units, not bytes.
  StretchBltFromMem(hdc, r.left, r.top, dstW, dstH,
                    buf.data(), sw, sh, sw);
#elif defined(_WIN32)
  // HALFTONE + SetBrushOrgEx — without these Win32 defaults to COLORONCOLOR
  // (per-pixel discard) when StretchDIBits downscales the 26 px source to
  // the 20 px icon rect, producing the aliased "pixelated" look the owner
  // observed on v2.0.2. HALFTONE is bilinear-equivalent; the SetBrushOrgEx
  // call is required after a HALFTONE switch per MSDN to keep brush
  // alignment stable across blits.
  const int prevMode = SetStretchBltMode(hdc, HALFTONE);
  SetBrushOrgEx(hdc, 0, 0, NULL);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = sw;
  bi.bmiHeader.biHeight = -sh;     // negative → top-down DIB layout
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  StretchDIBits(hdc, r.left, r.top, dstW, dstH, 0, 0, sw, sh,
                buf.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
  SetStretchBltMode(hdc, prevMode);
#elif defined(__APPLE__)
  BlitBGRABitmapMacOS(hdc, buf.data(), sw, sh, r.left, r.top, dstW, dstH);
#else
#  error "nav_icons.cpp: unsupported platform — add a blit path here."
#endif
}

}  // namespace NavIcons
