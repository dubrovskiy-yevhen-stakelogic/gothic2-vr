"""Exercise production hand transfers and world highlights without retail assets."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
source=(root/'engine/common/vr/vrgameplay.cpp').read_text()
def block(start):
    begin=source.index(start);end=source.index('{',begin)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[begin:end]
fixture=r'''
#include "vr/vrbowmath.h"
#include "vr/vrcombatmath.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace Vr;
int checks=0;
void test(bool ok,const char* name){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
struct Item {
  Vec3 position;bool torch=false,blocked=false;std::string visual="item";
  bool isTorchBurn()const{return torch;}Vec3 midPosition()const{return position;}
  struct Handle{std::string visual;};Handle handle()const{return {visual};}
  Matrix transform()const{auto m=Matrix::mkIdentity();m.translate(position);return m;}
};
struct World {
  bool blocked=false;std::vector<Item> items;
  template<class F> void detectItem(Vec3,float,F fn){for(auto& item:items)fn(item);}
};
struct Xr {std::array<bool,2> tracked={true,true};int pulses=0;
  bool gripTracked(uint32_t i)const{return tracked[i];}void haptic(uint32_t,float){++pulses;}
};
bool clearPath(World& world,Vec3,Vec3 end,float=0){
  if(world.blocked)return false;
  for(auto& item:world.items)if(item.position==end && item.blocked)return false;
  return true;
}
struct Fixture {
  static constexpr size_t None=size_t(-1);
  struct Held{size_t id=None;int slot=-1,holster=-1;Swing swing;Vec3 previousBase,previousTip;uint64_t strikeUntil=0;bool autoArrow=false;};
  struct Hand{Matrix grip=Matrix::mkIdentity();};
  struct ReturnItem{Item* item;};struct Highlight{std::string mesh;Matrix matrix;};
  std::array<Held,2> held;std::array<Hand,2> hands;std::array<ButtonEdge,2> grips;
  std::array<ReleaseMotion,2> releaseMotion;
  std::vector<ReturnItem> returning;std::vector<Highlight> highlights;
  BodyFrame body;HolsterSettings settings;World owner;World* world=&owner;Xr xr;
  bool allowed=true,chord=false,drawing=false;float units=100;
  int supportHand=-1,swordMain=-1,nockHand=-1;BowGesture bowGesture;
  void transfer(){
'''
fixture+=block('for(int receiver=0;receiver<2;++receiver)')+'\n}\nvoid highlight(){highlights.clear();\n'
fixture+=block('if(allowed && settings.pickupHighlight)')+'\n}\n};\n'
fixture+=r'''
int main(){
  for(int donor=0;donor<2;++donor){
    const int receiver=1-donor;
    Fixture f;f.held[donor].id=7;f.held[donor].slot=2;f.held[donor].strikeUntil=123;
    f.held[receiver].id=8;f.held[receiver].slot=3;f.held[receiver].autoArrow=true;
    f.hands[receiver].grip.translate(5,0,0);f.grips[receiver].pressed=true;
    f.bowGesture.active=true;f.drawing=true;f.nockHand=receiver;
    f.transfer();
    test(f.held[receiver].id==7 && f.held[receiver].slot==2 && f.held[donor].id==Fixture::None,"bow transfers directly in either direction over automatic arrow");
    test(!f.held[receiver].autoArrow && f.held[receiver].strikeUntil==0,"transfer clears automatic arrow and prior attack state");
    test(!f.drawing && !f.bowGesture.active && f.nockHand==-1 && f.xr.pulses==2,"transfer cancels pending shot and acknowledges both hands");
    f.grips[receiver].pressed=false;f.transfer();
    test(f.held[receiver].id==7,"holding grip after transfer cannot duplicate item");
  }
  for(int scenario=0;scenario<7;++scenario){
    Fixture f;f.held[1].id=7;f.held[1].slot=0;f.grips[0].pressed=true;
    if(scenario==0)f.hands[0].grip.translate(11,0,0);
    if(scenario==1)f.xr.tracked[1]=false;
    if(scenario==2)f.owner.blocked=true;
    if(scenario==3)f.held[0].id=8;
    if(scenario==4)f.chord=true;
    if(scenario==5)f.allowed=false;
    if(scenario==6)f.grips[0].pressed=false;
    f.transfer();test(f.held[1].id==7 && f.xr.pulses==0,"distance tracking walls occupied hands menu and held grip reject transfer");
  }
  Fixture glow;
  glow.owner.items={{{0,0,120},false,false,"collectible"},{{0,0,150},false,false,"thrown"},
                    {{0,0,190},false,true,"behind_wall"},{{0,0,210},true,false,"burning_torch"},
                    {{0,0,700},false,false,"far_item"}};
  glow.returning.push_back({&glow.owner.items[1]});
  glow.held[0].id=7;glow.held[1].id=8;
  glow.highlight();test(glow.highlights.size()==2 && glow.highlights[0].mesh=="collectible","collectibles reach the depth-tested overlay even when their center is occluded; thrown and distant items do not");
  glow.settings.pickupHighlight=false;glow.highlight();test(glow.highlights.empty(),"world item highlight switch removes every mark");
  glow.settings.pickupHighlight=true;glow.settings.pickupHighlightRange=1;glow.highlight();test(glow.highlights.empty(),"world highlight range takes effect immediately");
  glow.settings.pickupHighlightRange=8;glow.allowed=false;glow.highlight();test(glow.highlights.empty(),"paused gameplay cannot populate world highlights");
  glow.allowed=true;glow.highlight();test(glow.highlights.size()==3,"increased range includes more collectible items");
  glow.returning.clear();glow.owner.items.clear();
  for(int i=0;i<100;++i)glow.owner.items.push_back({{0,0,float(200-i)},false,false,std::to_string(i)});
  glow.highlight();test(glow.highlights.size()==64 && glow.highlights.front().mesh=="99","dense item piles are bounded and nearest items take priority");
  std::printf("VR 049 transfers and highlights: %d production checks passed\n",checks);
}
'''
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
(out/'regressions.cpp').write_text(fixture)
print('Extracted current hand transfer and collectible highlight blocks')
