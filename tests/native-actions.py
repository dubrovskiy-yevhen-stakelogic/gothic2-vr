"""Test production bow/potion adapters with deterministic inventory and script callbacks."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
source=(root/'engine/common/world/objects/npc.cpp').read_text()
def method(signature):
    start=source.index(signature);at=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    return source[start:at]
fixture=r'''
#include <Tempest/Matrix4x4>
#include "vr/vrbowmath.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
using namespace Tempest;
constexpr int ITM_CAT_NF=1,ITM_CAT_FF=4,ITM_CAT_POTION=128;
enum class WeaponState {None,Fist,W1H,W2H,Bow,CBow};
class Npc;
struct Item {
  static constexpr uint8_t NSLOT=255;
  struct Data {int munition=2;int on_state[1]={1};} data;
  size_t id=0,quantity=0;int flag=0;bool crossbow=false,twoHanded=false,equipped=false,allowed=true;
  int mainFlag() const{return flag;}bool isCrossbow() const{return crossbow;}size_t count() const{return quantity;}
  size_t clsId()const{return id;}const Data& handle()const{return data;}bool checkCond(Npc&)const{return allowed;}bool is2H()const{return twoHanded;}bool isEquipped()const{return equipped;}
};
struct Inventory {
  bool armed=true;void switchActiveWeapon(Npc&,uint8_t slot){armed=slot!=Item::NSLOT;}
  Item bow{{},1,1,ITM_CAT_FF},ammo{{},2,3,0},potion{{},3,3,ITM_CAT_POTION};int deletes=0;size_t current=0;Item melee{{},4,1,ITM_CAT_NF};Item* selected=&bow;bool equipFails=false;
  Item* activeWeapon(){return armed?selected:nullptr;}Item* getItem(size_t id){return id==1?&bow:id==2?&ammo:id==3?&potion:id==4?&melee:nullptr;}
  bool equip(size_t id,Npc& n,bool force){auto item=getItem(id);if(!item || equipFails || (!force && !item->checkCond(n)))return false;selected=item;item->equipped=true;return true;}
  size_t itemCount(size_t id){auto i=getItem(id);return i?i->quantity:0;}
  void delItem(size_t id,size_t count,Npc&){auto i=getItem(id);if(i)i->quantity-=count;++deletes;}
  void setCurrentItem(size_t id){current=id;}
};
struct Bullet {Vec3 position,direction;Npc* origin=nullptr;float chance=0;int damage=0;
  void setPosition(Vec3 p){position=p;}void setDirection(Vec3 p){direction=p;}void setOrigin(Npc* n){origin=n;}
  float visualScale=1;void setVisualScale(float s){visualScale=s;}
  void setDamage(int d){damage=d;}void setHitChance(float f){chance=f;}
  bool qualified=true;void setWeaponQualifiedVr(bool q){qualified=q;}
};
struct Script {bool consume=false;void invokeItem(Npc*,uint32_t);};
struct World {Bullet bullet;Script vm;int shots=0;
  Bullet& shootBullet(Item&,Npc&,void*,void*){++shots;return bullet;}Script& script(){return vm;}
};
struct DynamicWorld {static constexpr float bulletSpeed=3;};
struct DamageCalculator {static int rangeDamageValue(Npc&){return 7;}};
struct Visual {WeaponState state=WeaponState::None;bool setToFightMode(WeaponState s){bool changed=state!=s;state=s;return changed;}};
class Npc {public:Inventory invent;World owner;int health=10;Visual visual;int skeletonUpdates=0;
  struct Handle{int weapon=0;} data;Handle* hnpc=&data;
  void updateWeaponSkeleton(){++skeletonUpdates;}void readyFistsVr();bool isDown(){return false;}bool activeWeaponQualifiedVr()const{return true;}
  bool shootVr(const Vec3&,const Vec3&,float power=1.f,float visualScale=1.f);bool drinkVr(size_t);bool equipVr(size_t,bool ignoreRequirements=false);
};
void Script::invokeItem(Npc* npc,uint32_t) {npc->health+=25;if(consume)npc->invent.delItem(npc->invent.current,1,*npc);}
int checks=0;
void test(bool ok,const char* label) {++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
'''
fixture+=method('bool Npc::equipVr(')+method('bool Npc::shootVr(')+method('bool Npc::drinkVr(')+method('void Npc::readyFistsVr(')+r'''
int main() {
  Npc n;test(n.shootVr({10,20,30},{0,0,1}),"tracked arrow fires");
  test(n.owner.bullet.position==Vec3(10,20,30),"arrow starts at physical bow");
  test(n.owner.bullet.direction==Vec3(0,0,3),"direction uses native centimetres per millisecond speed");
  test(n.invent.ammo.quantity==2 && n.owner.bullet.origin==&n && n.owner.bullet.damage==7,"arrow consumes one round and retains shooter damage");
  test(!n.shootVr({},{0,0,0}) && n.invent.ammo.quantity==2,"invalid aim cannot consume ammo");
  test(n.shootVr({},{8,0,0},1.f,.3f) && n.owner.bullet.direction==Vec3(3,0,0) && n.owner.bullet.visualScale==.3f,"unnormalized aim cannot accelerate arrow");

  const auto beforeShort=n.invent.ammo.quantity;
  test(n.shootVr({},{0,0,1},0.f) && n.owner.bullet.direction.z<1.1f && n.invent.ammo.quantity==beforeShort-1,"short draw changes real projectile speed and consumes one arrow");
  n.invent.ammo.quantity=1;test(!n.shootVr({},{0,0,1},NAN) && n.invent.ammo.quantity==1,"invalid bow power cannot consume ammunition");
  n.invent.ammo.quantity=0;test(!n.shootVr({},{1,0,0}),"empty quiver cannot fire");
  test(n.drinkVr(3) && n.health==35 && n.invent.potion.quantity==2,"physical drink runs native effect and animation consumption");
  n.owner.vm.consume=true;test(n.drinkVr(3) && n.health==60 && n.invent.potion.quantity==1,"script-consumed potion is not deleted twice");
  test(!n.drinkVr(1) && n.invent.bow.quantity==1,"weapon cannot be consumed as potion");
  n.readyFistsVr();test(n.invent.activeWeapon()==nullptr && n.hnpc->weapon==1,"physical fists remove weapon stats and set fist state");
  n.readyFistsVr();test(n.invent.activeWeapon()==nullptr,"second punch cannot toggle equipped sword active");
  test(n.skeletonUpdates==1 && n.visual.state==WeaponState::Fist,"repeated punches do not rebuild unchanged skeleton");
  Npc heavy;heavy.invent.melee.twoHanded=true;
  test(heavy.equipVr(4) && heavy.invent.activeWeapon()->clsId()==4 && heavy.visual.state==WeaponState::W2H && heavy.hnpc->weapon==4,"two-handed weapon equips into the native melee slot");
  Npc cross;cross.invent.bow.crossbow=true;
  test(cross.equipVr(1) && cross.visual.state==WeaponState::CBow && cross.hnpc->weapon==6,"crossbow reaches native ranged state");
  test(cross.shootVr({0,150,0},{0,0,1},1,.6f) && cross.invent.ammo.quantity==2 && cross.owner.bullet.visualScale==.6f,"crossbow fires a real scaled bolt and consumes one round");
  Npc restricted;restricted.invent.melee.allowed=false;
  test(!restricted.equipVr(4) && restricted.invent.selected==&restricted.invent.bow,"normal mode retains RPG weapon requirements");
  test(restricted.equipVr(4,true) && restricted.invent.activeWeapon()->clsId()==4,"explicit requirement cheat permits testing a heavy weapon");
  Npc failed;failed.invent.equipFails=true;
  test(!failed.equipVr(4),"native equipment failure cannot report a successful VR grab");
  float previousSpeed=0;
  for(float draw:{.08f,.2f,.4f,.6f}){
    Npc archer;test(archer.shootVr({0,150,0},{0,0,1},Vr::bowPower(draw)),"axial draw fires");
    const float speed=archer.owner.bullet.direction.length();
    test(speed>previousSpeed,"increasing axial draw strictly increases native projectile speed");previousSpeed=speed;
  }
  std::printf("VR native actions: %d checks passed\n",checks);
}
'''
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
(out/'actions.cpp').write_text(fixture)
print('Extracted current bow / potion / fist adapters')
