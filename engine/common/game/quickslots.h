#pragma once

#include <cstddef>
#include <string_view>

class Item;
class Npc;

// Store script names, never pointers or world-specific symbol indices, across game loads.
class QuickSlots final {
  public:
    enum class Kind { Empty, Weapons, Magic, Potions, Food, Documents };
    static constexpr const char* directions[] = {"Up", "Down", "Left", "Right"};

    static Kind kind(const Item& item);
    static Kind kind(size_t slot);
    static std::string_view title(Kind kind);
    static bool assign(Npc& player, size_t slot, const Item& item);
    static Item* resolve(Npc& player, size_t slot);
  };
