// Adapted from GTA SA VR Quest native/src/SwimMotion.h.
// Copyright (c) 2026 the GTA San Andreas VR Quest port contributors.
// MIT license: see licenses/GTA-SA-VR-swimming.txt.
#pragma once

#include <algorithm>
#include <cmath>

namespace Vr::SwimMotion {

struct Vec {
    float x{}, y{}, z{};
    Vec operator+(Vec b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec operator*(float s) const { return {x*s,y*s,z*s}; }
};
inline float Dot(Vec a, Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline float Length(Vec a) { return std::sqrt(Dot(a,a)); }
inline bool Finite(Vec a) {
    return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);
}
inline Vec Unit(Vec a) {
    const float n=Length(a);
    return Finite(a)&&n>0.001f ? a*(1.0f/n) : Vec{};
}

// All recognition is in metres in tracking space (Y up), before any game
// locomotion transform. Moving through the water cannot generate more strokes.
struct Hand {
    Vec previous{}, velocity{};
    float distance{}, quiet{};
    bool valid{};
};
struct Effort { float impulse{}, support{}; };
class Strokes {
    Hand hands_[2]{};
public:
    void Reset() { *this=Strokes{}; }
    Effort Update(const Vec relative[2], const bool valid[2], Vec forward, float dt) {
        Effort out{};
        forward=Unit(forward);
        const Vec right=Unit({-forward.z,0,forward.x});
        for (int i=0;i<2;++i) {
            auto& h=hands_[i];
            if (!valid[i]||!Finite(relative[i])) { h={}; continue; }
            const Vec delta=relative[i]-h.previous;
            const bool fresh=h.valid&&dt>0.001f&&dt<=0.1f&&Length(delta)<0.45f;
            h.previous=relative[i]; h.valid=true;
            if (!fresh) { h.velocity={}; h.distance=h.quiet=0; continue; }
            const float blend=1.0f-std::exp(-dt/0.035f);
            h.velocity=h.velocity+(delta*(1.0f/dt)-h.velocity)*blend;
            const float back=-Dot(h.velocity,forward);
            const float side=std::max(0.0f,std::abs(Dot(h.velocity,right))-0.12f);
            const float down=std::max(0.0f,-h.velocity.y-0.12f);
            const float pull=std::clamp(back-0.12f,0.0f,3.0f);
            // A power pull has one result: propulsion along gaze. Only a
            // separate sculling movement supplies lift, never the pull itself.
            if (pull<=0.02f) out.support+=0.65f*side+0.9f*down;
            if (pull>0) {
                h.quiet=0;
                const float old=h.distance;
                h.distance=std::min(0.9f,h.distance+pull*dt);
                // A short paddle supplies gentle thrust; the longer power phase of a
                // large stroke adds a continuous shove, without a timed button.
                const auto work=[](float d) {
                    return 1.8f*std::max(0.0f,d-0.025f)+
                           2.6f*std::max(0.0f,d-0.22f);
                };
                out.impulse+=work(h.distance)-work(old);
            } else {
                h.quiet+=dt;
                if (back<-0.10f||h.quiet>0.18f) h.distance=0;
            }
        }
        out.support=std::clamp(out.support,0.0f,1.5f);
        return out;
    }
};

inline float SupportForGaze(float support,float verticalGaze) {
    // Looking down to dive must also suppress residual lift from the previous
    // scull and from the recovery half of the stroke (arms returning forward).
    return support*std::clamp((verticalGaze+0.25f)/0.20f,0.0f,1.0f);
}

// Native moveSpeed uses distance per 1/50 second. Keep this policy in SI
// units so the same stroke behaves consistently across game frame rates.
inline Vec Advance(Vec velocity, Vec impulse, float& verticalSpeed,
                   float support, float dt, float surfaceClearance,
                   bool atWorldFloor) {
    if (!(dt>0.0f&&dt<=0.1f)) return velocity;
    velocity=velocity*std::exp(-1.15f*dt)+impulse;
    // The stock surface spring is replaced: still hands settle at -0.22 m/s.
    // Sculling supplies lift; a pitched forward stroke also raises/lowers CJ.
    const float target=-0.22f+0.65f*support;
    // Match horizontal drag. The former 0.28s relaxation erased the downwards
    // impulse between strokes, then the same stroke's lift pushed CJ back up.
    verticalSpeed+=(target-verticalSpeed)*(1.0f-std::exp(-1.15f*dt));
    verticalSpeed=std::clamp(verticalSpeed+impulse.z,-1.5f,1.5f);
    velocity.z=verticalSpeed;
    const float speed=Length(velocity);
    if (speed>2.8f) velocity=velocity*(2.8f/speed);
    // No upward launch from repeated strokes at the waterline. Native climb
    // and jump-out states bypass this policy and keep their ordinary physics.
    const float ceiling=std::max(0.0f,surfaceClearance/dt);
    velocity.z=std::min(velocity.z,ceiling);
    if (atWorldFloor) velocity.z=std::max(0.0f,velocity.z);
    verticalSpeed=velocity.z;
    return velocity;
}

} // namespace Vr::SwimMotion
