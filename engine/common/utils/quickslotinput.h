#pragma once

#include <string_view>
#include <initializer_list>

namespace QuickSlotInput {
// These standard item families contain temporary restoration potions in both games.
inline std::string_view potionFamily(std::string_view name) {
  for(std::string_view family:{"ITPO_HEALTH_", "ITPO_MANA_", "ITFO_POTION_HEALTH_", "ITFO_POTION_MANA_"}) {
    if(!name.starts_with(family)) continue;
    const auto tier=name.substr(family.size());
    if(tier=="01" || tier=="02" || tier=="03") return family;
    }
  return {};
  }
}
