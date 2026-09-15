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
  constexpr float radians = std::numbers::pi_v<float>/180.f;
  const float pitch = std::clamp(cameraPitch,-85.f,85.f)*radians;
  const float forward = -y*std::cos(pitch);
  const float vertical = y*std::sin(pitch);
  // Camera elevation and Gothic's swimmer pitch have opposite signs.
  return {cameraYaw-std::atan2(x,forward)/radians,
          std::atan2(vertical,std::hypot(x,forward))/radians};
  }

}
