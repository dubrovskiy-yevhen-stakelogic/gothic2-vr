#pragma once

#include "feedback.h"
#include <algorithm>
#include <cstdint>

class HapticPolicy final {
  public:
    struct Pulse {
      uint32_t duration=0;
      float strength=0;
      bool gamepad=false;
      };

    bool setEnabled(bool value) {
      if(enabled==value) return false;
      enabled=value;
      until=0;
      return true;
      }

    bool setGamepad(bool value) {
      if(gamepad==value) return false;
      gamepad=value;
      until=0;
      return true;
      }

    Pulse next(Feedback::Effect effect, uint64_t now) {
      using Effect=Feedback::Effect;
      const unsigned priority=effect==Effect::Damage ? 3 : (effect==Effect::Navigate ? 0 : (effect==Effect::Hit || effect==Effect::Teleport ? 2 : 1));
      if(!enabled || (now<until && priority<=lastPriority))
        return {};
      lastPriority=priority;
      switch(effect) {
        case Effect::Navigate: until=now+65;  return {8, 0.15f,gamepad};
        case Effect::Confirm:  until=now+90;  return {20,0.35f,gamepad};
        case Effect::Reject:   until=now+120; return {35,0.45f,gamepad};
        case Effect::Shoot:    until=now+100; return {18,0.30f,gamepad};
        case Effect::Cast:     until=now+120; return {28,0.40f,gamepad};
        case Effect::Teleport: until=now+180; return {75,0.70f,gamepad};
        case Effect::Hit:      until=now+100; return {30,0.50f,gamepad};
        case Effect::Damage:   until=now+180; return {65,0.75f,gamepad};
        }
      return {};
      }

    Pulse charge(float intensity, uint64_t now) {
      if(!enabled || now<until)
        return {};
      intensity=std::clamp(intensity,0.f,1.f);
      lastPriority=0;
      until=now+250;
      return {uint32_t(12+20*intensity),0.12f+0.33f*intensity,gamepad};
      }

  private:
    bool enabled=false;
    bool gamepad=false;
    uint64_t until=0;
    unsigned lastPriority=0;
  };
