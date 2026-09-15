#pragma once

#include <cmath>
#include <cstdint>

namespace TwoFingerSwipe {

inline bool canPair(uint64_t elapsed, float firstTravel, float slop) {
  return elapsed<=350 && firstTravel<=slop;
  }

// Both fingers must travel vertically in the same direction; pinches and one-finger drags do not qualify.
inline int direction(float x0, float y0, float x1, float y1, float threshold, uint64_t elapsed) {
  if(elapsed>700 || std::abs(y0)<threshold || std::abs(y1)<threshold ||
     std::abs(y0)<1.5f*std::abs(x0) || std::abs(y1)<1.5f*std::abs(x1))
    return 0;
  if(y0<0 && y1<0) return -1;
  if(y0>0 && y1>0) return 1;
  return 0;
  }

inline int horizontalDirection(float x0, float y0, float x1, float y1, float threshold, uint64_t elapsed) {
  return direction(y0,x0,y1,x1,threshold,elapsed);
  }

}
