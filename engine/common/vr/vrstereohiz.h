#pragma once
// VR: the second eye's HiZ window over the first eye's tiles (hiz/hiz_stereo.comp).
//
// Model: the two eyes share one rotation and differ by a translation dv in the
// first eye's view space (dv.z ~ 0 on a headset). A point at first-eye view
// depth z and image pixel u_l appears in the second eye at pixel
// u_r = u_l + s(z), with s(z) = c + k/z exactly: c is the principal-point
// difference of the two off-axis projections, k the parallax factor of dv.x.
// A second-eye tile T of width t pixels can receive that point from the first
// eye tile T + i only when s(z) lies in the open interval (-t(i+1), t(1-i))
// (pixel rounding included, see tests/vrstereohiz.cpp). Because s is monotone
// in z, every window tile i has a largest depth zHi(i) a point can have while
// mapping through it. The seed of T is therefore
//   max over i of min(hiZLeft[T+i], depth(zHi(i)))
// which is conservative (never nearer than any point the second eye sees that
// the first eye also saw) and much tighter than the plain maximum: only the
// tiles of the far end of the window contribute their whole value, the tiles
// of the near parallax are capped at the depth of that parallax.
//
// Without a common rotation, or when the shift is not of the c + k/z form,
// the window falls back to the plain maximum with one tile of margin on each
// side (exact=false, no caps).
#include <Tempest/Matrix4x4>
#include <Tempest/Vec>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Vr {

struct StereoHiZWindow {
  static constexpr int MaxTiles = 16;
  int   shiftTiles  = 0;     // first source tile relative to the destination tile
  int   windowTiles = 0;     // tiles after the first
  int   marginY     = 0;     // rows above and below
  bool  exact       = false; // per-tile depth caps are valid
  float nearCm      = 0;     // nearest depth the window covers
  // Largest depth-buffer value (before packHiZ) of a point that reaches the
  // destination tile through window tile i; >= 1 means no cap.
  float depthClamp[MaxTiles] = {};
  };

// common.glsl packHiZ and the value hiz_pot stores for a depth-buffer value.
inline float packHiZ(float z) { return (z-0.95f)*20.f; }
inline float hiZStored(float depth) { return std::clamp(packHiZ(depth)+1.f/32768.f, 0.f, 1.f); }

inline float ndcX(const Tempest::Matrix4x4& proj, float x, float y, float z) {
  float px=x, py=y, pz=z, pw=1;
  proj.project(px,py,pz,pw);
  return pw!=0.f ? px/pw : px;
  }

inline float ndcY(const Tempest::Matrix4x4& proj, float x, float y, float z) {
  float px=x, py=y, pz=z, pw=1;
  proj.project(px,py,pz,pw);
  return pw!=0.f ? py/pw : py;
  }

// depth-buffer value of a point at view depth z
inline float depthAt(const Tempest::Matrix4x4& proj, float z) {
  float px=0, py=0, pz=z, pw=1;
  proj.project(px,py,pz,pw);
  return pw!=0.f ? pz/pw : pz;
  }

namespace detail {
// s(z) = c + k/z fitted from two samples, checked on a third
struct Shift { float c=0, k=0; bool affine=false; };
template<class F> Shift fitShift(F&& shiftAt) {
  const float z1=100.f, z2=200.f, z3=1700.f;
  const float s1=shiftAt(z1), s2=shiftAt(z2), s3=shiftAt(z3);
  Shift s;
  s.k = (s1-s2)/(1.f/z1-1.f/z2);
  s.c = s1 - s.k/z1;
  s.affine = std::abs(s.c + s.k/z3 - s3) < 1e-3f;
  return s;
  }
// largest depth >= nearCm whose shift lies in the open interval (lo,hi); 0 when
// no depth does, +inf when the far limit c itself lies inside. Boundaries are
// exact: a shift equal to a tile edge maps to one tile only (see the pixel
// model in tests/vrstereohiz.cpp), and a far limit that rounds onto an edge
// only adds a neighbouring tile.
inline float tileFarDepth(const Shift& s, float lo, float hi, float nearCm) {
  const float inf = std::numeric_limits<float>::infinity();
  float zHi = 0.f;
  if(s.k==0.f) zHi = (lo<s.c && s.c<hi) ? inf : 0.f;
  else if(s.k>0.f) {            // s > c, falls toward c with depth
    if(hi<=s.c) return 0.f;
    zHi = lo>s.c ? s.k/(lo-s.c) : inf;
    }
  else {                        // s < c, rises toward c with depth
    if(lo>=s.c) return 0.f;
    zHi = hi<s.c ? s.k/(hi-s.c) : inf;
    }
  if(!std::isinf(zHi)) zHi *= 1.001f; // boundary safety
  return zHi>=nearCm ? zHi : 0.f;
  }
}

// projL/projR: the two projections; viewL/viewR: the view matrices (viewR may
// be null: rotation assumed shared); originL/originR: eye positions in the
// same (LWC) space; widthPx/heightPx: image size; tilePx: HiZ tile size.
inline StereoHiZWindow stereoHiZWindow(const Tempest::Matrix4x4& projL, const Tempest::Matrix4x4& projR,
                                       const Tempest::Matrix4x4& viewL, const Tempest::Matrix4x4* viewR,
                                       const Tempest::Vec3& originL, const Tempest::Vec3& originR,
                                       float widthPx, float heightPx, float tilePx, float nearCm=50.f) {
  const float t = std::max(tilePx,1.f);
  Tempest::Vec3 a = originL, b = originR;
  viewL.project(a);
  viewL.project(b);
  const Tempest::Vec3 dv = b - a;
  bool parallel = std::abs(dv.z) < 0.1f;
  if(viewR!=nullptr)
    for(int r=0; r<3 && parallel; ++r)
      for(int c=0; c<3; ++c)
        if(std::abs(viewL.at(r,c)-viewR->at(r,c))>1e-4f) parallel = false;
  auto shiftX = [&](float z) { return (ndcX(projR,-dv.x,0.f,z) - ndcX(projL,0.f,0.f,z))*widthPx*0.5f; };
  auto shiftY = [&](float z) { return (ndcY(projR,0.f,-dv.y,z) - ndcY(projL,0.f,0.f,z))*heightPx*0.5f; };
  const auto sx = detail::fitShift(shiftX);
  const auto sy = detail::fitShift(shiftY);
  // Off-axis points: with different focal lengths the shift also depends on
  // the image position (a few pixels at most). Bound it over the image corners
  // and a range of depths, and widen every tile interval by that much.
  float ex = 0.f, ey = 0.f;
  {
    const float z0 = 1000.f;
    const float cxL = ndcX(projL,0,0,z0), fxL = (ndcX(projL,100,0,z0)-cxL)*z0/100.f;
    const float cyL = ndcY(projL,0,0,z0), fyL = (ndcY(projL,0,100,z0)-cyL)*z0/100.f;
    for(float z : {nearCm, 2.f*nearCm, 5.f*nearCm, 20.f*nearCm, 1e5f})
      for(float nx : {-1.f, 1.f})
        for(float ny : {-1.f, 1.f}) {
          const float px = (nx-cxL)/fxL*z, py = (ny-cyL)/fyL*z;
          const float ax = (ndcX(projR,px-dv.x,py-dv.y,z) - ndcX(projL,px,py,z))*widthPx*0.5f;
          const float ay = (ndcY(projR,px-dv.x,py-dv.y,z) - ndcY(projL,px,py,z))*heightPx*0.5f;
          ex = std::max(ex, std::abs(ax-(sx.c+sx.k/z)));
          ey = std::max(ey, std::abs(ay-(sy.c+sy.k/z)));
          }
    if(!(ex<t) || !(ey<t)) parallel = false; // not a shift at all: plain maximum with margins
    }

  StereoHiZWindow w;
  w.nearCm = nearCm;
  for(int attempt=0; attempt<32; ++attempt) {
    w.exact = parallel && sx.affine && sy.affine;
    int first=std::numeric_limits<int>::max(), last=std::numeric_limits<int>::min();
    float caps[2*StereoHiZWindow::MaxTiles+1] = {};
    if(w.exact) {
      for(int i=-StereoHiZWindow::MaxTiles; i<=StereoHiZWindow::MaxTiles; ++i) {
        const float zHi = detail::tileFarDepth(sx, -t*float(i+1)-ex, t*float(1-i)+ex, w.nearCm);
        if(zHi<=0.f) continue;
        first = std::min(first,i); last = std::max(last,i);
        caps[i+StereoHiZWindow::MaxTiles] = std::isinf(zHi) ? 2.f : std::max(depthAt(projL,zHi), depthAt(projR,zHi));
        }
      // rows: sign of the vertical pixel axis is not assumed, both directions count
      int rows = 0;
      for(int j=-StereoHiZWindow::MaxTiles; j<=StereoHiZWindow::MaxTiles; ++j) {
        const bool up   = detail::tileFarDepth(sy, -t*float(j+1)-ey, t*float(1-j)+ey, w.nearCm)>0.f;
        const bool down = detail::tileFarDepth(detail::Shift{-sy.c,-sy.k,true}, -t*float(j+1)-ey, t*float(1-j)+ey, w.nearCm)>0.f;
        if(up || down) rows = std::max(rows, std::abs(j));
        }
      w.marginY = rows;
      }
    if(!w.exact || first>last) {
      // plain maximum over the shift range with a tile of margin on each side
      w.exact = false;
      const float sFar=shiftX(1e6f), sNear=shiftX(w.nearCm);
      const float sMin=std::min(sFar,sNear), sMax=std::max(sFar,sNear);
      first = int(std::floor(-(sMax+ex)/t)) - 1;
      last  = int(std::ceil (-(sMin-ex)/t)) + 1;
      const float yFar=shiftY(1e6f), yNear=shiftY(w.nearCm);
      w.marginY = int(std::ceil((std::max(std::abs(yFar),std::abs(yNear))+ey)/t)) + 1;
      for(float& c:caps) c = 2.f;
      }
    if(last-first < StereoHiZWindow::MaxTiles) {
      w.shiftTiles  = first;
      w.windowTiles = last-first;
      for(int i=0; i<=w.windowTiles; ++i) {
        const int idx = first+i+StereoHiZWindow::MaxTiles;
        w.depthClamp[i] = (idx>=0 && idx<int(sizeof(caps)/sizeof(caps[0]))) ? caps[idx] : 2.f;
        }
      return w;
      }
    w.nearCm *= 1.5f; // too wide: cover less of the near parallax
    }
  w.exact = false; w.shiftTiles = -1; w.windowTiles = 2; w.marginY = 1;
  for(float& c:w.depthClamp) c = 2.f;
  return w;
  }

// hiz_stereo.comp on the host: tiles = the first eye's stored HiZ (hiz_pot
// output, w x h, row-major), result for destination tile (x,y).
inline float stereoHiZSeed(const float* tiles, int w, int h, int x, int y, const StereoHiZWindow& win) {
  float z = 0.f;
  for(int dy=-win.marginY; dy<=win.marginY; ++dy)
    for(int i=0; i<=win.windowTiles && i<StereoHiZWindow::MaxTiles; ++i) {
      const float cap = hiZStored(win.depthClamp[i]);
      const int sx = x+win.shiftTiles+i, sy = y+dy;
      const float v = (sx<0 || sy<0 || sx>=w || sy>=h) ? 1.f : tiles[sy*w+sx];
      z = std::max(z, std::min(v,cap));
      }
  return z;
  }

}
