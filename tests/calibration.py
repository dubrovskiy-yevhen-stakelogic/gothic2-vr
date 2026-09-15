from pathlib import Path
import sys
r=Path(__file__).resolve().parents[1];source=(r/'engine/common/vr/vrgameplay.cpp').read_text(encoding='utf-8')
def method(signature):
    start=source.index(signature);at=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    return source[start:at]
fixture=r'''
#include "vr/vrcontrols.h"
#include <vector>
#include <cctype>
#include <cstdio>
#include <cstdlib>
using namespace Vr;
constexpr int ITM_CAT_NF=1,ITM_CAT_FF=2,ITM_CAT_POTION=4,ITM_CAT_MUN=8;
struct Item {size_t id;int flags;size_t quantity=1;bool crossbow=false,twoHanded=false;bool is2H()const{return twoHanded;}
  int mainFlag()const{return flags;}bool isCrossbow()const{return crossbow;}size_t clsId()const{return id;}size_t count()const{return quantity;}struct Data{std::string visual="mesh";int munition=2;};Data handle()const{return {};}
};
struct Inventory {static constexpr int T_Inventory=0;std::vector<Item> items;
  struct Iter {const std::vector<Item>* items;size_t i=0;bool isValid()const{return i<items->size();}void operator++(){++i;}const Item& operator*()const{return (*items)[i];}const Item* operator->()const{return &(*items)[i];}};
  Iter iterator(int)const{return {&items};}
};
struct Mesh{float vrBowStringHeight=0;};struct Resources{static inline Mesh mesh;static Mesh* loadVrBowMesh(const std::string&){return &mesh;}};
struct World;
struct Npc {World* owner=nullptr;World& world()const{return *owner;}Inventory inv;size_t active=size_t(-1);const Inventory& inventory()const{return inv;}
  const Item* getItem(size_t id)const{for(auto& i:inv.items)if(i.id==id)return &i;return nullptr;}
  const Item* activeWeapon()const{return getItem(active);}
};
struct Symbol {std::string text;std::string_view name()const{return text;}};
struct Script {std::map<size_t,Symbol> symbols;size_t findSymbolIndex(const std::string& key)const{for(auto& entry:symbols)if(entry.second.text==key)return entry.first;return size_t(-1);}const Symbol* findSymbol(size_t id)const{auto i=symbols.find(id);return i==symbols.end()?nullptr:&i->second;}};
struct World {Script vm;Script& script(){return vm;}};
namespace Vr {
int cycle(int value,int delta,int count){return count>0?(value+delta+count)%count:0;}
struct Gameplay {static constexpr size_t None=size_t(-1);struct Held{size_t id=None;int slot=-1;};std::array<Held,2> held;
  struct Entry{size_t id;int category;};std::vector<Entry> items;
  bool seedRangedDefaults(HolsterSettings&)const;
  float units=100;
  World* context=nullptr;Npc* contextPlayer=nullptr;size_t calibrationItem=None;
  static bool compatible(const Item&,int);
  const Item* resolve(Npc&,int,const HolsterSettings&)const;
  std::string itemKey(size_t,int,std::string_view={})const;
  ItemCalibration profile(size_t,int,const HolsterSettings&,std::string_view={})const;
  void selectCalibration(Npc&,Menu&,int=0);
};
'''
for signature in ['std::string upper(', 'int itemKind(', 'bool Gameplay::compatible(', 'const Item* Gameplay::resolve(', 'std::string Gameplay::itemKey(', 'ItemCalibration Gameplay::profile(', 'bool Gameplay::seedRangedDefaults(', 'void Gameplay::selectCalibration(']:fixture+=method(signature)+'\n'
fixture+=r'''
}
int checks=0;void test(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
int main(){
  World world;world.vm.symbols={{1,{"SWORD"}},{2,{"POTION"}},{3,{"BOW"}}};Npc player;player.inv.items={{1,ITM_CAT_NF},{2,ITM_CAT_POTION},{3,ITM_CAT_FF}};
  Gameplay g;g.context=&world;g.contextPlayer=&player;g.held[0].id=1;player.active=3;Menu menu;
  g.selectCalibration(player,menu);test(g.calibrationItem==1 && menu.calibrationHand==0,"enter captures held left weapon even when right hand and legacy active weapon differ");
  g.selectCalibration(player,menu,1);test(g.calibrationItem==2,"next item comes from owned inventory without drawing");
  test(g.held[0].id==1 && player.active==3,"preview selection does not replace actual weapon ownership");
  test(menu.holsterPoint==1,"potion preview selects chest socket");
  HolsterSettings settings;settings.calibration["SWORD_R"]={{.4f,0,0},{}};settings.calibration["SWORD_HOL"]={{0,.2f,0},{}};
  test(g.profile(1,1,settings).offset.x==.4f && g.profile(1,1,settings,"HOL").offset.y==.2f,"production lookup separates hand and holstered model profiles");
  test(g.profile(1,0,settings).offset.x==-.4f,"unconfigured left hand inherits a mirrored right calibration");
  settings.calibration["SWORD_L"]={{.12f,0,0},{}};
  test(g.profile(1,0,settings).offset.x==.12f,"explicit left calibration takes precedence over the mirrored fallback");
  settings.calibration["BOW_R"]={{.01f,.02f,.03f},{10,90,20},.5f,.3f,1.2f,.1f};
  const auto leftBow=g.profile(3,0,settings);
  test(leftBow.rotation.y==-90 && leftBow.rotation.z==-20 && leftBow.rotation.x==10,"bow rotation mirrors to the other hand");
  test(leftBow.scale==.3f && leftBow.stringHeight==1.2f && leftBow.stringCenter==.1f,"bow dimensions carry across hands");
  test(g.profile(1,1,settings,"SUP").offset.z==-.16f,"support defaults to second hand behind primary grip");
  test(g.profile(2,0,settings).rotation.x==-90,"undrawn potion preview gets upright default");
  player.owner=&world;world.vm.symbols[4]={"CROSSBOW"};player.inv.items.push_back({4,ITM_CAT_FF,1,true});
  settings.items={"CROSSBOW","EMPTY","SWORD","EMPTY"};
  test(g.resolve(player,0,settings)->clsId()==4 && g.resolve(player,2,settings)->clsId()==1,"crossbow and sword can occupy arbitrary holster points");
  test(g.resolve(player,1,settings)==nullptr,"explicitly empty holster cannot fall back to an inventory potion");
  settings.calibration.erase("BOW_R");
  test(g.profile(3,1,settings).rotation.y==-90 && g.profile(3,1,settings,"AIM").rotation.y==90,"default bow model and aim axes agree");
  test(g.profile(3,1,settings,"SUP").offset.x==-.2f,"default string grip stays separate from transfer grip");

  settings.calibration["BOW_R"]={{-.015f,-.005f,.04f},{0,-97.5f,0}};
  settings.calibration["BOW_AIM_R"]={{},{0,90,0}};
  settings.calibration["BOW_SUP_R"]={{-.3f,0,.02f},{0,95,-5}};
  settings.calibration["CROSSBOW_R"]={{-.04f,.02f,.02f},{0,180,47.5f}};
  settings.calibration["CROSSBOW_AIM_R"]={{.045f,0,0},{180,2,0}};
  g.items={{3,3},{4,4}};
  test(g.seedRangedDefaults(settings),"first run seeds separate defaults from already calibrated bow and crossbow");
  test(!g.seedRangedDefaults(settings),"existing family defaults cannot be overwritten by later automatic seeding");
  world.vm.symbols[5]={"SECOND_BOW"};world.vm.symbols[6]={"SECOND_CROSSBOW"};
  player.inv.items.push_back({5,ITM_CAT_FF});player.inv.items.push_back({6,ITM_CAT_FF,1,true});
  test(g.profile(5,1,settings).rotation.y==-97.5f && g.profile(5,1,settings,"AIM").rotation.y==90,"uncalibrated bow inherits model and aim rotations");
  test(g.profile(5,0,settings).rotation.y==97.5f && g.profile(5,0,settings,"SUP").offset.x==.3f,"shared bow default mirrors to left hand with support grip");
  test(g.profile(6,1,settings).rotation.z==47.5f && g.profile(6,1,settings,"AIM").rotation.x==180,"uncalibrated crossbow inherits its own family rather than bow rotations");
  test(g.profile(6,1,settings,"SUP").offset.z==-.04f,"missing crossbow support keeps its category default");
  settings.calibration["SECOND_BOW_R"]={{},{0,12,0}};
  test(g.profile(5,1,settings).rotation.y==12 && g.profile(5,0,settings).rotation.y==-12,"explicit per-item profiles override family defaults in both hands");
  test(g.profile(6,1,settings,"HOL").rotation.y==0,"shared hand grip defaults do not rotate holstered models");
  settings.calibration["BOW_HOL"]={{.02f,-.03f,.04f},{60,0,0},.4f,.8f};
  test(g.seedRangedDefaults(settings),"existing held defaults do not prevent automatic holster seeding");
  const auto bowHol=g.profile(5,0,settings,"HOL");
  test(bowHol.offset==Vec3(.02f,-.03f,.04f) && bowHol.rotation.x==60 && bowHol.grip==.4f && bowHol.scale==.8f,"uncalibrated bow inherits the complete saved holstered model pose");
  test(g.profile(5,1,settings,"HOL").offset==bowHol.offset,"shared holster pose has no left/right mirroring");
  test(g.profile(6,1,settings,"HOL").rotation.x==-30,"crossbow holster fallback accounts for its different length axis");
  settings.calibration["SECOND_CROSSBOW_HOL"]={{.1f,0,0},{20,30,40}};
  test(g.profile(6,0,settings,"HOL").rotation.z==40,"custom crossbow holster overrides its category default");
  test(!g.seedRangedDefaults(settings),"later loads preserve the chosen shared holster profiles");
  Settings persisted;persisted.interaction=settings;std::ostringstream written;persisted.write(written);Settings restored;std::istringstream read(written.str());restored.read(read);
  test(g.profile(5,0,restored.interaction,"HOL").rotation.x==60,"shared bow holster survives saving and restart");
  Resources::mesh.vrBowStringHeight=180;
  settings.calibration.erase("SECOND_BOW_R");
  test(std::abs(g.profile(5,1,settings).stringHeight-1.8f)<.001f,"uncalibrated long bow uses its own authored string span");
  settings.calibration[rangedDefaultKey(false)].scale=.5f;
  test(std::abs(g.profile(5,0,settings).stringHeight-.9f)<.001f,"automatic string span follows model scale and mirrors without changing length");
  g.units=50;test(std::abs(g.profile(5,1,settings).stringHeight-1.8f)<.001f,"automatic span matches model geometry at a changed world scale");g.units=100;
  settings.calibration["SECOND_BOW_R"].stringHeight=1.23f;
  test(g.profile(5,0,settings).stringHeight==1.23f,"manually calibrated string height remains authoritative");
  test(g.profile(6,1,settings).stringHeight==.84f,"bow span detection does not alter crossbows");
  Resources::mesh.vrBowStringHeight=0;settings.calibration.erase("SECOND_BOW_R");
  test(g.profile(5,1,settings).stringHeight==.84f,"unidentified mesh preserves inherited height");
  world.vm.symbols[7]={"TWO_HAND_SWORD"};player.inv.items.push_back({7,ITM_CAT_NF,1,false,true});
  test(g.profile(7,1,settings,"SUP").rotation.x==90 && g.profile(7,1,settings,"SUP").offset.z==-.19f,"two-handed support uses corrected axe orientation by default");
  settings.calibration["DEFAULT_2H_SUP_R"]={{-.02f,.03f,-.2f},{90,15,20}};
  const auto support=g.profile(7,0,settings,"SUP");
  test(support.offset.x==.02f && support.rotation.x==90 && support.rotation.y==-15,"two-handed support default mirrors for left weapon hand");
  settings.calibration["TWO_HAND_SWORD_SUP_R"].rotation.x=12;
  test(g.profile(7,1,settings,"SUP").rotation.x==12,"individual support edit overrides shared two-hand default");
  Settings migrated;std::istringstream oldAxe("ItemCal_ITMW_2H_ORCAXE_01_SUP_R=-0.01 0.04 -0.19 90 0 0 -1 1 0.84 0 0 0\n");migrated.read(oldAxe);
  test(migrated.interaction.calibration.at("DEFAULT_2H_SUP_R").rotation.x==90,"existing calibrated axe seeds two-handed support family");
  player.inv.items.clear();g.held[0].id=Gameplay::None;g.selectCalibration(player,menu);test(g.calibrationItem==Gameplay::None,"empty inventory produces no stale calibration target");
  std::printf("VR calibration selection: %d production checks passed\n",checks);
}
'''
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True);(out/'calibration.cpp').write_text(fixture,encoding='utf-8')
print('Extracted calibration selection and profile lookup')
