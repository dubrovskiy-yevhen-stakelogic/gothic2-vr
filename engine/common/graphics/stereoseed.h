#pragma once

#include <cstddef>
#include <cstdint>

// A same-frame main-visibility snapshot may seed the other eye's depth pass.
// This tracks source lifetime only; final visibility still uses the other eye.
class StereoSeedState final {
  public:
    struct Snapshot {
      uint64_t generation=0, tick=0;
      size_t commands=0, payload=0;
      uint32_t width=0, height=0;
      bool operator==(const Snapshot&) const = default;
      };

    bool beginEye(bool requested, bool buffersReady, const Snapshot& current) {
      borrowing=requested && ready && buffersReady && source==current;
      ready=false; // A snapshot can be consumed once, never carried across eyes.
      return borrowing;
      }

    void mainReady(const Snapshot& current, bool buffersReady) {
      source=current;
      ready=buffersReady;
      }

    void invalidate() { ready=false; borrowing=false; }
    bool isBorrowing() const { return borrowing; }

  private:
    Snapshot source;
    bool ready=false, borrowing=false;
  };
