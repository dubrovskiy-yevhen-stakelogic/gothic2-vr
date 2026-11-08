#pragma once
#include <Tempest/Vec>
#include <algorithm>
#include <cmath>

namespace Vr {
// RDR VR's velocity-driven locomotion: dead zone is already applied by the
// controller mapper. Walk band .5..1.6 m/s, run band 1.6..3.6 m/s.
// Gothic uses centimetres, yaw 0 = +X, stick Y negative = forward.
inline Tempest::Vec3 groundStickVelocity(float x,float y,float yaw,float scale) {
  if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(yaw)||!std::isfinite(scale)) return {};
  const float r=std::hypot(x,y),m=std::min(r,1.f);
  if(r<=0.f || scale<=0.f) return {};
  const float speed=(m<=.65f ? .5f+1.1f*m/.65f : 1.6f+2.f*(m-.65f)/.35f)*100.f*scale;
  const float a=yaw*.017453292519943295f,c=std::cos(a),s=std::sin(a);
  return {(-y*c+x*s)*speed/r,0.f,(-y*s-x*c)*speed/r};
}
inline Tempest::Vec3 groundDisplacement(Tempest::Vec3 velocity,float seconds,float scriptScale) {
  return velocity*(std::max(0.f,seconds)*scriptScale);
}
}
