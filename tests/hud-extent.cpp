#include "vr/vrhudrect.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int checks=0;
void check(bool ok,const char* why) {
  ++checks;
  if(!ok) { std::printf("FAIL: %s\n",why); std::exit(1); }
}

int main() {
  // Bounds observed in SteamVR's shared-texture resize log during one session.
  for(auto painted:std::vector<Vr::HudRect>{{400,1100,516,78},{100,200,668,398},
                                          {200,200,741,879},{200,200,743,879},
                                          {0,0,1280,1370},{}}) {
    const auto quest=Vr::hudLayerRegion(painted,1280,1370,false);
    check(quest.x==painted.x && quest.y==painted.y && quest.w==painted.w && quest.h==painted.h,
          "other runtimes retain the painted bounds");
    const auto stable=Vr::hudLayerRegion(painted,1280,1370,true);
    check(stable.empty()==painted.empty(),"an empty HUD never creates an opaque extra layer");
    if(painted.empty()) continue;
    check(stable.x==0 && stable.y==0 && stable.w==1280 && stable.h==1370,
          "SteamVR subimage extent stays constant across text and menu changes");
    const auto copy=Vr::hudCopyRegion(stable,1280,1370);
    check(copy.x==0 && copy.y==0 && copy.w==1280 && copy.h==1370,
          "full transparent clear and copy remove stale world pixels from the HUD canvas");
    const auto small=Vr::hudQuad(painted,1280,1370,2.5f);
    const auto full=Vr::hudQuad(stable,1280,1370,2.5f);
    const float x=full.posX-full.sizeX/2+float(painted.x)/1280*full.sizeX;
    const float y=full.posY+full.sizeY/2-float(painted.y)/1370*full.sizeY;
    check(std::abs(x-(small.posX-small.sizeX/2))<1e-5f &&
          std::abs(y-(small.posY+small.sizeY/2))<1e-5f,
          "HUD pixels keep their world-space position and scale");
  }
  std::printf("HUD extent: %d production checks passed\n",checks);
}
