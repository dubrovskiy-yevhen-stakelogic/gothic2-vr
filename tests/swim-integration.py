"""Exercise the production Gothic water/collision adapter without retail assets."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
def method(signature):
    source=(root/'engine/common/game/movealgo.cpp').read_text()
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
fixture=r'''
#include "vr/vrswimming.h"
#include <Tempest/Matrix4x4>
#include <Tempest/Vec>
#include <cstdio>
#include <cstdlib>
using namespace Tempest;
namespace Tempest {struct Log {template<class... A> static void i(A...) {}};}
int checks=0;
void check(bool value,const char* label){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
struct DynamicWorld {struct CollisionTest {};};
struct Npc {
  struct Body {float radiusXZ()const{return 20;}} physic;
  struct World {
    struct Script {struct Guild {float water_depth_chest[1]={100};} values;Guild& guildVal(){return values;}const Guild& guildVal()const{return values;}} data;
    Script& script(){return data;}
    const Script& script()const{return data;}
    uint64_t tickCount()const{return 0;}
  } owner;
  World& world(){return owner;}
  const World& world()const{return owner;}
  int guild()const{return 0;}
  bool player=true;
  bool isPlayer()const{return player;}
  bool hasSwimAnimations()const{return true;}
  void setPosition(Vec3 p){pos=p;}
  bool tryTranslate(Vec3 p){pos=p;return true;}
  Vec3 pos{0,0,0};float pitch=0;
  Vec3 position()const{return pos;}
  void setDirectionY(float v){pitch=v;}
};
struct MoveAlgo {
  enum State{Run,Swim,Dive,InWater,InAir,Slide};State state=Swim;
  Vr::Swimming vrSwim;Npc npc;float water=140,ground=-500;
  float& chest=npc.owner.data.values.water_depth_chest[0];
  Vec3 fallSpeed;
  uint64_t vrSwimReportTime=0;
  const char* vrWaterReason="native";
  bool wall=false,ceiling=false,groundValid=true;float bank=-500,bankStart=0,slope=1;int collisions=0,dives=0;static constexpr float eps=2;
  bool vrSwimming()const{return vrSwim.input.enabled && (state==Swim || state==Dive);}
  bool isDive()const{return state==Dive;}
  bool isSwim()const{return state==Swim;}
  void setState(State s){if(s==Dive && state!=Dive)++dives;state=s;}
  float waterRay(Vec3)const{return water;}
  float dropRay(Vec3 p,bool& valid)const{valid=groundValid;return p.z>bankStart && bank>-500 ? bank : ground;}
  Vec3 normalRay(Vec3)const{return {0,slope,0};}
  float slideAngle()const{return .7f;}
  float stepHeight()const{return 50;}
  void clearSpeed(){}
  float waterDepthChest()const;
  bool canFlyOverWater()const{return false;}
  bool testSlide(Vec3,Vec3,DynamicWorld::CollisionTest&){return false;}
  void onMoveFailed(Vec3,DynamicWorld::CollisionTest&,uint64_t){}
  void emitWaterSplash(float){}
  uint64_t diveStart=0;
  bool tryMove(Vec3 dp,DynamicWorld::CollisionTest&){
    ++collisions;
    if(ceiling && dp.y>0)return false;
    if(wall && (dp.x!=0 || dp.z!=0))return false;
    if(dp.z!=0 && groundValid && npc.pos.y+dp.y<ground)return false;
    if(bank>-500 && npc.pos.z+dp.z+npc.physic.radiusXZ()>bankStart && dp.z>0 && npc.pos.y+dp.y<bank)return false;
    npc.pos+=dp;return true;
  }
  bool tryMove(float x,float y,float z){DynamicWorld::CollisionTest c;return tryMove(Vec3(x,y,z),c);}
  bool tryMove(float x,float y,float z,DynamicWorld::CollisionTest& c){return tryMove(Vec3(x,y,z),c);}
  bool tickVrSwim(uint64_t dt);
  bool vrSwimCanClimb()const;
  void setVrSwimInput(const Vr::SwimInput&,float);
  bool nativeWaterTick(uint64_t dt);
};
'''
fixture+=method('bool MoveAlgo::vrSwimCanClimb() const')+'\n'+method('bool MoveAlgo::tickVrSwim(uint64_t dt)')
fixture+='\n'+method('float MoveAlgo::waterDepthChest() const')+'\n'+method('void MoveAlgo::setVrSwimInput(')
native=(root/'engine/common/game/movealgo.cpp').read_text()
native=native[native.index('  // water interaction'):native.index('  // above ground/void')]
fixture+='''
bool MoveAlgo::nativeWaterTick(uint64_t dt) {
 const auto state=this->state;const bool dead=false,grav=false,swim=isSwim(),dive=isDive();
 const auto pos=npc.position(),pos0=pos;Vec3 dp,normal;DynamicWorld::CollisionTest info;
 const float water=this->water,ground=this->ground,chest=waterDepthChest(),knee=50;
'''+native+'\n return false;\n}\n'
npc_source=(root/'engine/common/world/objects/npc.cpp').read_text()
gate=npc_source[npc_source.index('Npc::JumpStatus Npc::tryJump()'):].splitlines()
gate=next(line.strip() for line in gate if 'if(isSlide()' in line)
fixture+='''
struct JumpGate {
 bool slide=false,swim=false,dive=false,allowed=false;
 bool isSlide()const{return slide;}bool isSwim()const{return swim;}bool isDive()const{return dive;}
 bool vrSwimCanClimb()const{return allowed;}
 bool blocked()const{
'''+gate+''' return true; } return false; }
};
'''
fixture+=r'''
Vr::SwimInput sample(){Vr::SwimInput in;in.enabled=true;in.valid[0]=in.valid[1]=true;in.forward={0,0,-1};in.direction={0,0,1};return in;}
void stroke(MoveAlgo& m,Vr::SwimInput in){m.vrSwim.sample(in,.016f);for(int i=1;i<=30;++i){in.relative[0].z=in.relative[1].z=i*.02f;m.vrSwim.sample(in,.016f);}}
int main(){
  JumpGate gate;gate.swim=true;check(gate.blocked(),"flat swimming keeps native jump gate");
  gate.allowed=true;check(!gate.blocked(),"VR swimmer at waterline reaches native collision probe");
  gate.swim=false;gate.dive=true;gate.allowed=false;check(gate.blocked(),"deep VR dive keeps native jump gate");
  gate.allowed=true;check(!gate.blocked(),"just-submerged head still allows a reachable ledge");
  gate.slide=true;check(gate.blocked(),"VR cannot bypass native slide gate");
  MoveAlgo m;auto in=sample();m.vrSwim.sample(in,.016f);
  check(m.vrSwimCanClimb(),"ledge probe allowed near surface");
  m.npc.pos.y=-100;check(!m.vrSwimCanClimb(),"deep underwater cannot launch a climb");m.npc.pos.y=0;
  for(int i=0;i<240;++i){m.vrSwim.sample(in,.016f);m.tickVrSwim(16);}
  check(m.npc.pos.y<-40 && m.npc.pos.y>-90,"same slow sinking as GTA when hands are still");
  check(m.state==MoveAlgo::Dive && m.dives==1,"mouth depth starts native dive clock once");
  in.direction={0,.8f,.6f};stroke(m,in);m.tickVrSwim(16);
  check(m.npc.pitch>40,"upward gaze steers pitch upwards");
  m.npc.pos.y=0;in.eyeHeight=160;m.vrSwim.sample(in,.016f);m.tickVrSwim(16);
  check(m.state==MoveAlgo::Swim,"mouth above surface exits native dive and restores breath");
  check(m.npc.pos.y<=0,"stroke cannot launch above waterline");
  m.npc.pos.y=-50;in.eyeHeight=90;m.vrSwim.sample(in,.016f);m.tickVrSwim(16);
  check(m.state==MoveAlgo::Dive,"tracked head height affects submersion");
  MoveAlgo wall;wall.wall=true;auto up=sample();up.direction={0,.8f,.6f};wall.npc.pos.y=-100;stroke(wall,up);
  wall.tickVrSwim(16);check(wall.npc.pos.z==0 && wall.npc.pos.y>-100 && wall.collisions==2,"collision stops travel but permits upward treading along wall");
  MoveAlgo floor;floor.npc.pos.y=floor.ground;floor.vrSwim.sample(sample(),.016f);floor.tickVrSwim(16);
  check(floor.npc.pos.y>=floor.ground,"Gothic lake floor bounds downward movement");
  MoveAlgo shore;shore.ground=60;shore.vrSwim.sample(sample(),.016f);shore.tickVrSwim(16);
  check(shore.state==MoveAlgo::Run && !shore.vrSwimming(),"shallow bank returns ownership to native walking");
  check(shore.npc.pos.y==shore.ground && shore.tryMove(0,0,5),"exit places feet on ground before first walking collision check");
  MoveAlgo ceiling;ceiling.ground=60;ceiling.ceiling=true;ceiling.vrSwim.sample(sample(),.016f);ceiling.tickVrSwim(16);
  check(ceiling.state!=MoveAlgo::Run && ceiling.npc.pos.y==0,"blocked grounding does not teleport through a ceiling");
  MoveAlgo slope;slope.bank=20;slope.bankStart=20;stroke(slope,sample());slope.tickVrSwim(16);
  check(slope.npc.pos.z>0 && slope.npc.pos.y>=20 && slope.state!=MoveAlgo::Run,"body front follows submerged slope before centre reaches wading depth");
  slope.bank=45;slope.bankStart=slope.npc.pos.z+20;stroke(slope,sample());slope.tickVrSwim(16);
  check(slope.npc.pos.z>0 && slope.npc.pos.y>=45 && slope.state!=MoveAlgo::Run,"raised body over deeper centre does not prematurely toggle walking");
  slope.ground=45;slope.tickVrSwim(16);
  check(slope.state==MoveAlgo::Run && slope.tryMove(0,0,5),"continuous shore approach ends with usable walking");
  MoveAlgo air;air.groundValid=false;air.npc.pos.y=150;air.vrSwim.sample(sample(),.016f);air.tickVrSwim(16);
  check(air.state==MoveAlgo::Run && !air.vrSwimming(),"water below feet cannot retain swim even without a ground hit");
  MoveAlgo step;step.bank=45;stroke(step,sample());step.tickVrSwim(16);
  check(step.npc.pos.z>0 && step.npc.pos.y==45 && step.state==MoveAlgo::Run,"stroke steps onto reachable shallow bank and releases walking");
  MoveAlgo cliff;cliff.bank=70;stroke(cliff,sample());cliff.tickVrSwim(16);
  check(cliff.npc.pos.z==0 && cliff.state!=MoveAlgo::Run,"high bank still requires native climb, not teleport");
  MoveAlgo steep;steep.bank=45;steep.slope=.2f;stroke(steep,sample());steep.tickVrSwim(16);
  check(steep.npc.pos.z==0 && steep.state!=MoveAlgo::Run,"steep bank cannot be stepped through");
  MoveAlgo boundary;boundary.water=100;boundary.ground=0;auto shortHead=sample();shortHead.eyeHeight=110;boundary.vrSwim.sample(shortHead,.016f);boundary.tickVrSwim(16);
  check(boundary.state!=MoveAlgo::Run,"exact native swim threshold does not oscillate between walking and swimming");
  MoveAlgo tall;tall.chest=180;tall.vrSwim.sample(sample(),.016f);tall.tickVrSwim(16);
  check(tall.state!=MoveAlgo::Run,"calibrated VR surface height does not falsely release a swimmer over deep water");
  MoveAlgo wade;wade.water=130;wade.ground=0;wade.setVrSwimInput(sample(),.016f);wade.tickVrSwim(16);
  check(wade.state==MoveAlgo::Run,"standing with mouth above water enables walking despite native animation depth");
  for(int i=0;i<120;++i){
    if(i%3==0)wade.setVrSwimInput(sample(),.016f);
    wade.nativeWaterTick(16);
    check(wade.state==MoveAlgo::Run,"native water entry cannot recapture VR wading between controller polls");
  }
  check(wade.tryMove(0,0,5),"wading accepts movement after repeated native updates");
  MoveAlgo deep;deep.water=160;deep.ground=0;deep.state=MoveAlgo::Run;deep.setVrSwimInput(sample(),.016f);deep.nativeWaterTick(16);
  check(deep.state==MoveAlgo::Swim && deep.vrSwimming(),"deepening water enters physical swimming without waiting for next input poll");
  wade.setVrSwimInput({},0);check(wade.waterDepthChest()==100,"focus loss releases VR depth policy");
  MoveAlgo flat;flat.water=130;flat.ground=0;flat.state=MoveAlgo::Run;flat.nativeWaterTick(16);
  check(flat.state==MoveAlgo::Swim,"flat mode retains native water depth");
  MoveAlgo npc;npc.npc.player=false;npc.setVrSwimInput(sample(),.016f);check(npc.waterDepthChest()==100,"NPC water depth remains scripted");
  MoveAlgo dry;dry.state=MoveAlgo::Run;auto hands=sample();
  for(int i=0;i<30;++i){hands.relative[0].z=hands.relative[1].z=i*.02f;dry.setVrSwimInput(hands,.016f);}
  dry.state=MoveAlgo::Swim;dry.tickVrSwim(16);check(dry.npc.pos.z==0,"gestures on land cannot accumulate a launch impulse for water entry");
  MoveAlgo stale;stroke(stale,sample());stale.tickVrSwim(1000);check(stale.npc.pos==Vec3(),"long simulation frame cannot teleport swimmer");
  stale.vrSwim.sample({},0);check(!stale.vrSwimming(),"focus/menu reset releases swim control");
  std::printf("PASS: %d Gothic swim integration checks\n",checks);
}
'''
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
(out/'swim-integration.cpp').write_text(fixture,encoding='utf-8')
