from pathlib import Path
import sys
r=Path(__file__).resolve().parents[1]
s=(r/'engine/common/game/movealgo.cpp').read_text()
a=s.index('Tempest::Vec3 MoveAlgo::npcMoveSpeed(');b=s.index('\nTempest::Vec3 MoveAlgo::npcFallSpeed',a)
body=s[a:b]
code=r'''
#include "vr/vrgroundmotion.h"
#include "vr/vrcontrols.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
using Tempest::Vec3;
enum {BS_STAND,BS_WALK,BS_RUN,BS_SPRINT,BS_SNEAK,BS_JUMP,BS_HIT};
struct Target {Vec3 position() const {return {};}};
struct Npc {
  enum {GT_No,GT_Script};
  struct {Target* npc=nullptr;Target* wp=nullptr;int flag=GT_No;Vec3 pos;} go2;
  int body=BS_RUN;bool player=true,down=false;
  int bodyStateMasked() const{return body;}
  bool isPlayer() const{return player;} bool isDown() const{return down;}
};
struct MoveAlgo {
  enum MvFlags {NoFlag,WaitMove}; enum {Run,InAir,Slide,Swim};
  Npc npc;bool vrGroundInput=true;int flags=Run;Vec3 vrStickVelocity;float mulSpeed=1;
  Vec3 pulse;
  Vec3 animMoveSpeed(uint64_t) {return pulse;}
  Vec3 go2NpcMoveSpeed(Vec3 dp,const Target&) {return dp;}
  Vec3 go2WpMoveSpeed(Vec3 dp,Vec3) {return dp;}
  Vec3 npcMoveSpeed(uint64_t,MvFlags);
};
void check(bool ok,const char* s){if(!ok){std::puts(s);std::exit(1);}}
'''+body+r'''
int main(){
  MoveAlgo m;m.vrStickVelocity=Vr::groundStickVelocity(0,-1,0,1);
  check(std::abs(m.vrStickVelocity.x-360.f)<.001f && m.vrStickVelocity.z==0,"yaw0 forward = +X");
  auto right=Vr::groundStickVelocity(1,0,0,1);
  check(right.z< -359.f && right.x==0,"yaw0 right = -Z");
  auto reverse=Vr::groundStickVelocity(0,-1,180,1);
  check(reverse.x< -359.f,"180 degree head turn reverses immediately");
  check(std::abs(Vr::groundStickVelocity(1,-1,0,1).length()-360.f)<.001f,"no diagonal boost");
  check(Vr::groundStickVelocity(0,0,0,1)==Vec3(),"neutral input stops immediately");
  float distance=0;
  for(unsigned i=0;i<100;++i) {
    m.pulse=Vec3(i%2?0.f:12.f,0,0); // alternating root animation impulses
    auto dp=m.npcMoveSpeed(10,MoveAlgo::NoFlag);
    check(std::abs(dp.x-3.6f)<.001f && dp.y==0,"constant stick motion ignores animation phase");distance+=dp.x;
  }
  check(std::abs(distance-360.f)<.001f,"one second distance");
  m.pulse=Vec3(7,0,0);m.vrStickVelocity={};
  check(m.npcMoveSpeed(16,MoveAlgo::NoFlag)==Vec3(),"stop animation cannot propel player");
  m.vrStickVelocity={360,0,0};m.mulSpeed=.5f;
  check(std::abs(m.npcMoveSpeed(100,MoveAlgo::NoFlag).x-18.f)<.001f,"script speed multiplier preserved");m.mulSpeed=1;
  m.npc.body=BS_HIT;check(m.npcMoveSpeed(10,MoveAlgo::NoFlag)==m.pulse,"combat stays native");m.npc.body=BS_RUN;
  m.flags=MoveAlgo::InAir;check(m.npcMoveSpeed(10,MoveAlgo::NoFlag)==m.pulse,"air stays native");m.flags=MoveAlgo::Run;
  m.npc.go2.flag=Npc::GT_Script;check(m.npcMoveSpeed(10,MoveAlgo::NoFlag)==m.pulse,"scripted travel stays native");m.npc.go2.flag=Npc::GT_No;
  m.npc.player=false;check(m.npcMoveSpeed(10,MoveAlgo::NoFlag)==m.pulse,"NPC animation movement unchanged");m.npc.player=true;
  m.vrGroundInput=false;check(m.npcMoveSpeed(10,MoveAlgo::NoFlag)==m.pulse,"lost input ownership cannot reuse stick velocity");
  Vr::Menu menu;menu.page=Vr::Menu::Page::Performance;
  check(std::string_view(menu.title())=="GOTHIC II VR - GRAPHICS","graphics title");
  const auto rows=menu.rows();check(rows.size()==11,"compact player graphics menu");
  for(auto row:rows) check(row!=Vr::Menu::RenderTests && row!=Vr::Menu::GpuSampling && row!=Vr::Menu::Power && row!=Vr::Menu::EarlyLeftEye && row!=Vr::Menu::DirectOutput,"no diagnostic controls exposed");
  menu.settings.sanitize();check(menu.settings.profilerView==1 && menu.settings.gpuSampling==0,"simple FPS without GPU probes");
  std::puts("PASS production ground-motion dispatch, animation independence, script/air gates and Graphics menu");
}
'''
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
(out/'ground-motion.cpp').write_text(code)
