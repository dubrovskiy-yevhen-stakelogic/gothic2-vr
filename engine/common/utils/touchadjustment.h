#pragma once

#include <algorithm>

class TouchAdjustment final {
  public:
    int drag(int delta, int pixelsPerStep) {
      const int step=std::max(1,pixelsPerStep);
      remainder+=delta;
      const int steps=remainder/step;
      remainder-=steps*step;
      return steps;
      }

    void reset() { remainder=0; }

  private:
    int remainder=0;
  };
