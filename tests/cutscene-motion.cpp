#include "vr/vrcontrols.h"
#include "vr/xrmath.h"
#include "vr/vrjump.h"
#include <cstdio>
#include <cstdlib>
void check(bool ok,const char* message) { if(!ok) {std::puts(message);std::exit(1);} }
int main() {
  Vr::Menu menu; check(menu.settings.cutsceneMode==1,"default cinematic stereo"); menu.settings.cutsceneMode=0; Vr::Input in; in.focused=true; in.cutscene=true;
  uint64_t now=0; auto step=[&]{return menu.update(in,now+=20);};
  step(); in.rightClick=true;
  check(step() && menu.settings.cutsceneMode==1 && menu.changed,"R3 selects stereo once");
  for(int i=0;i<20;++i) check(step() && menu.settings.cutsceneMode==1 && !menu.changed,"hold does not repeat");
  in.rightClick=false; check(step(),"release consumed");
  in.rightClick=true; step(); check(menu.settings.cutsceneMode==2,"R3 selects first person");
  in.rightClick=false; step(); in.rightClick=true; step();
  check(menu.settings.cutsceneMode==0,"R3 wraps to theater");
  in.rightClick=false; step(); in.leftClick=in.rightClick=true; step();
  check(menu.visible && menu.settings.cutsceneMode==0,"menu chord wins over cutscene cycle");
  Vr::Settings settings; settings.cutsceneMode=2;
  std::ostringstream out; settings.write(out); std::istringstream saved(out.str());
  Vr::Settings loaded; loaded.read(saved); check(loaded.cutsceneMode==2,"mode persists");
  std::istringstream invalid("CutsceneMode=99\n"); loaded.read(invalid);
  check(loaded.cutsceneMode==2,"corrupt mode is bounded");
  XrMath::Pose center{{0,.70710678f,0,.70710678f},{3,1.7f,-2}};
  const auto base=Tempest::Matrix4x4::mkIdentity();
  const auto view=XrMath::eyeView(center,center,base);
  for(int i=0;i<4;++i)for(int j=0;j<4;++j)
    check(std::abs(view[i][j]-(i==j?1.f:0.f))<.0001f,"scene starts at original camera despite prior head position/yaw");
  // A 64 mm stereo baseline remains 6.4 world cm, with world scale respected.
  auto left=center,right=center;left.position.z+=.032f;right.position.z-=.032f;
  auto l=XrMath::eyeView(left,center,base),r=XrMath::eyeView(right,center,base);
  l.inverse();r.inverse();Tempest::Vec3 lp{},rp{};l.project(lp);r.project(rp);
  check(std::abs((lp-rp).length()-6.4f)<.001f,"scene retains stereo separation");
  constexpr float gravity=.00098f;
  const float launch=Vr::jumpLaunchSpeed(gravity),apex=launch/gravity;
  check(std::abs(Vr::jumpDisplacement(launch,gravity,apex)-80.f)<.001f,"80 cm ballistic apex");
  check(std::abs(Vr::jumpDisplacement(launch,gravity,2*apex))<.001f,"returns to launch height");
  check(2*apex>700 && 2*apex<900,"jump lasts about 0.8 seconds");
  float y=0,v=launch;
  for(int i=0;i<60;++i){y+=Vr::jumpDisplacement(v,gravity,10);v-=gravity*10;}
  check(std::abs(y-Vr::jumpDisplacement(launch,gravity,600))<.001f,"trajectory independent of frame subdivision");
  std::puts("PASS: cutscene controls, persistence, stereo reference and ballistic jump");
}
