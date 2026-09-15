#pragma once

#include <cstdint>

class BufferedShot final {
  public:
    void press() {
      if(!active()) elapsed=0;
      held=true;
      pending=true;
      }

    void release() { held=false; }
    bool active() const { return held || pending; }

    void fired() {
      pending=false;
      elapsed=0;
      }

    void cancel() {
      held=false;
      pending=false;
      elapsed=0;
      }

    void advance(uint64_t dt) {
      if(!active()) return;
      // Do not leave an unfireable request waiting indefinitely on a missing animation.
      if(dt>=TimeoutMs-elapsed) cancel(); else elapsed+=dt;
      }

  private:
    static constexpr uint64_t TimeoutMs=5000;
    bool held=false;
    bool pending=false;
    uint64_t elapsed=0;
  };
