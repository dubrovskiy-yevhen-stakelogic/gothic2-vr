#pragma once

#include <cmath>

namespace TouchMovement {

enum class Direction { None, Forward, Back, Left, Right };

inline Direction classicDirection(float x, float y, bool blocking) {
  const float horizontal=std::abs(x);
  const float vertical=std::abs(y);
  // Enter block deliberately, with a small release margin to avoid boundary chatter.
  const float depth=blocking ? 0.55f : 0.65f;
  const float cone=blocking ? 0.50f : 0.40f;
  if(y>=depth && horizontal<=y*cone) return Direction::Back;
  if(y< -0.35f && vertical>=horizontal) return Direction::Forward;
  if(horizontal>0.35f && horizontal>vertical) return x<0 ? Direction::Left : Direction::Right;
  return Direction::None;
  }

}
