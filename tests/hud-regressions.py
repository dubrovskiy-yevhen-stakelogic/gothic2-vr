"""Exercise the production target-health selection and fixed VR overlay placement."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
def block(file,start):
 s=(root/file).read_text();a=s.index(start);b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
selection=block('engine/common/vr/vrgameplay.cpp','Npc* Gameplay::healthTarget(')
health=block('engine/common/vr/vrwindow.cpp','  if(gameplay && !vrMenu.visible)if(auto world=Gothic::inst().world()) {')
fixture=r'''
#include "vr/vrcombatmath.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace Vr;
constexpr int ATR_HITPOINTS=0,ATR_HITPOINTSMAX=1,AlignHCenter=1,AlignTop=2;
struct Npc{bool dead=false,blocked=false;Vec3 pos{0,0,150};int hp=75,maximum=100;bool isDead()const{return dead;}Vec3 position()const{return pos;}Vec3 displayPosition()const{return pos+Vec3(0,140,0);}int attribute(int id){return id==ATR_HITPOINTS?hp:maximum;}const char* displayName(){return "Enemy";}};
struct Focus{Npc* npc=nullptr;};
struct World{std::vector<Npc*> live;bool scan=false;Npc* hero=nullptr;Npc* player(){return hero;}template<class F>void detectNpc(Vec3,float,F fn){if(scan)for(auto npc:live)fn(*npc);}Focus validateFocus(Focus f){if(std::find(live.begin(),live.end(),f.npc)==live.end())f.npc=nullptr;return f;}};
bool clearPath(World& world,Vec3,Vec3 point){for(auto npc:world.live)if(npc->blocked && std::abs(point.x-npc->pos.x)<.01f && std::abs(point.z-npc->pos.z)<.01f)return false;return true;}
struct Gameplay{float units=100;Vec3 healthGazeDirection{0,0,1};Npc* hudTargetNpc=nullptr;World* hudTargetWorld=nullptr;World* context=nullptr;Npc* recentTarget=nullptr;uint64_t targetUntil=0;BodyFrame body;Npc* healthTarget(World&,const Focus&,uint64_t);};
'''+selection+r'''
struct Gothic{World* current=nullptr;static Gothic& inst(){static Gothic g;return g;}World* world(){return current;}};
struct Application{static uint64_t tickCount(){return 100;}};
struct Color{Color(int,int,int,int){}};struct Painter{void setBrush(Color){}};
struct Size{int w=80,h=24;};struct Font{Size textSize(const char*){return {};}void drawText(Painter&,int,int,const char*){}};
struct Overlay{Gameplay vrGameplay;struct {bool visible=false;float hudX=.225f,hudY=.075f;}vrMenu;
 struct {Focus f;Focus focus(){return f;}}player;int draws=0,barX=0,barY=0,barHp=1;float value=0;int width=1920,height=1080;
 int w(){return width;}int h(){return height;}Size playerBarSize(){return {};}
 void drawBar(Painter&,int,int x,int y,float hp,int){++draws;barX=x;barY=y;value=hp;}
 void paint(bool gameplay){Painter p;Font font;int line=30;
'''+health+r'''
 }
};
int checks=0;void test(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
int main(){
 World world,other;Npc first,second;world.live={&first,&second};Gameplay g;g.context=&world;g.body.head={0,140,0};g.recentTarget=&first;g.targetUntil=4100;
 test(g.healthTarget(world,{},100)==&first,"recent physical contact supplies health even when gaze has no NPC");
 test(g.healthTarget(world,{&second},100)==&second,"current gaze target takes precedence over old melee contact");
 g.hudTargetNpc=nullptr;first.dead=true;test(g.healthTarget(world,{},100)==nullptr,"dead targets disappear");first.dead=false;
 world.live={&second};test(g.healthTarget(world,{},100)==nullptr,"removed NPC is validated before dereferencing recent contact");world.live={&first,&second};
 test(g.healthTarget(other,{},100)==nullptr,"world changes cannot reuse stale combat targets");
 test(g.healthTarget(world,{},4101)==nullptr,"recent contact expires after four seconds");
 first.pos.z=900;test(g.healthTarget(world,{},100)==nullptr,"distant previous opponent does not retain a health bar");first.pos.z=150;
 Overlay ui;ui.vrGameplay=g;Gothic::inst().current=&world;ui.paint(true);
 test(ui.draws==1 && ui.value==.75f,"production overlay paints native current and maximum enemy HP");
 test(ui.barX==ui.w()/2 && ui.barY==ui.h()/12,"target health remains above center without the player's HUD shift");
 for(float offset:{-.3f,0.f,.3f}){ui.vrMenu.hudX=ui.vrMenu.hudY=offset;ui.paint(true);test(ui.barY>=0 && ui.barY<ui.h()/4 && ui.barX==ui.w()/2,"user HUD offsets cannot clip the top enemy bar");}
 const int before=ui.draws;ui.vrMenu.visible=true;ui.paint(true);test(ui.draws==before,"VR menu hides target health");ui.vrMenu.visible=false;
 ui.paint(false);test(ui.draws==before,"theater and inventory views do not receive a gameplay target overlay");
 first.maximum=0;ui.paint(true);test(ui.value==0 && std::isfinite(ui.value),"invalid maximum HP cannot produce NaN geometry");
 first.maximum=100;first.hp=200;ui.paint(true);test(ui.value==1,"over-max HP is clamped to a full health bar");
 World broad;Npc far,neighbor,hero;broad.live={&far,&neighbor,&hero};broad.scan=true;broad.hero=&hero;
 far.pos={300,0,1000};neighbor.pos={-500,0,1000};hero.pos={0,0,20};
 Gameplay wide;wide.context=&broad;wide.body.head={0,140,0};
 test(wide.healthTarget(broad,{},100)==&far,"HUD acquires an NPC three metres to the side at ten metres without native interaction focus");
 far.pos.x=450;test(wide.healthTarget(broad,{},120)==&far,"selected NPC remains visible inside the wider retention cone");
 neighbor.pos.x=-440;test(wide.healthTarget(broad,{},140)==&far,"small gaze differences do not switch away from the retained NPC");
 neighbor.pos.x=50;test(wide.healthTarget(broad,{},160)==&neighbor,"deliberately looking much closer to another NPC switches health target");
 neighbor.blocked=true;far.blocked=true;test(wide.healthTarget(broad,{},180)==nullptr,"walls prevent acquiring or retaining uncontacted NPCs");
 neighbor.blocked=false;neighbor.pos.z=-100;test(wide.healthTarget(broad,{},200)==nullptr,"NPC behind the head is not selected");
 far.blocked=false;far.pos={0,0,2200};test(wide.healthTarget(broad,{},220)==nullptr,"HUD target scan has a twenty-metre range limit");
 far.pos={0,0,150};test(wide.healthTarget(broad,{},240)==&far,"looking at a nearby NPC head works without aiming at its abdomen");
 wide.healthGazeDirection={0,-1,0};test(wide.healthTarget(broad,{},260)==nullptr,"full head pitch is used when looking away from the NPC");
 const Vec3 eye{0,140,0},forward{0,0,1};
 test(gazeScore(eye,forward,{300,140,1000})<0 && healthGazeScore(eye,forward,{300,140,1000},100,false)>=0,"wide HUD cone does not widen item and interaction gaze selection");
 test(healthGazeScore(eye,forward,{450,140,1000},100,false)<0 && healthGazeScore(eye,forward,{450,140,1000},100,true)>=0,"acquisition and retention use separate angular thresholds");
 std::printf("VR 054 target health: %d production checks passed\n",checks);
}
'''
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True);(out/'regressions.cpp').write_text(fixture)
print('Extracted production target selection and unshifted health overlay')
