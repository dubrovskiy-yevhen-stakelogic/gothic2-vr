#pragma once
// CPU copy of the projected-coverage estimate that lighting/light_visibility.comp
// uses to route local lights between the light-slab list and the volume list,
// plus the per-eye hysteresis gate that decides whether the slab route is worth
// its fixed per-frame cost. Header-only so the host regression
// (tests/light-slab.cpp) checks the GPU classification against the same code
// the renderer runs.
#include <Tempest/Matrix4x4>
#include <Tempest/Vec>

#include <algorithm>
#include <cmath>

namespace LightCoverage {

// Fraction of the screen inside the light's projected sphere bounds, clamped
// to [0,1]. `uncertain` is set when the sphere crosses the near plane or the
// input is not finite; those lights count as full coverage (1). `depthMin`
// receives the sphere's nearest clip depth when requested (0 when uncertain).
inline float projected(const Tempest::Matrix4x4& view, const Tempest::Matrix4x4& project,
                       const Tempest::Vec3& pos, float range, float znear,
                       bool& uncertain, float* depthMin = nullptr) {
  uncertain = false;
  if(depthMin) *depthMin = 0;
  if(!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) || !std::isfinite(range) || range<=0) {
    uncertain = true;
    return 1;
    }
  Tempest::Vec4 c4(pos.x, pos.y, pos.z, 1);
  view.project(c4);
  const Tempest::Vec3 c(c4.x, c4.y, c4.z);
  const float R = range;
  if(c.z-R<znear) {
    uncertain = true;
    return 1;
    }
  if(depthMin) *depthMin = project.at(3,2)/(c.z-R)+project.at(2,2);
  const float P00 = project.at(0,0), P11 = project.at(1,1);
  const Tempest::Vec3 cr = c*R;
  const float czr2 = c.z*c.z-R*R;
  const float vx = std::sqrt(c.x*c.x+czr2), minx = (vx*c.x-cr.z)/(vx*c.z+cr.x), maxx = (vx*c.x+cr.z)/(vx*c.z-cr.x);
  const float vy = std::sqrt(c.y*c.y+czr2), miny = (vy*c.y-cr.z)/(vy*c.z+cr.y), maxy = (vy*c.y+cr.z)/(vy*c.z-cr.y);
  const float ox = project.at(2,0), oy = project.at(2,1);
  const float lox = minx*P00+ox, loy = miny*P11+oy, hix = maxx*P00+ox, hiy = maxy*P11+oy;
  float x0 = std::min(lox,hix)*.5f+.5f, y0 = std::min(loy,hiy)*.5f+.5f;
  float x1 = std::max(lox,hix)*.5f+.5f, y1 = std::max(loy,hiy)*.5f+.5f;
  if(!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) {
    uncertain = true;
    return 1;
    }
  x0 = std::min(std::max(x0,0.f),1.f); y0 = std::min(std::max(y0,0.f),1.f);
  x1 = std::min(std::max(x1,0.f),1.f); y1 = std::min(std::max(y1,0.f),1.f);
  return std::max(x1-x0,0.f)*std::max(y1-y0,0.f);
  }

// Prior of the slab route: the sum of squared coverage over the lights at or
// above the per-light threshold. Four screen-sized overlapping lights (weight
// 4) are where the slab route started to win on Adreno, while any number of
// medium lights stays below 1 (PERFORMANCE.md, 0.0.20). Lights the camera is
// inside also weigh 1 each although they light only the nearby surfaces, so
// this weight only seeds and hints the measured-cost controller
// (vr/lightroutecontroller.h); it never decides the route by itself.
inline float gateWeight(float coverage) {
  return coverage*coverage;
  }

}
