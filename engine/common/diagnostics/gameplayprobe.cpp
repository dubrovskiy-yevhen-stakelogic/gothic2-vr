#include "gameplayprobe.h"
#include "gothic.h"
#include "game/playercontrol.h"
#include "game/gamescript.h"
#include "world/world.h"
#include "world/objects/npc.h"
#include "world/objects/item.h"
#include <Tempest/Log>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

std::atomic<GameplayProbe*> GameplayProbe::active = nullptr;
GameplayProbe::~GameplayProbe() {
  auto* expected = this;
  active.compare_exchange_strong(expected,nullptr);
}

void GameplayProbe::observeDamage(const Npc& victim, const Npc& attacker, int before, int after) {
  auto* self = active.load();
  if(self==nullptr || after>=before) return;
  if(&victim==self->targetIdentity && &attacker==self->heroIdentity) self->dealt += before-after;
  else if(&victim==self->heroIdentity && &attacker==self->targetIdentity) self->received += before-after;
  else if(&victim==self->heroIdentity || &victim==self->targetIdentity) self->foreign += before-after;
  else return;
  Tempest::Log::i("[gameplay] damage victim=",victim.instanceSymbol()," attacker=",attacker.instanceSymbol(),
                 " hp=",before,"->",after);
}

bool GameplayProbe::step(PlayerControl& input) {
  auto& world = *Gothic::inst().world();
  auto& hero = *Gothic::inst().player();
  const auto now = world.tickCount();
  if(hero.isDown()) throw std::runtime_error("gameplay probe: hero fell before completion");
  if(foreign.load()>0) throw std::runtime_error("gameplay probe: a third party damaged a duel participant");
  if(stage!=Begin && stage!=Complete && now-since>60000)
    throw std::runtime_error("gameplay probe stage timed out; inspect gameplay evidence");

  switch(stage) {
    case Begin:
      movementStart = hero.position();
      input.onKeyPressed(KeyCodec::Forward,Tempest::Event::K_Up,KeyCodec::Mapping::Primary);
      since = now; stage = Moving;
      Tempest::Log::i("[gameplay] holding forward through PlayerControl");
      break;
    case Moving:
      if(now-since<800) break;
      input.onKeyReleased(KeyCodec::Forward,KeyCodec::Mapping::Primary);
      input.clearInput();
      movementCm = (hero.position()-movementStart).length();
      if(movementCm<25.f) throw std::runtime_error("gameplay probe: forward input did not move hero by 25 cm");
      since = now; stage = Settling;
      Tempest::Log::i("[gameplay] movement cm=",movementCm);
      break;
    case Settling:
      if(now-since<1000 || hero.isAiBusy()) break;
      goldIndex = world.script().goldId()->index();
      goldBefore = hero.itemCount(goldIndex);
      pickupItem = world.addItem(goldIndex,hero.position()+Tempest::Vec3(20,10,0));
      if(pickupItem==nullptr) throw std::runtime_error("gameplay probe: world item creation failed");
      since = now; stage = Pickup;
      break;
    case Pickup:
      if(now-since<500 || !input.interact(*pickupItem)) break;
      goldAfter = hero.itemCount(goldIndex);
      if(goldAfter!=goldBefore+1) throw std::runtime_error("gameplay probe: pickup did not enter inventory");
      pickupItem = nullptr;
      since = now; stage = FindOpponent;
      Tempest::Log::i("[gameplay] world item picked up via PlayerControl/Npc::takeItem");
      break;
    case FindOpponent: {
      if(now-since<1500 || hero.isAiBusy()) break;
      auto* wolf = world.script().findSymbol("YWOLF");
      if(wolf==nullptr) throw std::runtime_error("gameplay probe: YWOLF script instance missing");
      Npc* enemy = nullptr;
      float bestSeparation = 0;
      // Prefer an existing isolated young wolf so a third party cannot satisfy the damage check.
      for(uint32_t i=0; i<world.npcCount(); ++i) {
        auto* candidate = world.npcById(i);
        if(candidate->instanceSymbol()!=wolf->index() || candidate->isDown()) continue;
        float separation = 1e12f;
        for(uint32_t j=0; j<world.npcCount(); ++j) {
          auto* other = world.npcById(j);
          if(other!=candidate && other!=&hero && !other->isDown()) separation = std::min(separation,candidate->qDistTo(*other));
          }
        if(separation>bestSeparation) { enemy=candidate; bestSeparation=separation; }
        }
      if(enemy==nullptr) throw std::runtime_error("gameplay probe: no living YWOLF in loaded world");
      targetId = world.npcId(enemy); targetName = wolf->name();
      heroIdentity = &hero; targetIdentity = enemy;
      // Diagnostic setup only: enough HP to observe the unmodified wolf AI and damage path.
      // No damage, strength, protection, hit chance or enemy attributes are overridden.
      hero.changeAttribute(ATR_HITPOINTSMAX,200-hero.attribute(ATR_HITPOINTSMAX),false);
      hero.changeAttribute(ATR_HITPOINTS,200-hero.attribute(ATR_HITPOINTS),false);
      heroHpBefore = hero.attribute(ATR_HITPOINTS); enemyHpBefore = enemy->attribute(ATR_HITPOINTS);
      hero.setPosition(enemy->position()+Tempest::Vec3(0,0,120));
      hero.setDirection(enemy->position()-hero.position());
      input.clearInput();
      input.onKeyPressed(KeyCodec::Weapon,Tempest::Event::K_Space,KeyCodec::Mapping::Primary);
      input.onKeyReleased(KeyCodec::Weapon,KeyCodec::Mapping::Primary);
      since = now; lastAttack = now; stage = Duel; active.store(this);
      Tempest::Log::i("[gameplay] duel target=",targetName," world_id=",targetId," enemy_hp=",enemyHpBefore,
                     " nearest_other_cm=",std::sqrt(bestSeparation));
      break;
      }
    case Duel: {
      auto* enemy = world.npcById(targetId);
      if(enemy==nullptr || enemy!=targetIdentity) throw std::runtime_error("gameplay probe: target disappeared");
      if(enemy->isDown()) {
        if(dealt.load()<=0 || received.load()<=0) throw std::runtime_error("gameplay probe: duel ended without verified damage in both directions");
        input.clearInput(); hero.closeWeapon(true);
        active.store(nullptr); since=now; stage=Quiesce;
        Tempest::Log::i("[gameplay] enemy defeated; player damage=",dealt.load()," enemy damage=",received.load());
        break;
        }
      if(hero.weaponState()!=WeaponState::Fist) break;
      hero.setDirection(enemy->position()-hero.position());
      input.actionFocus(*enemy);
      // Let the normal enemy AI land its first hit, then send buffered combat taps.
      if(received.load()>0 && now-lastAttack>=850) {
        input.controllerCombat(0,true);
        input.controllerCombat(0,false);
        lastAttack = now;
        }
      break;
      }
    case Quiesce:
      if(now-since<1500 || hero.isAiBusy()) break;
      stage=Complete;
      return true;
    case Complete: return true;
    }
  return false;
}

std::string GameplayProbe::evidence() const {
  rapidjson::StringBuffer b; rapidjson::Writer<rapidjson::StringBuffer> w(b);
  w.StartObject();
  w.Key("stage"); w.Uint(stage);
  w.Key("movement_cm"); w.Double(movementCm);
  w.Key("gold_before_pickup"); w.Uint64(goldBefore);
  w.Key("gold_after_pickup"); w.Uint64(goldAfter);
  w.Key("target"); w.String(targetName.c_str());
  w.Key("target_world_id"); w.Uint(targetId);
  w.Key("hero_hp_fixture"); w.Int(heroHpBefore);
  w.Key("enemy_initial_hp"); w.Int(enemyHpBefore);
  w.Key("damage_from_hero"); w.Int(dealt.load());
  w.Key("damage_from_enemy"); w.Int(received.load());
  w.Key("third_party_damage"); w.Int(foreign.load());
  w.Key("fixture_adjustments"); w.String("one spawned gold item; hero teleported to existing isolated YWOLF; hero HP/max HP set to 200 before duel; no damage or AI overrides");
  w.Key("scope"); w.String("PlayerControl and native RPG integration; not physical VR combat, input hardware, natural route or balance acceptance");
  w.EndObject(); return {b.GetString(),b.GetSize()};
}

std::string GameplayProbe::targetSnapshot() const {
  auto* enemy = Gothic::inst().world()->npcById(targetId);
  if(!enemy) throw std::runtime_error("gameplay probe: target absent from save state");
  rapidjson::StringBuffer b; rapidjson::Writer<rapidjson::StringBuffer> w(b);
  w.StartObject();
  w.Key("world_id"); w.Uint(targetId);
  w.Key("instance"); w.Uint(enemy->instanceSymbol());
  w.Key("hp"); w.Int(enemy->attribute(ATR_HITPOINTS));
  w.Key("dead"); w.Bool(enemy->isDead());
  w.Key("unconscious"); w.Bool(enemy->isUnconscious());
  w.EndObject(); return {b.GetString(),b.GetSize()};
}
