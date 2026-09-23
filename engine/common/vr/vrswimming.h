#pragma once
#include "vrswimmotion.h"
#include <cstdint>

namespace Vr {
struct SwimInput {
  bool enabled=false;
  uint64_t poseTime=0; // OpenXR predicted display time, nanoseconds
  SwimMotion::Vec relative[2]{};
  bool valid[2]{};
  SwimMotion::Vec forward{},direction{}; // tracking Y-up; game Y-up
  float eyeHeight=160, surfaceOffset=140, units=100;
};
class Swimming {
  public:
    SwimInput input;
    void reset() { *this=Swimming{}; }
    void sample(const SwimInput& in,float dt) {
      if(!in.enabled || !SwimMotion::Finite(in.direction)) { reset(); return; }
      input=in;
      // The gamepad timer can sample the same XR pose more than once. Only new
      // poses contribute strokes, using tracking time rather than timer cadence.
      if(in.poseTime!=0) {
        if(in.poseTime==lastPoseTime) return;
        dt=lastPoseTime!=0 && in.poseTime>lastPoseTime ? float(in.poseTime-lastPoseTime)*1.e-9f : 0.f;
        lastPoseTime=in.poseTime;
      }
      const auto effort=strokes.Update(in.relative,in.valid,in.forward,dt);
      if(dt>0 && dt<=.1f) {
        support+=(effort.support-support)*(1-std::exp(-dt/.22f));
        // Advance uses GTA's Z-up coordinates. Recognition remains OpenXR Y-up.
        const auto d=SwimMotion::Unit(in.direction);
        impulse=impulse+SwimMotion::Vec{d.x,d.z,d.y}*effort.impulse;
      } else { support=0; impulse={}; velocity={}; vertical=0; }
    }
    SwimMotion::Vec advance(float dt,float clearance,bool floor) {
      velocity=SwimMotion::Advance(velocity,impulse,vertical,
          SwimMotion::SupportForGaze(support,input.direction.y),dt,clearance,floor);
      impulse={};
      return {velocity.x,velocity.z,velocity.y};
    }
    void blocked() { velocity={}; impulse={}; vertical=0; }
  private:
    SwimMotion::Strokes strokes;
    SwimMotion::Vec impulse{},velocity{};
    float support=0,vertical=0;
    uint64_t lastPoseTime=0;
};
}
