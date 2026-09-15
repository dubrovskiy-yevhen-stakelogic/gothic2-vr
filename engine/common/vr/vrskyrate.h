#pragma once
// Reduced-rate sky LUTs for Quest. Sun, camera height, night, weather and
// cloud inputs change slowly. Schedule, first eye only:
//  - viewLut (128x64 atmosphere raymarch): on a frame of the odd phase when an
//    input moved beyond its epsilon since the LUT was drawn, else every
//    `viewEvery` frames. The LUT is therefore never more than the epsilons
//    (plus one frame of motion) behind its inputs;
//  - viewCldLut (512x256 clouds; water reflection, irradiance and exposure
//    read it): every even phase, i.e. at most one frame old (clouds scroll
//    ~0.05 texel per frame);
//  - irradiance: every odd phase, so each frame carries about half the work;
//  - everything on the same frame on the first frame, after a world load or a
//    sky reset (`force`), on a time skip (sun jump) and a teleport (camera
//    height jump), and on a weather switch (sun intensity jump).
// The second eye never draws the LUTs (unchanged); exposure is shared from the
// first eye by the renderer.
#include <Tempest/Vec>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Vr {

struct SkyRate {
  struct Inputs {
    Tempest::Vec3 sunDir;
    float         height       = 0; // m above the world's lowest point: the LUT's plPosY (SceneGlobals::setWorld)
    float         sunIntensity = 0; // GSunIntensity
    float         night        = 0; // Sky::isNight
    };
  struct Plan {
    bool viewLut    = true;
    bool cloudLut   = true;
    bool irradiance = true;
    bool full       = true;
    };

  // Epsilons from the fog-tone fixture (tests/fog-tone.cpp SkyRateFixture B):
  // 1.25x these keep the view LUT and the cloud LUT drawn from it within the
  // fog-fold tolerance at every sun position and height; 0.0005 in sun.y and a
  // fixed 1 m / 25 cm of height did not (sunset cloud LUT mean 0.65 LSB; 2 m
  // above the world's lowest point max 19 / 12 LSB, because the rays just below
  // the horizon hit the ground at a distance proportional to the height). The
  // game sun moves ~5e-5 in y per 8 frames (game time x14.5), so the periodic
  // refresh normally comes first; the change test catches fast changes.
  uint32_t viewEvery  = 8;
  float    sunEps     = 0.00008f; // sunDir.y (the LUT depends on the sun altitude only), ~0.005 deg
  float    heightEps  = 0.01f;    // m, plus heightRel of the height
  float    heightRel  = 0.02f;
  float    relEps     = 0.001f;   // relative GSunIntensity
  float    nightEps   = 0.001f;
  float    jumpSunDeg = 1.f;      // per frame: a time skip
  float    jumpHeight = 5.f;      // m per frame: a teleport
  float    jumpRel    = 0.05f;    // per frame: a weather switch

  float heightTol(float h) const { return heightEps + heightRel*std::max(h,0.f); }

  bool     valid   = false;
  uint32_t phase   = 0; // frames since the last full refresh
  uint32_t viewAge = 0; // frames since the last viewLut
  Inputs   lastView, lastFrame;

  void reset() { valid=false; }

  static float angleDeg(const Tempest::Vec3& a, const Tempest::Vec3& b) {
    const float s = Tempest::Vec3::crossProduct(a,b).length(), c = Tempest::Vec3::dotProduct(a,b);
    return std::atan2(s,c)*57.2957795f;
    }
  static bool relChanged(float a, float b, float eps) {
    return std::abs(a-b) > eps*std::max(std::abs(b),1e-6f);
    }

  bool viewChanged(const Inputs& in) const {
    return std::abs(in.sunDir.y-lastView.sunDir.y)>sunEps || std::abs(in.height-lastView.height)>heightTol(lastView.height) ||
           relChanged(in.sunIntensity,lastView.sunIntensity,relEps) || std::abs(in.night-lastView.night)>nightEps ||
           ((in.night==0.f)!=(lastView.night==0.f));
    }
  bool jumped(const Inputs& in) const {
    return angleDeg(in.sunDir,lastFrame.sunDir)>jumpSunDeg || std::abs(in.height-lastFrame.height)>jumpHeight ||
           relChanged(in.sunIntensity,lastFrame.sunIntensity,jumpRel);
    }

  Plan plan(const Inputs& in, bool force) {
    const bool jump = valid && jumped(in);
    lastFrame = in;
    if(force || !valid || jump) {
      valid    = true;
      phase    = 0;
      viewAge  = 0;
      lastView = in;
      return Plan{};
      }
    ++phase;
    ++viewAge;
    Plan p;
    p.full       = false;
    const bool odd = (phase&1u)!=0;
    p.cloudLut   = !odd;
    p.irradiance = odd;
    p.viewLut    = odd && (viewAge>=viewEvery || viewChanged(in));
    if(p.viewLut) {
      viewAge  = 0;
      lastView = in;
      }
    return p;
    }
  };

}
