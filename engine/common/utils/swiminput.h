#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace SwimInput {

struct Direction {
  float yaw;
  float pitch;
  };

inline Direction direction(float x, float y, float cameraYaw, float cameraPitch) {
  // Physical swimming supplies neutral axes. atan2(0, -0) would turn the body
  // away from the head, so the first walking steps on shore go back into water.
  if(x==0.f && y==0.f)
    return {cameraYaw,0.f};
  constexpr float radians = std::numbers::pi_v<float>/180.f;
  const float pitch = std::clamp(cameraPitch,-85.f,85.f)*radians;
  const float forward = -y*std::cos(pitch);
  const float vertical = y*std::sin(pitch);
  // Camera elevation and Gothic's swimmer pitch have opposite signs.
  return {cameraYaw-std::atan2(x,forward)/radians,
          std::atan2(vertical,std::hypot(x,forward))/radians};
  }

}
