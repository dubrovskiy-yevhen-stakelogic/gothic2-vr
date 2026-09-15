#include "quickslots.h"

#include "gothic.h"
#include "world/objects/item.h"
#include "world/objects/npc.h"
#include "world/world.h"
#include "utils/string_frm.h"
#include "utils/quickslotinput.h"

QuickSlots::Kind QuickSlots::kind(const Item& item) {
  if((item.mainFlag()&(ITM_CAT_NF|ITM_CAT_FF))!=0) return Kind::Weapons;
  if(item.isSpellOrRune()) return Kind::Magic;
  if((item.mainFlag()&ITM_CAT_POTION)!=0) return Kind::Potions;
  if((item.mainFlag()&ITM_CAT_FOOD)!=0) return Kind::Food;
  if((item.mainFlag()&ITM_CAT_DOCS)!=0) return Kind::Documents;
  return Kind::Empty;
  }

QuickSlots::Kind QuickSlots::kind(size_t slot) {
  if(slot>=4 || Gothic::settingsGetS("QuickSlots",directions[slot]).empty()) return Kind::Empty;
  const auto category=Gothic::settingsGetS("QuickSlots",string_frm(directions[slot],"Kind"));
  for(auto k:{Kind::Weapons,Kind::Magic,Kind::Potions,Kind::Food,Kind::Documents})
    if(category==title(k)) return k;
  return Kind::Empty;
  }

std::string_view QuickSlots::title(Kind kind) {
  switch(kind) {
    case Kind::Weapons: return "Weapons";
    case Kind::Magic: return "Magic";
    case Kind::Potions: return "Potions";
    case Kind::Food: return "Food";
    case Kind::Documents: return "Maps and documents";
    default: return "Empty";
    }
  }

bool QuickSlots::assign(Npc& player,size_t slot,const Item& item) {
  if(slot>=4 || kind(item)==Kind::Empty || !item.checkCond(player) || player.itemCount(item.clsId())==0) return false;
  auto symbol=player.world().script().findSymbol(item.clsId());
  if(symbol==nullptr) return false;
  Gothic::settingsSetS("QuickSlots",directions[slot],symbol->name());
  Gothic::settingsSetS("QuickSlots",string_frm(directions[slot],"Kind"),title(kind(item)));
  Gothic::flushSettings();
  return true;
  }

Item* QuickSlots::resolve(Npc& player,size_t slot) {
  if(kind(slot)==Kind::Empty) return nullptr;
  auto& script=player.world().script();
  const auto name=Gothic::settingsGetS("QuickSlots",directions[slot]);
  if(auto item=player.getItem(script.findSymbolIndex(name)); item!=nullptr && item->count()>0) return item;

  // Only ordinary healing/mana potions substitute automatically, never permanent bonuses.
  const auto family=QuickSlotInput::potionFamily(name);
  if(family.empty()) return nullptr;
  for(auto tier:{"01","02","03"}) {
    if(auto item=player.getItem(script.findSymbolIndex(string_frm(family,tier))); item!=nullptr && item->count()>0 &&
       kind(*item)==Kind::Potions && item->checkCond(player)) {
      assign(player,slot,*item);
      return item;
      }
    }
  return nullptr;
  }
