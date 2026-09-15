#pragma once
// Baked sun visibility from the Spacer's static vertex light of a Gothic world
// mesh (zenkit::VertexFeature::light). The bake is ambient + sun * max(0, n.d)
// * visibility for a fixed sun direction d; fitting ambient/sun by least
// squares (with a coarse search of d) and dividing the residual out gives a
// per-vertex visibility that carries the compiled cast shadows (forest floors,
// under roofs and cliffs). It is only trusted on faces the baked sun lit well
// (n.d >= minNdl); other faces, light-mapped polygons and worlds without a
// usable fit return 255 (fully lit). Free at run time: the value travels in
// the landscape vertex colour alpha, the GBuffer hint bits 4-7 and multiplies
// the direct sun term (lighting/direct_light.frag).
#include <Tempest/Vec>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Vr {

struct BakedSample {
  float    nx=0, ny=0, nz=0;
  uint32_t light=0;   // 0xAARRGGBB
  bool     trusted=true; // false: light-mapped polygon, no vertex light
  };

struct BakedFit {
  Tempest::Vec3 dir     = {-0.45f, 0.87f, 0.21f}; // Khorinis (NEWWORLD.ZEN) audit
  float         ambient = 0;
  float         sun     = 0;
  float         rms     = 0;
  bool          valid   = false;
  };

inline float bakedLuminance(uint32_t argb) {
  const float r = float((argb>>16)&0xFF)/255.f, g = float((argb>>8)&0xFF)/255.f, b = float(argb&0xFF)/255.f;
  return 0.299f*r + 0.587f*g + 0.114f*b;
  }

// Least squares lum = ambient + sun*max(0, n.d) over every `stride`-th trusted sample.
inline BakedFit fitBakedSunDir(const std::vector<BakedSample>& s, const Tempest::Vec3& dirIn, size_t stride=1) {
  BakedFit f; f.dir = Tempest::Vec3::normalize(dirIn);
  double sx=0, sy=0, sxx=0, sxy=0, n=0;
  for(size_t i=0; i<s.size(); i+=std::max<size_t>(stride,1)) {
    if(!s[i].trusted) continue;
    const double x = std::max(0.f, s[i].nx*f.dir.x + s[i].ny*f.dir.y + s[i].nz*f.dir.z);
    const double y = bakedLuminance(s[i].light);
    sx+=x; sy+=y; sxx+=x*x; sxy+=x*y; n+=1;
    }
  const double den = n*sxx - sx*sx;
  if(n<64 || std::abs(den)<1e-9)
    return f;
  f.sun     = float((n*sxy - sx*sy)/den);
  f.ambient = float((sy - double(f.sun)*sx)/n);
  double err=0;
  for(size_t i=0; i<s.size(); i+=std::max<size_t>(stride,1)) {
    if(!s[i].trusted) continue;
    const double x = std::max(0.f, s[i].nx*f.dir.x + s[i].ny*f.dir.y + s[i].nz*f.dir.z);
    const double r = bakedLuminance(s[i].light) - (f.ambient + f.sun*x);
    err += r*r;
    }
  f.rms   = float(std::sqrt(err/n));
  f.valid = f.sun>0.05f && f.ambient>=-0.05f && f.ambient<0.9f;
  return f;
  }

inline uint8_t bakedVisibility(const BakedFit& f, const BakedSample& s, float minNdl);

// Coarse search of the baked sun direction (15 deg grid above the horizon)
// on a subsample, then the full fit for the best direction.
inline BakedFit fitBakedSun(const std::vector<BakedSample>& s) {
  BakedFit best; double bestErr = 1e300;
  for(int el=15; el<=90; el+=15)
    for(int az=0; az<360; az+=15) {
      const double e = el*3.14159265358979/180.0, a = az*3.14159265358979/180.0;
      const Tempest::Vec3 d(float(std::cos(e)*std::cos(a)), float(std::sin(e)), float(std::cos(e)*std::sin(a)));
      const BakedFit f = fitBakedSunDir(s, d, 16);
      if(!f.valid) continue;
      const double err = double(f.rms);
      if(err<bestErr) { bestErr=err; best=f; }
      if(el==90) break; // one sample of the zenith
      }
  if(!best.valid)
    return best;
  // Least squares over a lit/shadowed mixture underestimates the sun term;
  // refit on the upper envelope (samples the first fit calls at least 60 % lit).
  BakedFit f = fitBakedSunDir(s, best.dir, 1);
  for(int pass=0; pass<3 && f.valid; ++pass) {
    std::vector<BakedSample> lit; lit.reserve(s.size());
    for(const auto& smp:s)
      if(smp.trusted && bakedVisibility(f, smp, 0.f)>=153)
        lit.push_back(smp);
    const BakedFit g = fitBakedSunDir(lit, f.dir, 1);
    if(!g.valid) break;
    f = g;
    }
  return f;
  }

// 255 = lit / no information, 0 = fully shadowed by the bake.
inline uint8_t bakedVisibility(const BakedFit& f, const BakedSample& s, float minNdl) {
  if(!f.valid || !s.trusted)
    return 255;
  const float x = std::max(0.f, s.nx*f.dir.x + s.ny*f.dir.y + s.nz*f.dir.z);
  if(x<minNdl)
    return 255;
  const float pred = f.ambient + f.sun*x;
  const float vis  = (bakedLuminance(s.light) - f.ambient)/std::max(pred - f.ambient, 1e-3f);
  return uint8_t(std::lround(std::clamp(vis, 0.f, 1.f)*255.f));
  }

inline uint8_t bakedVisibility(const BakedFit& f, const BakedSample& s) { return bakedVisibility(f, s, 0.3f); }

inline std::vector<uint8_t> bakedVisibilityOf(const std::vector<BakedSample>& s, const BakedFit& f, float minNdl=0.3f) {
  std::vector<uint8_t> out(s.size(), 255);
  if(!f.valid)
    return out;
  for(size_t i=0; i<s.size(); ++i)
    out[i] = bakedVisibility(f, s[i], minNdl);
  return out;
  }

}
