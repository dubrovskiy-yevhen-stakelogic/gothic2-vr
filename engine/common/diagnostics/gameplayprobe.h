#pragma once
#include <Tempest/Vec>
#include <atomic>
#include <cstdint>
#include <string>

class Npc;
class Item;
class PlayerControl;

// A disposable integration scenario, never enabled by ordinary game sessions.
// Inputs use PlayerControl; the damage observer does not change damage or AI.
class GameplayProbe final {
  public:
    ~GameplayProbe();
    bool step(PlayerControl& player);
    std::string evidence() const;
    std::string targetSnapshot() const;
    static void observeDamage(const Npc& victim, const Npc& attacker, int before, int after);
  private:
    enum Stage { Begin, Moving, Settling, Pickup, FindOpponent, Duel, Quiesce, Complete };
    Stage stage = Begin;
    uint64_t since = 0, lastAttack = 0;
    Tempest::Vec3 movementStart;
    float movementCm = 0;
    Item* pickupItem = nullptr;
    size_t goldIndex = 0, goldBefore = 0, goldAfter = 0;
    uint32_t targetId = uint32_t(-1);
    std::string targetName;
    const Npc* heroIdentity = nullptr;
    const Npc* targetIdentity = nullptr;
    int heroHpBefore = 0, enemyHpBefore = 0;
    std::atomic<int> dealt = 0, received = 0, foreign = 0;
    static std::atomic<GameplayProbe*> active;
};
