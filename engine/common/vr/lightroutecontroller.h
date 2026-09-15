#pragma once
// Per-eye local-light route controller for the Quest renderer: chooses between
// the light-slab route (masked fullscreen pass for the large lights) and the
// plain volume route from MEASURED GPU cost instead of a cost model.
//
// Every frame the current route is used; every `probeInterval` frames one
// frame is recorded with the other route (a probe). When the GPU time of the
// local-light passes of a frame becomes known (one frame later in the
// pipelined VR frame loop) it is fed back: the current route's cost is
// smoothed, a probe's cost is compared against that smoothed cost and the
// route switches when the probe was cheaper by a margin. A camera-side prior
// (the slab gate weight of graphics/lightcoverage.h) only seeds the initial
// route and requests an early probe when it disagrees with the current route,
// so a walk from outdoors to a torch-lit wall reacts within a few frames
// instead of waiting for the next scheduled probe; a prior that keeps
// disagreeing with the measurement backs off to the scheduled cadence.
//
// A wrong switch costs at most one probe interval of the worse route and a
// route change alters at most the last R11G11B10 rounding step of a pixel.
// Header-only, no engine dependencies: tests/light-route.cpp exercises it with
// synthetic cost sequences.
#include <cstdint>
#include <cmath>
#include <algorithm>

class LightRouteController final {
  public:
    enum Route : uint8_t { Volumes = 0, Slabs = 1 };

    struct Params {
      uint32_t probeInterval   = 90;   // frames between scheduled probes per eye
      uint32_t priorInterval   = 15;   // frames before a disagreeing prior may probe (doubles on failure)
      float    emaAlpha        = 0.1f; // smoothing of the reported cost estimate
      float    recentAlpha     = 0.3f; // faster smoothing used as the reference a probe is compared against
      float    marginMs        = 0.15f;// probe must beat the current cost by this ...
      float    marginRel       = 0.05f;// ... and by this fraction ...
      float    marginNoise     = 2.f;  // ... and by this multiple of the observed frame-to-frame deviation
      float    priorOpenAt     = 4.f;  // gate weight that suggests the slab route
      float    priorCloseAt    = 3.f;  // gate weight below which volumes are suggested
      uint32_t staleFrames     = 64;   // a probe older than this is abandoned
      };

    LightRouteController() = default;
    explicit LightRouteController(const Params& p) : params(p) {}

    // Route for eye `eye` in frame `frameId`. `priorWeight` is the camera-side
    // slab gate weight of this eye (NaN when unknown). Must be called once per
    // eye per recorded frame, with increasing frame ids.
    Route decide(uint8_t eye, uint64_t frameId, float priorWeight) {
      auto& e = eyes[eye & 1];
      e.lastProbe = false;
      if(!e.seeded) {
        e.seeded       = true;
        e.route        = priorSuggests(priorWeight, Volumes);
        e.priorBackoff = params.priorInterval;
        }
      ++e.framesSinceProbe;
      ++e.framesSincePrior;
      if(e.probePending && frameId>e.probeFrame+params.staleFrames)
        e.probePending = false; // the report never came (dropped frame); try again later
      const Route suggested = priorSuggests(priorWeight, e.route);
      if(suggested==e.route) {
        e.priorBackoff = params.priorInterval; // the prior agrees again; react fast next time
        }
      if(!e.probePending) {
        const bool scheduled = e.framesSinceProbe>=params.probeInterval;
        const bool hinted    = suggested!=e.route && e.framesSincePrior>=e.priorBackoff;
        if(scheduled || hinted) {
          e.probePending    = true;
          e.probeHinted     = hinted && !scheduled;
          e.probeFrame      = frameId;
          e.framesSinceProbe= 0;
          e.framesSincePrior= 0;
          e.lastProbe       = true;
          return other(e.route);
          }
        }
      return e.route;
      }

    // GPU cost (ms) of the local-light passes of frame `frameId` for `eye`,
    // recorded with `route`. Order of arrival does not matter; unknown frames
    // are ignored.
    void report(uint8_t eye, uint64_t frameId, Route route, float ms) {
      auto& e = eyes[eye & 1];
      if(!(ms==ms) || ms<0)
        return;
      if(e.probePending && frameId==e.probeFrame) {
        e.probePending = false;
        if(route!=other(e.route))
          return; // not the probe we scheduled (route not applied)
        // Compare against the recent cost of the current route (fast
        // smoothing: the scene drifts while walking) with a margin above the
        // observed frame-to-frame noise.
        const float current = e.recent[e.route];
        e.cost[route]   = ms;
        e.recent[route] = ms;
        e.lastSample[route] = ms;
        e.samples[route] = 1;
        const float margin = std::max(std::max(params.marginMs, current*params.marginRel), params.marginNoise*e.deviation[e.route]);
        if(current>=0 && ms < current - margin) {
          e.route = route;
          ++e.switches;
          e.priorBackoff = params.priorInterval;
          }
        else if(e.probeHinted) {
          // The prior asked for this probe and was wrong: wait longer before
          // trusting it again, up to the scheduled cadence.
          e.priorBackoff = std::min(e.priorBackoff*2u, params.probeInterval);
          }
        return;
        }
      if(route!=e.route)
        return; // stale frame from before a switch, or an abandoned probe
      float& c = e.cost[route];
      if(c<0 || e.samples[route]==0) {
        c = ms;
        e.recent[route] = ms;
        e.lastSample[route] = ms;
        e.samples[route] = 1;
        return;
        }
      // Spikes (GPU clock changes, loading) do not move the estimates much.
      // Noise is the frame-to-frame difference, so a smooth drift while
      // walking does not widen the switch margin.
      const float bounded = std::min(ms, c*3.f);
      e.deviation[route]  = e.deviation[route]*(1.f-params.emaAlpha) + 0.7f*std::abs(bounded-e.lastSample[route])*params.emaAlpha;
      e.recent[route]     = e.recent[route]*(1.f-params.recentAlpha) + bounded*params.recentAlpha;
      c = c*(1.f-params.emaAlpha) + bounded*params.emaAlpha;
      e.lastSample[route] = bounded;
      ++e.samples[route];
      }

    Route    route(uint8_t eye) const { return eyes[eye & 1].route; }
    // True when the last decide() of this eye returned a probe route.
    bool     probing(uint8_t eye) const { return eyes[eye & 1].lastProbe; }
    bool     probePending(uint8_t eye) const { return eyes[eye & 1].probePending; }
    float    cost(uint8_t eye, Route r) const { return eyes[eye & 1].cost[r]; }
    float    deviation(uint8_t eye, Route r) const { return eyes[eye & 1].deviation[r]; }
    uint32_t switches(uint8_t eye) const { return eyes[eye & 1].switches; }

    void reset() { eyes[0] = Eye(); eyes[1] = Eye(); }

  private:
    struct Eye {
      bool     seeded = false;
      Route    route  = Volumes;
      float    cost[2] = {-1.f, -1.f};
      float    recent[2] = {-1.f, -1.f};     // fast-smoothed cost, the probe reference
      float    lastSample[2] = {0.f, 0.f};
      float    deviation[2] = {0.f, 0.f};    // smoothed frame-to-frame |difference| of ordinary frames
      uint32_t samples[2] = {0, 0};
      uint32_t framesSinceProbe = 0;
      uint32_t framesSincePrior = 0;
      uint32_t priorBackoff = 15;
      bool     probePending = false;
      bool     probeHinted  = false;
      bool     lastProbe    = false;
      uint64_t probeFrame   = 0;
      uint32_t switches     = 0;
      };

    static Route other(Route r) { return r==Slabs ? Volumes : Slabs; }

    Route priorSuggests(float weight, Route current) const {
      if(!(weight==weight))
        return current;
      if(weight>=params.priorOpenAt)
        return Slabs;
      if(weight<params.priorCloseAt)
        return Volumes;
      return current;
      }

    Params params;
    Eye    eyes[2];
  };
