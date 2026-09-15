#include <Tempest/VulkanApi>
#ifdef _WIN32
#include <Tempest/DirectX12Api>
#endif
#include <Tempest/Device>
#include <Tempest/Fence>
#include <Tempest/Pixmap>
#include <Tempest/Log>

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace Tempest;

template<class Api>
void checkChunks() {
  Api api{ApiFlags::Validation};
  Device device(api);
  std::vector<Attachment> colors;
  for(size_t i=0;i<97;++i)
    colors.push_back(device.attachment(TextureFormat::RGBA8,1,1));

  // Reusing the same buffer exercises reset; leaving the scope exercises destruction.
  for(int repeat=0;repeat<2;++repeat) {
    auto cmd=device.commandBuffer();
    const size_t finalCount=repeat==0 ? 1 : 97;
    for(size_t count : {size_t(1),size_t(32),size_t(33),size_t(64),size_t(65),size_t(97),finalCount}) {
      {
      auto enc=cmd.startEncoding(device);
      for(size_t i=0;i<count;++i) {
        // Each render pass starts a command chunk and writes a distinct, verifiable output.
        enc.setFramebuffer({{colors[i],Vec4(float(i)/255.f,float(count)/255.f,0,1),Preserve}});
        enc.setFramebuffer({});
        }
      }
      auto fence=device.submit(cmd);
      fence.wait();
      for(size_t i=0;i<count;++i) {
        auto pixel=device.readPixels(textureCast<const Texture2d&>(colors[i]));
        const auto* data=reinterpret_cast<const uint8_t*>(pixel.data());
        if(data[0]!=i || data[1]!=count || data[2]!=0 || data[3]!=255)
          throw std::runtime_error("A command chunk was skipped or executed incorrectly");
        }
      }
    }
  device.waitIdle();
  }

int main(int argc, char** argv) {
  std::atomic_uint errors{0};
  Log::setOutputCallback([&](Log::Mode mode,const char* message) {
    std::cerr << message << '\n';
    if(mode==Log::Error)
      ++errors;
    });
  try {
#ifdef _WIN32
    if(argc>1 && std::string_view(argv[1])=="--directx")
      checkChunks<DirectX12Api>();
    else
#endif
      checkChunks<VulkanApi>();
    if(errors!=0)
      throw std::runtime_error("Graphics validation or engine errors occurred");
    std::cout << "Command chunk recording, submission, readback, reuse and destruction passed\n";
    }
  catch(const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
    }
  }
