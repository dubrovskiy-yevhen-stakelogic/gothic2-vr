#pragma once

#include <algorithm>
#include <cmath>

namespace MovementResponse {

inline bool walk(float magnitude, float deadZone, float threshold, float hysteresis, bool wasWalking) {
  if(magnitude<=deadZone) return false;
  // Use physical stick travel so the turning curve does not delay the transition to running.
  const float boundary=threshold+(wasWalking ? hysteresis : -hysteresis);
  return magnitude<std::clamp(boundary,deadZone,1.f);
  }

inline float turn(float delta, float magnitude, float speed, float boost, float dt) {
  // Boost deliberate corners, then ease back to the base rate near the requested heading.
  // Angles are in degrees and dt is in seconds.
  delta=std::remainder(delta,360.f);
  const float strength=std::clamp(magnitude,0.f,1.f);
  const float corner=std::min(std::abs(delta)/90.f,1.f);
  const float step=std::max(speed,0.f)*strength*(1.f+std::max(boost,0.f)*strength*corner)*std::max(dt,0.f);
  return std::clamp(delta,-step,step);
  }

}
