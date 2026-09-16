"""Exercise the production focus selector with head gaze and wall callbacks."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
source=(root/'engine/common/world/worldobjects.cpp').read_text(encoding='utf-8')
def method(signature):
    start=source.index(signature);at=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    return source[start:at]
fixture=r'''
#include "vr/vrcombatmath.h"
#include <cstdio>
#include <cstdlib>
using namespace Tempest;
struct Item {Vec3 pos;Vec3 bounds[2]={{-2,-2,-2},{2,2,2}};Vec3 position()const{return pos;}Vec3 midPosition()const{return pos;}
  const Vec3* bBox()const{return bounds;}Matrix4x4 transform()const{auto m=Matrix4x4::mkIdentity();m.translate(pos);return m;}};
struct Npc {Vec3 pos;float facing=0;float rotationRad()const{return facing;}Vec3 position()const{return pos;}Vec3 displayPosition()const{return pos+Vec3(0,180,0);}
  float qDistTo(const Item& item)const{return Vr::dot(item.pos-pos,item.pos-pos);}float qDistTo(const Npc& npc)const{return Vr::dot(npc.pos-pos,npc.pos-pos);}};
template<class T>T& deref(T& v){return v;}
template<class T>bool checkFlag(T&,int){return true;}
template<class T>bool checkTargetType(T&,int){return true;}
bool canSee(const Npc&,const Item&){return false;} // Body LOS must not govern VR gaze.
bool canSee(const Npc&,const Npc&){return false;}
struct Physics {bool wall=false;struct Hit{bool hasCol;Vec3 v;};Hit ray(Vec3 head,Vec3 point){return {wall,head+(point-head)*.5f};}};
class WorldObjects {public:
  enum SearchFlg{NoAngle=1,NoRay=2};
  struct SearchOpt{bool gaze=true;Vec3 gazeHead,gazeDirection={0,0,1};float rangeMin=0,rangeMax=250,azi=30;int flags=0,collectType=0;};
  struct World{Physics physics;Physics* physic(){return &physics;}} owner;
  template<class T>bool testObj(T&,const Npc&,const SearchOpt&,float&);
};
'''
fixture+=method('static Vec3 gazePoint(const Item&')+'\n'
fixture+=method('static float gazeScore(const Npc&')+'\n'
fixture+='template<class T>\n'+method('static float gazeScore(const T&')+'\n'
fixture+='template<class T>\n'+method('bool WorldObjects::testObj(T &src, const Npc &pl, const WorldObjects::SearchOpt &opt,float& rlen)')+'\n'
fixture+=r'''
int checks=0;void test(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
int main(){
  WorldObjects world;Npc player;player.facing=3.14f;WorldObjects::SearchOpt search;Item item;item.pos={10,0,100};
  auto select=[&]{float score=search.rangeMax*search.rangeMax;return world.testObj(item,player,search,score);};
  test(select(),"item slightly off headset ray highlights despite opposite hidden-body facing");
  world.owner.physics.wall=true;test(!select(),"wall rejects otherwise selectable item");
  search.flags=WorldObjects::NoRay;test(!select(),"legacy no-ray focus flag cannot bypass VR wall check");
  world.owner.physics.wall=false;item.pos.z=-100;test(!select(),"item behind headset cannot highlight");
  item.pos={0,0,100};search.gaze=false;search.flags=0;test(!select(),"non-VR focus retains its native body visibility policy");
  search.gaze=true;search.gazeHead={0,170,0};search.gazeDirection={0,-1,0};item.pos={10,0,0};test(select(),"head pitch selects a small floor item");
  item.pos={10,0,350};test(!select(),"gaze retains native interaction range");
  Npc npc;npc.pos={0,0,200};search.gazeHead={0,170,0};search.rangeMax=400;search.flags=0;
  auto selectNpc=[&](Vec3 look){search.gazeDirection=Vr::normalized(look-search.gazeHead);float score=search.rangeMax*search.rangeMax;return world.testObj(npc,player,search,score);};
  test(selectNpc({0,165,200}),"looking at an NPC face selects it for dialog");
  test(!selectNpc({150,165,200}),"looking well past the NPC does not select it");
  test(!selectNpc({75,165,200}),"drift beyond the acquire cone does not acquire");
  search.flags=WorldObjects::NoAngle;test(selectNpc({75,165,200}),"cached NPC target is kept in the retention cone");
  world.owner.physics.wall=true;test(!selectNpc({0,165,200}),"walls still block a cached NPC target");
  world.owner.physics.wall=false;search.flags=0;
  std::printf("VR focus: %d production checks passed\n",checks);
}
'''
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
(out/'focus.cpp').write_text(fixture,encoding='utf-8')
print('Extracted production focus geometry and selector')
