#pragma once

#include <cmath>
#include <optional>

namespace MeleeAssist {

inline std::optional<float> facing(float yaw, float targetYaw, float distance, float maxAngle, float maxDistance) {
  if(!std::isfinite(yaw) || !std::isfinite(targetYaw) || !std::isfinite(distance) ||
     !std::isfinite(maxAngle) || !std::isfinite(maxDistance) ||
     distance<=0.f || distance>maxDistance || maxAngle<=0.f)
    return std::nullopt;
  const float delta=std::remainder(targetYaw-yaw,360.f);
  if(std::abs(delta)>maxAngle) return std::nullopt;
  return yaw+delta;
  }

}
