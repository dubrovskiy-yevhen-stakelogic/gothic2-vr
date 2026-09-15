#pragma once

#include <algorithm>
#include <cmath>

namespace CameraMath {

inline float yawDelta(float from, float to) {
  return std::remainder(to-from,360.f);
  }

inline float yawNear(float yaw, float reference) {
  return reference+yawDelta(reference,yaw);
  }

inline float followYawDelta(float from, float to, float dt, float smoothing, float strength) {
  // Exponential following keeps the response consistent across frame rates.
  const float amount=std::clamp(strength,0.f,1.f);
  const float blend=-std::expm1(-std::max(dt,0.f)*amount/std::max(smoothing,0.001f));
  return yawDelta(from,to)*blend;
  }

}
