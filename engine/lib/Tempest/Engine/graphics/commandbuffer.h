#pragma once

#include <Tempest/AbstractGraphicsApi>
#include <Tempest/Encoder>
#include "../utility/dptr.h"

namespace Tempest {

class Device;
class Frame;
class RenderPipeline;
class DescriptorSet;
class Texture2d;

template<class T>
class Encoder;

class FrameBufferLayout;
class CommandBuffer;

class CommandBuffer final {
  public:
    CommandBuffer()=default;
    CommandBuffer(CommandBuffer&& f)=default;
    ~CommandBuffer();
    CommandBuffer& operator = (CommandBuffer&& other)=default;

    // Optional Vulkan timestamps measure elapsed intervals between debug markers.
    // Unsupported backends return no timings and keep rendering normally.
    auto startEncoding(Tempest::Device& dev, bool gpuProfiling = false) -> Encoder<CommandBuffer>;

    // Call after this command buffer's submission fence completes, before re-encoding.
    // Does not wait for the GPU; disabled, incomplete or unavailable results are empty.
    std::vector<AbstractGraphicsApi::GpuTiming> gpuTimings() const;

  private:
    CommandBuffer(Tempest::Device& dev, AbstractGraphicsApi::CommandBuffer* impl);

    Tempest::Device*                                    dev=nullptr;
    Detail::DPtr<AbstractGraphicsApi::CommandBuffer*>   impl;

  friend class Tempest::Device;
  friend class Tempest::Encoder<CommandBuffer>;
  };

}
