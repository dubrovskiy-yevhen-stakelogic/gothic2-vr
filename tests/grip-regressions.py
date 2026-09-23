"""Exercise the production bow attachment and pickup visibility paths."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
source=(root/'engine/common/vr/vrgameplay.cpp').read_text()
def block(start):
    begin=source.index(start);end=source.index('{',begin)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[begin:end]
shader=(root/'engine/shader/vr_pickup.frag').read_text()
begin=shader.index('float pickupVisibility(');end=shader.index('\n}',begin)+2
visibility=shader[begin:end].replace('clamp(pixelSlope,0.5,2.0)','std::clamp(pixelSlope,.5f,2.f)')
window=(root/'engine/common/vr/vrwindow.cpp').read_text()
start=window.index('  if(vrMenu.action==Vr::Menu::OpenGameMenu) {');end=window.index('\n  else if(vrMenu.action>=0)',start)
open_inventory=window[start:end]
fixture=r'''
#include "vr/vrbowmath.h"
#include "vr/vrcontrols.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace Vr;
namespace Tempest {struct Log {template<class...T> static void i(T...){}};}
struct Item {size_t id;bool crossbow=false;int quantity=10;size_t count()const{return quantity;}size_t clsId()const{return id;}bool isCrossbow()const{return crossbow;}
 struct Data{int munition=2;std::string visual="mesh";};Data handle()const{return {};}};
struct Npc {Item bow{1},arrow{2};int shots=0;float power=0;Vec3 direction;
 Item* getItem(size_t id){return id==1?&bow:id==2 && arrow.quantity>0?&arrow:nullptr;}
 bool shootVr(Vec3,Vec3 d,float p,float){++shots;--arrow.quantity;power=p;direction=d;return true;}
 bool isDown()const{return false;}};
struct DynamicWorld{static constexpr float bulletSpeed=1;struct Hit{bool hasCol=false;void* npcHit=nullptr;Vec3 v;};Hit rayNpc(Vec3,Vec3,Npc*){return {};}};
struct World{DynamicWorld physics;DynamicWorld* physic(){return &physics;}};
bool clearPath(World&,Vec3,Vec3){return true;}
struct Mesh{std::array<Vec3,2> bbox()const{return {Vec3(-1,-80,-1),Vec3(1,10,1)};}};
struct Resources{static const Mesh* loadMesh(const std::string&){static Mesh mesh;return &mesh;}};
struct Xr{bool tracked[2]={true,true};bool gripTracked(uint32_t i){return tracked[i];}void haptic(uint32_t,float){}};
struct Fixture {
 static constexpr size_t None=size_t(-1);
 struct Held{size_t id=None;int slot=-1;bool autoArrow=false;};
 struct Hand{Matrix grip=Matrix::mkIdentity(),aim=Matrix::mkIdentity();bool anchored=false;float squeeze=0;Matrix palmPose=Matrix::mkIdentity();void anchor(bool left){anchored=true;palmPose=handPalmPose(grip,aim,left,true);}};
 struct Visual{std::string mesh;Vec3 position,forward,up;int kind;float grip,scale;};
 Npc owner;Npc* player=&owner;World worldData;World* world=&worldData;Xr xr;
 HolsterSettings settings;BodyFrame body;std::array<Held,2> held;std::array<Hand,2> hands;
 std::array<ButtonEdge,2> grips;std::array<Matrix,2> weaponPoses={Matrix::mkIdentity(),Matrix::mkIdentity()};
 std::vector<Visual> visuals;std::vector<std::pair<Vec3,Vec3>> aimLines,lines;
 bool allowed=true,calibrationPreview=false,drawing=false;float units=100,drawLength=0;int nockHand=-1,supportHand=0,swordMain=1;Matrix swordPalmLocal=Matrix::mkIdentity();BowGesture bowGesture;
 Vec3 palmOffset{0,0,-.2f};
 ItemCalibration profile(size_t id,int,const HolsterSettings&,std::string_view domain={})const{
  ItemCalibration c;if(id==1 && domain.empty()){c.stringSide=.03f;c.stringDepth=.02f;}
  if(domain=="SUP")c.offset=palmOffset;
  if(domain=="STR")c.offset.z=-.2f;
  if(id==2){c.offset={.02f,.03f,.04f};c.scale=.6f;}
  return c;
 }
 Fixture(){held[1]={1,2,false};}
 void tick(Vec3 position,bool press=false,bool release=false){
  hands[0].grip=itemPose(position,{1,0,0},{0,1,0},{},units);hands[0].aim=hands[0].grip;hands[0].anchored=false;
  grips[0].pressed=press;grips[0].released=release;visuals.clear();lines.clear();aimLines.clear();
'''
start=source.index('  int bow=-1,arrow=-1;');end=source.index('  if(allowed && settings.pickupHighlight)',start)
fixture+=source[start:end]+'\n}\nvoid sword(){\n'+block('if(!calibrationPreview && supportHand>=0 && swordMain>=0)')+'\n}\n};\n'
fixture+=r'''
struct Gothic {static Gothic& inst(){static Gothic g;return g;}Npc hero;bool hasPlayer=true;Npc* player(){return hasPlayer?&hero:nullptr;}
 struct Camera {bool cutscene=false;bool isCutscene(){return cutscene;}} cam;Camera* camera(){return &cam;}
 const char* menuMain(){return "MENU_MAIN";}
 enum class LoadState{Idle,Loading};LoadState loading=LoadState::Idle;LoadState checkLoading()const{return loading;}};
struct KeyCodec {enum Action{Status=1,Escape=2};};
struct InventoryUiFixture {
 struct {int action=Menu::OpenGameInterface;} vrMenu;
 struct {int calls=0;void suspend(){++calls;}} vrGameplay;
 struct {bool active=false;bool isActive(){return active;}} dialogs;
 struct {bool active=false;bool isActive(){return active;}} video,chapter,document,console;
 struct {bool active=true;std::string menu;int key=0;Npc* player=nullptr;bool isActive(){return active;}void closeAll(){active=false;}void setMenu(const char* name,int k){menu=name;key=k;active=true;}void setPlayer(Npc& p){player=&p;}void showVersion(bool){}} rootMenu;
 struct {int opens=0;bool active=false,wheel=false;bool isActive(){return active;}bool isWheelOpen(){return wheel;}void close(){active=wheel=false;}void open(Npc&){++opens;active=true;}} inventory;
 int cleared=0;void clearInput(){++cleared;}
 void open(){
 auto& game=Gothic::inst();
'''+open_inventory+r'''
 }
};
'''
fixture+=r'''
float smoothstep(float a,float b,float x){float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}
'''+visibility+r'''
int checks=0;void test(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
int main(){
 InventoryUiFixture pause;pause.vrMenu.action=Menu::OpenGameMenu;pause.rootMenu.active=false;pause.inventory.active=true;pause.open();
 test(pause.rootMenu.active && pause.rootMenu.menu=="MENU_MAIN" && pause.rootMenu.key==KeyCodec::Escape && !pause.inventory.active && pause.cleared==1,"game shortcut opens native pause menu and clears inventory/input");
 pause.open();test(!pause.rootMenu.active,"game shortcut closes the native menu");
 pause.dialogs.active=true;pause.open();test(!pause.rootMenu.active,"game shortcut does not interrupt dialogue");pause.dialogs.active=false;
 Gothic::inst().cam.cutscene=true;pause.open();test(!pause.rootMenu.active,"game shortcut does not interrupt cutscene");Gothic::inst().cam.cutscene=false;
 Gothic::inst().loading=Gothic::LoadState::Loading;pause.open();test(!pause.rootMenu.active,"game shortcut cannot open during loading");Gothic::inst().loading=Gothic::LoadState::Idle;
 Gothic::inst().hasPlayer=false;pause.rootMenu.active=true;pause.open();test(pause.rootMenu.active,"game shortcut cannot dismiss title screen without a player");Gothic::inst().hasPlayer=true;
 InventoryUiFixture ui;ui.open();test(ui.inventory.active && ui.inventory.opens==1 && ui.cleared==1 && !ui.rootMenu.active,"game interface action opens inventory with native player");
 ui.open();test(ui.inventory.opens==1,"already open inventory is preserved");
 ui.inventory.wheel=true;ui.open();test(ui.inventory.active && !ui.inventory.wheel && ui.inventory.opens==2,"dedicated inventory action replaces the quick wheel with the full item list");
 ui.inventory.active=false;ui.dialogs.active=true;ui.open();test(!ui.inventory.active,"inventory action cannot overlap a dialogue");
 ui.dialogs.active=false;Gothic::inst().hasPlayer=false;ui.open();test(!ui.inventory.active,"title screen without player cannot open inventory");Gothic::inst().hasPlayer=true;
 InventoryUiFixture stats;stats.vrMenu.action=Menu::OpenCharacterStats;stats.rootMenu.active=false;stats.inventory.active=true;stats.open();
 test(stats.rootMenu.active && stats.rootMenu.menu=="MENU_STATUS" && stats.rootMenu.key==KeyCodec::Status && stats.rootMenu.player==&Gothic::inst().hero && !stats.inventory.active && stats.vrGameplay.calls==1 && stats.cleared==1,"character stats action opens the native status screen for the player");
 stats.rootMenu.active=false;stats.dialogs.active=true;stats.open();test(!stats.rootMenu.active,"character stats cannot overlap a dialogue");
 Fixture f;const Vec3 rest(3,0,-18);
 f.tick({40,20,10});test(f.visuals.size()==1 && !f.hands[0].anchored && !f.drawing,"arrow starts in the freely tracked hand");
 const auto free=f.visuals[0].position;f.tick({50,20,10});test((f.visuals[0].position-free-Vec3(10,0,0)).length()<.001f,"before nocking arrow follows physical hand translation");
 test(f.lines.size()==2 && (f.lines[0].second-rest).length()<.001f,"resting string uses horizontal offsets before a draw");
 f.tick(rest,true);test(f.drawing && f.hands[0].anchored && (f.visuals[0].position-rest).length()<.001f,"grip near string attaches both arrow and support hand");
 f.tick(rest+Vec3(25,12,-30));test(f.hands[0].anchored && (f.visuals[0].position-rest-Vec3(0,0,-30)).length()<.001f,"sideways draw cannot move attached arrow off bow axis");
 test((origin(f.hands[0].grip)-f.visuals[0].position-Vec3(-3,0,-2)).length()<.001f,"virtual hand keeps its independently calibrated offset from arrow nock");
 const auto nockBefore=f.visuals[0].position,stringBefore=f.lines[0].first,palmBefore=origin(f.hands[0].grip);
 f.palmOffset+=Vec3(.07f,.02f,-.03f);f.tick(rest+Vec3(25,12,-30));
 test((f.visuals[0].position-nockBefore).length()<.001f && (f.lines[0].first-stringBefore).length()<.001f,"support XYZ cannot move the arrow or string");
 test((origin(f.hands[0].grip)-palmBefore-Vec3(7,2,-3)).length()<.001f,"support XYZ moves the attached palm relative to the string");
 f.tick(rest+Vec3(25,12,-30),false,true);test(f.owner.shots==1 && std::abs(f.owner.power-.5f)<.001f && (f.owner.direction-Vec3(0,0,1)).length()<.001f,"release fires along primary hand aim with axial draw power");
 f.tick({45,20,-40});test(f.visuals.size()==1 && !f.hands[0].anchored && !f.drawing,"next arrow returns to free hand after firing");
 f.tick(rest,true);f.tick(rest+Vec3(0,0,-2),false,true);test(f.owner.shots==1 && !f.hands[0].anchored && !f.drawing,"short pull cancels and releases the virtual hand");
 f.tick(rest,true);f.xr.tracked[0]=false;f.tick(rest);test(!f.drawing && !f.hands[0].anchored,"tracking loss cancels attachment");

 Fixture sword;const auto initial=itemPose({20,150,30},{0,0,1},{0,1,0},{{.02f,.08f,.06f},{-77,20,0}},100);
 sword.swordPalmLocal=relativeHandPalm(initial,Matrix::mkIdentity(),Matrix::mkIdentity(),false);
 for(float angle:{-90.f,-20.f,0.f,50.f,170.f}) {
  auto moved=initial;moved.rotateOY(angle);moved.translate(25,40,10);sword.weaponPoses[1]=moved;sword.sword();
  auto inverse=moved;inverse.inverse();const auto primary=inverse*sword.hands[1].palmPose;
  test(sword.hands[0].anchored && sword.hands[1].anchored,"two-handed production path anchors both visible palms");
  test((origin(primary)-origin(sword.swordPalmLocal)).length()<.001f,"production primary palm stays at its captured sword grip");
  const auto support=inverse*sword.hands[0].palmPose;
  test((origin(support)-Vec3(0,0,-20)).length()<.001f,"production support palm stays at calibrated sword grip");
 }
 for(float z:{30.f,100.f,600.f,2000.f}) {
  for(float noise:{-.2f,-.01f,0.f,.01f,.2f})test(pickupVisibility(z+noise,z,0)==1,"coplanar highlight remains steady under sub-centimetre depth noise");
  test(pickupVisibility(z+10,z,0)==0,"opaque surface in front still fully occludes highlight");
  test(pickupVisibility(z-10,z,0)==1,"highlight in front of surface stays fully visible");
 }
 test(pickupVisibility(101,100,0)==0 && pickupVisibility(100.75f,100,0)>.49f,"depth transition is narrow and continuous");
 test(pickupVisibility(110,100,100)==0,"steep depth slope cannot expand visibility through a wall");
 std::printf("VR 051 attachment and depth: %d production checks passed\n",checks);
}
'''
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True);(out/'regressions.cpp').write_text(fixture)
print('Extracted current bow attachment and pickup depth visibility')
