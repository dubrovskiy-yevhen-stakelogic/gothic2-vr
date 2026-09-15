"""Run the production melee sweep on short and long weapon fixtures."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
s=(root/'engine/common/vr/vrgameplay.cpp').read_text()
a=s.index('    if(holding.slot==0 && allowed) {');b=s.index('    if(allowed && item->isCrossbow()',a)
melee=s[a:b]
fixture=r'''
#include "vr/vrcombatmath.h"
#include <cstdio>
#include <cstdlib>
using namespace Vr;
namespace Tempest {struct Log{template<class...T>static void i(T...){}};}
constexpr int ATR_HITPOINTS=0;
struct Npc {int hp=100,contacts=0;bool down=false;bool isDown()const{return down;}int attribute(int)const{return hp;}
 void takeDamage(Npc&,const void*){++contacts;hp-=10;}void setGuardVr(Guard){}
};
struct Item{bool twoHanded=false;int swordLength()const{return 140;}const char* displayName()const{return "test blade";}bool is2H()const{return twoHanded;}
 struct Data{const char* visual="test";};Data handle(){return {};}
};
struct Mesh{Vec3 low{-1,-1,0},high{1,1,140};std::array<Vec3,2> bounds;const Vec3* bbox(){bounds={low,high};return bounds.data();}};
struct Resources{static inline Mesh mesh;static Mesh* loadMesh(const char*){return &mesh;}};
struct Physics{Npc target;Vec3 center{0,0,110};struct Hit{Npc* npcHit=nullptr;};
 Hit rayNpcMelee(Vec3 from,Vec3 to,Npc*,float radius=3.f){float t=0;return {meleeContact(from,to,center,Vec3(20,85,20)+radius,0,t)?&target:nullptr};}
};
struct World{Physics physics;bool blocked=false;Physics* physic(){return &physics;}uint64_t tickCount(){return 100;}};
bool clearPath(World& world,Vec3,Vec3){return !world.blocked;}
struct Xr{bool tracked=true;float grip=1;bool gripTracked(uint32_t){return tracked;}float gripValue(uint32_t){return grip;}Vec3 trackingPoint(uint32_t,const Matrix&,Vec3 point){return point/100.f;}void haptic(uint32_t,float,float){}};
struct Fixture{
 World owner;World* world=&owner;Npc hero;Npc* player=&hero;Xr xr;Item weapon;Item* item=&weapon;
 struct Held{int slot=0;size_t id=1;Swing swing;uint64_t strikeUntil=0;Vec3 previousBase,previousTip;bool insideTarget=false;} holding;
 struct Hand{Matrix grip=Matrix::mkIdentity();}h;
 struct Settings{bool physicalCombat=true;float swingSpeed=2.2f;} settings;
 int swordMain=1,supportHand=0;uint64_t twoHandNoticeAfter=0;int notices=0;void message(const char*,uint64_t){++notices;}
 BodyFrame body;bool allowed=true;float units=100;uint64_t now=100;int i=1;
 void rememberTarget(Npc*,uint64_t){}
 ItemCalibration profile(size_t,int,const Settings&){return {};}
 Fixture(){body.update(Matrix::mkIdentity());}
 void tick(float x,float angle=0){now+=20;Vec3 position{x,0,0},forward{0,0,1},up{0,1,0};auto calibrated=itemPose(position,forward,up,{},units);h.grip=calibrated;calibrated.rotateOY(angle);
'''+melee+r'''
 }
};
int checks=0;void test(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
int main(){
 Resources::mesh.high.z=140;
 for(int reason=0;reason<4;++reason) {
   Fixture one;one.weapon.twoHanded=true;
   if(reason==0)one.supportHand=-1;
   if(reason==1)one.swordMain=0;
   if(reason==2)one.xr.tracked=false;
   if(reason==3)one.xr.grip=.1f;
   for(float x=-100;x<=100;x+=5)one.tick(x);
   test(one.owner.physics.target.contacts==0 && one.notices==1,"two-handed damage requires attached, tracked, squeezed support on the same weapon");
   for(float x=95;x>=-100;x-=5)one.tick(x);
   test(one.owner.physics.target.contacts==0 && one.notices==1,"invalid-grip contact cannot deal damage or spam notices");
 }
 Fixture regain;regain.weapon.twoHanded=true;regain.supportHand=-1;
 for(float x=-100;x<=0;x+=5)regain.tick(x);
 regain.supportHand=0;for(float x=5;x<=100;x+=5)regain.tick(x);
 test(regain.owner.physics.target.contacts==0,"grabbing support while inserted does not damage on extraction");
 for(float x=95;x>=-100;x-=5)regain.tick(x);
 test(regain.owner.physics.target.contacts==1,"valid two-hand reentry restores normal melee damage");
 for(bool twoHanded:{false,true}){
  Resources::mesh.high.z=twoHanded?200.f:40.f;Fixture f;f.weapon.twoHanded=twoHanded;f.owner.physics.center.z=twoHanded?170.f:25.f;
  for(float x=-100;x<=100;x+=5)f.tick(x);
  test(f.owner.physics.target.contacts==1,"production short/long blade damages on contact late in the stroke");
  test(f.owner.physics.target.hp==90,"one damage application per follow-through");
  for(float x=95;x>=-100;x-=5)f.tick(x);
  test(f.owner.physics.target.contacts==2,"production return stroke hits without a stationary hold");
  for(int i=0;i<30;++i)f.tick(0);
  test(f.owner.physics.target.contacts==2,"stationary blade resting inside target cannot deal damage");
  Fixture extraction;extraction.owner.physics.center.z=f.owner.physics.center.z;
  for(float x=-100;x<=0;x+=5)extraction.tick(x);
  test(extraction.owner.physics.target.contacts==1,"first entry is registered before withdrawing the weapon");
  for(int pause=0;pause<30;++pause)extraction.tick(0);
  for(float x=-5;x>=-100;x-=5)extraction.tick(x);
  test(extraction.owner.physics.target.contacts==1,"withdrawing after a pause cannot damage or react twice");
  for(float x=-95;x<=0;x+=5)extraction.tick(x);
  test(extraction.owner.physics.target.contacts==2,"a new strike after full withdrawal can damage again");
  Fixture inserted;inserted.owner.physics.center.z=f.owner.physics.center.z;
  for(float x=-100;x<=0;x+=1)inserted.tick(x);
  test(inserted.owner.physics.target.contacts==0,"slow insertion is below the physical swing threshold");
  for(float x=-5;x>=-100;x-=5)inserted.tick(x);
  test(inserted.owner.physics.target.contacts==0,"speed reached only on withdrawal cannot create a delayed first hit");
  Fixture jitter;jitter.owner.physics.center.z=f.owner.physics.center.z;
  for(float x=-100;x<=-20;x+=5)jitter.tick(x);
  for(int frame=0;frame<100;++frame)jitter.tick(frame%2?-21.f:-25.f);
  test(jitter.owner.physics.target.contacts==1,"edge jitter must clear the contact margin before a new entry");
  Fixture chase;chase.owner.physics.center.z=f.owner.physics.center.z;
  for(float x=-100;x<=0;x+=5)chase.tick(x);
  test(chase.owner.physics.target.contacts==1,"first contact precedes the NPC's movement");
  for(int frame=1;frame<=10;++frame){chase.owner.physics.center.x=float(frame)*10;chase.tick(float(frame)*5);}
  test(!chase.holding.insideTarget && chase.owner.physics.target.contacts==1,"running opponent clears the blade without withdrawal damage");
  for(float x=55;x<=150;x+=5)chase.tick(x);
  test(chase.owner.physics.target.contacts==2,"fresh entry against moving NPC does not require reversing or settling the previous stroke");
  Fixture quick;quick.owner.physics.center.z=f.owner.physics.center.z;
  for(float x=-40;x<=40;x+=10)quick.tick(x);
  test(quick.owner.physics.target.contacts==1 && !quick.holding.insideTarget,"fast pass fully clears target before the second slash");
  quick.tick(30);quick.tick(20);
  test(quick.owner.physics.target.contacts==2,"new clean entry is not rejected by the earlier stroke's 280 ms timer");
  Fixture stationary;stationary.owner.physics.center.z=f.owner.physics.center.z;
  stationary.tick(0);stationary.owner.physics.center.x=-100;
  for(float x=-100;x<=100;x+=10){stationary.owner.physics.center.x=x;stationary.tick(0);}
  test(stationary.owner.physics.target.contacts==0,"NPC movement alone cannot turn a stationary weapon into an attack");
  Fixture wall;wall.owner.blocked=true;wall.owner.physics.center.z=f.owner.physics.center.z;
  for(float x=-100;x<=100;x+=5)wall.tick(x);
  test(wall.owner.physics.target.contacts==0,"occluded tracked weapon cannot hit through a wall");
  Fixture disabled;disabled.settings.physicalCombat=false;disabled.owner.physics.center.z=f.owner.physics.center.z;
  for(float x=-100;x<=100;x+=5)disabled.tick(x);
  test(disabled.owner.physics.target.contacts==0,"disabled physical combat cannot generate native damage");
 }
 Resources::mesh.high.z=200;Fixture fast;fast.owner.physics.center={0,0,170};
 fast.tick(0,-20);fast.tick(0,20);
 test(fast.holding.swing.speed>24 && fast.owner.physics.target.contacts==1,"fast rotating long blade strikes on first pass with a continuous controller");
 Fixture lost;lost.owner.physics.center={0,0,170};lost.tick(-100);lost.tick(0);
 test(!lost.holding.swing.continuous && lost.owner.physics.target.contacts==0,"a one-metre controller discontinuity cannot strike");
 std::printf("VR 053 melee sweeps: %d production checks passed\n",checks);
}
'''
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True);(out/'regressions.cpp').write_text(fixture)
print('Extracted production short and long blade sweep')
