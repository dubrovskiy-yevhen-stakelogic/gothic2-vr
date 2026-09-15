"""Compile actual native damage/drop adapters against deterministic world boundaries."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
source=(root/'engine/common/world/objects/npc.cpp').read_text(encoding='utf-8')
def method(signature):
    start=source.index(signature);at=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    return source[start:at]
fixture=r'''
#include "vr/vrcombatmath.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
using namespace Tempest;
struct Log{template<class...T> static void i(T...){}};
class Npc;
enum CollideMask{COLL_DOEVERYTHING};constexpr int PERC_ASSESSFIGHTSOUND=1;
struct Bullet{bool isSpell()const{return false;}};
struct Item{size_t id=7,quantity=1;Vec3 velocity;size_t clsId()const{return id;}void setCount(size_t n){quantity=n;}void setVelocityVr(Vec3 v){velocity=v;}};
struct Pose{bool defending=false,jumping=false;bool isJumpBack(uint64_t)const{return jumping;}bool isDefence(uint64_t)const{return defending;}};
struct Visual{Pose p;int effects=0;const Pose& pose(){return p;}void emitBlockEffect(Npc&,Npc&){++effects;}};
struct Inventory{Item weapon;bool armed=true;Item* activeWeapon(){return armed?&weapon:nullptr;}void delItem(size_t,size_t n,Npc&){weapon.quantity-=n;}};
struct World{uint64_t now=900;bool allocation=true;int created=0;Item dropped;
  uint64_t tickCount()const{return now;}int script(){return 0;}void sendPassivePerc(Npc&,Npc&,Npc&,int){}
  Item* addItemDyn(size_t,const Matrix4x4&,size_t){if(!allocation)return nullptr;++created;return &dropped;}
};
struct Fight{bool inRange=true;bool isInAttackRange(Npc&,Npc&,int){return inRange;}bool isInFocusAngle(Npc&,Npc&){return true;}bool isInJumpBackAngle(Npc&,Npc&){return true;}};
struct Handle{size_t symbol_index(){return 4;}};
class Npc{public:
  bool player=false,down=false,vrPhysicalMelee=false;Vr::Guard vrGuard;unsigned vrParryFeedback=0;
  Npc* currentTarget=nullptr;Npc* lastHit=nullptr;World owner;Fight fghAlgo;Visual visual;Inventory invent;Handle h;Handle* hnpc=&h;
  int damageCalls=0,closed=0;Vec3 pos;
  bool isPlayer()const{return player;}bool isDown()const{return down;}bool isMonster()const{return false;}
  const char* displayName(){return "target";}Inventory& inventory(){return invent;}void setOther(Npc*){}Vec3 position()const{return pos;}
  Item* activeWeapon(){return invent.activeWeapon();}Item* getItem(size_t id){return id==7 && invent.weapon.quantity?&invent.weapon:nullptr;}
  size_t itemCount(size_t id){auto p=getItem(id);return p?p->quantity:0;}void closeWeapon(bool){++closed;invent.armed=false;}
  void commitDamage();void takeDamage(Npc&,const Bullet*,int32_t=2147483647);
  void takeDamage(Npc&,const Bullet*,CollideMask,int,bool,int32_t){++damageCalls;}
  Item* dropItemVr(size_t,const Matrix4x4&,const Vec3&);
};
'''
for signature in ['void Npc::commitDamage()', 'void Npc::takeDamage(Npc &other, const Bullet* b, int32_t meleeCap)', 'Item* Npc::dropItemVr(']:
    fixture+=method(signature)+'\n'
xr_source=(root/'engine/common/vr/questxr.cpp').read_text(encoding='utf-8')
fixture+=r'''
#include "vr/xrmath.h"
struct QuestXr {std::array<XrMath::Pose,2> gripPoses;float unitsPerMeter=100;
  static XrMath::Pose pose(const XrMath::Pose& p){return p;}
  Vec3 trackingPoint(uint32_t,const Matrix4x4&,const Vec3&)const;
  Vec3 trackingVector(uint32_t,const Matrix4x4&,const Vec3&)const;
};
'''
source=xr_source
for signature in ['Tempest::Vec3 QuestXr::trackingPoint(', 'Tempest::Vec3 QuestXr::trackingVector(']:fixture+=method(signature)+'\n'
fixture+=r'''
int checks=0;void test(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
int main(){

  for(float units:{50.f,100.f,200.f})for(float yaw:{0.f,120.f}) {
    QuestXr xr;xr.unitsPerMeter=units;xr.gripPoses[1]={{0,.258819f,0,.9659258f},{.3f,1.2f,-.5f}};
    auto camera=Matrix4x4::mkIdentity();camera.translate(2000,70,-1200);camera.rotateOY(yaw);
    auto c=Matrix4x4::mkIdentity();c.scale(1,-1,-1);
    const auto hand=camera*c*XrMath::poseMatrix(xr.gripPoses[1],units)*c;
    auto worldTip=Vec3(.02f,-.1f,.3f)*units;hand.project(worldTip);
    auto expected=Vec3(.02f,.1f,-.3f);XrMath::poseMatrix(xr.gripPoses[1],1).project(expected);
    test((xr.trackingPoint(1,hand,worldTip)-expected).length()<.0001f,"production raw tip transform rejects camera changes across all world scales");
    const auto velocity=Vec3(1,.2f,-.3f),worldVelocity=xr.trackingVector(1,hand,velocity);
    const auto recovered=xr.trackingPoint(1,hand,Vr::origin(hand)+worldVelocity)-xr.gripPoses[1].position;
    test((recovered-velocity).length()<.0001f,"production throw vector preserves controller velocity through camera rotation and world scale");
  }
  Npc attacker,target;attacker.player=true;attacker.currentTarget=&target;attacker.commitDamage();
  test(target.damageCalls==1,"ordinary animation hit retains native damage");
  attacker.vrPhysicalMelee=true;attacker.commitDamage();test(target.damageCalls==1,"VR player animation cannot duplicate physical sweep damage");
  target.takeDamage(attacker,nullptr);test(target.damageCalls==2,"physical contact still invokes native damage");
  attacker.player=false;attacker.commitDamage();test(target.damageCalls==3,"NPC animation attacks remain active");
  target.visual.p.jumping=true;attacker.player=true;
  target.takeDamage(attacker,nullptr);test(target.damageCalls==4,"actual VR blade contact is not cancelled by jump-back animation immunity");
  attacker.vrPhysicalMelee=false;target.takeDamage(attacker,nullptr);test(target.damageCalls==4,"ordinary animation attack retains native jump-back dodge");
  attacker.vrPhysicalMelee=true;target.visual.p.jumping=false;target.visual.p.defending=true;
  target.takeDamage(attacker,nullptr);test(target.damageCalls==4,"VR blade still respects an actual NPC defensive pose");
  Npc defender,enemy;defender.player=true;defender.vrPhysicalMelee=true;enemy.pos={0,0,150};
  defender.vrGuard={{-30,140,40},{40,140,40},{0,170,0},{0,0,1},100,1000,1};
  defender.takeDamage(enemy,nullptr);test(defender.damageCalls==0 && defender.visual.effects==1 && defender.vrParryFeedback==2,"physical parry uses native block and feedback without damage");
  Bullet arrow;defender.takeDamage(enemy,&arrow);test(defender.damageCalls==1,"crosswise melee parry does not stop arrows");
  enemy.pos.z=-150;defender.takeDamage(enemy,nullptr);test(defender.damageCalls==2,"back melee bypasses physical guard");
  enemy.pos.z=150;defender.owner.now=1001;defender.takeDamage(enemy,nullptr);test(defender.damageCalls==3,"expired tracked pose does not keep invulnerability");
  defender.owner.now=900;defender.vrPhysicalMelee=false;defender.takeDamage(enemy,nullptr);test(defender.damageCalls==4,"physical combat toggle disables gesture parry");
  defender.vrPhysicalMelee=true;defender.invent.armed=false;defender.takeDamage(enemy,nullptr);test(defender.damageCalls==5,"stowed weapon cannot parry using stale pose");
  Npc dropping;dropping.invent.weapon.quantity=2;auto matrix=Matrix4x4::mkIdentity();
  auto item=dropping.dropItemVr(7,matrix,{100,200,0});
  test(item && item->quantity==1 && dropping.invent.weapon.quantity==1 && dropping.owner.created==1,"release transfers exactly one inventory item to world");
  test(dropping.closed==1 && item->velocity==Vec3(100,200,0),"release closes native weapon and preserves launch velocity");
  dropping.owner.allocation=false;test(!dropping.dropItemVr(7,matrix,{}) && dropping.invent.weapon.quantity==1,"failed world allocation keeps inventory ownership");
  test(!dropping.dropItemVr(8,matrix,{}) && dropping.owner.created==1,"unknown item cannot spawn a duplicate");
  test(!dropping.dropItemVr(7,matrix,{NAN,0,0}) && dropping.invent.weapon.quantity==1,"invalid release velocity cannot lose item");
  std::printf("VR combat native: %d production checks passed\n",checks);
}
'''
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
(out/'combat.cpp').write_text(fixture,encoding='utf-8')
print('Extracted production melee damage, parry and world drop adapters')
