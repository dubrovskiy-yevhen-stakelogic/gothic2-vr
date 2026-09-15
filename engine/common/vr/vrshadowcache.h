#pragma once
// Cached sun shadow for the Quest 3.
//
// The desktop cascades are head-centred and head-rotated, so a cached map
// would be invalid after every head turn. Here both shadow maps are
// world-aligned orthographic light projections (no rotation term), which
// makes a map valid until the head moves out of it or the sun moves:
//  - map 0 ("dynamic"): the animated casters (NPCs, morph meshes) around the
//    head, rebuilt every stereo frame; small caster count, cheap;
//  - map 1 ("static"): landscape and static/movable objects over a wider
//    area, built over `slices` stereo frames into a back buffer (one command
//    range per frame, visibility pass on the first frame only) and swapped in
//    at the frame after the last slice. The shading samples both maps and
//    keeps the darker result (lighting/shadow_sampling.glsl, VR branch).
// Depth convention: the existing shadow maps clear to 0 = unoccluded and test
// Greater, so z grows towards the sun: z = 0.5 + dot(sunDir, p - centre)/depth.
// The map centre is snapped to the map's texel grid in light space so a
// rebuilt map never swims against the previous one.
#include <Tempest/Matrix4x4>
#include <Tempest/Vec>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Vr {

// Light basis: z towards the sun, x/y across the map.
inline void shadowBasis(const Tempest::Vec3& sunDir, Tempest::Vec3& x, Tempest::Vec3& y, Tempest::Vec3& z) {
  z = Tempest::Vec3::normalize(sunDir);
  const auto up = std::abs(z.z)<0.999f ? Tempest::Vec3(0,0,1) : Tempest::Vec3(1,0,0);
  x = Tempest::Vec3::normalize(Tempest::Vec3::crossProduct(z, up));
  y = Tempest::Vec3::crossProduct(x, z);
  }

// Centre snapped to the texel grid of a map of `width` cm over `resolution` texels.
inline Tempest::Vec3 snapCenter(const Tempest::Vec3& center, const Tempest::Vec3& sunDir, float width, uint32_t resolution) {
  if(resolution==0 || width<=0.f)
    return center;
  Tempest::Vec3 x,y,z; shadowBasis(sunDir,x,y,z);
  const float texel = width/float(resolution);
  const float u = Tempest::Vec3::dotProduct(x,center), v = Tempest::Vec3::dotProduct(y,center);
  const float du = std::floor(u/texel)*texel - u, dv = std::floor(v/texel)*texel - v;
  return center + x*du + y*dv;
  }

// World-space light matrix: clip x/y in [-1,1] over `width` cm around the
// centre, clip z in [0,1] over `depth` cm centred on it (reverse: towards the sun).
inline Tempest::Matrix4x4 shadowMatrix(const Tempest::Vec3& center, const Tempest::Vec3& sunDir, float width, float depth) {
  Tempest::Vec3 x,y,z; shadowBasis(sunDir,x,y,z);
  const float sw = 2.f/std::max(width,1.f), sz = 1.f/std::max(depth,1.f);
  // Tempest's 16-value constructor takes the rows of the mathematical matrix
  // (clip = M * [p,1]): row i is the scaled basis vector i, its last entry
  // places the centre.
  Tempest::Matrix4x4 m(x.x*sw, x.y*sw, x.z*sw, -Tempest::Vec3::dotProduct(x,center)*sw,
                       y.x*sw, y.y*sw, y.z*sw, -Tempest::Vec3::dotProduct(y,center)*sw,
                       z.x*sz, z.y*sz, z.z*sz, -Tempest::Vec3::dotProduct(z,center)*sz + 0.5f,
                       0,      0,      0,      1);
  return m;
  }

// Angle between two directions in degrees; the atan2 form keeps the digits of
// tiny angles that acos(dot) loses near 1.
inline float angleDeg(const Tempest::Vec3& a, const Tempest::Vec3& b) {
  const float s = Tempest::Vec3::crossProduct(a,b).length(), c = Tempest::Vec3::dotProduct(a,b);
  return std::atan2(s,c)*57.2957795f;
  }

// Frame schedule of the static map: one slice of the draw commands per
// stereo frame; `begin` frames run the visibility pass and clear the back
// buffer, `end` frames complete the map (swap it in on the next frame).
//
// With `onDemand`, a finished map stays in use until a trigger (see wants()).
// Otherwise a new cycle starts as soon as the previous one ends.
struct ShadowSlicer {
  uint32_t slices = 8;

  bool     onDemand      = false;
  float    moveCm        = 800.f;  // snapped centre away from the build centre (map +-32 m)
  float    sunDeg        = 0.25f;  // sun turned since the build (~4 cm at a 10 m caster)
  uint64_t intervalMs    = 10000;  // safety rebuild: objects moved without any other trigger
  uint64_t movedCasterMs = 1000;   // a static caster moved inside the map: rebuild at most this often (0 = ignore)

  enum Reason : uint8_t { R_None=0, R_First, R_Commands, R_Cycle, R_Moved, R_Sun, R_Caster, R_Interval, R_Count };
  static const char* reasonName(Reason r) {
    static const char* names[R_Count] = {"none","first","commands","cycle","moved","sun","caster","interval"};
    return r<R_Count ? names[r] : "?";
    }

  struct Step {
    uint32_t slice  = 0;
    uint32_t slices = 1;
    bool     begin  = false;
    bool     end    = false;
    bool     build  = false;  // this frame culls and draws a slice (false: idle, the finished map stays)
    Reason   reason = R_None; // why the cycle began (begin frames)
    };
  struct Inputs {
    Tempest::Vec3 center;          // snapped static map centre
    Tempest::Vec3 sun;             // sun direction
    uint64_t      generation  = 0; // draw-command generation
    uint64_t      nowMs       = 0; // monotonic clock
    uint32_t      casterMoves = 0; // running count of static casters moved inside the map area
    };

  Tempest::Matrix4x4 build;         // light matrix of the map under construction
  bool               active = false;
  uint32_t           next   = 0;
  // The cycle in progress, or the last one started.
  bool          started = false;
  Tempest::Vec3 buildCenter, buildSun;
  uint64_t      buildGeneration = 0, buildMs = 0;
  uint32_t      buildCasterMoves = 0;

  void reset() { active=false; next=0; started=false; }

  // Why a new cycle should start now (R_None: keep the map).
  Reason wants(const Inputs& in) const {
    if(!started)                                  return R_First;
    if(in.generation!=buildGeneration)            return R_Commands; // cluster ranges and indirect buffers were rebuilt (0.0.37)
    if(!onDemand)                                 return R_Cycle;
    if((in.center-buildCenter).length()>moveCm)   return R_Moved;
    if(angleDeg(in.sun,buildSun)>sunDeg)          return R_Sun;
    if(movedCasterMs>0 && in.casterMoves!=buildCasterMoves && in.nowMs>=buildMs+movedCasterMs)
      return R_Caster;
    if(intervalMs>0 && in.nowMs>=buildMs+intervalMs)
      return R_Interval;
    return R_None;
    }

  Step step(const Tempest::Matrix4x4& current, const Inputs& in) {
    // A command-list change mid-cycle restarts it: no slice may miss clusters.
    if(active && in.generation!=buildGeneration) {
      active = false;
      next   = 0;
      }
    Reason why = R_None;
    if(!active) {
      why = wants(in);
      if(why==R_None)
        return Step{};
      build            = current;
      next             = 0;
      active           = true;
      started          = true;
      buildCenter      = in.center;
      buildSun         = in.sun;
      buildGeneration  = in.generation;
      buildMs          = in.nowMs;
      buildCasterMoves = in.casterMoves;
      }
    Step s = advance();
    s.reason = s.begin ? why : R_None;
    return s;
    }

  // 0.0.34 schedule without triggers: a new cycle whenever the last one ended.
  Step step(const Tempest::Matrix4x4& current) {
    const uint32_t n = std::max<uint32_t>(slices,1);
    if(!active || next>=n) {
      build  = current;
      next   = 0;
      active = true;
      }
    return advance();
    }

  // Command range [first,last) of a slice: contiguous, disjoint, covering [0,total).
  static void range(uint32_t slice, uint32_t slices, uint32_t total, uint32_t& first, uint32_t& last) {
    const uint64_t n = std::max<uint32_t>(slices,1);
    first = uint32_t(uint64_t(total)*std::min<uint64_t>(slice,n)/n);
    last  = uint32_t(uint64_t(total)*std::min<uint64_t>(slice+1,n)/n);
    }

  // Balancing weight of one cluster: every cluster in a range costs one
  // visibility-pass thread; a caster inside the map adds its meshlets' draw,
  // ~500x a cull thread on the Quest (047: a 120k-cluster cull 0.08 ms, a
  // 12-slice map ~1.7 ms over a few thousand caster meshlets).
  static uint32_t clusterWeight(bool caster, uint32_t meshlets) {
    return 1u + (caster ? 512u*std::min<uint32_t>(meshlets, 1u<<20) : 0u);
    }

  // Does a cluster sphere reach the map's square footprint (light-space x/y of
  // the build matrix)? Depth is not tested: the map spans 300 m along the sun.
  static bool touchesMap(const Tempest::Matrix4x4& build, const Tempest::Vec3& pos, float r, float width) {
    Tempest::Vec3 q = pos;
    build.project(q);
    const float lim = 1.f + 2.f*std::max(r,0.f)/std::max(width,1.f);
    return std::abs(q.x)<=lim && std::abs(q.y)<=lim;
    }

  // Balanced cluster ranges: slice i is [bounds[i],bounds[i+1])
  // with bounds[0]=0 and bounds[slices]=count, so the slices still cover every
  // cluster; each inner bound sits at the prefix sum nearest to i/slices of the
  // total weight, so a slice differs from the mean by at most the largest
  // single weight. The raw index split left the objects in 1-2 of 12 slices
  // behind ~100k skipped landscape clusters (047: p50 0.016 ms, p90 0.54).
  static void balancedBounds(const uint32_t* weight, uint32_t count, uint32_t slices, uint32_t* bounds) {
    const uint32_t n = std::max<uint32_t>(slices,1);
    uint64_t total = 0;
    for(uint32_t i=0; i<count; ++i)
      total += weight[i];
    bounds[0] = 0;
    bounds[n] = count;
    uint64_t acc = 0;
    uint32_t at  = 0;
    for(uint32_t s=1; s<n; ++s) {
      const uint64_t target = (total*s + n/2)/n;
      while(at<count && acc+weight[at]<=target) {
        acc += weight[at];
        ++at;
        }
      if(at<count && acc+weight[at]-target < target-acc) {
        acc += weight[at];
        ++at;
        }
      bounds[s] = at;
      }
    }

  private:
    Step advance() {
      const uint32_t n = std::max<uint32_t>(slices,1);
      Step s;
      s.slice  = next;
      s.slices = n;
      s.begin  = next==0;
      s.end    = next+1>=n;
      s.build  = true;
      ++next;
      if(s.end)
        active = false;
      return s;
      }
  };

}
