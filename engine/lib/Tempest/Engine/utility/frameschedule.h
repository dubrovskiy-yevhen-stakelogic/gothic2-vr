#pragma once

#include <chrono>
#include <cstdint>

namespace Tempest::Detail {

// Pure deadline calculation, kept separate from the platform wait for deterministic tests.
class FrameSchedule {
  public:
    using Clock = std::chrono::steady_clock;

    void setFrameRate(uint32_t value) {
      // Sub-nanosecond frame intervals cannot be represented by this clock schedule.
      value = value>1000000000u ? 1000000000u : value;
      if(rate==value)
        return;
      rate = value;
      reset();
      }

    void reset() {
      armed = false;
      remainder = 0;
      }

    Clock::time_point nextDeadline(Clock::time_point now) {
      if(rate==0) {
        reset();
        return now;
        }
      // A missed deadline starts a new schedule instead of producing a burst of catch-up frames.
      if(!armed || now>next) {
        next = now;
        remainder = 0;
        armed = true;
        }
      const auto deadline = next;
      uint64_t ns = 1000000000u/rate;
      remainder += 1000000000u%rate;
      if(remainder>=rate) {
        ++ns;
        remainder -= rate;
        }
      next += std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(ns));
      return deadline;
      }

  private:
    uint32_t rate = 0;
    uint64_t remainder = 0;
    bool armed = false;
    Clock::time_point next;
  };

}
