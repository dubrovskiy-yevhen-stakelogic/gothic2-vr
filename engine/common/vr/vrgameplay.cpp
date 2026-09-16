#include "vrgameplay.h"
#if defined(GOTHIC2VR_OPENXR)
#include "questxr.h"
#include "gothic.h"
#include "world/world.h"
#include "world/focus.h"
#include "world/objects/npc.h"
#include "world/objects/item.h"
#include "game/gamescript.h"
#include "game/inventory.h"
#include "graphics/worldview.h"
#include "graphics/mesh/protomesh.h"
#include "resources.h"
#include <Tempest/Log>
#include <cctype>

namespace Vr {
namespace {
int cycle(int value,int delta,int count) { return count>0?(value+delta+count)%count:0; }
std::string upper(std::string_view str) { std::string s(str);for(auto& c:s)c=char(std::toupper(static_cast<unsigned char>(c)));return s; }
const char* slots[]={"Right belt","Left chest","Left back","Right back"};
const char* itemCategories[]={"All","One-handed weapons","Two-handed weapons","Bows","Crossbows","Potions","Arrows / bolts","Armor","Magic","Other"};
const char* enemyCategories[]={"All","Animals","Goblins","Orcs","Undead","Golems","Other monsters","People"};
const char* weatherNames[]={"Original","Clear","Overcast","Rain"};
int itemKind(const Item& item) {
  if((item.mainFlag()&ITM_CAT_POTION)!=0)return 1;
  if((item.mainFlag()&ITM_CAT_FF)!=0)return 2;
  if((item.mainFlag()&ITM_CAT_MUN)!=0)return 3;
  return 0;
}
bool clearPath(World& world,Vec3 from,Vec3 to,float margin=3.f) {
  const auto hit=world.physic()->ray(from,to);
  return !hit.hasCol || (hit.v-to).length()<margin;
}
}
static std::string requirementWarning(int32_t atr,int32_t need,int32_t have) {
  const char* name=atr==ATR_STRENGTH?"Strength":atr==ATR_DEXTERITY?"Dexterity":atr==ATR_MANAMAX?"Max mana":atr==ATR_HITPOINTSMAX?"Max health":"Attribute";
  char text[160];
  std::snprintf(text,sizeof(text),"%s %d required (you have %d) - damage -%d%%",name,int(need),int(have),int(std::lround((1.f-unqualifiedWeaponDamage)*100.f)));
  return text;
}
Npc* Gameplay::healthTarget(World& world,const Focus& focus,uint64_t now) {
  if(hudTargetWorld!=&world){hudTargetWorld=&world;hudTargetNpc=nullptr;}
  Focus previous;previous.npc=hudTargetNpc;
  hudTargetNpc=world.validateFocus(previous).npc;
  auto focused=world.validateFocus(focus).npc;
  // interaction focus wins: the HUD shows the NPC that B talks to
  if(focused && focused!=world.player() && !focused->isDead()){hudTargetNpc=focused;return focused;}
  Npc* selected=nullptr;float best=2.f;
  auto consider=[&](Npc& npc,bool retained) {
    if(&npc==world.player() || npc.isDead())return;
    const auto feet=npc.position(),top=npc.displayPosition();
    float score=-1;
    for(float height:{.4f,.7f,1.f}) {
      const auto point=feet+(top-feet)*height;
      const float candidate=healthGazeScore(body.head,healthGazeDirection,point,units,retained);
      if(candidate<0 || (score>=0 && candidate>=score))continue;
      if(!clearPath(world,body.head,point))continue;
      score=candidate;
    }
    if(score<0)return;
    // Prefer the current target until another NPC is substantially closer to the gaze center.
    if(retained)score*=.7f;
    if(score<best){best=score;selected=&npc;}
  };
  if(hudTargetNpc)consider(*hudTargetNpc,true);
  if(focused && focused!=hudTargetNpc)consider(*focused,false);
  world.detectNpc(body.head,20.f*units,[&](Npc& npc) {
    if(&npc!=hudTargetNpc && &npc!=focused)consider(npc,false);
  });
  hudTargetNpc=selected;
  if(selected)return selected;
  if(context!=&world || now>targetUntil)return nullptr;
  Focus contact;contact.npc=recentTarget;
  auto target=world.validateFocus(contact).npc;
  if(!target || target->isDead() || (target->position()-body.head).length()>800.f)return nullptr;
  return target;
}

void Gameplay::suspend() {
  for(auto& g:grips) g.update(0,false);
  for(auto& t:triggers) t.update(0,false);
  for(auto& h:held) { h.swing.reset();h.strikeUntil=0; }
  for(auto& m:releaseMotion)m.reset();
  for(auto& r:releaseDebounce)r.reset();
  bowGesture.reset();drawing=false;nockHand=-1;drawLength=0;supportHand=swordMain=-1;
}
bool Gameplay::compatible(const Item& item,int slot) {
  switch(slot) {
    case 0:return (item.mainFlag()&ITM_CAT_NF)!=0;
    case 1:return (item.mainFlag()&ITM_CAT_POTION)!=0;
    case 2:return (item.mainFlag()&ITM_CAT_FF)!=0;
    case 3:return (item.mainFlag()&ITM_CAT_MUN)!=0;
  }return false;
}
const Item* Gameplay::resolve(Npc& player,int slot,const HolsterSettings& settings) const {
  if(settings.items[size_t(slot)]=="EMPTY")return nullptr;
  if(!settings.items[size_t(slot)].empty()) {
    auto id=player.world().script().findSymbolIndex(settings.items[size_t(slot)]);
    auto item=player.getItem(id);return item && item->count()>0 && (compatible(*item,0) || compatible(*item,1) || compatible(*item,2) || compatible(*item,3))?item:nullptr;
  }
  if(slot==3) {
    for(const auto& h:held) if(h.slot==2) {
      const auto bow=player.getItem(h.id);
      if(bow) return player.getItem(size_t(bow->handle().munition));
    }
  }
  const Item* best=nullptr;
  for(auto it=player.inventory().iterator(Inventory::T_Inventory);it.isValid();++it) {
    if(!compatible(*it,slot) || it->count()==0) continue;
    if(!best) best=&*it;
    if(slot==1) { auto name=upper(it->handle().visual);if(name.find("HEALTH")!=std::string::npos) return &*it; }
  }
  return best;
}
std::string Gameplay::itemKey(size_t id,int hand,std::string_view domain) const {
  if(!context || id==None)return {};
  auto symbol=context->script().findSymbol(id);if(!symbol)return {};
  auto key=std::string(symbol->name());
  if(domain=="HOL")return key+"_HOL";
  if(!domain.empty())key+="_"+std::string(domain);
  return key+(hand==0?"_L":"_R");
}
ItemCalibration Gameplay::profile(size_t id,int hand,const HolsterSettings& settings,std::string_view domain) const {
  auto key=itemKey(id,hand,domain);auto found=settings.calibration.find(key);
  if(found!=settings.calibration.end())return found->second;
  if(domain!="HOL") {
    const auto other=settings.calibration.find(itemKey(id,1-hand,domain));
    if(other!=settings.calibration.end())return mirrorCalibration(other->second);
  }
  ItemCalibration result;
  if(contextPlayer)if(auto item=contextPlayer->getItem(id);item && compatible(*item,2)) {
    const auto common=settings.calibration.find(rangedDefaultKey(item->isCrossbow(),domain));
    result=common==settings.calibration.end()?defaultRangedCalibration(item->isCrossbow(),domain):common->second;
    // Per-item edits above remain authoritative; inherited heights fit each bow's authored string.
    if(domain.empty() && !item->isCrossbow())if(auto mesh=Resources::loadVrBowMesh(item->handle().visual);mesh && mesh->vrBowStringHeight>0)
      result.stringHeight=mesh->vrBowStringHeight*result.scale/units;
    return hand==0 && domain!="HOL"?mirrorCalibration(result):result;
  }
  if(domain.empty() && contextPlayer)if(auto item=contextPlayer->getItem(id);item && compatible(*item,0))return hand==0?mirrorCalibration(settings.meleeDefault):settings.meleeDefault;
  if(domain=="SUP" && contextPlayer)if(auto item=contextPlayer->getItem(id);item && item->is2H()) {
    const auto common=settings.calibration.find("DEFAULT_2H_SUP_R");
    result=common==settings.calibration.end()?defaultTwoHandSupport():common->second;
    return hand==0?mirrorCalibration(result):result;
  }
  if(domain=="SUP"){result.offset.z=-.16f;if(contextPlayer)if(auto item=contextPlayer->getItem(id);item && compatible(*item,2))result.offset.z=-.04f;}
  if(domain.empty() && contextPlayer)if(auto item=contextPlayer->getItem(id);item && compatible(*item,1))result.rotation.x=-90.f;
  return result;
}
std::string Gameplay::calibrationKey(int hand) const {
  return itemKey(calibrating?calibrationItem:held[size_t(std::clamp(hand,0,1))].id,hand);
}
ItemCalibration Gameplay::calibrationFor(int hand,const HolsterSettings& settings) const {
  return profile(calibrating?calibrationItem:held[size_t(std::clamp(hand,0,1))].id,hand,settings);
}
void Gameplay::selectCalibration(Npc& player,Menu& menu,int direction) {
  std::vector<size_t> choices;
  for(auto it=player.inventory().iterator(Inventory::T_Inventory);it.isValid();++it) {
    if(it->count()==0)continue;
    if((it->mainFlag()&(ITM_CAT_NF|ITM_CAT_FF|ITM_CAT_POTION|ITM_CAT_MUN))!=0)choices.push_back(it->clsId());
  }
  if(direction==0) {
    calibrationItem=None;
    for(int hand:{menu.calibrationHand,1-menu.calibrationHand})if(held[size_t(hand)].id!=None && player.getItem(held[size_t(hand)].id)) {
      calibrationItem=held[size_t(hand)].id;menu.calibrationHand=hand;break;
    }
    if(calibrationItem==None && player.activeWeapon())calibrationItem=player.activeWeapon()->clsId();
  }
  auto at=std::find(choices.begin(),choices.end(),calibrationItem);
  if(choices.empty()){calibrationItem=None;return;}
  const int index=at==choices.end()?0:int(at-choices.begin());
  calibrationItem=choices[size_t(direction==0?index:cycle(index,direction,int(choices.size())))];
  if(auto item=player.getItem(calibrationItem))menu.holsterPoint=itemKind(*item);
}
void Gameplay::catalog(World& world) {
  items.clear();enemies.clear();auto& script=world.script();
  for(size_t id=0;id<script.symbolsCount();++id) {
    const auto* s=script.findSymbol(id);
    if(!s || s->type()!=zenkit::DaedalusDataType::INSTANCE || s->address()==0) continue;
    auto cls=s;size_t depth=0;
    while(cls && cls->parent()!=uint32_t(-1) && ++depth<128) cls=script.findSymbol(cls->parent());
    if(!cls || depth>=128) continue;
    auto name=upper(s->name());
    if(cls->name()=="C_ITEM") {
      // Inventory-only metadata: no world mesh or physics object is created.
      auto symbol=script.findSymbol(id);const auto previous=symbol->get_instance();
      struct Restore {zenkit::DaedalusSymbol* symbol;std::shared_ptr<zenkit::DaedalusInstance> previous;~Restore(){symbol->set_instance(previous);}} restore{symbol,previous};
      Item item(world,id,Item::T_Inventory);const auto flags=item.mainFlag();
      int category=9;
      if((flags&ITM_CAT_NF)!=0)category=item.is2H()?2:1;
      else if((flags&ITM_CAT_FF)!=0)category=item.isCrossbow()?4:3;
      else if((flags&ITM_CAT_POTION)!=0)category=5;
      else if((flags&ITM_CAT_MUN)!=0)category=6;
      else if((flags&ITM_CAT_ARMOR)!=0)category=7;
      else if((flags&(ITM_CAT_RUNE|ITM_CAT_MAGIC))!=0)category=8;
      items.push_back({id,std::string(s->name()),category,std::string(item.displayName()),item.cost()});
    } else if(cls->name()=="C_NPC" && name!="PC_HERO" && !name.starts_with("PC_")) {
      auto has=[&](const char* text){return name.find(text)!=std::string::npos;};int category=6;
      if(has("GOBBO")||has("GOBLIN"))category=2;
      else if(has("ORC"))category=3;
      else if(has("SKELETON")||has("ZOMBIE")||has("UNDEAD")||has("SEEKER"))category=4;
      else if(has("GOLEM"))category=5;
      else if(has("WOLF")||has("SHEEP")||has("SCAVENGER")||has("MOLERAT")||has("SNAPPER")||has("RAT")||has("LURKER")||has("WARAN"))category=1;
      else if(name.find('_')!=std::string::npos && std::any_of(name.begin(),name.end(),[](char c){return c>='0'&&c<='9';}))category=7;
      enemies.push_back({id,std::string(s->name()),category,std::string(s->name())});
    }
  }
  auto order=[](const Entry& a,const Entry& b) {return a.value==b.value?a.display<b.display:a.value<b.value;};
  std::sort(items.begin(),items.end(),order);std::sort(enemies.begin(),enemies.end(),order);
  itemIndex=enemyIndex=0;symbolCount=script.symbolsCount();
  Tempest::Log::i("VR catalog: items=",items.size()," NPCs=",enemies.size());
}
std::vector<const Gameplay::Entry*> Gameplay::filtered(bool npc) const {
  std::vector<const Entry*> result;const int category=npc?enemyCategory:itemCategory;
  for(const auto& entry:npc?enemies:items) if(category==0 || entry.category==category) result.push_back(&entry);
  return result;
}
// Auto-assign picked-up melee to slot 0 and ranged to slot 2 when free.
bool Gameplay::autoHolsterPickup(Npc& player,HolsterSettings& settings,size_t id,uint64_t now) {
  auto item=player.getItem(id);
  if(!item || item->count()==0)return false;
  const int point=holsterPointForPickup(compatible(*item,0),compatible(*item,2));
  if(point<0)return false;
  auto symbol=player.world().script().findSymbol(id);
  if(!symbol)return false;
  std::string name(symbol->name());
  if(!holsterFreeForPickup(settings,point,name,resolve(player,point,settings)!=nullptr))return false;
  assignHolsterItem(settings,point,name);
  message(std::string(slots[point])+": "+std::string(item->displayName()),now);
  return true;
}
bool Gameplay::seedRangedDefaults(HolsterSettings& settings) const {
  bool changed=false;
  const std::string_view domains[]={"","AIM","SUP"};
  for(bool crossbow:{false,true}) {
    if(settings.calibration.contains(rangedDefaultKey(crossbow)))continue;
    for(const auto& entry:items) {
      if(entry.category!=(crossbow?4:3))continue;
      int hand=1;
      if(!settings.calibration.contains(itemKey(entry.id,hand)))hand=0;
      if(!settings.calibration.contains(itemKey(entry.id,hand)))continue;
      std::array<ItemCalibration,3> profiles;
      for(size_t i=0;i<3;++i) {
        auto c=defaultRangedCalibration(crossbow,domains[i]);
        if(hand==0)c=mirrorCalibration(c);
        auto found=settings.calibration.find(itemKey(entry.id,hand,domains[i]));
        if(found!=settings.calibration.end())c=found->second;
        else if(auto other=settings.calibration.find(itemKey(entry.id,1-hand,domains[i]));other!=settings.calibration.end())c=mirrorCalibration(other->second);
        profiles[i]=c;
      }
      storeRangedDefaults(settings,crossbow,profiles,hand==0);
      if(!crossbow) {
        auto anchor=profiles[2];
        if(auto found=settings.calibration.find(itemKey(entry.id,hand,"STR"));found!=settings.calibration.end())anchor=found->second;
        else if(auto other=settings.calibration.find(itemKey(entry.id,1-hand,"STR"));other!=settings.calibration.end())anchor=mirrorCalibration(other->second);
        settings.calibration.try_emplace(rangedDefaultKey(false,"STR"),hand==0?mirrorCalibration(anchor):anchor);
      }
      changed=true;break;
    }
  }
  for(bool crossbow:{false,true}) {
    const auto key=rangedDefaultKey(crossbow,"HOL");
    if(settings.calibration.contains(key))continue;
    bool found=false;
    for(const auto& entry:items) {
      if(entry.category!=(crossbow?4:3))continue;
      const auto own=settings.calibration.find(itemKey(entry.id,0,"HOL"));
      if(own==settings.calibration.end())continue;
      settings.calibration[key]=own->second;changed=found=true;break;
    }
    if(!found && crossbow) {
      const auto bow=settings.calibration.find(rangedDefaultKey(false,"HOL"));
      if(bow!=settings.calibration.end()) {
        auto c=bow->second;
        // Bow length follows model Y; crossbow length follows model Z.
        c.rotation.x=std::remainder(c.rotation.x-90.f,360.f);
        settings.calibration[key]=c;changed=true;
      }
    }
  }
  return changed;
}
bool Gameplay::actionsTick(World& world,Menu& menu,uint64_t now,QuestXr& xr,const Matrix& base) {
  bool settingsChanged=false;auto player=world.player();
  for(const auto a:actions) {
    const int d=a.direction;auto& settings=menu.settings.interaction;
    auto pinHolsters=[&] {
      std::array<std::string,4> names;
      for(int i=0;i<4;++i) {
        const auto item=resolve(*player,i,settings);
        const auto symbol=item?world.script().findSymbol(item->clsId()):nullptr;
        names[size_t(i)]=symbol?std::string(symbol->name()):"EMPTY";
      }
      settings.items=std::move(names);
    };
    switch(Menu::Row(a.row)) {
      case Menu::ItemSwords:case Menu::ItemTwoHanded:case Menu::ItemBows:case Menu::ItemCrossbows:case Menu::ItemPotions:case Menu::ItemAmmo:case Menu::ItemArmor:case Menu::ItemMagic:case Menu::ItemOther:case Menu::ItemAll:
        itemCategory=(a.row-int(Menu::ItemSwords)+1)%10;itemIndex=0;break;
      case Menu::EnemyAnimals:case Menu::EnemyGoblins:case Menu::EnemyOrcs:case Menu::EnemyUndead:case Menu::EnemyGolems:case Menu::EnemyMonsters:case Menu::EnemyPeople:case Menu::EnemyAll:
        enemyCategory=(a.row-int(Menu::EnemyAnimals)+1)%8;enemyIndex=0;break;
      case Menu::CalItem:selectCalibration(*player,menu,d);break;
      case Menu::CalBow:case Menu::CalBowString:case Menu::CalArrow: {
        const Item* bow=nullptr;int hand=menu.calibrationHand;
        for(int i=0;i<2;++i)if(held[size_t(i)].slot==2){bow=player->getItem(held[size_t(i)].id);hand=i;}
        if(!bow)bow=resolve(*player,2,settings);
        if(!bow){message("Give yourself a bow first",now);break;}
        const auto id=a.row==Menu::CalArrow?size_t(bow->handle().munition):bow->clsId();
        if(!player->getItem(id)){message("Give yourself arrows first",now);break;}
        calibrationItem=id;menu.calibrationHand=a.row==Menu::CalArrow?1-hand:hand;
        menu.page=a.row==Menu::CalBowString?Menu::Page::Support:Menu::Page::Calibration;menu.selected=0;
        message(a.row==Menu::CalBowString?"Support XYZ moves the palm; String controls move the string":"Calibration item selected",now);break;
      }
      case Menu::CalHolstered: {
        menu.holsterPoint=std::clamp(a.point,0,3);
        if(auto item=resolve(*player,menu.holsterPoint,settings))calibrationItem=item->clsId();
        break;
      }
      case Menu::CalUseBowDefault:case Menu::CalUseCrossbowDefault: {
        auto item=player->getItem(calibrationItem);const bool crossbow=a.row==Menu::CalUseCrossbowDefault;
        if(!item || !compatible(*item,2) || item->isCrossbow()!=crossbow){message(crossbow?"Select a crossbow first":"Select a bow first",now);break;}
        const int hand=menu.calibrationHand;
        const std::array<ItemCalibration,3> profiles={profile(calibrationItem,hand,settings),profile(calibrationItem,hand,settings,"AIM"),profile(calibrationItem,hand,settings,"SUP")};
        const auto holstered=profile(calibrationItem,hand,settings,"HOL");
        storeRangedDefaults(settings,crossbow,profiles,hand==0);
        settings.calibration[rangedDefaultKey(crossbow,"HOL")]=holstered;
        if(!crossbow) {
          const auto anchor=profile(calibrationItem,hand,settings,"STR");
          settings.calibration[rangedDefaultKey(false,"STR")]=hand==0?mirrorCalibration(anchor):anchor;
        }
        settingsChanged=true;
        message(crossbow?"Crossbow held and holstered defaults saved":"Bow held and holstered defaults saved",now);break;
      }
      case Menu::CalUseDefault: {
        auto item=player->getItem(calibrationItem);if(!item || !compatible(*item,0)){message("Select a melee weapon for the default grip",now);break;}
        auto c=profile(calibrationItem,menu.calibrationHand,settings);settings.meleeDefault=menu.calibrationHand==0?mirrorCalibration(c):c;settingsChanged=true;message("Default melee grip saved",now);break;
      }
      case Menu::CalCopyHand: {
        if(calibrationItem==None){message("Give yourself an item first",now);break;}
        for(auto domain:{"","AIM","SUP","STR"})settings.calibration[itemKey(calibrationItem,1-menu.calibrationHand,domain)]=mirrorCalibration(profile(calibrationItem,menu.calibrationHand,settings,domain));
        settingsChanged=true;message("Grip, aim and support copied to other hand",now);break;
      }
      case Menu::CalScale:case Menu::CalStringHeight:case Menu::CalStringCenter:case Menu::CalStringSide:case Menu::CalStringDepth:
      case Menu::CalX:case Menu::CalY:case Menu::CalZ:case Menu::CalPitch:case Menu::CalYaw:case Menu::CalRoll:case Menu::CalReset:case Menu::CalGrip:case Menu::CalFlip:
      case Menu::CalAimX:case Menu::CalAimY:case Menu::CalAimZ:case Menu::CalAimPitch:case Menu::CalAimYaw:case Menu::CalAimRoll:case Menu::CalAimReset:
      case Menu::CalSupX:case Menu::CalSupY:case Menu::CalSupZ:case Menu::CalSupPitch:case Menu::CalSupYaw:case Menu::CalSupRoll:case Menu::CalSupReset:
      case Menu::CalHolX:case Menu::CalHolY:case Menu::CalHolZ:case Menu::CalHolPitch:case Menu::CalHolYaw:case Menu::CalHolRoll:case Menu::CalHolReset: {
        std::string_view domain;int first=Menu::CalX,reset=Menu::CalReset;
        if(a.row>=Menu::CalAimX && a.row<=Menu::CalAimReset){domain="AIM";first=Menu::CalAimX;reset=Menu::CalAimReset;}
        if(a.row>=Menu::CalSupX && a.row<=Menu::CalSupReset){domain="SUP";first=Menu::CalSupX;reset=Menu::CalSupReset;}
        if(a.row>=Menu::CalHolX && a.row<=Menu::CalHolReset){domain="HOL";first=Menu::CalHolX;reset=Menu::CalHolReset;}
        const auto key=itemKey(calibrationItem,menu.calibrationHand,domain);if(key.empty()){message("Give yourself an item first",now);break;}
        auto cal=profile(calibrationItem,menu.calibrationHand,settings,domain);
        if(a.row==reset)settings.calibration.erase(key);
        else {
          const float offsets[]={.001f,.005f,.02f},angles[]={.1f,.5f,5.f};const auto step=size_t(menu.calibrationStep);
          if(a.row==Menu::CalGrip){cal.grip=cal.grip<0?(d>0?0.f:1.f):cal.grip+float(d)*offsets[step]*2.f;if(cal.grip<0 || cal.grip>1)cal.grip=-1;}
          else if(a.row==Menu::CalScale)cal.scale+=float(d)*(step==0?.01f:step==1?.05f:.1f);
          else if(a.row==Menu::CalStringHeight)cal.stringHeight+=float(d)*offsets[step]*2.f;
          else if(a.row==Menu::CalStringCenter)cal.stringCenter+=float(d)*offsets[step];
          else if(a.row==Menu::CalStringSide)cal.stringSide+=float(d)*offsets[step];
          else if(a.row==Menu::CalStringDepth)cal.stringDepth+=float(d)*offsets[step];
          else if(a.row==Menu::CalFlip)cal.rotation.y=std::remainder(cal.rotation.y+180.f,360.f);
          else {float* values[]={&cal.offset.x,&cal.offset.y,&cal.offset.z,&cal.rotation.x,&cal.rotation.y,&cal.rotation.z};const int axis=a.row-first;*values[axis]+=float(d)*(axis<3?offsets[step]:angles[step]);}
          cal.sanitize();settings.calibration[key]=cal;
        }
        settingsChanged=true;message("Calibration saved",now);break;
      }
      case Menu::GodMode:Gothic::inst().setGodMode(!Gothic::inst().isGodMode());break;
      case Menu::Heal:player->changeAttribute(ATR_HITPOINTS,player->attribute(ATR_HITPOINTSMAX)-player->attribute(ATR_HITPOINTS),false);message("Health restored",now);break;
      case Menu::EnemyCategory:enemyCategory=cycle(enemyCategory,d,8);enemyIndex=0;break;
      case Menu::ItemCategory:itemCategory=cycle(itemCategory,d,10);itemIndex=0;break;
      case Menu::EnemySelect:enemyIndex=cycle(enemyIndex,d,int(filtered(true).size()));break;
      case Menu::ItemSelect:itemIndex=cycle(itemIndex,d,int(filtered(false).size()));break;
      case Menu::ItemQuantity:quantity=std::clamp(quantity+d*(quantity>=10?10:1),1,100);break;
      case Menu::HolsterAtLeft:case Menu::HolsterAtRight: {
        const uint32_t hand=a.row==Menu::HolsterAtLeft?0u:1u;
        if(!xr.focused() || !xr.gripTracked(hand)) {message("Controller tracking unavailable",now);break;}
        if(!body.placeHolster(settings,a.point,origin(xr.handWorld(hand,base)),units)) {message("Keep controller near your body",now);break;}
        settingsChanged=true;xr.haptic(hand,.4f);message("Holster moved to controller - position saved",now);break;
      }
      case Menu::HolsterMove: {
        pinHolsters();
        if(swapHolsterItems(settings,a.point,menu.holsterMoveTarget)) {
          settingsChanged=true;message("Holster items moved / swapped",now);
        }
        break;
      }
      case Menu::HolsterClear:
        pinHolsters();settings.items[size_t(a.point)]="EMPTY";settingsChanged=true;
        message("Holster cleared - item remains in inventory",now);break;
      case Menu::HolsterDrop: {
        const auto item=resolve(*player,a.point,settings);
        if(!item){message("Holster is empty",now);break;}
        const auto id=item->clsId();
        const auto position=body.head+body.forward*.6f*units-Vec3(0,.35f*units,0);
        if(!clearPath(world,body.head,position)){message("Drop blocked - face a clear area",now);break;}
        const auto c=profile(id,1,settings);
        auto model=itemPose(position,body.forward,{0,1,0},c,units);
        if(auto mesh=Resources::loadMesh(item->handle().visual))model=weaponModelMatrix(mesh->bbox()[0],mesh->bbox()[1],item->isCrossbow()?4:itemKind(*item),model,c.grip,c.scale);
        const auto previousAssignments=settings.items;pinHolsters();
        if(!player->dropItemVr(id,model,{})){settings.items=previousAssignments;message("Cannot drop this item",now);break;}
        for(auto& h:held)if(h.id==id)h=Held{};
        suspend();
        if(!player->getItem(id) || player->getItem(id)->count()==0)settings.items[size_t(a.point)]="EMPTY";
        settingsChanged=true;message("Item dropped into world",now);break;
      }
      case Menu::HolsterItem: {
        pinHolsters();std::vector<std::string> choices={"EMPTY"};
        for(auto it=player->inventory().iterator(Inventory::T_Inventory);it.isValid();++it)
          if(it->count()>0 && (compatible(*it,0) || compatible(*it,1) || compatible(*it,2) || compatible(*it,3)))
            if(auto symbol=world.script().findSymbol(it->clsId()))choices.emplace_back(symbol->name());
        auto found=std::find(choices.begin(),choices.end(),settings.items[size_t(a.point)]);
        const int at=found==choices.end()?0:int(found-choices.begin());
        assignHolsterItem(settings,a.point,choices[size_t(cycle(at,d,int(choices.size())))]);
        settingsChanged=true;message("Holster assignment saved",now);break;
      }
      case Menu::ItemGive: {
        const auto list=filtered(false);if(list.empty()) {message("No items in this category",now);break;}
        const auto e=list[size_t(itemIndex)%list.size()];
        auto item=player->addItem(e->id,size_t(quantity));
        if(!item){message("Cannot create item",now);break;}
        if(compatible(*item,0) || compatible(*item,1) || compatible(*item,2) || compatible(*item,3)) {
          pinHolsters();const int point=itemKind(*item);
          assignHolsterItem(settings,point,e->name);menu.holsterPoint=point;settingsChanged=true;
          message("Added to "+std::string(slots[point])+": "+e->display,now);
        } else message("Added: "+e->display,now);
        break;
      }
      case Menu::EnemySpawn: {
        const auto list=filtered(true);if(list.empty()) {message("No enemies in this category",now);break;}
        Vec3 spawn;
        // Start floor rays at chest height, below indoor ceilings. Try nearer
        // points and side angles when the nominal point lies behind a wall.
        const bool found=enemySpawnPoint(player->position(),body.forward,body.right,
          [&](Vec3 a,Vec3 b){return world.physic()->ray(a,b);},
          [&](Vec3 p){bool occupied=false;world.detectNpc(p,65.f,[&](Npc& n){if(!n.isDown())occupied=true;});return occupied;},spawn);
        if(!found) {message("Spawn: no free floor nearby - face an open area",now);Tempest::Log::i("VR spawn rejected: no floor / clearance");break;}
        const auto e=list[size_t(enemyIndex)%list.size()];auto npc=world.addNpc(e->id,spawn);
        if(npc) {npc->setAttitude(ATT_HOSTILE);npc->setTarget(player);npc->updateAnimation(0,true);}
        Tempest::Log::i("VR spawn ",e->name," success=",npc!=nullptr," position=",spawn.x,",",spawn.y,",",spawn.z);
        message(npc?"Spawned: "+e->name:"Cannot spawn NPC",now);break;
      }
      case Menu::TimeHour:hour=cycle(hour,d,24);break;
      case Menu::TimeApply:world.setDayTime(hour,0);message("World time changed",now);break;
      case Menu::WeatherMode:case Menu::WeatherApply:
        if(a.row==Menu::WeatherMode)weather=cycle(weather,d,4);
        world.view()->setVrWeather(weather);world.view()->updateLights(world.time());roofCheck=0;
        message(std::string("Weather applied: ")+weatherNames[weather],now);Tempest::Log::i("VR weather applied ",weather);break;
      default:break;
    }
  }
  if(settingsChanged)for(auto& h:held)if(h.id!=None && !h.autoArrow) {
    h.holster=-1;
    for(int i=0;i<4;++i)if(auto item=resolve(*player,i,menu.settings.interaction);item && item->clsId()==h.id){h.holster=i;break;}
  }
  actions.clear();return settingsChanged;
}
void Gameplay::returnItems(World& world,Npc& player,uint64_t now) {
  for(auto it=returning.begin();it!=returning.end();) {
    Focus focus;focus.item=it->item;focus=world.validateFocus(focus);
    if(!focus.item)it=returning.erase(it);
    else if(now>=it->time && player.takeItemVr(*focus.item)){message("Weapon returned to holster",now);it=returning.erase(it);}
    else ++it;
  }
}
bool Gameplay::update(World* world,QuestXr& xr,Menu& menu,const Matrix& base,uint64_t now,bool allowed) {
  highlights.clear();visuals.clear();lines.clear();aimLines.clear();for(auto& h:hands){h.visible=false;h.anchored=false;}
  auto player=world?world->player():nullptr;
  if(!world || !player) {context=nullptr;contextPlayer=nullptr;calibrating=false;calibrationItem=None;items.clear();enemies.clear();returning.clear();held={};actions.clear();suspend();return false;}
  bool changed=false;
  if(context!=world || contextPlayer!=player || symbolCount!=world->script().symbolsCount()) {
    context=world;contextPlayer=player;recentTarget=nullptr;targetUntil=0;hudTargetNpc=nullptr;hudTargetWorld=nullptr;returning.clear();held={};calibrationItem=None;calibrating=false;suspend();catalog(*world);roofCheck=0;changed=seedRangedDefaults(menu.settings.interaction);
  }
  units=100.f/menu.settings.worldScale;auto head=xr.headView(base);head.inverse();body.update(head);healthGazeDirection=normalized(axis(head,2));
  const bool calibrationPreview=menu.visible && menu.calibrationPage() && xr.focused();
  if(calibrationPreview && !calibrating)selectCalibration(*player,menu);
  calibrating=calibrationPreview;
  changed=actionsTick(*world,menu,now,xr,base) || changed;
  std::array<Matrix,2> weaponPoses={Matrix::mkIdentity(),Matrix::mkIdentity()};
  const auto& settings=menu.settings.interaction;
  returnItems(*world,*player,now);
  for(const auto id:player->consumePickupsVr())
    changed=autoHolsterPickup(*player,menu.settings.interaction,id,now) || changed;
  player->setPhysicalCombatVr(settings.physicalCombat);
  player->setWeaponRequirementsVr(true,settings.ignoreWeaponRequirements);
  const auto parried=player->consumeParryVr();
  for(unsigned i=0;i<2;++i)if(parried&(1u<<i)){xr.haptic(i,.8f,.08f);message("Parried",now);Tempest::Log::i("VR melee parry hand=",i);}
  allowed=allowed && xr.focused() && !player->isDown() && !player->isSwim() && !player->isDive() && player->interactive()==nullptr;
  const bool preview=menu.interactionPreview(xr.focused());
  if(now>=roofCheck || (body.head-roofPosition).length()>100.f) {
    raining=world->view()->sky().vrWeather()==3 && !world->physic()->ray(body.head,body.head+Vec3(0,3000,0)).hasCol;
    roofCheck=now+300;roofPosition=body.head;
  }
  if(releaseFrame!=0 && now>releaseFrame+ReleaseDebounce::gapMs)releaseBlockedUntil=now+ReleaseDebounce::blockMs;
  releaseFrame=now;
  if(!allowed) {suspend();if(!preview)return changed;}
  const auto pad=xr.gamepad();
  // Both grips belong to the VR-menu chord. It must never draw or strike too.
  const bool chord=xr.gripValue(0)>.65f && xr.gripValue(1)>.65f && (pad.buttons&Tempest::GamepadState::Start)!=0;
  // Resolve release for both hands before catch/draw, independent of loop order.
  for(int i=0;i<2;++i) {
    auto& h=hands[size_t(i)];h.grip=xr.handWorld(uint32_t(i),base);h.aim=xr.handWorld(uint32_t(i),base,true);
    h.squeeze=xr.gripValue(uint32_t(i));h.trigger=i==0?pad.leftTrigger:pad.rightTrigger;
    const bool tracked=xr.gripTracked(uint32_t(i));
    grips[size_t(i)].update(h.squeeze,tracked && allowed && !chord);
    triggers[size_t(i)].update(h.trigger,tracked && allowed && !chord);
    releaseMotion[size_t(i)].update(xr.trackingPoint(uint32_t(i),h.grip,origin(h.grip)),now,tracked && allowed && !chord);
  }
  for(int i=0;i<2;++i) {
    auto& holding=held[size_t(i)];const auto& h=hands[size_t(i)];
    if(holding.id==None || holding.autoArrow || (holding.slot==3 && held[size_t(1-i)].slot==2)){releaseDebounce[size_t(i)].reset();continue;}
    const int slot=body.closest(origin(h.grip),settings,units);
    const auto release=releaseAction(allowed && !chord,xr.gripTracked(uint32_t(i)),h.squeeze,settings.gripLock,slot>=0 && (slot==holding.holster || resolve(*player,slot,settings)==nullptr));
    if(!releaseDebounce[size_t(i)].confirm(release!=ReleaseAction::Keep,now,releaseBlockedUntil,releaseMotion[size_t(i)].velocity))continue;
    const auto releaseVelocity=releaseDebounce[size_t(i)].velocity;releaseDebounce[size_t(i)].reset();
    const bool stow=release==ReleaseAction::Stow;
    auto item=player->getItem(holding.id);if(!item){holding=Held{};continue;}
    if(!stow && swordMain==i && supportHand>=0 && xr.gripTracked(uint32_t(supportHand)) && xr.gripValue(uint32_t(supportHand))>.65f) {
      auto& receiver=held[size_t(supportHand)];receiver=Held{};receiver.id=holding.id;receiver.slot=holding.slot;receiver.holster=holding.holster;
      holding=Held{};supportHand=swordMain=-1;continue;
    }
    if(stow) {
      if(auto symbol=world->script().findSymbol(holding.id)) {
        assignHolsterItem(menu.settings.interaction,slot,std::string(symbol->name()));changed=true;
      }
      if(holding.slot==0 || holding.slot==2)player->closeWeapon(true);
    } else {
      const auto calibration=profile(holding.id,i,settings);
      const auto pose=itemPose(origin(h.grip),axis(h.aim,2),-axis(h.grip,1),calibration,units);
      auto model=pose;
      if(auto mesh=Resources::loadMesh(item->handle().visual))model=weaponModelMatrix(mesh->bbox()[0],mesh->bbox()[1],item->isCrossbow()?4:holding.slot,pose,calibration.grip,calibration.scale,i==0);
      auto velocity=releaseVelocity;
      const float speed=velocity.length();if(speed>6.f)velocity*=6.f/speed;
      velocity=xr.trackingVector(uint32_t(i),h.grip,velocity);
      auto dropped=clearPath(*world,body.head,origin(pose))?player->dropItemVr(holding.id,model,velocity):nullptr;
      if(!dropped) {
        message("Cannot release inside a wall - move hand out",now);continue;
      }
      if(holding.slot==0 || holding.slot==2)returning.push_back({dropped,now+1800});
      Tempest::Log::i("VR release hand=",i," speed=",velocity.length()/units);
    }
    holding=Held{};drawing=false;nockHand=-1;supportHand=swordMain=-1;xr.haptic(uint32_t(i),.18f);
  }
  // Transfer inventory ownership between hands before either hand builds its visual.
  // The active weapon stays equipped; no temporary world drop or duplicate item.
  for(int receiver=0;receiver<2;++receiver) {
    const int donor=1-receiver;
    if(!allowed || chord || !grips[size_t(receiver)].pressed ||
       !xr.gripTracked(uint32_t(receiver)) || !xr.gripTracked(uint32_t(donor)))continue;
    auto& target=held[size_t(receiver)];auto& source=held[size_t(donor)];
    if((target.id!=None && !target.autoArrow) || source.id==None || source.autoArrow)continue;
    if(!handTransferReach(origin(hands[size_t(receiver)].grip),origin(hands[size_t(donor)].grip),units) ||
       !clearPath(*world,body.head,origin(hands[size_t(receiver)].grip)))continue;
    target=Held{};target.id=source.id;target.slot=source.slot;target.holster=source.holster;source=Held{};
    supportHand=swordMain=-1;bowGesture.reset();drawing=false;nockHand=-1;
    releaseMotion[size_t(receiver)].reset();releaseMotion[size_t(donor)].reset();
    xr.haptic(uint32_t(receiver),.4f);xr.haptic(uint32_t(donor),.18f);
    break;
  }
  for(int i=0;i<2;++i) {
    auto& h=hands[size_t(i)];h.grip=xr.handWorld(uint32_t(i),base);h.aim=xr.handWorld(uint32_t(i),base,true);
    h.squeeze=xr.gripValue(uint32_t(i));h.trigger=i==0?pad.leftTrigger:pad.rightTrigger;
    const bool valid=xr.gripTracked(uint32_t(i));
    h.visible=valid && settings.showHands;
    if(!valid) {held[size_t(i)].swing.reset();held[size_t(i)].strikeUntil=0;drawing=false;nockHand=-1;continue;}
    if(calibrationPreview){if(i==menu.calibrationHand)h.squeeze=std::max(h.squeeze,.85f);continue;}
    auto& holding=held[size_t(i)];
    if(holding.autoArrow && held[size_t(1-i)].slot!=2)holding=Held{};
    auto item=player->getItem(holding.id);
    if(holding.id!=None && (!item || item->count()==0)) {holding=Held{};item=nullptr;}
    if(item && (holding.slot==0 || holding.slot==2) && (!player->activeWeapon() || player->activeWeapon()->clsId()!=holding.id)) {
      Tempest::Log::i("VR held weapon cleared by engine hand=",i," item=",item->displayName()," weaponState=",int(player->weaponState()));
      holding=Held{};item=nullptr;drawing=false;nockHand=-1;
    }
    if(item && (holding.slot==0 || holding.slot==2) && !settings.ignoreWeaponRequirements && now>=requirementNoticeAfter) {
      int32_t atr=0,need=0;
      if(!item->checkCondUse(*player,atr,need)) {
        message(requirementWarning(atr,need,player->attribute(Attribute(atr))),now);
        requirementNoticeAfter=now+6000;
      }
    }
    Vec3 position=origin(h.grip),forward=normalized(axis(h.aim,2));
    auto up=normalized(-axis(h.grip,1),{0,1,0});
    if(supportHand==i) {
      if(!grips[size_t(i)].down || swordMain<0 || !xr.gripTracked(uint32_t(swordMain)))supportHand=swordMain=-1;
      else continue;
    }
    auto calibrated=itemPose(position,forward,up,calibrationFor(i,settings),units);
    if(swordMain==i && supportHand>=0) {
      const auto other=xr.handWorld(uint32_t(supportHand),base);const auto delta=origin(other)-position;
      const auto socket=itemPose(origin(calibrated),axis(calibrated,2),axis(calibrated,1),profile(holding.id,i,settings,"SUP"),units);
      const auto expected=origin(socket)-position;
      if(!xr.gripTracked(uint32_t(supportHand)) || delta.length()<.06f*units || delta.length()>.65f*units || expected.length()<.02f*units)supportHand=swordMain=-1;
      else {
        const auto offset=rotateBetween(origin(calibrated)-position,expected,delta);
        for(size_t k=0;k<3;++k){const auto v=rotateBetween(axis(calibrated,int(k)),expected,delta);calibrated[k][0]=v.x;calibrated[k][1]=v.y;calibrated[k][2]=v.z;}
        const auto p=position+offset;calibrated[3][0]=p.x;calibrated[3][1]=p.y;calibrated[3][2]=p.z;
      }
    }
    weaponPoses[size_t(i)]=calibrated;
    position=origin(calibrated);forward=axis(calibrated,2);up=axis(calibrated,1);
    const int slot=body.closest(origin(h.grip),settings,units);
    if(slot!=hovered[size_t(i)] && slot>=0 && resolve(*player,slot,settings)) xr.haptic(uint32_t(i),.14f,.018f);
    hovered[size_t(i)]=slot;
    if(grips[size_t(i)].pressed && !item && supportHand<0 && held[size_t(1-i)].slot==0) {
      auto sword=player->getItem(held[size_t(1-i)].id);
      const auto raw=xr.handWorld(uint32_t(1-i),base),aim=xr.handWorld(uint32_t(1-i),base,true);
      const auto mainPose=itemPose(origin(raw),axis(aim,2),-axis(raw,1),profile(held[size_t(1-i)].id,1-i,settings),units);
      const auto socket=itemPose(origin(mainPose),axis(mainPose,2),axis(mainPose,1),profile(held[size_t(1-i)].id,1-i,settings,"SUP"),units);
      if(sword && sword->is2H() && (origin(h.grip)-origin(socket)).length()<.20f*units && clearPath(*world,body.head,origin(h.grip))) {
        supportHand=i;swordMain=1-i;swordPalmLocal=relativeHandPalm(mainPose,raw,aim,swordMain==0);held[size_t(1-i)].swing.reset();held[size_t(1-i)].strikeUntil=0;
        xr.haptic(uint32_t(i),.5f);message("Two-hand grip - hold support grip",now);continue;
      }
    }
    const size_t priorId=holding.id;
    // A squeezed free hand catches a world weapon, including one just released
    // by the other hand. The world owns it until takeItemVr transfers ownership.
    bool caught=false;
    if(!item && grips[size_t(i)].down && supportHand!=i) {
      Item* nearest=nullptr;float distance=.24f*units;
      world->detectItem(origin(h.grip),distance+200.f,[&](Item& candidate) {
        if(!compatible(candidate,0) && !compatible(candidate,2))return;
        if(held[size_t(1-i)].slot==0 || held[size_t(1-i)].slot==2)return;
        auto inverse=candidate.transform();inverse.inverse();auto p=origin(h.grip);inverse.project(p);
        const auto bounds=candidate.bBox();if(!bounds)return;p.x=std::clamp(p.x,bounds[0].x,bounds[1].x);p.y=std::clamp(p.y,bounds[0].y,bounds[1].y);p.z=std::clamp(p.z,bounds[0].z,bounds[1].z);
        candidate.transform().project(p);const float d=(p-origin(h.grip)).length();
        if(d<distance && clearPath(*world,body.head,origin(h.grip)) && clearPath(*world,origin(h.grip),p,1.f)){nearest=&candidate;distance=d;}
      });
      if(nearest) {
        const auto id=nearest->clsId();const int kind=itemKind(*nearest);
        const bool thrown=std::any_of(returning.begin(),returning.end(),[&](const ReturnItem& r){return r.item==nearest;});
        if(player->takeItemVr(*nearest)) {
          caught=true;
          if(!thrown)changed=autoHolsterPickup(*player,menu.settings.interaction,id,now) || changed;
          if(player->equipVr(id,true)){holding=Held{};holding.id=id;holding.slot=kind;holding.holster=-1;
            for(int point=0;point<4;++point)if(auto assigned=resolve(*player,point,settings);assigned && assigned->clsId()==id){holding.holster=point;break;}
            item=player->getItem(id);caught=true;xr.haptic(uint32_t(i),.5f);}
          else message("Cannot draw this weapon",now);
        }
      }
    }
    if(grips[size_t(i)].pressed && !caught) {
      if(item) {
        // Grip lock is released at the matching holster by opening the grip.
      } else if(slot>=0) {
        auto selected=resolve(*player,slot,settings);
        if(!selected) message("Holster empty - assign an inventory item",now);
        else if((itemKind(*selected)==0 || itemKind(*selected)==2) && (held[size_t(1-i)].slot==0 || held[size_t(1-i)].slot==2)) message("Stow the other weapon first",now);
        else if((itemKind(*selected)==0 || itemKind(*selected)==2) && !player->equipVr(selected->clsId(),true)) message("Cannot draw this weapon",now);
        else { holding.id=selected->clsId();holding.slot=itemKind(*selected);holding.holster=slot;item=player->getItem(holding.id);xr.haptic(uint32_t(i),.4f); }
      } else {
        Item* nearest=nullptr;float distance=settings.pickupRadius*units;
        world->detectItem(position,distance+60.f,[&](Item& candidate) {
          if(candidate.isTorchBurn()) return;
          const float d=(candidate.midPosition()-position).length();
          if(d<distance && clearPath(*world,body.head,position) && clearPath(*world,position,candidate.midPosition(),8.f)) {nearest=&candidate;distance=d;}
        });
        if(nearest) {
          const auto name=std::string(nearest->displayName());const auto id=nearest->clsId();
          if(player->takeItemVr(*nearest)) {
            if(!autoHolsterPickup(*player,menu.settings.interaction,id,now))message("Picked up: "+name,now);
            else changed=true;
            xr.haptic(uint32_t(i),.35f);
          }
        }
      }
    }
    if(item && priorId!=holding.id) {
      holding.swing.reset();holding.strikeUntil=0;
      calibrated=itemPose(origin(h.grip),axis(h.aim,2),-axis(h.grip,1),profile(holding.id,i,settings),units);
      weaponPoses[size_t(i)]=calibrated;position=origin(calibrated);forward=axis(calibrated,2);up=axis(calibrated,1);
    }
    if(!item) {
      const auto tip=origin(h.grip)+normalized(axis(h.aim,2))*8.f;
      const bool fists=allowed && settings.physicalCombat && h.squeeze>.65f && held[0].slot<0 && held[1].slot<0 && player->activeWeapon()==nullptr;
      const auto tracking=xr.trackingPoint(uint32_t(i),h.grip,tip);
      if(holding.swing.update(tip,body,units,now,fists,1.6f,&tracking))holding.strikeUntil=now+180;
      if(!holding.swing.continuous || !fists || holding.swing.speed<.7f)holding.strikeUntil=0;
      if(holding.strikeUntil>=now && clearPath(*world,body.head,tip)) {
        Npc* target=nullptr;
        for(Vec3 offset:{Vec3(),Vec3(4,0,0),Vec3(-4,0,0),Vec3(0,4,0)}) {
          target=world->physic()->rayNpcMelee(holding.previousTip+offset,tip+offset,player,4.f).npcHit;if(target)break;
        }
        if(target && !target->isDown()) {rememberTarget(target,now);player->readyFistsVr();const auto hp=target->attribute(ATR_HITPOINTS);target->takeDamage(*player,nullptr,5);Tempest::Log::i("VR fist contact speed=",holding.swing.speed," damage=",hp-target->attribute(ATR_HITPOINTS));holding.strikeUntil=0;xr.haptic(uint32_t(i),.55f);}
      }
      holding.previousTip=tip;continue;
    }
    if(holding.slot==1 && triggers[size_t(i)].pressed) {
      const auto mouth=body.head-Vec3(0,.12f*units,0);
      if((position-mouth).length()<.30f*units) {
        if(player->drinkVr(holding.id)) {holding=Held{};xr.haptic(uint32_t(i),.35f);message("Potion used",now);}
        else message("Potion cannot be used now",now);
        continue;
      } else message("Bring potion to mouth, then press trigger",now);
    }
    if(holding.slot==0 && allowed) {
      Vec3 bladeBase=position,tip=position+forward*std::clamp(float(item->swordLength()),45.f,160.f);
      float bladeRadius=3.f;
      if(auto mesh=Resources::loadMesh(item->handle().visual)) {
        const auto bounds=mesh->bbox();const auto size=bounds[1]-bounds[0];const float lengths[]={size.x,size.y,size.z};
        int longest=0;for(int k=1;k<3;++k)if(lengths[k]>lengths[longest])longest=k;
        bladeRadius=std::clamp(.5f*std::max(lengths[(longest+1)%3],lengths[(longest+2)%3])*profile(holding.id,i,settings).scale,3.f,15.f);
        bladeBase=tip=(bounds[0]+bounds[1])*.5f;
        float* lo[]={&bladeBase.x,&bladeBase.y,&bladeBase.z};float* hi[]={&tip.x,&tip.y,&tip.z};
        const float mins[]={bounds[0].x,bounds[0].y,bounds[0].z},maxs[]={bounds[1].x,bounds[1].y,bounds[1].z};
        *lo[longest]=mins[longest];*hi[longest]=maxs[longest];
        const auto transform=weaponModelMatrix(bounds[0],bounds[1],0,calibrated,profile(holding.id,i,settings).grip,profile(holding.id,i,settings).scale);
        transform.project(bladeBase);transform.project(tip);
        if((bladeBase-position).length()>(tip-position).length())std::swap(bladeBase,tip);
      }
      const bool previous=holding.swing.valid;
      const auto tracking=xr.trackingPoint(uint32_t(i),h.grip,tip);
      const auto gripTracking=xr.trackingPoint(uint32_t(i),h.grip,origin(h.grip));
      holding.swing.update(tip,body,units,now,settings.physicalCombat,settings.swingSpeed,&tracking,&gripTracking);
      holding.strikeUntil=holding.swing.contactReady?now+1:0;
      if(!settings.physicalCombat || !previous || !holding.swing.continuous || holding.swing.speed<.7f) holding.strikeUntil=0;
      if(settings.physicalCombat && holding.swing.continuous && holding.swing.speed<1.5f && holding.strikeUntil==0 && clearPath(*world,body.head,position))
        player->setGuardVr({bladeBase,tip,body.head,body.forward,units,world->tickCount()+100,i});
      // Track occupancy even below the swing threshold. Reversal inside a body is extraction, not entry.
      const auto overlap=world->physic()->rayNpcMelee(bladeBase,tip,player,bladeRadius+(holding.insideTarget?5.f:0.f)).npcHit;
      const bool entry=!holding.insideTarget;
      if(entry && overlap)rememberTarget(overlap,now);
      if(entry && overlap && holding.strikeUntil<now && previous)
        Tempest::Log::i("VR melee entry rejected weapon=",item->displayName()," speed=",holding.swing.speed," continuous=",holding.swing.continuous," travel=",holding.swing.contactTravel," requiredSpeed=",settings.swingSpeed);
      if(entry && holding.strikeUntil>=now && clearPath(*world,body.head,position)) {
        Npc* hit=nullptr;
        const int samples=std::clamp(int(std::ceil((tip-bladeBase).length()/10.f)),4,20);
        for(int k=0;k<=samples && !hit;++k) {
          const float t=float(k)/float(samples);const auto old=holding.previousBase+(holding.previousTip-holding.previousBase)*t;
          const auto current=bladeBase+(tip-bladeBase)*t;
          if(clearPath(*world,position,current)) hit=world->physic()->rayNpcMelee(old,current,player,bladeRadius).npcHit;
        }
        if(!hit)hit=overlap;
        if(hit && !hit->isDown()) {
          rememberTarget(hit,now);
          const bool twoHands=swordMain==i && supportHand>=0 && xr.gripTracked(uint32_t(supportHand)) && xr.gripValue(uint32_t(supportHand))>.65f;
          if(item->is2H() && !twoHands) {
            if(now>=twoHandNoticeAfter){message("Two-handed weapons deal damage only with a two-handed grip.",now);twoHandNoticeAfter=now+4000;}
          } else {
            const auto hp=hit->attribute(ATR_HITPOINTS);hit->takeDamage(*player,nullptr);
            Tempest::Log::i("VR blade contact weapon=",item->displayName()," twoHanded=",item->is2H()," speed=",holding.swing.speed," damage=",hp-hit->attribute(ATR_HITPOINTS));
            xr.haptic(uint32_t(i),.8f,.065f);
          }
          holding.strikeUntil=0;holding.swing.contact();
        }
      }
      holding.insideTarget=overlap!=nullptr;
      holding.previousBase=bladeBase;holding.previousTip=tip;
    }
    if(allowed && item->isCrossbow() && triggers[size_t(i)].pressed && now>=holding.shootAfter) {
      const auto aim=itemPose(position,forward,up,profile(holding.id,i,settings,"AIM"),units);
      const auto muzzle=origin(aim)+axis(aim,2)*.35f*units;
      const auto ammoProfile=profile(size_t(item->handle().munition),1-i,settings);
      if(clearPath(*world,body.head,muzzle) && player->shootVr(muzzle,axis(aim,2),1.f,ammoProfile.scale)) {
        holding.shootAfter=now+800;xr.haptic(uint32_t(i),.6f);
      } else message("Crossbow: no bolts or muzzle blocked",now);
    }
    h.squeeze=std::max(h.squeeze,.85f);
    visuals.push_back({std::string(item->handle().visual),position,forward,up,item->isCrossbow()?4:holding.slot,profile(holding.id,i,settings).grip,profile(holding.id,i,settings).scale,i==0,holding.slot==2 && !item->isCrossbow()});
  }
  int bow=-1,arrow=-1;
  for(int i=0;i<2;++i) {
    auto item=player->getItem(held[size_t(i)].id);
    if(held[size_t(i)].slot==2 && item && !item->isCrossbow())bow=i;
    if(held[size_t(i)].slot==3)arrow=i;
  }
  if(allowed && settings.physicalCombat && bow>=0 && arrow<0 && held[size_t(1-bow)].id==None) {
    auto weapon=player->getItem(held[size_t(bow)].id);
    auto ammo=weapon?player->getItem(size_t(weapon->handle().munition)):nullptr;
    if(ammo && ammo->count()>0){arrow=1-bow;auto& h=held[size_t(arrow)];h=Held{};h.id=ammo->clsId();h.slot=3;h.autoArrow=true;}
  }
  if(!calibrationPreview && bow>=0 && xr.gripTracked(uint32_t(bow))) {
    const auto bowItem=player->getItem(held[size_t(bow)].id);
    const auto bowPose=weaponPoses[size_t(bow)];
    const auto socket=itemPose(origin(bowPose),axis(bowPose,2),axis(bowPose,1),profile(bowItem->clsId(),bow,settings,"SUP"),units);
    const auto anchor=itemPose(origin(bowPose),axis(bowPose,2),axis(bowPose,1),profile(bowItem->clsId(),bow,settings,"STR"),units);
    auto chordPose=bowPose;
    for(size_t k=0;k<3;++k)chordPose[3][k]=anchor[3][k];
    const auto tips=bowStringTips(chordPose,profile(bowItem->clsId(),bow,settings),units);
    const auto rest=(tips[0]+tips[1])*.5f;
    const auto shot=bowShotPose(bowPose,profile(bowItem->clsId(),bow,settings,"AIM"),rest,{},units);
    const auto forward=axis(shot,2),up=axis(shot,1);
    Vec3 nock=rest;
    auto ammo=arrow>=0?player->getItem(held[size_t(arrow)].id):nullptr;
    if(allowed && settings.physicalCombat && ammo && ammo->count()>0 &&
       size_t(bowItem->handle().munition)==ammo->clsId() && xr.gripTracked(uint32_t(arrow))) {
      const auto physicalHand=origin(hands[size_t(arrow)].grip);
      const float axial=bowAxialDraw(rest,physicalHand,forward,units);
      const float start=bowGesture.active?bowGesture.nockDistance:axial;
      const bool nearString=(physicalHand-rest).length()<.24f*units;
      const bool trackingSafe=(physicalHand-rest).length()<1.3f*units;
      const auto event=bowGesture.update(trackingSafe,nearString,grips[size_t(arrow)].pressed,grips[size_t(arrow)].released,axial);
      drawing=bowGesture.active;nockHand=drawing?arrow:-1;
      drawLength=std::clamp(axial-start,0.f,.6f);
      if(event==BowGesture::Nocked)xr.haptic(uint32_t(arrow),.25f);
      if(drawing || event==BowGesture::Fired)nock=bowNockPoint(rest,forward,drawLength,units);
      const auto arrowProfile=profile(ammo->clsId(),arrow,settings);
      if(drawing || event==BowGesture::Fired) {
        float grip=arrowProfile.grip;
        if(grip<0)if(auto mesh=Resources::loadMesh(ammo->handle().visual))grip=arrowNockGrip(mesh->bbox()[0],mesh->bbox()[1]);
        ItemCalibration roll;roll.rotation.z=arrowProfile.rotation.z;
        const auto arrowPose=itemPose(nock,forward,up,roll,units);
        std::erase_if(visuals,[](const Visual& v){return v.kind==3;});
        visuals.push_back({std::string(ammo->handle().visual),origin(arrowPose),axis(arrowPose,2),axis(arrowPose,1),3,grip,arrowProfile.scale});
      } else if(std::none_of(visuals.begin(),visuals.end(),[](const Visual& v){return v.kind==3;})) {
        const auto& hand=hands[size_t(arrow)];
        const auto freeArrow=itemPose(origin(hand.grip),axis(hand.aim,2),-axis(hand.grip,1),arrowProfile,units);
        visuals.push_back({std::string(ammo->handle().visual),origin(freeArrow),axis(freeArrow,2),axis(freeArrow,1),3,arrowProfile.grip,arrowProfile.scale});
      }
      if(drawing) {
        auto& h=hands[size_t(arrow)];h.grip=h.aim=socket;
        for(size_t k=0;k<3;++k)h.grip[1][k]=-h.grip[1][k];
        const auto palm=origin(socket)+(nock-rest);
        h.grip[3][0]=h.aim[3][0]=palm.x;h.grip[3][1]=h.aim[3][1]=palm.y;h.grip[3][2]=h.aim[3][2]=palm.z;
        h.squeeze=.85f;h.anchor(arrow==0);
      }
      if(drawing && settings.bowSight && drawLength>=.08f) {
        const auto velocity=forward*(DynamicWorld::bulletSpeed*1000.f*bowSpeedScale(bowPower(drawLength)));
        auto previous=origin(shot),end=previous;
        for(int step=1;step<=64;++step) {
          const auto next=arrowPath(origin(shot),velocity,float(step)*.05f);
          const auto hit=world->physic()->rayNpc(previous,next,player);
          end=hit.hasCol && !hit.npcHit?hit.v:next;aimLines.push_back({previous,end});
          if(hit.hasCol)break;previous=next;
        }
        for(auto axis:{body.right,Vec3(0,1,0)})aimLines.push_back({end-axis*.045f*units,end+axis*.045f*units});
      }
      if(event==BowGesture::Fired && clearPath(*world,body.head,origin(shot)) &&
         player->shootVr(origin(shot),forward,bowPower(drawLength),arrowProfile.scale)) {
        Tempest::Log::i("VR bow shot draw=",drawLength," power=",bowPower(drawLength));
        held[size_t(arrow)]=Held{};std::erase_if(visuals,[](const Visual& v){return v.kind==3;});
        xr.haptic(uint32_t(bow),.65f);xr.haptic(uint32_t(arrow),.35f);
        nock=rest;
      }
    } else {bowGesture.reset();drawing=false;nockHand=-1;}
    lines.push_back({tips[0],nock});lines.push_back({nock,tips[1]});
  } else {bowGesture.reset();drawing=false;nockHand=-1;}
  if(allowed && settings.pickupHighlight) {
    std::vector<std::pair<float,Item*>> candidates;
    const float range=settings.pickupHighlightRange*units;
    world->detectItem(body.head,range+200.f,[&](Item& candidate) {
      if(candidate.isTorchBurn() || std::any_of(returning.begin(),returning.end(),
          [&](const ReturnItem& entry){return entry.item==&candidate;}))return;
      const auto point=candidate.midPosition();const float distance=(point-body.head).length();
      if(distance<=range)candidates.push_back({distance,&candidate});
    });
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    for(const auto& entry:candidates) {
      if(highlights.size()>=64)break;
      const auto& candidate=*entry.second;
      // Per-pixel scene depth handles visibility; a head-to-center ray can hide an entire partly visible item.
      highlights.push_back({std::string(candidate.handle().visual),candidate.transform()});
    }
  }
  if(preview && (menu.page==Menu::Page::Holsters || menu.page==Menu::Page::HolsterSlot)) {
    // The cross marks the actual grab point, not the sword tip hanging below it.
    const auto p=body.point(settings.offsets[size_t(menu.holsterPoint)],units);
    for(Vec3 axis:{body.right,Vec3(0,1,0),body.forward})lines.push_back({p-axis*.055f*units,p+axis*.055f*units});
  }
  auto holsterVisual=[&](const Item& item,int slot,bool marker) {
    const auto point=body.point(settings.offsets[size_t(slot)],units);
    const auto pose=itemPose(point,Vec3(0,slot==0?-1.f:1.f,0),body.forward,profile(item.clsId(),0,settings,"HOL"),units);
    const int kind=item.isCrossbow()?4:itemKind(item);
    visuals.push_back({std::string(item.handle().visual),origin(pose),axis(pose,2),axis(pose,1),kind,profile(item.clsId(),0,settings,"HOL").grip,profile(item.clsId(),0,settings,"HOL").scale});
    if(marker)for(Vec3 v:{body.right,Vec3(0,1,0),body.forward})lines.push_back({point-v*.06f*units,point+v*.06f*units});
  };
  if(settings.enabled && settings.showHolsters)for(int slot=0;slot<4;++slot) {
    const bool socketPreview=preview && (menu.page==Menu::Page::Holsters || menu.page==Menu::Page::HolsterSlot) && slot==menu.holsterPoint;
    if(((held[0].holster==slot || held[1].holster==slot) && !socketPreview) || (calibrationPreview && menu.page==Menu::Page::Holstered && slot==menu.holsterPoint))continue;
    const Item* item=nullptr;
    if(socketPreview)for(const auto& heldItem:held)if(heldItem.holster==slot)item=player->getItem(heldItem.id);
    if(!item)item=resolve(*player,slot,settings);
    if(item && (socketPreview || std::none_of(held.begin(),held.end(),[&](const Held& h){return h.id==item->clsId();})))holsterVisual(*item,slot,false);
  }
  if(calibrationPreview)if(auto item=player->getItem(calibrationItem)) {
    const int hand=menu.calibrationHand;const auto& h=hands[size_t(hand)];
    if(menu.page==Menu::Page::Holstered)holsterVisual(*item,menu.holsterPoint,true);
    else if(xr.gripTracked(uint32_t(hand))) {
      const auto model=profile(calibrationItem,hand,settings);
      const auto pose=itemPose(origin(h.grip),axis(h.aim,2),-axis(h.grip,1),model,units);
      const int kind=item->isCrossbow()?4:itemKind(*item);
      visuals.push_back({std::string(item->handle().visual),origin(pose),axis(pose,2),axis(pose,1),kind,model.grip,model.scale,hand==0,kind==2});
      const auto aim=itemPose(origin(pose),axis(pose,2),axis(pose,1),profile(calibrationItem,hand,settings,"AIM"),units);
      aimLines.push_back({origin(aim),origin(aim)+axis(aim,2)*units*1.2f});
      const auto socket=itemPose(origin(pose),axis(pose,2),axis(pose,1),profile(calibrationItem,hand,settings,"SUP"),units);
      if(kind==2) {
        const auto anchor=itemPose(origin(pose),axis(pose,2),axis(pose,1),profile(calibrationItem,hand,settings,"STR"),units);
        auto chordPose=pose;
        for(size_t k=0;k<3;++k)chordPose[3][k]=anchor[3][k];
        const auto tips=bowStringTips(chordPose,model,units);
        lines.push_back({tips[0],tips[1]});
      }
      if(item->is2H() || menu.page==Menu::Page::Support) {
        auto& second=hands[size_t(1-hand)];second.visible=settings.showHands;second.squeeze=.85f;second.grip=second.aim=socket;
        const auto supportPosition=origin(socket);
        second.grip[3][0]=second.aim[3][0]=supportPosition.x;
        second.grip[3][1]=second.aim[3][1]=supportPosition.y;
        second.grip[3][2]=second.aim[3][2]=supportPosition.z;
        for(size_t k=0;k<3;++k)second.grip[1][k]=-second.grip[1][k];
        second.anchor(1-hand==0);
        for(Vec3 v:{axis(socket,0),axis(socket,1),axis(socket,2)})lines.push_back({supportPosition-v*.035f*units,supportPosition+v*.035f*units});
      }
      // Wrist cross stays at the controller while the model slides/rotates around it.
      for(Vec3 v:{axis(pose,0),axis(pose,1),axis(pose,2)})lines.push_back({origin(h.grip)-v*.025f*units,origin(h.grip)+v*.025f*units});
    }
  }
  if(!calibrationPreview && supportHand>=0 && swordMain>=0) {
    const auto pose=weaponPoses[size_t(swordMain)];
    const auto socket=itemPose(origin(pose),axis(pose,2),axis(pose,1),profile(held[size_t(swordMain)].id,swordMain,settings,"SUP"),units);
    auto& h=hands[size_t(supportHand)];h.grip=h.aim=socket;h.squeeze=.85f;
    for(size_t k=0;k<3;++k)h.grip[1][k]=-h.grip[1][k];
    h.anchor(supportHand==0);
    auto& main=hands[size_t(swordMain)];main.anchored=true;
    main.palmPose=pose*swordPalmLocal;
  }
  return changed;
}
std::string Gameplay::label(Menu::Row row,const Menu& menu) const {
  auto toggle=[](bool v){return v?"On":"Off";};const auto& s=menu.settings.interaction;
  const auto p=menu.holsterPoint;char b[160];
  auto contents=[&](int point) {
    const auto item=contextPlayer?resolve(*contextPlayer,point,s):nullptr;
    if(!item)return std::string("Empty");
    std::string name=std::string(item->displayName())+" x"+std::to_string(item->count());
    for(int hand=0;hand<2;++hand)if(held[size_t(hand)].id==item->clsId())name+=hand==0?" [left hand]":" [right hand]";
    return name;
  };
  if(row>=Menu::HolsterSlot0 && row<=Menu::HolsterSlot3) {
    const int point=int(row)-int(Menu::HolsterSlot0);
    return std::string(slots[point])+": "+contents(point)+"  >";
  }
  if(row>=Menu::ItemSwords && row<=Menu::ItemAll)return std::string(itemCategories[(int(row)-int(Menu::ItemSwords)+1)%10])+"  >";
  if(row>=Menu::EnemyAnimals && row<=Menu::EnemyAll)return std::string(enemyCategories[(int(row)-int(Menu::EnemyAnimals)+1)%8])+"  >";
  if(row>=Menu::MapA && row<=Menu::MapR3) {const char* buttons[]={"A","B","X","Y","L3","R3"};const auto index=size_t(row-Menu::MapA);return std::string(buttons[index])+": < "+Settings::mappingName(menu.settings.mapping[index])+" >";}
  std::string_view domain;int first=-1;
  if(row>=Menu::CalX && row<=Menu::CalRoll)first=Menu::CalX;
  if(row>=Menu::CalAimX && row<=Menu::CalAimRoll){first=Menu::CalAimX;domain="AIM";}
  if(row>=Menu::CalSupX && row<=Menu::CalSupRoll){first=Menu::CalSupX;domain="SUP";}
  if(row>=Menu::CalHolX && row<=Menu::CalHolRoll){first=Menu::CalHolX;domain="HOL";}
  if(first>=0) {
    const auto c=profile(calibrationItem,menu.calibrationHand,s,domain);const float values[]={c.offset.x*100,c.offset.y*100,c.offset.z*100,c.rotation.x,c.rotation.y,c.rotation.z};
    const char* names[]={"Offset X (cm)","Offset Y (cm)","Offset Z (cm)","Pitch (degrees)","Yaw (degrees)","Roll (degrees)"};
    std::snprintf(b,sizeof(b),"%s: < %+.1f >",names[int(row)-first],double(values[int(row)-first]));return b;
  }
  switch(row) {
    case Menu::Controls:return "Button mapping  >";
    case Menu::ItemCalibrationMenu:return "Weapon calibration  >";
    case Menu::CalModel:return "Model / primary grip  >";
    case Menu::CalAim:return "Aim ray  >";
    case Menu::CalSupport:return "Support hand / second grip  >";
    case Menu::CalHolstered:return "Holstered model  >";
    case Menu::CalStep:{const char* steps[]={"Fine: 1 mm / 0.1 deg","Normal: 5 mm / 0.5 deg","Coarse: 2 cm / 5 deg"};return std::string("Step: < ")+steps[size_t(menu.calibrationStep)]+" >";}
    case Menu::CalGrip:{const auto c=calibrationFor(menu.calibrationHand,s);if(c.grip<0)return "Grip origin: < Authored model origin >";std::snprintf(b,sizeof(b),"Grip origin: < %.1f%% along model >",double(c.grip*100));return b;}
    case Menu::CalFlip:return "Flip model 180 degrees";
    case Menu::CalUseBowDefault:return "Use as bow default";
    case Menu::CalUseCrossbowDefault:return "Use as crossbow default";
    case Menu::CalUseDefault:return "Use this grip as default for melee weapons";
    case Menu::CalCopyHand:return "Copy grip / aim / support to other hand";
    case Menu::CalAimReset:return "Reset aim for this item";
    case Menu::CalSupReset:return "Reset support for this item";
    case Menu::CalHolReset:return "Reset holstered model for this item";
    case Menu::MapReset:return "Reset: A jump / hold L3 run / R3 crouch";
    case Menu::CalHand:return std::string("Hand: < ")+(menu.calibrationHand==0?"Left":"Right")+" >";
    case Menu::CalItem:{auto item=contextPlayer?contextPlayer->getItem(calibrationItem):nullptr;return item?"Item: < "+std::string(item->displayName())+" >":"No owned items - use Give items";}
    case Menu::CalReset:return "Reset this item / hand";
    case Menu::Holsters:return "Hands / holsters  >";case Menu::Cheats:return "Cheats  >";
    case Menu::CheatPlayer:return "Player  >";case Menu::CheatEnemies:return "Spawn enemies  >";case Menu::CheatItems:return "Give items  >";case Menu::CheatWorld:return "Time / weather  >";
    case Menu::Hands:return std::string("Hands: ")+toggle(s.showHands);
    case Menu::HolsterEnabled:return std::string("Holsters: ")+toggle(s.enabled);
    case Menu::HolsterModels:return std::string("Show holstered items: ")+toggle(s.showHolsters);
    case Menu::PickupHighlight:return std::string("World item highlight: ")+toggle(s.pickupHighlight);
    case Menu::PickupHighlightRange:std::snprintf(b,sizeof(b),"Highlight range: < %.0f m >",double(s.pickupHighlightRange));return b;
    case Menu::BowSight:return std::string("Bow sight: ")+toggle(s.bowSight);
    case Menu::CalScale:std::snprintf(b,sizeof(b),"Model scale: < %.2fx >",double(calibrationFor(menu.calibrationHand,s).scale));return b;
    case Menu::CalStringHeight:std::snprintf(b,sizeof(b),"String height: < %.1f cm >",double(calibrationFor(menu.calibrationHand,s).stringHeight*100));return b;
    case Menu::CalStringCenter:std::snprintf(b,sizeof(b),"String center: < %+.1f cm >",double(calibrationFor(menu.calibrationHand,s).stringCenter*100));return b;
    case Menu::CalStringSide:std::snprintf(b,sizeof(b),"String horizontal X: < %+.1f cm >",double(calibrationFor(menu.calibrationHand,s).stringSide*100));return b;
    case Menu::CalStringDepth:std::snprintf(b,sizeof(b),"String horizontal Z: < %+.1f cm >",double(calibrationFor(menu.calibrationHand,s).stringDepth*100));return b;
    case Menu::CalBow:return "Bow grip calibration  >";
    case Menu::CalBowString:return "Bow string / second grip  >";
    case Menu::CalArrow:return "Arrow in drawing hand  >";
    case Menu::GripLock:return std::string("Grip lock: ")+toggle(s.gripLock);
    case Menu::PhysicalCombat:return std::string("Physical combat: ")+toggle(s.physicalCombat);
    case Menu::HolsterAtLeft:return "Place selected holster at LEFT hand";
    case Menu::HolsterAtRight:return "Place selected holster at RIGHT hand";
    case Menu::HolsterPoint:return std::string("Point: < ")+slots[size_t(p)]+" >";
    case Menu::HolsterItem:return "Item: < "+contents(p)+" >";
    case Menu::HolsterMoveTarget:return "Move to: < "+std::string(slots[menu.holsterMoveTarget])+" >";
    case Menu::HolsterMove:return "Move / swap with selected holster";
    case Menu::HolsterClear:return "Clear holster (keep item)";
    case Menu::HolsterDrop:return "Drop one item into world";
    case Menu::IgnoreWeaponRequirements:return std::string("Ignore weapon requirements: ")+toggle(s.ignoreWeaponRequirements);
    case Menu::OpenGameInterface:return "Open game interface";
    case Menu::OpenCharacterStats:return "Character stats (level, attributes)";
    case Menu::HolsterX:case Menu::HolsterY:case Menu::HolsterZ: {
      float v=row==Menu::HolsterX?s.offsets[size_t(p)].x:row==Menu::HolsterY?s.offsets[size_t(p)].y:s.offsets[size_t(p)].z;
      std::snprintf(b,sizeof(b),"%s: < %+.1f cm >",row==Menu::HolsterX?"Right / left":row==Menu::HolsterY?"Height below head":"Forward / back",double(v*100));return b;
    }
    case Menu::HolsterRadius:case Menu::PickupRadius:std::snprintf(b,sizeof(b),"%s radius: < %.0f cm >",row==Menu::HolsterRadius?"Holster":"Pickup",double((row==Menu::HolsterRadius?s.radius:s.pickupRadius)*100));return b;
    case Menu::SwingSpeed:std::snprintf(b,sizeof(b),"Strike speed: < %.1f m/s >",double(s.swingSpeed));return b;
    case Menu::HolsterReset:return "Reset hands / holsters";
    case Menu::GodMode:return std::string("Immortality: ")+toggle(Gothic::inst().isGodMode());
    case Menu::Heal:return "Restore health";
    case Menu::EnemyCategory:return std::string("Category: < ")+enemyCategories[enemyCategory]+" >";
    case Menu::ItemCategory:return std::string("Category: < ")+itemCategories[itemCategory]+" >";
    case Menu::EnemySelect:case Menu::ItemSelect: {
      auto list=filtered(row==Menu::EnemySelect);if(list.empty())return "Load a game / empty category";
      const size_t i=size_t(row==Menu::EnemySelect?enemyIndex:itemIndex)%list.size();
      return std::to_string(i+1)+"/"+std::to_string(list.size())+": < "+list[size_t(i)]->name+" >";
    }
    case Menu::EnemySpawn:return "SPAWN selected enemy nearby";
    case Menu::ItemQuantity:return "Quantity: < "+std::to_string(quantity)+" >";
    case Menu::ItemGive:return "Add selected item to inventory";
    case Menu::TimeHour:return "Hour: < "+std::to_string(hour)+":00 >";
    case Menu::TimeApply:return "Apply time (next occurrence)";
    case Menu::WeatherMode:return std::string("Weather (live): < ")+weatherNames[weather]+" >";
    case Menu::WeatherApply:return "Apply weather";
    default:return {};
  }
}
}
#endif
