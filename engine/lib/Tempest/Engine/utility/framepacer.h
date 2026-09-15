#pragma once

#include "frameschedule.h"

namespace Tempest {

// Application-side rate limiting; this does not synchronize presentation with display vsync.
// Call wait once before each frame on the render thread, and reset after a lifecycle interruption.
class FramePacer {
  public:
    void setFrameRate(uint32_t framesPerSecond) { schedule.setFrameRate(framesPerSecond); }
    void reset() { schedule.reset(); }
    void wait();

  private:
    Detail::FrameSchedule schedule;
  };

}
