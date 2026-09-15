#pragma once
#include <Tempest/Vec>
namespace Vr {
// Tracking-space metres. Only collision-accepted movement is removed from the eye pose.
class RoomMotion {
  public:
    void reset() { consumed={}; ready=false; active=false; }
    void invalidate() { ready=false; active=false; }
    Tempest::Vec3 delta(Tempest::Vec3 head,bool enabled) {
      head.y=0; active=enabled;
      // Pausing the body must retain its accumulated camera correction.
      if(!enabled) return {};
      if(!ready) { consumed=head; ready=true; return {}; }
      auto result=head-consumed;
      if(result.length()>0.5f) { consumed=head; return {}; }
      return result;
    }
    void consume(Tempest::Vec3 accepted) { accepted.y=0; consumed+=accepted; }
    float blocked(Tempest::Vec3 head,float units) const {
      head.y=0;
      return active&&ready?(head-consumed).length()*units:0.f;
    }
    Tempest::Vec3 correction(Tempest::Vec3 head,float units) const {
      auto result=consumed*units;
      if(active&&ready) {
        head.y=0; auto remaining=(head-consumed)*units;
        const float length=remaining.length();
        if(length>8.f) result+=remaining*(1.f-8.f/length);
      }
      return result;
    }
  private:
    Tempest::Vec3 consumed;
    bool ready=false,active=false;
};
}
