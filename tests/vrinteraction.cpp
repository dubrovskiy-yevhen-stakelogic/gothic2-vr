#include "vr/vrcontrols.h"
#include "vr/uxrh.h"
#include "vr/vrcombatmath.h"
#include "vr/vrbowmath.h"
#include "vr/vrbowmesh.h"
#include "vr/xrmath.h"
#include "utils/gamepadbindings.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <limits>
int checks=0;
void test(bool pass,const char* name) {++checks;if(!pass) {std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
int main() {
  using namespace Vr;
  ButtonEdge grip;
  grip.update(1,true);test(!grip.pressed,"held grip on entry cannot draw");
  grip.update(0,true);grip.update(.8f,true);test(grip.pressed,"fresh squeeze draws");
  grip.update(.8f,true);test(!grip.pressed,"held grip cannot duplicate items");
  grip.update(.4f,true);test(grip.down,"release hysteresis");
  grip.update(.2f,true);test(grip.released,"release edge");
  grip.update(1,false);grip.update(1,true);test(!grip.pressed,"tracking recovery requires release");
  HolsterSettings holsters;BodyFrame body;auto head=Matrix::mkIdentity();head.translate(20,170,50);body.update(head);
  for(int i=0;i<4;++i) {
    const auto p=body.point(holsters.offsets[size_t(i)],100);
    test(body.closest(p,holsters,100)==i,"body-relative holster selection");
    test((body.local(p,100)-holsters.offsets[size_t(i)]).length()<.0001f,"holster round trip");
  }
  head.rotateOY(90);body.update(head);test(body.closest(body.point(holsters.offsets[0],50),holsters,50)==0,"rotated holster respects world scale");
  holsters.enabled=false;test(body.closest(body.head,holsters,100)==-1,"disabled holsters");
  Swing swing;body.update(Matrix::mkIdentity());
  test(!swing.update({0,0,100},body,100,100,true,2.2f),"first sample cannot strike");
  swing.update({0,0,100},body,100,120,true,2.2f);
  test(swing.update({10,0,100},body,100,140,true,2.2f),"physical five metres per second swing");
  test(!swing.update({20,0,100},body,100,160,true,2.2f),"one strike per follow-through");
  test(!swing.update({20,0,100},body,100,600,true,2.2f)&&!swing.continuous,"long sample gap cancels contact window");
  swing.update({20,0,100},body,100,620,true,2.2f);
  test(swing.update({30,0,100},body,100,640,true,2.2f),"settled weapon rearms");
  swing.reset();swing.update({0,0,100},body,100,1000,true,2.2f);swing.update({0,0,100},body,100,1020,true,2.2f);
  body.head.x+=10;
  test(!swing.update({10,0,100},body,100,1040,true,2.2f),"locomotion alone cannot attack");
  body.head.x+=1000;
  test(!swing.update({1010,0,100},body,100,1060,true,2.2f)&&!swing.continuous,"teleport cancels sweep");
  body.forward={1,0,0};
  test(!swing.update({1010,0,100},body,100,1080,true,2.2f)&&!swing.continuous,"snap turn cancels sweep");
  test(!swing.update({1010,0,100},body,100,1100,false,2.2f)&&!swing.valid,"menu pause clears motion");
  Settings settings;settings.interaction.items[0]="ITMW_1H_SWORD_01";settings.interaction.offsets[1]={-.2f,-.3f,.25f};settings.shadows=false;settings.objectDistance=2;
  std::ostringstream saved;settings.write(saved);Settings restored;std::istringstream input(saved.str());restored.read(input);
  test(restored.interaction.items==settings.interaction.items,"holster assignments persist");
  test((restored.interaction.offsets[1]-settings.interaction.offsets[1]).length()<.001f,"holster positions persist");
  test(!restored.shadows && restored.objectDistance==2,"interaction persistence preserves renderer preferences");
  std::istringstream bad("HolsterItem0=../../unsafe\nHolsterX0=999\nPickupRadius=999\nSwingSpeed=-99\n");restored.read(bad);
  test(restored.interaction.items[0]==settings.interaction.items[0],"invalid symbol assignment rejected");
  test(restored.interaction.offsets[0].x==.65f && restored.interaction.pickupRadius==.65f && restored.interaction.swingSpeed==1.f,"physical settings bounded");

  settings.mapping={2,1,0,4,5,6};
  settings.interaction.calibration["ITFO_POTION_R"]={{.03f,-.02f,.10f},{-90,15,5}};
  settings.interaction.calibration["ITFO_POTION_L"]={{-.04f,0,0},{0,0,90}};
  std::ostringstream saveCal;settings.write(saveCal);std::istringstream readCal(saveCal.str());restored.read(readCal);
  test(restored.mapping==settings.mapping,"button mappings survive restart");
  test(restored.interaction.calibration.size()==2,"independent per-item per-hand calibration persists");
  test(restored.interaction.calibration.at("ITFO_POTION_R").rotation.x==-90,"potion rotation persists");
  test(restored.interaction.calibration.at("ITFO_POTION_L").offset.x==-.04f,"left hand calibration not overwritten by right");
  auto pose=itemPose({10,20,30},{1,0,0},{0,1,0},{{0,0,.10f},{}},100);
  test((origin(pose)-Vec3(20,20,30)).length()<.001f,"item offset follows hand basis");
  pose=itemPose({10,20,30},{1,0,0},{0,1,0},{{0,0,.10f},{}},50);
  test((origin(pose)-Vec3(15,20,30)).length()<.001f,"item offset respects world scale");
  pose=itemPose({},{0,0,1},{0,1,0},{{},{-90,0,0}},100);
  test((axis(pose,2)-Vec3(0,1,0)).length()<.001f,"bottle long axis rotates upright");
  Vec3 blade;
  test(twoHandDirection({0,0,20},{0,0,0},100,blade) && blade==Vec3(0,0,1),"two-hand sword points from support toward primary");
  test(!twoHandDirection({0,0,2},{0,0,0},100,blade),"overlapping hands cannot flip blade");
  test(!twoHandDirection({0,0,80},{0,0,0},100,blade),"overextended support releases");
  test(twoHandDirection({0,0,4},{0,0,0},50,blade),"support threshold respects scale");
  XrMath::CinemaAnchor cinema;XrMath::Pose headPose{{},{0,1.7f,0}};
  const auto anchor=cinema.update(headPose,true);
  test((anchor.position-Vec3(0,1.58f,-2)).length()<.001f,"cinema starts below eye level two metres ahead");
  headPose.position={1,1.2f,3};headPose.orientation={0,.7071068f,0,.7071068f};
  const auto frozen=cinema.update(headPose,true);
  test(frozen.position==anchor.position && frozen.orientation.w==anchor.orientation.w,"cinema remains fixed across head movement and turns");
  cinema.update(headPose,false);const auto second=cinema.update(headPose,true);
  test((second.position-Vec3(-1,1.08f,3)).length()<.001f,"new cinematic anchors at current gaze");
  cinema.reset();headPose.position.x+=2;test(cinema.update(headPose,true).position.x>second.position.x+1,"explicit recenter moves static screen");
  struct Hit {bool hasCol=false;Vec3 v{},n{};};Vec3 spawn;int rays=0;
  auto floorRay=[&](Vec3 from,Vec3 to) {
    ++rays;test(from.y==75,"spawn rays remain below low indoor ceiling");
    if(from.y==to.y)return Hit{};
    return Hit{true,{from.x,0,from.z},{0,1,0}};
  };
  test(enemySpawnPoint({},{0,0,1},{1,0,0},floorRay,[](Vec3){return false;},spawn) && spawn==Vec3(0,5,260),"enemy placed above floor ahead");
  test(enemySpawnPoint({},{0,0,1},{1,0,0},floorRay,[](Vec3 p){return std::abs(p.x)<50;},spawn) && std::abs(spawn.x)>50,"occupied first spawn falls back to side");
  auto wallRay=[](Vec3 a,Vec3 b){return a.y==b.y?Hit{true,(a+b)*.5f,{0,0,-1}}:Hit{};};
  test(!enemySpawnPoint({},{0,0,1},{1,0,0},wallRay,[](Vec3){return false;},spawn),"spawn rejected behind closed walls");
  auto cliffRay=[](Vec3 a,Vec3 b){return a.y==b.y?Hit{}:Hit{true,{a.x,-200,a.z},{0,1,0}};};
  test(!enemySpawnPoint({},{0,0,1},{1,0,0},cliffRay,[](Vec3){return false;},spawn),"spawn rejected over deep drop");
  using GB=GamepadBindings;using Act=GB::Action;using Ctx=GB::Context;
  GB bindings;std::array<Act,6> mapping={Act::Jump,Act::Interact,Act::Inventory,Act::Journal,Act::Run,Act::Sneak};
  bindings.setVrMapping(mapping);
  auto press=[&](const char* key,Ctx context,Act action) {
    bindings.reset();bindings.update(0,context,100);
    const auto events=bindings.update(GB::button(key),context,120);
    return events.size()==1 && events[0].action==action && events[0].phase==GB::Phase::Press;
  };
  test(press("A",Ctx::Gameplay,Act::Jump),"A jumps in exploration");
  test(press("A",Ctx::ClassicMelee,Act::Jump),"A still jumps with melee weapon drawn");
  test(press("L3",Ctx::Gameplay,Act::Run),"L3 dispatches run hold action");
  test(press("R3",Ctx::ClassicMelee,Act::Sneak),"R3 crouches with weapon drawn");
  test(press("A",Ctx::UI,Act::Accept),"gameplay remap preserves menu acceptance");
  test(press("View",Ctx::Gameplay,Act::Journal),"Quest Y routed through View maps to journal");
  press("A",Ctx::Gameplay,Act::Jump);mapping[0]=Act::Interact;bindings.setVrMapping(mapping);
  test(bindings.update(GB::button("A"),Ctx::Gameplay,140).empty(),"remapping held button cannot fire new action");
  test(press("A",Ctx::Gameplay,Act::Interact),"new controller mapping applies after release");
  Settings migrated;std::istringstream legacy("HolsterX0=0.23\nHolsterY0=-0.7\nHolsterZ0=0\nShadows=0\nTerrainLod=2\n");migrated.read(legacy);
  test(migrated.interaction.offsets[0]==Vec3(.23f,-.45f,.10f),"old untouched belt migrates upward and forward");
  test(!migrated.shadows && migrated.terrainLod==2,"belt migration retains current renderer settings");
  Settings custom;std::istringstream customIni("HolsterX0=0.3\nHolsterY0=-0.8\nHolsterZ0=0.1\n");custom.read(customIni);
  test(custom.interaction.offsets[0]==Vec3(.3f,-.8f,.1f),"custom belt is not moved by migration");
  custom.interaction.offsets[0]={.23f,-.70f,0};std::ostringstream versioned;custom.write(versioned);
  Settings reloaded;std::istringstream versionedIn(versioned.str());reloaded.read(versionedIn);
  test(reloaded.interaction.offsets[0]==Vec3(.23f,-.70f,0),"explicit old-height choice survives future restarts");
  BodyFrame placement;auto facing=Matrix::mkIdentity();facing.rotateOY(80);facing.translate(10,170,20);placement.update(facing);
  HolsterSettings placed;auto handAt=placement.point({.3f,-.4f,.2f},50);
  test(placement.placeHolster(placed,0,handAt,50),"place belt at tracked controller");
  test((placement.point(placed.offsets[0],50)-handAt).length()<.001f && placement.closest(handAt,placed,50)==0,"visible placement equals pickup point with rotated head and scaled world");
  const auto savedOffset=placed.offsets[0];
  test(!placement.placeHolster(placed,0,{NAN,0,0},50) && placed.offsets[0]==savedOffset,"invalid tracking preserves previous position");
  test(!placement.placeHolster(placed,0,placement.point({3,0,0},50),50),"out of reach capture rejected");
  Menu previewMenu;previewMenu.visible=true;
  for(auto page:{Menu::Page::Main,Menu::Page::Holsters,Menu::Page::Calibration,Menu::Page::CheatItems}) {
    previewMenu.page=page;test(previewMenu.interactionPreview(true),"hands and holsters remain visible through VR menu navigation");
  }
  test(!previewMenu.interactionPreview(false),"lost focus does not enable interaction preview");
  Menu menu;Input in;in.focused=true;uint64_t now=100;
  auto step=[&] {now+=20;menu.update(in,now);};
  auto release=[&] {in={};in.focused=true;step();};release();
  in.leftGrip=in.rightGrip=in.menu=true;step();release();
  menu.selected=5;in.a=true;step();test(menu.page==Menu::Page::Cheats,"cheat sections reachable");release();
  menu.selected=2;in.a=true;step();test(menu.page==Menu::Page::CheatItems,"item section reachable");release();
  menu.selected=0;in.a=true;step();test(menu.page==Menu::Page::ItemList,"one-handed category reachable");release();
  menu.selected=3;in.x=1;step();test(menu.action==-1,"horizontal stick cannot give items");release();
  in.trigger=1;step();test(menu.action==Menu::ItemGive,"explicit trigger gives item");step();test(menu.action==-1,"held trigger cannot repeat give");release();
  in.b=true;step();test(menu.page==Menu::Page::CheatItems,"back returns to item categories");release();
  in.b=true;step();test(menu.page==Menu::Page::Cheats && menu.row()==Menu::CheatItems,"back remembers cheat section");release();
  in.b=true;step();test(menu.page==Menu::Page::Main && menu.row()==Menu::Cheats,"back remembers root section");release();
  menu.selected=4;in.a=true;step();test(menu.page==Menu::Page::Holsters,"holster submenu reachable");release();
  auto selectRow=[&](Menu::Row row){const auto rows=menu.rows();menu.selected=int(std::find(rows.begin(),rows.end(),row)-rows.begin());};
  selectRow(Menu::GripLock);in.x=1;step();test(menu.settings.interaction.gripLock,"holster menu toggles grip lock");release();
  selectRow(Menu::HolsterX);in.x=1;step();test(menu.changed,"holster position edit requests save");

  release();menu.settings.interaction.calibration=settings.interaction.calibration;
  menu.settings.interaction.offsets[0].x=.6f;selectRow(Menu::HolsterReset);in.a=true;step();
  test(menu.settings.interaction.calibration.size()==2,"holster reset preserves per-item calibration");
  test(menu.settings.interaction.offsets[0].x==.23f,"holster reset still restores belt position");
  release();selectRow(Menu::HolsterAtRight);in.x=1;step();test(menu.action==-1,"stick navigation cannot capture holster position");
  release();in.a=true;step();test(menu.action==Menu::HolsterAtRight,"right-hand holster placement is reachable");
  step();test(menu.action==-1,"held confirm cannot keep moving holster");

  Menu armedMenu;Input armedInput;armedInput.focused=true;uint64_t armedTime=5000;
  auto armedStep=[&]{armedTime+=20;armedMenu.update(armedInput,armedTime);};armedStep();
  armedInput.leftGrip=armedInput.rightGrip=armedInput.menu=true;armedStep();test(armedMenu.visible,"menu opens while gripping weapon");
  armedInput.menu=false;armedStep();armedMenu.selected=7;armedInput.a=true;armedStep();
  test(armedMenu.page==Menu::Page::CalibrationHome,"held grips no longer block calibration menu");
  armedInput.a=false;armedStep();armedMenu.selected=2;armedInput.a=true;armedStep();
  test(armedMenu.page==Menu::Page::Calibration,"model calibration reachable without dropping weapon");
  armedInput.a=false;armedStep();armedMenu.selected=3;armedInput.trigger=.8f;armedStep();
  test(armedMenu.action==Menu::CalX && armedMenu.actionDirection==1,"right trigger edits model offset with grips held");
  armedInput.trigger=0;armedStep();armedInput.secondaryTrigger=.8f;armedStep();
  test(armedMenu.action==Menu::CalX && armedMenu.actionDirection==-1,"left trigger decreases model offset");
  armedInput.secondaryTrigger=0;armedStep();armedInput.b=true;armedStep();
  test(armedMenu.page==Menu::Page::CalibrationHome,"back returns to calibration sections");
  for(auto entry:{std::pair(3,Menu::Page::Aim),std::pair(4,Menu::Page::Support),std::pair(5,Menu::Page::Holstered)}) {
    armedInput.b=false;armedStep();armedMenu.selected=entry.first;armedInput.a=true;armedStep();
    test(armedMenu.page==entry.second,"all calibration sections are reachable while gripping");
    armedInput.a=false;armedStep();armedInput.b=true;armedStep();
  }
  Settings profiles;profiles.interaction.calibration["SWORD_R"]={{.9f,-.1f,.2f},{10,20,30},.93f};
  profiles.interaction.calibration["SWORD_L"]={{-.2f,0,0},{0,45,0},-1};
  profiles.interaction.calibration["SWORD_AIM_R"]={{.01f,.02f,.03f},{2,3,4}};
  profiles.interaction.calibration["SWORD_SUP_R"]={{0,0,-.2f},{90,0,0}};
  profiles.interaction.calibration["SWORD_HOL"]={{.1f,.2f,.3f},{-90,10,0}};
  std::ostringstream fullProfiles;profiles.write(fullProfiles);Settings readProfiles;std::istringstream readFull(fullProfiles.str());readProfiles.read(readFull);
  test(readProfiles.interaction.calibration.size()==6,"model aim support and holster profiles persist independently");
  test(readProfiles.interaction.calibration.at("SWORD_R").grip==.93f,"grip origin override survives restart");
  test(readProfiles.interaction.calibration.at("SWORD_R").offset.x==.9f,"long weapon grip can move beyond old half metre limit");
  test(readProfiles.interaction.calibration.at("SWORD_SUP_R").rotation.x==90,"support orientation survives restart");
  test(readProfiles.interaction.calibration.at("SWORD_HOL").offset.y==.2f,"holstered model offset survives independently of socket");
  Settings oldSix;std::istringstream six("ItemCal_SWORD_R=0.1 0.2 0.3 10 20 30\n");oldSix.read(six);
  test(oldSix.interaction.calibration.at("SWORD_R").grip==-1 && oldSix.interaction.calibration.at("SWORD_R").rotation.z==30,"old six-value calibration remains readable");
  const Vec3 low(-3,-80,-2),high(3,20,2);const auto wrist=itemPose({10,150,20},{0,0,1},{0,1,0},{},100);
  auto nativeModel=weaponModelMatrix(low,high,0,wrist);Vec3 nativeOrigin;nativeModel.project(nativeOrigin);
  test((nativeOrigin-origin(wrist)).length()<.001f,"authored sword origin maps to wrist rather than arbitrary blade percentage");
  Vec3 bladeEnd(0,-80,0);nativeModel.project(bladeEnd);
  test((bladeEnd-Vec3(10,150,100)).length()<.001f,"negative authored blade axis points out of hand");
  auto alternate=weaponModelMatrix(low,high,0,wrist,.8f);Vec3 gripAnchor(0,0,0);alternate.project(gripAnchor);
  test((gripAnchor-origin(wrist)).length()<.001f,"explicit 80 percent grip selects the authored handle in asymmetric fixture");
  auto centred=weaponModelMatrix(low,high,0,wrist,.5f);gripAnchor={0,-30,0};centred.project(gripAnchor);
  test((gripAnchor-origin(wrist)).length()<.001f,"chosen grip point follows controller exactly");
  test((rotateBetween({0,0,1},{0,0,-1},{1,0,0})-Vec3(-1,0,0)).length()<.001f,"two-hand alignment rotates expected support vector");
  test((rotateBetween({0,0,1},{0,0,1},{0,0,-1})-Vec3(0,0,-1)).length()<.001f,"opposite support vectors stay finite");
  const auto muzzle=itemPose({0,0,0},{0,0,1},{0,1,0},{{0,0,.1f},{0,90,0}},100);
  test((origin(muzzle)-Vec3(0,0,10)).length()<.001f && (axis(muzzle,2)-Vec3(1,0,0)).length()<.001f,"aim translation and rotation use final weapon basis");

  {

  test(releaseAction(true,true,0,false,false)==ReleaseAction::Drop,"open grip drops away from holster");
  test(releaseAction(true,true,0,true,false)==ReleaseAction::Keep,"grip lock holds away from holster");
  test(releaseAction(true,true,0,true,true)==ReleaseAction::Stow,"locked item stows when open grip reaches its holster");
  test(releaseAction(true,true,.8f,false,true)==ReleaseAction::Keep,"closed grip does not stow during draw");
  test(releaseAction(false,true,0,false,false)==ReleaseAction::Keep && releaseAction(true,false,0,false,false)==ReleaseAction::Keep,"menu and lost tracking cannot release inventory");
  test(penalizedDamage(40,true)==40 && penalizedDamage(40,false)==10 && penalizedDamage(0,false)==0,"weapons past their requirements deal a quarter of their damage");
  {
    HolsterSettings pickup;pickup.items={"ITMW_2H_AXE_L_01","EMPTY","EMPTY","ITRW_ARROW"};
    test(holsterPointForPickup(true,false)==0 && holsterPointForPickup(false,true)==2 && holsterPointForPickup(false,false)==-1,"melee pickups go to the right belt and bows to the back left");
    test(holsterFreeForPickup(pickup,2,"ITRW_BOW_L_01",false),"empty back-left holster accepts a picked-up bow");
    test(!holsterFreeForPickup(pickup,0,"ITMW_1H_MACE_L_01",true),"owned weapon already on the belt keeps its holster");
    test(holsterFreeForPickup(pickup,0,"ITMW_1H_MACE_L_01",false),"belt assignment of an item no longer owned is free");
    pickup.items[1]="ITRW_BOW_L_01";test(!holsterFreeForPickup(pickup,2,"ITRW_BOW_L_01",false),"an item the player placed elsewhere keeps that holster");
    pickup.items[0]="";test(holsterFreeForPickup(pickup,0,"ITMW_1H_MACE_L_01",true),"automatic belt selection is replaced by the picked-up weapon");
  }
  {
    ReleaseDebounce debounce;
    test(!debounce.confirm(true,1000,0,{3,0,0}),"first open grip frame does not release");
    test(!debounce.confirm(false,1014,0,{}),"closed grip frame cancels a pending release");
    test(!debounce.confirm(true,1028,0,{1,0,0}) && !debounce.confirm(true,1056,0,{0,0,0}),"release waits for the confirm window");
    test(debounce.confirm(true,1070,0,{0,0,0}) && (debounce.velocity-Vec3(1,0,0)).length()<.001f,"sustained open grip releases with first-frame throw velocity");
    debounce.reset();
    test(!debounce.confirm(true,3000,3300,{}) && !debounce.confirm(true,3290,3300,{}),"releases are blocked after a frame stall");
    test(!debounce.confirm(true,3300,3300,{}) && debounce.confirm(true,3340,3300,{}),"release resumes once the stall block expires");
  }
  {
    const Vec3 eye{0,170,0},feet{0,0,200},top{0,180,200};
    auto aim=[&](Vec3 p){return normalized(p-eye);};
    test(npcGazeScore(eye,aim({0,165,200}),feet,top,false)>=0,"looking at an NPC face two metres away selects it");
    test(npcGazeScore(eye,aim({0,110,200}),feet,top,false)>=0 && npcGazeScore(eye,aim({0,40,200}),feet,top,false)>=0,"chest and legs select the NPC");
    test(npcGazeScore(eye,aim({0,165,300}),{0,0,300},{0,180,300},false)>=0,"face at three metres selects the NPC");
    test(npcGazeScore(eye,aim({120,165,200}),feet,top,false)<0,"thirty degrees to the side does not select");
    const auto jitter=aim({75,165,200});
    test(npcGazeScore(eye,jitter,feet,top,false)<0 && npcGazeScore(eye,jitter,feet,top,true)>=0,"retained NPC survives gaze drift beyond the acquire cone");
    test(npcGazeScore(eye,aim({0,165,-200}),feet,top,true)<0,"NPC behind the head is never selected");
    Vec3 probes[3];size_t count=0;
    test(npcGazeScore(eye,aim({0,165,200}),feet,top,false,probes,&count)>=0 && count>=2,"face gaze provides extra line-of-sight probes");
    test(npcGazeScore(eye,aim({0,165,200}),feet,top,false)<npcGazeScore(eye,aim({40,165,200}),feet,top,false),"closer to gaze centre scores better");
  }
  test(gazeScore({},{0,0,1},{0,0,100})>=0 && gazeScore({},{0,0,1},{15,0,100})>=0,"gaze cone selects both exact ray and nearby small item");
  test(gazeScore({},{0,0,1},{60,0,100})<0 && gazeScore({},{0,0,1},{0,0,-100})<0,"side and rear objects do not highlight");
  test(gazeScore({0,170,0},{0,-1,0},{10,0,0})>=0,"head pitch can select ground item");
  test(gazeScore({},{0,0,1},{0,0,100})<gazeScore({},{0,0,1},{10,0,100}),"gaze prioritizes centred item within cone");
  Settings newButtons;test(newButtons.mapping[4]==9 && newButtons.mapping[5]==5,"L3 runs and R3 crouches by default");
  std::istringstream oldButtons("ButtonMap4=5\nButtonMap5=6\n");newButtons.read(oldButtons);test(newButtons.mapping[4]==9 && newButtons.mapping[5]==5,"existing default buttons migrate to requested layout");
  Settings editedButtons;std::istringstream editedMap("ButtonMap4=2\nButtonMap5=5\n");editedButtons.read(editedMap);test(editedButtons.mapping[4]==2 && editedButtons.mapping[5]==5,"custom button mapping survives layout migration");

  BowGesture bowInput;
  test(bowInput.update(true,false,true,false,.4f)==BowGesture::None,"grip away from string cannot start bow draw");
  test(bowInput.update(true,true,true,false,.1f)==BowGesture::Nocked && bowInput.active,"grip near calibrated string nocks arrow");
  test(bowInput.update(true,false,false,false,.5f)==BowGesture::None && bowInput.active,"holding drawing hand keeps string under tension");
  test(bowInput.update(true,false,false,true,.5f)==BowGesture::Fired && !bowInput.active,"opening drawing grip fires once");
  test(bowInput.update(true,false,false,true,.5f)==BowGesture::None,"held open grip cannot repeat arrow shot");
  bowInput.update(true,true,true,false,.1f);test(bowInput.update(false,false,false,true,.5f)==BowGesture::Cancelled,"lost controller tracking cancels rather than fires");
  bowInput.update(true,true,true,false,.1f);test(bowInput.update(true,false,false,false,1.f)==BowGesture::Cancelled,"excessive draw separation cancels safely");
  bowInput.update(true,true,true,false,.1f);test(bowInput.update(true,true,false,true,.08f)==BowGesture::Cancelled,"release without drawing does not consume arrow");
  bowInput.update(true,true,true,false,.1f);bowInput.reset();test(bowInput.update(true,false,false,true,.5f)==BowGesture::None,"menu reset cannot release a delayed arrow");
  bowInput.update(true,true,true,false,.2f);test(bowInput.update(true,true,false,true,.2f)==BowGesture::Cancelled,"nocking at edge of string radius without pulling cannot fire");
  test(bowSpeedScale(bowPower(.6f))==1.f && bowSpeedScale(bowPower(.2f))<.6f,"pull distance controls arrow speed");
  const auto arc=arrowPath({0,100,0},{0,0,3000},1.f);test(arc.y< -390 && arc.y> -410 && arc.z==3000,"trajectory uses native arrow speed and gravity units");
  Settings glow;glow.interaction.pickupHighlight=false;std::ostringstream glowFile;glow.write(glowFile);Settings glowRead;std::istringstream glowInput(glowFile.str());glowRead.read(glowInput);test(!glowRead.interaction.pickupHighlight,"pickup highlight toggle survives restart");
  const ItemCalibration asymmetric{{.12f,.07f,-.04f},{27,38,-19},.82f};
  const auto mirrored=mirrorCalibration(asymmetric),twice=mirrorCalibration(mirrored);
  test((twice.offset-asymmetric.offset).length()<.00001f && (twice.rotation-asymmetric.rotation).length()<.00001f && twice.grip==asymmetric.grip,"hand mirror is reversible and preserves grip anchor");
  const auto rm=itemPose({},{0,0,1},{0,1,0},asymmetric,100),lm=itemPose({},{0,0,1},{0,1,0},mirrored,100);
  for(int k=0;k<4;++k){auto v=k==3?origin(rm):axis(rm,k);v.x=-v.x;if(k==0)v=-v;test((v-(k==3?origin(lm):axis(lm,k))).length()<.001f,"mirrored transform reflects X including all Euler rotations");}
  Settings migration;std::istringstream copied("ItemCal_ITMW_FRANCISDAGGER_MIS_R=-0.02 0.075 0.06 -77 0 0 -1\nItemCal_ITMW_FRANCISDAGGER_MIS_L=-0.02 0.075 0.06 -77 0 0 -1\n");migration.read(copied);
  test(migration.interaction.calibration.at("ITMW_FRANCISDAGGER_MIS_L").offset.x==.02f,"reported headset copy migrates to mirrored left grip");
  Settings customLeft;std::istringstream customGrip("ItemCal_ITMW_FRANCISDAGGER_MIS_R=-0.02 0.075 0.06 -77 0 0 -1\nItemCal_ITMW_FRANCISDAGGER_MIS_L=0.03 0.08 0.06 -77 0 0 -1\n");customLeft.read(customGrip);
  test(customLeft.interaction.calibration.at("ITMW_FRANCISDAGGER_MIS_L").offset.x==.03f,"migration retains independently edited left grip");
  Settings explicitPair;std::istringstream pairVersioned("CalibrationMirrorVersion=1\nItemCal_ITMW_FRANCISDAGGER_MIS_R=-0.02 0.075 0.06 -77 0 0 -1\nItemCal_ITMW_FRANCISDAGGER_MIS_L=-0.02 0.075 0.06 -77 0 0 -1\n");explicitPair.read(pairVersioned);
  test(explicitPair.interaction.calibration.at("ITMW_FRANCISDAGGER_MIS_L").offset.x==-.02f,"versioned intentional equal profiles are preserved");
  Settings defaults;test(defaults.interaction.meleeDefault.rotation.x==-77.f && defaults.interaction.meleeDefault.offset.y==.075f,"user-tested sword placement is the melee baseline");
  defaults.interaction.gripLock=true;defaults.interaction.meleeDefault=asymmetric;std::ostringstream defaultText;defaults.write(defaultText);Settings restoredDefault;std::istringstream defaultInput(defaultText.str());restoredDefault.read(defaultInput);
  test(restoredDefault.interaction.gripLock && restoredDefault.interaction.meleeDefault.offset.x==.12f,"grip lock and editable melee baseline survive restart");
  Menu lockMenu;lockMenu.visible=true;lockMenu.page=Menu::Page::Holsters;lockMenu.selected=0;
  test(lockMenu.row()==Menu::HolsterSlot0,"holster overview starts with actual point contents");
  float fraction=0;
  test(meleeContact({0,0,0},{.01f,0,0},{},{1,2,1},0,fraction) && fraction==0,"short blade sweep starting inside NPC registers contact");
  test(meleeContact({-2,0,0},{2,0,0},{},{1,2,1},0,fraction) && std::abs(fraction-.25f)<.0001f,"nearest segment entry is reported");
  test(meleeContact({-2,2,0},{2,2,0},{},{1,2,1},0,fraction),"tangent blade contact is retained");
  test(!meleeContact({-2,2.1f,0},{2,2.1f,0},{},{1,2,1},0,fraction),"blade outside actual body misses");
  test(!meleeContact({2,0,0},{2,0,0},{},{1,2,1},0,fraction),"zero movement outside body is not contact");
  test(meleeContact({0,0,0},{0,0,0},{},{1,2,1},0,fraction),"overlapping stationary segment supports current-blade test during real swing");
  test(!meleeContact({-.2f,0,.8f},{.2f,0,.8f},{},{1,2,.1f},0,fraction) && meleeContact({-.2f,0,.8f},{.2f,0,.8f},{},{1,2,.1f},1.5707963f,fraction),"NPC bounds rotate with its body");
  test(!meleeContact({},{1,0,0},{},{0,1,1},0,fraction),"invalid collision radii are rejected");
  Guard guard{{-30,140,40},{40,140,40},{0,170,0},{0,0,1},100,1000,1};
  test(guard.blocks({0,0,150},900),"crosswise blade covering torso parries front attack");
  test(!guard.blocks({0,0,-150},900),"back attack bypasses guard");
  test(!guard.blocks({0,0,150},1001),"stale guard expires");
  auto wrong=guard;wrong.tip={-30,140,110};test(!wrong.blocks({0,0,150},900),"pointing blade at attacker does not parry");
  wrong=guard;wrong.tip={-30,200,40};test(!wrong.blocks({0,0,150},900),"vertical blade is not crosswise guard");
  wrong=guard;wrong.base.y=wrong.tip.y=50;test(!wrong.blocks({0,0,150},900),"weapon at belt does not cover upper body");
  wrong=guard;wrong.base.z=wrong.tip.z=-20;test(!wrong.blocks({0,0,150},900),"weapon behind player cannot block front attack");
  wrong=guard;wrong.hand=-1;test(!wrong.blocks({0,0,150},900),"cleared or untracked guard cannot parry");
  BodyFrame stable;stable.head={0,170,0};Swing physical;Vec3 tracked(0,1.4f,-.4f),blade(0,140,40);
  auto rest=[&](uint64_t first){for(uint64_t t=first;t<=first+100;t+=10)physical.update(blade,stable,100,t,true,2.2f,&tracked);};
  rest(100);test(physical.armed,"physical strike rearms after a real settled interval");
  BodyFrame turned=stable;turned.forward=normalized({.15f,0,1});turned.right=cross({0,1,0},turned.forward);
  test(!physical.update(blade,turned,100,210,true,2.2f,&tracked) && physical.speed==0,"head rotation cannot create speed with stationary tracked blade");
  tracked.x+=.005f;blade.x+=.5f;test(!physical.update(blade,stable,100,220,true,2.2f,&tracked),"sub-centimetre jitter does not strike");
  physical.reset();rest(300);tracked.x+=.05f;blade.x+=5;
  test(physical.update(blade,stable,100,410,true,2.2f,&tracked),"five-centimetre physical swing crosses speed and travel gates");
  tracked.x+=.05f;blade.x+=5;test(!physical.update(blade,stable,100,420,true,2.2f,&tracked),"follow-through does not open a second strike");
  physical.update(blade,stable,100,430,false,2.2f,&tracked);test(!physical.armed && !physical.valid,"menu and tracking loss clear physical strike history");
  ReleaseMotion release;release.update({0,0,0},100,true);release.update({.1f,0,0},150,true);
  test(release.velocity.x>1 && release.velocity.x<2.1f,"release estimates filtered raw controller velocity");
  release.update({20,0,0},160,true);test(release.velocity.length()==0,"tracking jump cannot launch weapon");
  release.update({20,0,0},180,false);test(!release.valid && release.velocity.length()==0,"tracking loss clears throw velocity");
  }
  for(const auto* name:{"BigHandLeft.uxrh","BigHandRight.uxrh"}) {
    std::ifstream file(std::string("android/assets/vrhands/")+name,std::ios::binary);std::vector<char> data{std::istreambuf_iterator<char>(file),{}};
    auto asset=HandAsset::read(data);test(asset.vertices.size()>100 && asset.indices.size()>300,"real Vice City mesh parses");
    auto rejects=[&](std::vector<char> corrupt) {try {HandAsset::read(corrupt);return false;}catch(const std::runtime_error&){return true;}};
    auto truncated=data;truncated.pop_back();test(rejects(truncated),"truncated hand data rejected");
    auto corrupt=data;corrupt[0]='X';test(rejects(corrupt),"invalid mesh signature rejected");
    corrupt=data;corrupt[corrupt.size()-1]=char(255);corrupt[corrupt.size()-2]=char(255);test(rejects(corrupt),"out of bounds mesh index rejected");
  }
  const auto open=HandAsset::weights(0,0),closed=HandAsset::weights(1,1),mixed=HandAsset::weights(.3f,.7f);
  test(open[0]==1 && closed[3]==1,"authored open and closed poses preserved");
  test(std::abs(mixed[0]+mixed[1]+mixed[2]+mixed[3]-1.f)<.0001f,"grip/trigger interpolation normalized");

  {
    Settings tuning;
    tuning.interaction.bowSight=false;
    tuning.interaction.pickupHighlightRange=9;
    tuning.interaction.meleeDefault.scale=.6f;
    tuning.interaction.calibration["BOW_R"]={{.02f,.04f,0},{0,90,0},.5f,.3f,1.2f,.1f};
    std::ostringstream output;tuning.write(output);
    Settings loaded;std::istringstream stream(output.str());loaded.read(stream);
    const auto& c=loaded.interaction.calibration.at("BOW_R");
    test(!loaded.interaction.bowSight && loaded.interaction.pickupHighlightRange==9,"bow sight and world highlight range persist");
    test(c.scale==.3f && c.stringHeight==1.2f && c.stringCenter==.1f,"arrow scale and bow string dimensions persist");
    test(loaded.interaction.meleeDefault.scale==.6f,"shared melee scale persists");
    Settings legacy;std::istringstream seven("ItemCal_ARROW_R=0 0 0 0 0 0 -1\n");legacy.read(seven);
    test(legacy.interaction.calibration.at("ARROW_R").scale==1.f,"legacy calibration does not shrink models");
    auto bounded=c;bounded.scale=NAN;bounded.stringHeight=99;bounded.stringCenter=-99;bounded.sanitize();
    test(bounded.scale==1.f && bounded.stringHeight==3.f && bounded.stringCenter==-1.f,"invalid calibration dimensions are bounded");
    auto tiny=weaponModelMatrix(low,high,3,wrist,.5f,.25f);
    Vec3 anchor(0,-30,0),end(0,-80,0);tiny.project(anchor);tiny.project(end);
    test((anchor-origin(wrist)).length()<.001f,"arrow scaling keeps chosen grip at hand");
    test(std::abs((end-anchor).length()-12.5f)<.001f,"arrow dimensions scale uniformly");
    for(float yaw:{-90.f,0.f,90.f,180.f}) {
      const auto bowPose=itemPose({10,140,20},{0,0,1},{0,1,0},{{},{0,yaw,0}},100);
      for(float aimYaw:{-90.f,90.f}) {
        ItemCalibration aim;aim.rotation={0,aimYaw,12};aim.offset={.02f,0,.03f};
        const auto preview=itemPose(origin(bowPose),axis(bowPose,2),axis(bowPose,1),aim,100);
        const auto socket=origin(bowPose)+axis(bowPose,2)*-12.f;
        const auto draw=socket-axis(preview,2)*45.f;
        const auto shot=bowShotPose(bowPose,aim,socket,draw,100);
        test((axis(shot,2)-axis(preview,2)).length()<.001f,"draw along calibration ray cannot rotate aim by 90 degrees");
        test(std::abs(dot(axis(shot,1),axis(shot,2)))<.001f && std::abs(axis(shot,2).length()-1)<.001f,"shot keeps orthonormal axes");
        const auto coincident=bowShotPose(bowPose,aim,socket,socket,100);
        test(finite(axis(coincident,2)),"coincident draw hands keep a finite fallback direction");
      }
      ItemCalibration string;string.stringHeight=1.2f;string.stringCenter=.1f;
      const auto tips=bowStringTips(bowPose,string,100);
      test(std::abs((tips[0]-tips[1]).length()-120)<.001f,"calibrated string spans full selected height");
      test(((tips[0]+tips[1])*.5f-origin(bowPose)-axis(bowPose,1)*10).length()<.001f,"string center moves both endpoints vertically");
    }
    test(handTransferReach({9.9f,0,0},{},100) && !handTransferReach({10.1f,0,0},{},100),"hand transfer only within ten centimetres");
    test(handTransferReach({4.9f,0,0},{},50) && !handTransferReach({5.1f,0,0},{},50),"hand transfer radius respects world scale");
    test(!handTransferReach({NAN,0,0},{},100),"invalid tracking cannot transfer weapon");
    Menu sightMenu;Input sightInput;sightInput.focused=true;
    sightMenu.update(sightInput,100);sightInput.leftGrip=sightInput.rightGrip=true;
    test(!sightMenu.update(sightInput,120) && !sightMenu.visible,"two gameplay grips alone cannot consume locomotion input");
    sightMenu.visible=true;sightMenu.page=Menu::Page::Hud;
    const auto rows=sightMenu.rows();sightMenu.selected=int(std::find(rows.begin(),rows.end(),Menu::BowSight)-rows.begin());
    sightInput.a=true;sightMenu.update(sightInput,140);
    test(!sightMenu.settings.interaction.bowSight && sightMenu.changed,"bow sight menu toggle requests immediate save");
    sightMenu.page=Menu::Page::Calibration;
    test(sightMenu.interactionPreview(true),"disabled bow sight keeps calibration preview available");
  }

  {
    const auto bow=itemPose({10,150,20},{0,0,1},{0,1,0},{{-.015f,-.005f,.04f},{0,-97.5f,0}},100);
    ItemCalibration aim;aim.rotation.y=90;
    const auto rest=origin(bow)-axis(bow,0)*30.f;
    const auto fixed=bowShotPose(bow,aim,rest,rest,100);
    const auto forward=axis(fixed,2),up=axis(fixed,1);
    for(float back:{0.f,.08f,.2f,.4f,.6f})for(float lateral:{-.3f,0.f,.3f}) {
      const auto tracked=rest-forward*(back*100)+axis(fixed,0)*(lateral*100)+up*12.f;
      const auto shot=bowShotPose(bow,aim,rest,tracked,100);
      test((axis(shot,2)-forward).length()<.0001f,"support hand movement cannot steer bow aim");
      test(std::abs(bowAxialDraw(rest,tracked,forward,100)-back)<.0001f,"only backward axial travel contributes bow power");
      const auto nock=bowNockPoint(rest,forward,back,100);
      test(cross(rest-nock,forward).length()<.001f,"nocked arrow remains on bow axis under sideways controller movement");
    }
    const auto nock=bowNockPoint(rest,forward,.9f,100);
    test(std::abs((nock-rest).length()-60)<.001f,"visual pull is bounded at full draw");
    test(bowPower(.6f)==1 && bowPower(1)==1 && bowPower(-1)==0,"pull power saturates monotonically without inversion");
    auto stringPose=bow;for(size_t k=0;k<3;++k)stringPose[3][k]=k==0?rest.x:k==1?rest.y:rest.z;
    ItemCalibration string;const auto tips=bowStringTips(stringPose,string,100);
    test(((tips[0]+tips[1])*.5f-rest).length()<.001f && cross(tips[0]-rest,tips[1]-rest).length()<.001f,"resting string endpoints and nock are collinear");
    const auto pulled=bowNockPoint(rest,forward,.3f,100);
    test(cross(tips[0]-pulled,tips[1]-pulled).length()>100,"backward pull creates a triangular string");
    const ItemCalibration right{{-.015f,-.005f,.04f},{0,-97.5f,0}};
    const auto rightPose=itemPose({},{0,0,1},{0,1,0},right,100);
    const auto leftPose=itemPose({},{0,0,1},{0,1,0},mirrorCalibration(right),100);
    const Vec3 lo(-30,-50,-2),hi(30,50,2);
    auto rightModel=weaponModelMatrix(lo,hi,2,rightPose,-1,1,false);
    auto leftModel=weaponModelMatrix(lo,hi,2,leftPose,-1,1,true);
    Vec3 rfront(20,0,0),lfront=rfront;rightModel.project(rfront);leftModel.project(lfront);rfront.x=-rfront.x;
    test((rfront-lfront).length()<.001f,"left bow model front matches mirrored right grip instead of reversing");
    test(arrowNockGrip({-1,-80,-1},{1,10,1})==1 && arrowNockGrip({-1,-10,-1},{1,80,1})==0,"arrow rear endpoint is selected for either authored axis direction");
    HolsterSettings layout;layout.items={"SWORD","POTION","BOW","ARROW"};
    test(swapHolsterItems(layout,0,2) && layout.items[0]=="BOW" && layout.items[2]=="SWORD","moving to occupied holster swaps contents");
    test(assignHolsterItem(layout,1,"BOW") && layout.items[0]=="POTION" && layout.items[1]=="BOW","assigning already holstered weapon swaps rather than duplicates");
    test(assignHolsterItem(layout,1,"EMPTY") && layout.items[1]=="EMPTY","clear leaves an explicitly empty slot");
    const auto unchanged=layout.items;test(!swapHolsterItems(layout,-1,4) && layout.items==unchanged,"invalid holster move preserves loadout");
    Settings loadout;loadout.interaction=layout;loadout.interaction.ignoreWeaponRequirements=true;
    std::ostringstream written;loadout.write(written);Settings read;std::istringstream text(written.str());read.read(text);
    test(read.interaction.items==layout.items && read.interaction.ignoreWeaponRequirements,"arbitrary holster assignments empty slots and requirement option persist");
    Menu stock;Input input;input.focused=true;stock.update(input,100);stock.visible=true;
    auto rows=stock.rows();stock.selected=int(std::find(rows.begin(),rows.end(),Menu::OpenGameInterface)-rows.begin());
    input.x=1;stock.update(input,120);test(stock.action==-1 && stock.visible,"horizontal navigation cannot open game interface accidentally");
    input.x=0;stock.update(input,140);input.a=true;stock.update(input,160);
    test(stock.action==Menu::OpenGameInterface && !stock.visible,"open game interface requests native menu and hides VR menu");
    test(stock.update(input,180) && stock.action==-1,"held activation cannot also confirm an item in native interface");
    {
      Menu stats;Input in;in.focused=true;stats.update(in,100);stats.visible=true;
      auto statRows=stats.rows();const auto at=std::find(statRows.begin(),statRows.end(),Menu::OpenCharacterStats);
      test(at!=statRows.end(),"main VR menu lists character stats");
      stats.selected=int(at-statRows.begin());
      in.x=1;stats.update(in,120);test(stats.action==-1 && stats.visible,"horizontal navigation cannot open character stats accidentally");
      in.x=0;stats.update(in,140);in.a=true;stats.update(in,160);
      test(stats.action==Menu::OpenCharacterStats && !stats.visible,"character stats requests native status screen and hides VR menu");
    }
    input.a=false;stock.update(input,200);stock.visible=true;stock.page=Menu::Page::Holsters;stock.selected=2;
    input.a=true;stock.update(input,220);
    test(stock.page==Menu::Page::HolsterSlot && stock.holsterPoint==2,"each holster summary opens that point's item management");
    input.a=false;stock.update(input,240);rows=stock.rows();stock.selected=int(std::find(rows.begin(),rows.end(),Menu::HolsterDrop)-rows.begin());
    input.x=1;stock.update(input,260);test(stock.action==-1,"scrolling cannot drop a holstered inventory item");
    input.x=0;stock.update(input,280);input.a=true;stock.update(input,300);
    test(stock.action==Menu::HolsterDrop,"explicit activation requests selected holster drop");
  }

  {
    ItemCalibration c;c.stringSide=.07f;c.stringDepth=-.04f;c.stringCenter=.02f;
    const auto pose=itemPose({10,150,20},{0,0,1},{0,1,0},{{},{12,-73,26}},100);
    const auto tips=bowStringTips(pose,c,100);
    test(((tips[0]+tips[1])*.5f-origin(pose)-bowStringOffset(pose,c,100)).length()<.001f,"horizontal string calibration moves both tips and nock together");
    test(std::abs((tips[0]-tips[1]).length()-84.f)<.001f,"horizontal offsets preserve string height");
    const auto mirrored=mirrorCalibration(c);
    test(mirrored.stringSide==-.07f && mirrored.stringDepth==-.04f,"horizontal string calibration mirrors across hands");
    Settings saved;saved.interaction.calibration["BOW_R"]=c;
    std::ostringstream text;saved.write(text);Settings restored;std::istringstream read(text.str());restored.read(read);
    const auto loaded=restored.interaction.calibration.at("BOW_R");
    test(loaded.stringSide==c.stringSide && loaded.stringDepth==c.stringDepth,"both horizontal string offsets survive save and load");
    Settings legacy;std::istringstream old("ItemCal_BOW_R=0 0 0 0 90 0 -1 1 .84 .1\n");legacy.read(old);
    test(legacy.interaction.calibration.at("BOW_R").stringSide==0 && legacy.interaction.calibration.at("BOW_R").stringDepth==0,"existing calibration retains placement until horizontal offset is edited");
    c.stringSide=NAN;c.stringDepth=99;c.sanitize();test(c.stringSide==0 && c.stringDepth==1,"invalid horizontal offsets are bounded");
    for(bool left:{false,true})for(float yaw:{-170.f,-90.f,-30.f,0.f,55.f,120.f})for(float pitch:{-85.f,-20.f,0.f,40.f,85.f}) {
      const auto socket=itemPose({},{0,0,1},{0,1,0},{{},{pitch,yaw,17}},100);
      auto grip=socket;for(size_t k=0;k<3;++k)grip[1][k]=-grip[1][k];
      const auto expected=handBasis(grip,socket,left,true);
      for(float noise:{-.000001f,.000001f}) {
        auto perturbed=grip;const auto z=axis(grip,2)+expected.right*noise;
        perturbed[2][0]=z.x;perturbed[2][1]=z.y;perturbed[2][2]=z.z;
        const auto stable=handBasis(perturbed,socket,left,true);
        test(dot(stable.right,expected.right)>.99999f && dot(stable.up,expected.up)>.99999f,"anchored palm cannot flip when perpendicular dot changes sign");
        test(std::abs(dot(stable.right,stable.forward))<.00001f && std::abs(stable.right.length()-1)<.00001f,"support palm basis remains orthonormal");
      }
    }
    Menu menu;Input in;in.focused=true;menu.update(in,1);menu.visible=true;menu.page=Menu::Page::Support;
    for(auto row:{Menu::CalStringSide,Menu::CalStringDepth}) {
      const auto rows=menu.rows();menu.selected=int(std::find(rows.begin(),rows.end(),row)-rows.begin());
      test(menu.selected<int(rows.size()) && menu.calibrationValueRow(),"horizontal string controls support calibration trigger editing");
      in.x=1;menu.update(in,1000+int(row));test(menu.action==row,"horizontal calibration edit reaches gameplay save action");
      in.x=0;menu.update(in,2000+int(row));
    }
  }

  {
    for(bool left:{false,true}) {
      const auto grip=itemPose({20,130,10},{0,0,1},{0,1,0},{{},{-10,30,20}},100);
      const auto aim=itemPose(origin(grip),axis(grip,2),axis(grip,1),{{},{12,0,0}},100);
      const auto weapon=itemPose(origin(grip),axis(aim,2),-axis(grip,1),defaultMeleeGrip(),100);
      const auto palm=handPalmPose(grip,aim,left,false),local=relativeHandPalm(weapon,grip,aim,left);
      auto attached=weapon*local;
      for(int a=0;a<3;++a)test((axis(attached,a)-axis(palm,a)).length()<.0001f,"attaching primary palm preserves its initial orientation");
      test((origin(attached)-origin(palm)).length()<.001f,"attaching primary palm preserves grip position");
      for(float turn:{-120.f,-35.f,0.f,45.f,155.f}) {
        auto next=weapon;next.rotateOY(turn);next.translate(30,-20,50);
        attached=next*local;auto inverse=next;inverse.inverse();const auto recovered=inverse*attached;
        for(int a=0;a<3;++a)test((axis(recovered,a)-axis(local,a)).length()<.0001f,"primary palm cannot rotate independently of held two-handed weapon");
        test((origin(recovered)-origin(local)).length()<.001f,"primary palm cannot slide along weapon under tracked movement");
      }
    }
    struct BowMesh {
      struct Wedge{uint16_t index;};struct Triangle{std::array<uint16_t,3> wedges;};
      struct Part{std::vector<Wedge> wedges;std::vector<Triangle> triangles;};
      std::vector<Vec3> positions;std::vector<Part> sub_meshes;
    };
    auto makeBow=[] {
      BowMesh mesh;
      mesh.positions={{-50,0,0},{50,0,0},{0,4,30},{-45,0,-2},{45,0,-2},{0,1,-1}};
      mesh.sub_meshes.resize(1);auto& part=mesh.sub_meshes[0];part.wedges.resize(6);
      for(uint16_t i=0;i<6;++i)part.wedges[i].index=i;
      part.triangles={{{0,1,2}},{{3,4,5}}};return mesh;
    };
    float detectedHeight=0;auto bow=makeBow();test(removeBowString(bow,&detectedHeight)==1 && bow.sub_meshes[0].triangles.size()==1,"only detached thin span is removed from bow");
    test(detectedHeight==90,"authored string span is captured before triangles are removed");
    test(bow.positions.size()==6 && bow.sub_meshes[0].wedges.size()==6 && bow.sub_meshes[0].triangles[0].wedges[2]==2,"body indices and morph vertex IDs remain unchanged");
    auto unknown=makeBow();unknown.positions[5].z=20;test(removeBowString(unknown)==0 && unknown.sub_meshes[0].triangles.size()==2,"ambiguous curved geometry is preserved");
    auto invalid=makeBow();invalid.sub_meshes[0].triangles[0].wedges[0]=100;test(removeBowString(invalid)==0,"invalid mesh indices cannot trigger a destructive filter");
    auto onlyString=makeBow();onlyString.sub_meshes[0].triangles.erase(onlyString.sub_meshes[0].triangles.begin());test(removeBowString(onlyString)==0,"filter cannot erase the entire mesh");
  }

  {
    const std::array<ItemCalibration,3> profiles={ItemCalibration{{.02f,0,0},{0,97.5f,0}},ItemCalibration{{},{0,-90,0}},ItemCalibration{{.3f,0,.02f},{0,-95,5}}};
    Settings saved;storeRangedDefaults(saved.interaction,false,profiles,true);
    auto cross=profiles;cross[0].rotation={0,180,47.5f};storeRangedDefaults(saved.interaction,true,cross,false);
    std::ostringstream text;saved.write(text);Settings loaded;std::istringstream file(text.str());loaded.read(file);
    test(loaded.interaction.calibration.at(rangedDefaultKey(false)).rotation.y==-97.5f,"left-hand apply normalizes bow family default to the right-hand convention");
    test(loaded.interaction.calibration.at(rangedDefaultKey(true)).rotation.z==47.5f,"crossbow family default persists independently");
    test(loaded.interaction.calibration.at(rangedDefaultKey(false,"SUP")).offset.x==-.3f,"family support default is saved with mirrored position");
    const auto pose=Matrix::mkIdentity();
    const auto light=weaponModelMatrix({-29,-6,-27},{29,6,27},4,pose),heavy=weaponModelMatrix({-44,-8,-49},{44,8,49},4,pose);
    for(int a=0;a<3;++a)test((axis(light,a)-axis(heavy,a)).length()<.001f,"light and heavy crossbows use consistent authored axes despite different longest dimensions");
    Menu menu;Input in;in.focused=true;menu.update(in,1);menu.visible=true;menu.page=Menu::Page::CalibrationHome;
    for(auto row:{Menu::CalUseBowDefault,Menu::CalUseCrossbowDefault}) {
      auto rows=menu.rows();menu.selected=int(std::find(rows.begin(),rows.end(),row)-rows.begin());
      test(menu.selected<int(rows.size()),"each ranged category has a visible apply-default action");
      in.x=1;menu.update(in,1000+int(row));test(menu.action==-1,"navigating cannot overwrite a family default");
      in.x=0;menu.update(in,2000+int(row));in.a=true;menu.update(in,3000+int(row));test(menu.action==row,"A activates selected family default");
      in.a=false;menu.update(in,4000+int(row));
    }
  }
  {
    Settings old;std::istringstream input("ItemCal_BOW_SUP_R=-0.3 0 0.02 0 95 -5\nItemCal_DEFAULT_BOW_SUP_R=-0.3 0 0.02 0 95 -5\n");old.read(input);
    test(old.interaction.calibration.at("BOW_STR_R").offset==Vec3(-.3f,0,.02f),"old support position becomes an independent saved string anchor");
    old.interaction.calibration.at("BOW_SUP_R").offset.x+=.1f;
    std::ostringstream saved;old.write(saved);Settings loaded;std::istringstream readback(saved.str());loaded.read(readback);
    test(loaded.interaction.calibration.at("BOW_STR_R").offset.x==-.3f,"saving and reloading support edits cannot move the migrated string");
    test(loaded.interaction.calibration.at("DEFAULT_BOW_STR_R").offset.x==-.3f,"shared bow default retains its old string position");
    int values=0;
    for(int page=0;page<=int(Menu::Page::HolsterSlot);++page) {
      Menu listing;listing.page=Menu::Page(page);
      for(size_t row=0;row<listing.rows().size();++row) {
        listing.selected=int(row);if(!listing.valueRow())continue;++values;
        for(int direction:{-1,1}) {
          Menu m;m.visible=true;m.page=listing.page;m.selected=int(row);Input in;in.focused=true;m.update(in,100);
          if(direction<0)in.secondaryTrigger=1;else in.trigger=1;
          in.y=1;m.update(in,120);
          test(m.selected==int(row),"trigger editing freezes row selection despite stick drift on every value row");
          test(m.changed || m.action>=0 || m.row()==Menu::CalStep || m.row()==Menu::CalHand || m.row()==Menu::HolsterPoint || m.row()==Menu::HolsterMoveTarget,"both triggers reach every adjustable row");
          if(m.action>=0)test(m.actionDirection==direction,"action-backed selectors receive the left/right trigger direction");
          if(m.toggleRow()){m.update(in,800);test(!m.changed && m.action==-1,"held trigger does not oscillate a checkbox");}
          in.trigger=in.secondaryTrigger=1;m.update(in,900);test(!m.changed && m.action==-1,"both triggers held cancel value changes");
        }
      }
    }
    test(values>100,"all menu pages participate in trigger regression coverage");
  }
  {
    BodyFrame body;body.update(Matrix::mkIdentity());Swing swing;Vec3 point(0,0,100),tracking(0,0,1);
    swing.update(point,body,100,100,true,2.2f,&tracking);
    point.x+=5;tracking.x+=.05f;
    test(swing.update(point,body,100,120,true,2.2f,&tracking),"moving weapon can start a strike without a sixty-millisecond stationary hold");
    for(uint64_t now=140;now<=440;now+=20){point.x+=5;tracking.x+=.05f;swing.update(point,body,100,now,true,2.2f,&tracking);}
    test(swing.active,"long blade keeps its contact window beyond 220 milliseconds while still swinging");
    swing.contact();test(!swing.active,"first contact consumes the current stroke");
    point.x+=5;tracking.x+=.05f;test(!swing.update(point,body,100,460,true,2.2f,&tracking),"continuing through an enemy cannot hit it repeatedly");
    point.x-=5;tracking.x-=.05f;
    test(swing.update(point,body,100,480,true,2.2f,&tracking),"a returning slash rearms directly without requiring a stationary pose");
    swing.update(point,body,100,500,false,2.2f,&tracking);test(!swing.active,"menu or tracking loss closes an active stroke");
  }
  std::printf("VR interaction: %d checks passed\n",checks);
}
