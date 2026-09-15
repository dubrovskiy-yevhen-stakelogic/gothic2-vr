#include <Tempest/VulkanApi>
#include <Tempest/Device>
#include <Tempest/Fence>
#include <Tempest/Log>
#include <Tempest/Pixmap>
#include <Tempest/RenderState>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Tempest;

static_assert(sizeof(DrawIndexedIndirectCommand)==20);
static void require(bool value,const char* message) {
  if(!value) throw std::runtime_error(message);
}
template<class F> static void error(F fn, GraphicsErrc expected) {
  try { fn(); } catch(const std::system_error& e) {
    require(e.code()==expected,"Wrong API error");
    return;
  }
  throw std::runtime_error("Invalid call was accepted");
}
static void check(const Pixmap& pm,int draws) {
  require(pm.format()==TextureFormat::RGBA8 && pm.w()==64 && pm.h()==64,"Unexpected framebuffer format");
  const auto* bytes=static_cast<const uint8_t*>(pm.data());
  size_t checked=0;
  for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
    // Exclude two pixels around each analytically projected quad edge.
    const float nx=(float(x)+0.5f)/32.f-1.f, ny=(float(y)+0.5f)/32.f-1.f;
    if(std::abs(std::abs(ny)-0.6f)<0.06f || std::abs(std::abs(nx)-0.8f)<0.06f || std::abs(std::abs(nx)-0.2f)<0.06f) continue;
    const bool red=draws>=1 && nx>-0.8f && nx<-0.2f && std::abs(ny)<0.6f;
    const bool green=draws>=2 && nx>0.2f && nx<0.8f && std::abs(ny)<0.6f;
    const auto* p=bytes+4*(y*64+x);
    if(p[0]!=(red ? 255 : 0) || p[1]!=(green ? 255 : 0) || p[2]!=0 || p[3]!=255)
      throw std::runtime_error("Indexed geometry or instance index mismatch at "+std::to_string(x)+","+std::to_string(y));
    ++checked;
  }
  std::cout << "Verified " << checked << " pixels for " << draws << " instances\n";
}
template<class I> static void run(Device& device, const RenderPipeline& pipeline,const ComputePipeline& compute) {
  const std::vector<I> indices={99,99,99,0,1,2,2,1,3};
  auto ibo=device.ibo(indices);
  std::array<uint32_t,28> data={};
  const DrawIndexedIndirectCommand draw={6,2,3,5,7};
  std::memcpy(data.data()+4,&draw,sizeof(draw));
  auto commands=device.ssbo(data.data(),sizeof(data));
  uint32_t counts[2]={0,0};
  auto count=device.ssbo(counts,sizeof(counts));
  auto target=device.attachment(TextureFormat::RGBA8,64,64);
  auto cmd=device.commandBuffer();
  {
    auto enc=cmd.startEncoding(device);
    error([&] { enc.drawIndexedIndirect(ibo,commands,16); },GraphicsErrc::DrawCallWithoutFbo);
    error([&] { enc.drawIndexedIndirectCount(ibo,commands,16,count,4,2,32); },GraphicsErrc::DrawCallWithoutFbo);
    enc.setFramebuffer({{target,Vec4(0,0,0,1),Preserve}});
    enc.setPipeline(pipeline);
    error([&] { enc.drawIndexedIndirect(ibo,commands,2); },GraphicsErrc::InvalidStorageBuffer);
    error([&] { enc.drawIndexedIndirect(ibo,commands,16,2,18); },GraphicsErrc::InvalidStorageBuffer);
    error([&] { enc.drawIndexedIndirect(ibo,commands,16,2,16); },GraphicsErrc::InvalidStorageBuffer);
    error([&] { enc.drawIndexedIndirect(ibo,commands,100); },GraphicsErrc::InvalidStorageBuffer);
    error([&] { enc.drawIndexedIndirect(ibo,commands,std::numeric_limits<size_t>::max()-3); },GraphicsErrc::InvalidStorageBuffer);
    error([&] { enc.drawIndexedIndirect(ibo,commands,16,std::numeric_limits<size_t>::max(),32); },GraphicsErrc::InvalidStorageBuffer);
    error([&] { enc.drawIndexedIndirectCount(ibo,commands,16,count,2,2,32); },GraphicsErrc::InvalidStorageBuffer);
    error([&] { enc.drawIndexedIndirectCount(ibo,commands,16,count,8,2,32); },GraphicsErrc::InvalidStorageBuffer);
    IndexBuffer<I> empty;
    error([&] { enc.drawIndexedIndirect(empty,commands,16); },GraphicsErrc::InvalidStorageBuffer);
    enc.drawIndexedIndirect(empty,StorageBuffer(),0,0);
    enc.drawIndexedIndirect(ibo,commands,16);
  }
  auto fence=device.submit(cmd); fence.wait();
  check(device.readPixels(target),2);
  for(int mode=0;mode<6;++mode) {
    const uint32_t requested = mode==0 ? 0 : mode==1 ? 1 : mode==2 ? 2 : mode==3 ? 3 : 2;
    {
      auto enc=cmd.startEncoding(device);
      enc.setPipeline(compute);
      enc.setBinding(0,commands);
      enc.setBinding(1,count);
      enc.setPushData(requested);
      enc.dispatch(1);
      enc.setFramebuffer({{target,Vec4(0,0,0,1),Preserve}});
      enc.setPipeline(pipeline);
      if(mode<4)
        enc.drawIndexedIndirectCount(ibo,commands,16,count,4,2,32);
      else if(mode==4)
        enc.drawIndexedIndirect(ibo,commands,16,2,32);
      else
        enc.drawIndexedIndirect(ibo,commands,48); // firstInstance=8 -> right quad only
    }
    auto completion=device.submit(cmd); completion.wait();
    if(mode<5) check(device.readPixels(target),std::min(requested,2u));
    else {
      const auto pm=device.readPixels(target);
      const auto* bytes=static_cast<const uint8_t*>(pm.data());
      require(bytes[(32*64+16)*4]==0,"Single command accidentally read the first offset");
      require(bytes[(32*64+48)*4+1]==255,"Single command did not preserve firstInstance");
    }
  }
}
int main() {
  std::atomic_uint errors{0};
  Log::setOutputCallback([&](Log::Mode mode,const char* text) {
    std::cerr << text << '\n'; if(mode==Log::Error) ++errors;
  });
  try {
    VulkanApi api{ApiFlags::Validation};
    Device device(api);
    const auto& caps=device.properties().indirect;
    std::cout << "Device=" << device.properties().name << " indexed=" << caps.indexed << " multiDraw=" << caps.multiDraw
              << " firstInstance=" << caps.firstInstance << " count=" << caps.count << " maxDrawCount=" << caps.maxDrawCount << '\n';
    if(!caps.indexed || !caps.multiDraw || !caps.firstInstance || !caps.count) return 77;
    auto vert=device.shader("test.vert.spv"), frag=device.shader("test.frag.spv"), comp=device.shader("test.comp.spv");
    RenderState state; state.setCullFaceMode(RenderState::CullMode::NoCull);
    auto pipeline=device.pipeline(Topology::Triangles,state,vert,frag);
    auto compute=device.pipeline(comp);
    run<uint16_t>(device,pipeline,compute);
    run<uint32_t>(device,pipeline,compute);
    device.waitIdle();
    require(errors==0,"Vulkan or engine errors occurred");
    std::cout << "PASS: indexed indirect, multi-draw, GPU count clamp, firstIndex/vertexOffset/firstInstance, 16/32-bit indices, reuse, compute barriers and invalid input\n";
  } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
