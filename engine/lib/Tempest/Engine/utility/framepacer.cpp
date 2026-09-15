#include "framepacer.h"

#include <Tempest/CpuTrace>

#include <thread>

void Tempest::FramePacer::wait() {
  const auto now = Detail::FrameSchedule::Clock::now();
  const auto deadline = schedule.nextDeadline(now);
  if(deadline<=now)
    return;
  CpuTrace trace("Tempest::frame pacing wait");
  std::this_thread::sleep_until(deadline);
  }
