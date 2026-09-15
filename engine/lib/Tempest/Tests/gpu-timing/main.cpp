#include <Tempest/VulkanApi>
#include <Tempest/Device>
#include <Tempest/Fence>
#include <Tempest/Log>

#include "../../Engine/utility/gputimestamp.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Tempest;
using Tempest::Detail::gpuTimestampDelta;

static_assert(gpuTimestampDelta(10,30,64)==20);
static_assert(gpuTimestampDelta(250,5,8)==11);
static_assert(gpuTimestampDelta(UINT64_MAX-2,3,64)==6);
static_assert(gpuTimestampDelta(0x1fa,0x205,8)==11);
static_assert(gpuTimestampDelta(1,2,0)==0);
static_assert(gpuTimestampDelta(1,2,65)==0);

static void require(bool value, const char* message) {
  if(!value)
    throw std::runtime_error(message);
  }

int main(int argc, char** argv) {
  if(argc>1 && std::string_view(argv[1])=="--math-only") {
    std::cout << "Timestamp math passed\n";
    return 0;
    }
  std::atomic_uint errors{0};
  Log::setOutputCallback([&](Log::Mode mode,const char* text) {
    std::cerr << text << '\n';
    if(mode==Log::Error)
      ++errors;
    });
  try {
    VulkanApi api{ApiFlags::Validation};
    Device device(api);
    auto color = device.attachment(TextureFormat::RGBA8,64,64);
    auto cmd = device.commandBuffer();
    require(cmd.gpuTimings().empty(),"Unrecorded buffer returned timings");

    for(int frame=0; frame<5; ++frame) {
      const bool enabled = frame!=2;
      {
      auto enc = cmd.startEncoding(device,enabled);
      require(cmd.gpuTimings().empty(),"Recording buffer returned stale timings");
      enc.setDebugMarker("Clear A");
      enc.setFramebuffer({{color,Tempest::Vec4(1,0,0,1),Preserve}});
      enc.setDebugMarker("Inside rendering");
      enc.setFramebuffer({});
      enc.setDebugMarker("Clear B");
      enc.setFramebuffer({{color,Tempest::Vec4(0,1,0,1),Preserve}});
      enc.setFramebuffer({});
      if(frame==4)
        for(int i=0; i<300; ++i)
          enc.setDebugMarker("Overflow");
      }
      auto fence = device.submit(cmd);
      fence.wait();
      const auto timings = cmd.gpuTimings();
      if(!enabled) {
        require(timings.empty(),"Disabled recording returned timings");
        continue;
        }
      if(frame==0 && timings.empty()) {
        std::cerr << "SKIP: GPU timestamps unavailable\n";
        return errors==0 ? 77 : 1;
        }
      require(!timings.empty(),"Completed recording lost its timings");
      require(timings.front().name=="Unmarked","Wrong first region");
      require(timings.size()>=3,"Missing opening or closing interval");
      require(timings[1].name=="Clear A","Wrong marker order");
      require(timings[2].name=="Clear B","Final render pass lost its interval");
      for(const auto& t:timings) {
        require(t.name!="Inside rendering","Render-pass marker created a GPU interval");
        require(std::isfinite(t.milliseconds) && t.milliseconds>=0 && t.milliseconds<10000,
                "Invalid timestamp interval");
        }
      if(frame==4) {
        require(timings.size()==255,"Query capacity was not bounded");
        require(timings.back().name=="[marker limit]","Overflow tail was not labeled");
        } else {
        require(timings.size()==3,"Wrong region count");
        }
      }
    device.waitIdle();
    require(errors==0,"Vulkan validation or engine errors occurred");
    std::cout << "GPU timing reuse, render passes, disable/enable and overflow passed\n";
    }
  catch(const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
    }
  return 0;
  }
