#pragma once
#include <Tempest/Matrix4x4>
#include <cmath>

// OpenXR uses metres, +Y up and -Z forward. Gothic uses centimetres and a
// camera space with +Y down, +Z forward. Keep this conversion in one place.
namespace XrMath {
struct Quaternion { float x=0, y=0, z=0, w=1; };
struct Pose { Quaternion orientation; Tempest::Vec3 position; };
inline Tempest::Matrix4x4 poseMatrix(const Pose& p,float unitsPerMeter=100.f) {
  const auto q=p.orientation;
  return Tempest::Matrix4x4(
    1-2*(q.y*q.y+q.z*q.z), 2*(q.x*q.y-q.z*q.w), 2*(q.x*q.z+q.y*q.w), p.position.x*unitsPerMeter,
    2*(q.x*q.y+q.z*q.w), 1-2*(q.x*q.x+q.z*q.z), 2*(q.y*q.z-q.x*q.w), p.position.y*unitsPerMeter,
    2*(q.x*q.z-q.y*q.w), 2*(q.y*q.z+q.x*q.w), 1-2*(q.x*q.x+q.y*q.y), p.position.z*unitsPerMeter,
    0,0,0,1);
}
inline float yaw(const Quaternion& q) {
  return std::atan2(2*(q.w*q.y+q.x*q.z),1-2*(q.y*q.y+q.x*q.x));
}
inline Pose reference(const Pose& head) {
  const float a=yaw(head.orientation)*0.5f;
  return {{0,std::sin(a),0,std::cos(a)},head.position};
}
struct CinemaAnchor {
  Pose pose;bool valid=false;
  void reset() {valid=false;}
  const Pose& update(const Pose& head,bool enabled) {
    if(!enabled) {reset();return pose;}
    if(!valid) {
      pose=reference(head);const float angle=yaw(pose.orientation);
      pose.position+=Tempest::Vec3(-2.f*std::sin(angle),-.12f,-2.f*std::cos(angle));valid=true;
    }
    return pose;
  }
};
inline Tempest::Matrix4x4 relative(const Pose& eye,const Pose& reference,float unitsPerMeter=100.f) {
  auto ref=poseMatrix(reference,unitsPerMeter); ref.inverse();
  return ref*poseMatrix(eye,unitsPerMeter);
}
inline Tempest::Matrix4x4 eyeView(const Pose& eye,const Pose& reference,const Tempest::Matrix4x4& base) {
  auto inv=relative(eye,reference); inv.inverse();
  auto c=Tempest::Matrix4x4::mkIdentity(); c.scale(1,-1,-1);
  return c*inv*c*base;
}
inline Tempest::Matrix4x4 projection(float left,float right,float down,float up,float nearZ,float farZ) {
  const float l=std::tan(left),r=std::tan(right),b=std::tan(down),t=std::tan(up);
  const float k=farZ/(farZ-nearZ);
  return Tempest::Matrix4x4(2/(r-l),0,-(r+l)/(r-l),0,
                           0,2/(t-b),(t+b)/(t-b),0,
                           0,0,k,-nearZ*k, 0,0,1,0);
}
class SnapTurn {
  public:
    float update(float x,bool enabled) {
      if(!enabled) { armed=false; return 0; }
      if(std::abs(x)<0.25f) armed=true;
      if(armed && std::abs(x)>0.7f) { armed=false; return x>0 ? -30.f : 30.f; }
      return 0;
    }
  private:
    bool armed=false;
};
}
