#pragma once

#include <cstdint>
#include <limits>

namespace Tempest::Detail {

// Vulkan only guarantees the low timestampValidBits bits on the selected queue.
// The interval must be shorter than one counter wrap.
constexpr uint64_t gpuTimestampDelta(uint64_t begin, uint64_t end, uint32_t validBits) {
  if(validBits==0 || validBits>64)
    return 0;
  const uint64_t mask = validBits==64 ? std::numeric_limits<uint64_t>::max() : (uint64_t(1)<<validBits)-1;
  return (end-begin)&mask;
  }

}
