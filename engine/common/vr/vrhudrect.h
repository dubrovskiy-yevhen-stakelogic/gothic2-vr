#pragma once
// VR: the HUD composition layer covers only the rectangle the UI actually
// painted. The compositor (TimeWarp, ~1.4 ms of GPU per frame on the Quest 3
// with a full-view HUD quad, VrApi "TW=") shades every display pixel a layer
// covers, so a quad that wraps the status bars instead of the whole view is
// cheaper, and an empty HUD needs no layer at all. Pure integer/float math,
// no OpenXR types, so tests/vrhudrect.cpp can check it on the host.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Vr {

struct HudRect {
  int x=0, y=0, w=0, h=0; // image pixels, y down
  bool empty() const { return w<=0 || h<=0; }
  };

// Adds the NDC bounds of a painted VectorImage (Tempest: x = px*2/W-1,
// y = py*2/H-1, y down) shifted by whole pixels, padded, clamped to the image.
inline void hudRectAdd(HudRect& acc, float nx0, float ny0, float nx1, float ny1,
                       int shiftX, int shiftY, int imgW, int imgH, int pad=8) {
  if(!(nx1>=nx0) || !(ny1>=ny0) || imgW<=0 || imgH<=0) return; // empty or NaN
  const float eps = 1e-3f; // NDC round trips of whole pixels
  int x0 = int(std::floor((nx0+1.f)*0.5f*float(imgW)+eps)) + shiftX - pad;
  int y0 = int(std::floor((ny0+1.f)*0.5f*float(imgH)+eps)) + shiftY - pad;
  int x1 = int(std::ceil ((nx1+1.f)*0.5f*float(imgW)-eps)) + shiftX + pad;
  int y1 = int(std::ceil ((ny1+1.f)*0.5f*float(imgH)-eps)) + shiftY + pad;
  x0 = std::clamp(x0, 0, imgW); x1 = std::clamp(x1, 0, imgW);
  y0 = std::clamp(y0, 0, imgH); y1 = std::clamp(y1, 0, imgH);
  if(x1<=x0 || y1<=y0) return;
  if(acc.empty()) { acc = {x0, y0, x1-x0, y1-y0}; return; }
  const int ax1 = std::max(acc.x+acc.w, x1), ay1 = std::max(acc.y+acc.h, y1);
  acc.x = std::min(acc.x, x0); acc.y = std::min(acc.y, y0);
  acc.w = ax1-acc.x; acc.h = ay1-acc.y;
  }

// The HUD image region that is cleared, drawn and copied to the XR HUD image
//: the compositor samples only the layer's rectangle,
// so nothing outside it needs a clear, a store or a copy. Empty or off-image
// = the whole image (the old full clear and copy).
inline HudRect hudCopyRegion(const HudRect& r, uint32_t imgW, uint32_t imgH) {
  const int W = int(imgW), H = int(imgH);
  const int x0 = std::clamp(r.x, 0, W),     y0 = std::clamp(r.y, 0, H);
  const int x1 = std::clamp(r.x+r.w, 0, W), y1 = std::clamp(r.y+r.h, 0, H);
  if(r.empty() || x1<=x0 || y1<=y0) return {0, 0, W, H};
  return {x0, y0, x1-x0, y1-y0};
  }

// The world eye image region: at render scale < 1 the
// renderer tonemaps the internal size from the image origin, and that
// rectangle is the projection view's imageRect and the eye copy region.
// Clamped to the image; empty = the whole image (render scale 1, menu frames).
inline HudRect eyeImageRegion(int w, int h, uint32_t imgW, uint32_t imgH) {
  const int W = int(imgW), H = int(imgH);
  if(w<=0 || h<=0) return {0, 0, W, H};
  return {0, 0, std::min(w, W), std::min(h, H)};
  }

// The quad in the view space: a full-image HUD is 1.1*distance metres wide
// at `distance` (as before); a sub-rectangle keeps the same pixel scale and
// sits where its pixels were (image y down -> view y up).
struct HudQuad {
  float sizeX=0, sizeY=0; // metres
  float posX=0,  posY=0;  // metres, relative to the view centre
  };

inline HudQuad hudQuad(const HudRect& r, uint32_t imgW, uint32_t imgH, float distance) {
  HudQuad q;
  const float W = float(std::max(1u,imgW)), H = float(std::max(1u,imgH));
  const float fullX = 1.1f*distance, fullY = 1.1f*distance*H/W;
  if(r.empty()) { q.sizeX = fullX; q.sizeY = fullY; return q; }
  q.sizeX = fullX*float(r.w)/W;
  q.sizeY = fullY*float(r.h)/H;
  q.posX  =  (float(r.x)+float(r.w)*0.5f - W*0.5f)/W*fullX;
  q.posY  = -(float(r.y)+float(r.h)*0.5f - H*0.5f)/H*fullY;
  return q;
  }

}
