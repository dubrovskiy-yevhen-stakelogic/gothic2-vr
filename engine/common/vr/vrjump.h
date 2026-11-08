#pragma once
#include <cmath>

namespace Vr {
// Gothic positions use centimetres, simulation time uses milliseconds.
inline float jumpLaunchSpeed(float gravity) { return std::sqrt(2.f*gravity*80.f); }
inline float jumpDisplacement(float velocity,float gravity,float dt) {
  return velocity*dt-.5f*gravity*dt*dt;
}
}
