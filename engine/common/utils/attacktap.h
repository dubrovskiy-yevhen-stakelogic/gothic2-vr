#pragma once

#include <cstdint>
#include <optional>

class AttackTap final {
  public:
    void begin(uint64_t now, uint64_t holdMs) {
      pressedAt=now;
      duration=holdMs;
      }

    bool release(uint64_t now) {
      const bool tap=pressedAt && now>=*pressedAt && now-*pressedAt<duration;
      cancel();
      return tap;
      }

    bool active() const { return pressedAt.has_value(); }
    void cancel() { pressedAt.reset(); }

  private:
    std::optional<uint64_t> pressedAt;
    uint64_t duration=0;
  };
