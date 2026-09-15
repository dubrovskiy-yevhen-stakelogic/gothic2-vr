#pragma once
#include <array>
#include <algorithm>
#include <vector>
#include <span>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace Vr {
struct HandAsset {
  struct Vertex { float position[4][3],normal[4][3],uv[2]; };
  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;
  static HandAsset read(std::span<const char> data) {
    static_assert(sizeof(Vertex)==104);
    uint32_t h[4]={};
    if(data.size()<sizeof(h)) throw std::runtime_error("Truncated UXRH header");
    std::memcpy(h,data.data(),sizeof(h));
    if(std::memcmp(data.data(),"UXRH",4)!=0 || h[1]!=1 || h[2]==0 || h[2]>65535 || h[3]==0 || h[3]>1000000 || h[3]%3!=0)
      throw std::runtime_error("Invalid UXRH header");
    if(data.size()!=16+size_t(h[2])*sizeof(Vertex)+size_t(h[3])*2) throw std::runtime_error("Invalid UXRH length");
    HandAsset mesh;mesh.vertices.resize(h[2]);mesh.indices.resize(h[3]);
    std::memcpy(mesh.vertices.data(),data.data()+16,mesh.vertices.size()*sizeof(Vertex));
    std::memcpy(mesh.indices.data(),data.data()+16+mesh.vertices.size()*sizeof(Vertex),mesh.indices.size()*2);
    for(const auto i:mesh.indices) if(i>=mesh.vertices.size()) throw std::runtime_error("Invalid UXRH index");
    for(const auto& v:mesh.vertices) {
      for(int p=0;p<4;++p) for(int a=0;a<3;++a)
        if(!std::isfinite(v.position[p][a]) || !std::isfinite(v.normal[p][a]) || std::abs(v.position[p][a])>2.f) throw std::runtime_error("Invalid UXRH vertex");
      if(!std::isfinite(v.uv[0]) || !std::isfinite(v.uv[1])) throw std::runtime_error("Invalid UXRH UV");
    }
    return mesh;
  }
  static std::array<float,4> weights(float grip,float trigger) {
    grip=std::clamp(grip,0.f,1.f);trigger=std::clamp(trigger,0.f,1.f);
    return {(1-grip)*(1-trigger),grip*(1-trigger),(1-grip)*trigger,grip*trigger};
  }
};
}
