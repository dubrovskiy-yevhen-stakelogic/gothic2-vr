#include "playercontrol.h"
#include "utils/feedback.h"

#include <cmath>
#include <algorithm>
#include <iterator>
#include <Tempest/Application>

#include "world/objects/npc.h"
#include "world/objects/item.h"
#include "world/objects/interactive.h"
#include "world/world.h"
#include "ui/dialogmenu.h"
#include "ui/inventorymenu.h"
#include "gothic.h"
#include "utils/gamepadbindings.h"
#include "utils/swiminput.h"
#include "utils/meleeassist.h"
#include "utils/movementresponse.h"

PlayerControl::PlayerControl(DialogMenu& dlg, InventoryMenu &inv)
  :dlg(dlg),inv(inv) {
  Gothic::inst().onSettingsChanged.bind(this,&PlayerControl::setupSettings);
  setupSettings();
  }

PlayerControl::~PlayerControl() {
  Gothic::inst().onSettingsChanged.ubind(this,&PlayerControl::setupSettings);
  }

void PlayerControl::setupSettings() {
#if defined(__ANDROID__)
  // Android uses the selected combat scheme for either game, including modern touch attacks and block.
  g2Ctrl = Gothic::inst().settingsGetI("GAME","USEGOTHIC1CONTROLS")==0;
#else
  if(Gothic::inst().version().game==2) {
    g2Ctrl = Gothic::inst().settingsGetI("GAME","USEGOTHIC1CONTROLS")==0;
    } else {
    g2Ctrl = false;
    }
#endif
  }

void PlayerControl::setTarget(Npc *other) {
  auto w  = Gothic::inst().world();
  auto pl = w ? w->player() : nullptr;
  if(pl==nullptr || pl->isFinishingMove())
    return;
  const auto ws    = pl->weaponState();
  const bool melle = (ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H);
  if(other==nullptr) {
    if(!(melle && ctrl[Action::ActionGeneric])) {
      // dont lose focus in melee combat
      pl->setTarget(nullptr);
      }
    } else {
    pl->setTarget(other);
    }
  }

void PlayerControl::onKeyPressed(KeyCodec::Action a, Tempest::KeyEvent::KeyType key, KeyCodec::Mapping mapping) {
  controllerKeyReleases[a]=false;
  auto       w    = Gothic::inst().world();
  auto       c    = Gothic::inst().camera();
  auto       pl   = w  ? w->player() : nullptr;
  auto       ws   = pl ? pl->weaponState() : WeaponState::NoWeapon;
  uint8_t    slot = pl ? pl->inventory().currentSpellSlot() : Item::NSLOT;

  if(w!=nullptr && w->isCutsceneLock())
    return;

  if(a==KeyCodec::LockTarget) {
    toggleTargetLock();
    return;
    }

  handleMovementAction(KeyCodec::ActionMapping{a,mapping}, true);

  if(pl!=nullptr && pl->interactive()!=nullptr && c!=nullptr && !c->isFree()) {
    auto inter = pl->interactive();
    if(inter->needToLockpick(*pl)) {
      processPickLock(*pl,*inter,a);
      return;
      }
    if(inter->isLadder()) {
      ctrl[a] = true;
      return;
      }
    }

  if(pl!=nullptr) {
    if(a==Action::Weapon) {
      if(ws!=WeaponState::NoWeapon) //Currently a weapon is active
        wctrl[WeaponClose] = true;
      else {
        if(wctrlLast>=WeaponAction::Weapon3 && pl->inventory().currentSpell(static_cast<uint8_t>(wctrlLast-3))==nullptr)
          wctrlLast=WeaponAction::WeaponBow;  //Spell no longer available -> fallback to Bow.
        if(wctrlLast==WeaponAction::WeaponBow && pl->currentRangedWeapon()==nullptr)
          wctrlLast=WeaponAction::WeaponMele; //Bow no longer available -> fallback to Mele.
        wctrl[wctrlLast] = true;
        }
      return;
      }

    if(a==Action::WeaponMele) {
      if(ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H)
        wctrl[WeaponClose] = true; else
        wctrl[WeaponMele ] = true;
      return;
      }

    if(a==Action::WeaponBow) {
      if(ws==WeaponState::Bow || ws==WeaponState::CBow)
        wctrl[WeaponClose] = true; else
        wctrl[WeaponBow  ] = true;
      return;
      }

    if(a>=Action::WeaponMage3 && a<=Action::WeaponMage10) {
      int id = (a-Action::WeaponMage3+3);
      if(ws==WeaponState::Mage && slot==id)
        wctrl[WeaponClose] = true; else
        wctrl[id         ] = true;
      return;
      }

    if(key==Tempest::KeyEvent::K_Return)
      ctrl[Action::K_ENTER] = true;
    }

  // this odd behaviour is from original game, seem more like a bug
  // const bool actTunneling = (pl!=nullptr && pl->isAttackAnim());
  const bool actTunneling = false;

  int fk = -1;
  if((ctrl[KeyCodec::ActionGeneric] || actTunneling) && !g2Ctrl) {
    if(a==Action::Forward) {
      if(pl!=nullptr && pl->target()!=nullptr && pl->canFinish(*pl->target()) && !pl->isAttackAnim()) {
        fk = ActKill;
        } else {
        fk = ActForward;
        }
      }
    if(ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H) {
      if(a==Action::Back)
        fk = ActBack;
      }
    if(ws!=WeaponState::NoWeapon && !g2Ctrl && !pl->hasState(BS_RUN)) {
      if(a==Action::Left  || a==Action::RotateL)
        fk = ActLeft;
      if(a==Action::Right || a==Action::RotateR)
        fk = ActRight;
      }
    }

  if(g2Ctrl) {
    if(ws!=WeaponState::NoWeapon) {
      if(a==Action::ActionGeneric) {
        if(pl!=nullptr && pl->target()!=nullptr && pl->canFinish(*pl->target()) && !pl->isAttackAnim()) {
          fk = ActKill;
          } else {
          if(this->wantsToMoveForward())
            fk = ActMove; else
            fk = ActForward;
          }
        }
      }
    if(ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H) {
      if(a==Action::Parade)
        fk = ActBack;
      }
    if(ws!=WeaponState::NoWeapon && !pl->hasState(BS_RUN)) {
      if(a==Action::ActionLeft)
        fk = ActLeft;
      if(a==Action::ActionRight)
        fk = ActRight;
      }
    }

  if(fk>=0) {
    std::memset(actrl,0,sizeof(actrl));
    actrl[ActGeneric] = ctrl[KeyCodec::ActionGeneric];
    actrl[fk]         = true;

    ctrl[a] = true;
    return;
    }

  if(a==KeyCodec::ActionGeneric) {
    FocusAction fk = ActGeneric;
    if(this->wantsToMoveForward())
      fk = ActMove;
    std::memset(actrl,0,sizeof(actrl));
    actrl[fk] = true;
    ctrl[a]   = true;
    return;
    }

  if(a==Action::Walk) {
    toggleWalkMode();
    return;
    }

  if(a==Action::Sneak) {
    toggleSneakMode();
    return;
    }

  if(a==Action::FirstPerson) {
    if(auto c = Gothic::inst().camera())
      c->setFirstPerson(!c->isFirstPerson());
    return;
    }

  if(a==Action::K_O && Gothic::inst().isMarvinEnabled())
    marvinO();

  ctrl[a] = true;
  }

void PlayerControl::onKeyReleased(KeyCodec::Action a, KeyCodec::Mapping mapping) {
  ctrl[a] = false;

  handleMovementAction(KeyCodec::ActionMapping{a, mapping}, false);

  auto w  = Gothic::inst().world();
  auto pl = w ? w->player() : nullptr;

  if(a==KeyCodec::Map && pl!=nullptr) {
    w->script().playerHotKeyScreenMap(*pl);
    }
  if(a==KeyCodec::Heal && pl!=nullptr) {
    w->script().playerHotLameHeal(*pl);
    }
  if(a==KeyCodec::Potion && pl!=nullptr) {
    w->script().playerHotLamePotion(*pl);
    }

  auto ws = pl==nullptr ? WeaponState::NoWeapon : pl->weaponState();
  if(ws==WeaponState::Bow || ws==WeaponState::CBow || ws==WeaponState::Mage) {
    if(a==KeyCodec::ActionGeneric || (!g2Ctrl && ws==WeaponState::Mage && a==KeyCodec::Forward))
      std::memset(actrl,0,sizeof(actrl));
    } else {
    std::memset(actrl,0,sizeof(actrl));
    }
  }

auto PlayerControl::handleMovementAction(KeyCodec::ActionMapping actionMapping, bool pressed) -> void {
  auto[action, mapping] = actionMapping;
  auto mappingIndex = (mapping == KeyCodec::Mapping::Primary ? size_t(0) : size_t(1));
  if (action == Action::Forward)
    movement.forwardBackward.main[mappingIndex] = pressed;
  else if (action == Action::Back)
    movement.forwardBackward.reverse[mappingIndex] = pressed;
  else if (action == Action::Right)
    movement.strafeRightLeft.main[mappingIndex] = pressed;
  else if (action == Action::Left)
    movement.strafeRightLeft.reverse[mappingIndex] = pressed;
  else if (action == Action::RotateR)
    movement.turnRightLeft.main[mappingIndex] = pressed;
  else if (action == Action::RotateL)
    movement.turnRightLeft.reverse[mappingIndex] = pressed;
  }

bool PlayerControl::isPressed(KeyCodec::Action a) const {
  return ctrl[a];
  }

void PlayerControl::setGamepadAxis(float lx, float ly) {
  touchAnalogMovement=false;
  touchTurn=0;
  controllerSwimming=false;
  swimJumpHeld=false;
  swimDiveStroke=false;
  if(controllerWalkApplied) {
    if(auto pl=Gothic::inst().player())
      pl->setWalkMode(WalkBit(uint8_t(pl->walkMode()) & ~uint8_t(WalkBit::WM_Walk)));
    controllerWalkApplied=false;
    }
  controllerGroundStrafe = false;
  controllerDirectional = false;
  gamepadLX = lx;
  gamepadLY = ly;
  }

void PlayerControl::setTurnMovement(float turn,float forward,bool walk,float turnSpeed) {
  setGamepadAxis(0.f,forward);
  touchAnalogMovement=true;
  applyControllerWalk(walk);
  controllerTurnSpeed=turnSpeed;
  touchTurn=std::clamp(turn,-1.f,1.f);
  }

void PlayerControl::applyControllerWalk(bool walk) {
  auto pl=Gothic::inst().player();
  if(pl==nullptr) return;
  if(controllerWalkApplied && !walk) {
    pl->setWalkMode(WalkBit(uint8_t(pl->walkMode()) & ~uint8_t(WalkBit::WM_Walk)));
    controllerWalkApplied=false;
    }
  if(walk && (pl->walkMode()&WalkBit::WM_Walk)==WalkBit::WM_Run) {
    pl->setWalkMode(pl->walkMode()|WalkBit::WM_Walk);
    controllerWalkApplied=true;
    }
  }

void PlayerControl::setControllerMovement(float x,float y,float cameraYaw,bool walk,float turnSpeed,float turnBoost) {
  touchTurn=0;
  touchAnalogMovement=false;
  controllerSwimming=false;
  swimJumpHeld=false;
  swimDiveStroke=false;
  auto pl=Gothic::inst().player();
  if(pl==nullptr) return;
  applyControllerWalk(walk);
  controllerDirectional=true;
  controllerGroundStrafe=false;
  controllerTurnSpeed=turnSpeed;
  controllerTurnBoost=turnBoost;
  if(pl->isSwim() || pl->isDive()) {
    gamepadLX=x; gamepadLY=y;
    } else if(controllerTarget!=nullptr) {
    controllerGroundStrafe=true;
    const auto axis=GamepadBindings::targetMovementAxis(x,y);
    gamepadLX=axis.first; gamepadLY=axis.second;
    } else {
    gamepadLX=0; gamepadLY=-std::sqrt(x*x+y*y);
    }
  controllerYaw=cameraYaw-std::atan2(x,-y)*180.f/float(M_PI);
  }

void PlayerControl::releaseControllerKey(KeyCodec::Action action,bool cancel) {
  if(!cancel && ctrl[action]) {
    controllerKeyReleases[action]=true;
    return;
    }
  controllerKeyReleases[action]=false;
  ctrl[action]=false;
  handleMovementAction({action,KeyCodec::Mapping::Secondary},false);
  }

void PlayerControl::setControllerSwim(float x,float y,float cameraYaw,float cameraPitch,float turnSpeed) {
  touchAnalogMovement=false;
  touchTurn=0;
  // Use the same movement curve as walking, but let camera pitch steer underwater.
  if(controllerWalkApplied) {
    if(auto pl=Gothic::inst().player())
      pl->setWalkMode(WalkBit(uint8_t(pl->walkMode()) & ~uint8_t(WalkBit::WM_Walk)));
    controllerWalkApplied=false;
    }
  controllerSwimming=true;
  controllerDirectional=false;
  controllerGroundStrafe=false;
  controllerTarget=nullptr;
  controllerTurnSpeed=turnSpeed;
  const auto aim=SwimInput::direction(x,y,cameraYaw,cameraPitch);
  controllerYaw=aim.yaw;
  swimPitch=aim.pitch;
  gamepadLX=0;
  gamepadLY=-std::min(1.f,std::hypot(x,y));
  }

void PlayerControl::setMeleeAssist(bool enabled,float maxAngle,float maxDistance) {
  meleeAssist=enabled;
  meleeAssistMaxAngle=maxAngle;
  meleeAssistMaxDistance=maxDistance;
  }

void PlayerControl::assistMeleeAttack(Npc& pl) {
  const auto ws=pl.weaponState();
  if(!meleeAssist || controllerTarget!=nullptr || pl.isAttackAnim() || !pl.isRotationAllowed() || pl.isSwim() || pl.isDive() ||
     (ws!=WeaponState::Fist && ws!=WeaponState::W1H && ws!=WeaponState::W2H)) return;
  auto& world=pl.world();
  auto target=world.validateFocus(currentFocus).npc;
  if(target==nullptr || target==&pl || target!=pl.target() || target->isDown() || !world.testFocusNpc(target)) return;
  const auto delta=target->centerPosition()-pl.centerPosition();
  if(delta.x==0.f && delta.z==0.f) return;
  const float targetYaw=std::atan2(delta.z,delta.x)*180.f/float(M_PI);
  const auto yaw=MeleeAssist::facing(pl.rotation(),targetYaw,delta.length(),
                                   meleeAssistMaxAngle,meleeAssistMaxDistance);
  if(yaw) {
    pl.setDirection(*yaw);
    pl.setAnimRotate(0);
    }
  }

void PlayerControl::controllerCombat(int direction,bool pressed,bool cancel,uint64_t holdMs) {
  // Explicit combat requests must not turn movement into an attack modifier.
  if(direction<0 || direction>=7) return;
  auto bowPlayer=Gothic::inst().player();
  const auto bowState=bowPlayer!=nullptr ? bowPlayer->weaponState() : WeaponState::NoWeapon;
  if(direction==ActForward && (bowState==WeaponState::Bow || bowState==WeaponState::CBow || controllerBowShot.active())) {
    if(cancel) {
      controllerBowShot.cancel();
      }
    else if(!pressed) {
      controllerBowShot.release();
      }
    else if(bowPlayer!=nullptr && !bowPlayer->isDown() && !bowPlayer->isAiBusy() &&
            bowPlayer->interactive()==nullptr && bowPlayer->hasAmmunition() &&
            (bowState==WeaponState::Bow || bowState==WeaponState::CBow)) {
      controllerBowWeapon=bowPlayer->inventory().activeWeapon()->clsId();
      controllerBowShot.press();
      }
    return;
    }
  if(direction==ActForward && (!pressed || cancel)) {
    controllerFinisher=nullptr;
    controllerFinishTime=0;
    }
  if(cancel) {
    actrl[direction]=false;
    if(direction==ActForward) {
      controllerReleaseAttack=false;
      controllerEmptyAttack.cancel();
      }
    if(direction==ActBack) controllerBlockPending=false;
    return;
    }
  if(!pressed) {
    // Keep short attack and parry taps until the simulation has consumed them.
    if(direction==ActForward && controllerEmptyAttack.active()) {
      const bool tap=controllerEmptyAttack.release(Tempest::Application::tickCount());
      auto pl=Gothic::inst().player();
      if(tap && pl!=nullptr && !pl->isDown() && pl->interactive()==nullptr && !pl->isAiBusy()) {
        const auto ws=pl->weaponState();
        if(ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H)
          actrl[ActForward]=true;
        }
      }
    if(direction==ActForward) controllerReleaseAttack=true;
    return;
    }
  auto pl=Gothic::inst().player();
  if(pl==nullptr || pl->isDown() || pl->interactive()!=nullptr || pl->isAiBusy()) return;
  const auto ws=pl->weaponState();
  if(ws==WeaponState::NoWeapon) return;
  if(direction==ActKill && (pl->target()==nullptr || !pl->canFinish(*pl->target()))) return;
  if(ws==WeaponState::Fist && (direction==ActLeft || direction==ActRight)) return;
  if(direction==ActForward && (ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H) &&
     currentFocus.npc==nullptr) {
    // An empty-target press becomes a swing only on a short release, never on a long hold.
    controllerEmptyAttack.begin(Tempest::Application::tickCount(),holdMs);
    actrl[ActForward]=false;
    controllerReleaseAttack=false;
    return;
    }
  if(direction==ActForward && (ws==WeaponState::W1H || ws==WeaponState::W2H) &&
     pl->target()!=nullptr && pl->canFinish(*pl->target())) {
    // Only a press begun over a finishable NPC can become a finishing blow.
    // Normal attacks stay immediate and cannot turn into executions when an enemy falls.
    controllerFinisher=pl->target();
    controllerFinishTime=holdMs;
    actrl[ActForward]=false;
    controllerReleaseAttack=false;
    return;
    }
  if(direction==ActForward) controllerReleaseAttack=false;
  if(direction==ActBack) controllerBlockPending=true;
  actrl[direction]=true;
  }

void PlayerControl::controllerInteract(bool sheath) {
  auto w=Gothic::inst().world();
  auto pl=Gothic::inst().player();
  if(w==nullptr || pl==nullptr || pl->isDown()) return;
  if(pl->weaponState()!=WeaponState::NoWeapon) {
    if(!sheath) return;
    pendingInteraction=w->findFocus(*pl,Focus(),true);
    pendingInteractionUntil=w->tickCount()+3000;
    controllerTarget=nullptr;
    wctrl[WeaponClose]=true;
    return;
    }
  auto f=w->findFocus(Focus());
  if(f.item) interact(*f.item);
  else if(f.interactive) interact(*f.interactive);
  else if(f.npc) interact(*f.npc);
  }

void PlayerControl::controllerEquip(size_t item,bool toggleDraw) {
  auto pl=Gothic::inst().player();
  auto active=pl!=nullptr ? pl->activeWeapon() : nullptr;
  // Direct slot taps toggle the weapon in hand; wheel choices only ready equipment.
  if(toggleDraw && active!=nullptr && active->clsId()==item) {
    pendingEquipment=size_t(-1);
    std::fill(std::begin(wctrl),std::end(wctrl),false);
    wctrl[WeaponClose]=true;
    return;
    }
  pendingEquipment=item;
  }

void PlayerControl::toggleTargetLock() {
  if(controllerTarget!=nullptr) { controllerTarget=nullptr; return; }
  auto w=Gothic::inst().world();
  auto pl=Gothic::inst().player();
  if(w==nullptr || pl==nullptr || pl->weaponState()==WeaponState::NoWeapon) return;
  auto f=w->findFocus(Focus());
  if(f.npc!=nullptr && !f.npc->isDown() && w->testFocusNpc(f.npc)) {
    controllerTarget=f.npc;
    currentFocus=f;
    setTarget(f.npc);
    }
  }

void PlayerControl::switchControllerTarget(bool right) {
  if(controllerTarget==nullptr) return;
  auto world=Gothic::inst().world();
  Focus target;
  target.npc=controllerTarget;
  if(world==nullptr || world->validateFocus(target).npc==nullptr) {
    controllerTarget=nullptr;
    return;
    }
  auto old=controllerTarget;
  currentFocus=Focus(*controllerTarget);
  moveFocus(right?ActRight:ActLeft);
  if(currentFocus.npc!=nullptr && !currentFocus.npc->isDown())
    controllerTarget=currentFocus.npc;
  else currentFocus=Focus(*old);
  setTarget(controllerTarget);
  }

void PlayerControl::onRotateMouse(float dAngleX, float dAngleY) {
  rotMouse  += dAngleX;
  rotMouseY += dAngleY;
  }

void PlayerControl::drawVobRay(DbgPainter& p) const {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return;
  auto pl    = w->player();
  auto focus = findFocus(&currentFocus);
  if(focus.interactive!=nullptr) {
    focus.interactive->drawVobRay(p, *pl);
    }
  if(focus.item!=nullptr) {
    focus.item->drawVobRay(p, *pl);
    }
  if(focus.npc!=nullptr) {
    pl->drawVobRay(p, *focus.npc);
    }
  }

void PlayerControl::tickFocus() {
  auto w=Gothic::inst().world();
  auto pl=Gothic::inst().player();
  if(controllerTarget!=nullptr && w!=nullptr && pl!=nullptr) {
    Focus target;
    target.npc=controllerTarget;
    auto valid=w->validateFocus(target);
    // Retain focus with Gothic's existing cached-target rules, not acquisition angle tests.
    if(valid.npc==nullptr || valid.npc->isDown() || pl->isDown() || pl->weaponState()==WeaponState::NoWeapon ||
       w->findFocus(target).npc!=controllerTarget)
      controllerTarget=nullptr;
    }
  currentFocus = findFocus(&currentFocus);
  if(controllerTarget!=nullptr)
    currentFocus=Focus(*controllerTarget);
  setTarget(currentFocus.npc);

  if(pendingInteractionUntil!=0 && w!=nullptr && pl!=nullptr) {
    const auto valid=w->validateFocus(pendingInteraction);
    const auto now=w->findFocus(*pl,Focus(),true);
    if(w->tickCount()>pendingInteractionUntil || pl->isDown() ||
       valid.item!=pendingInteraction.item || valid.npc!=pendingInteraction.npc || valid.interactive!=pendingInteraction.interactive ||
       now.item!=pendingInteraction.item || now.npc!=pendingInteraction.npc || now.interactive!=pendingInteraction.interactive) {
      pendingInteractionUntil=0;
      }
    else if(pl->weaponState()==WeaponState::NoWeapon && canInteract()) {
      pendingInteractionUntil=0;
      controllerInteract(false);
      }
    }

  if(!ctrl[Action::ActionGeneric])
    return;

  auto focus = currentFocus;
  if(focus.interactive!=nullptr && interact(*focus.interactive)) {
    clearInput();
    }
  else if(focus.npc!=nullptr && interact(*focus.npc)) {
    clearInput();
    }
  else if(focus.item!=nullptr && interact(*focus.item)) {
    clearInput();
    }

  if(focus.npc)
    actionFocus(*focus.npc); else
    emptyFocus();
  }

void PlayerControl::clearFocus() {
  controllerBowShot.cancel();
  controllerReleaseAttack=false;
  currentFocus = Focus();
  controllerTarget=nullptr;
  pendingInteractionUntil=0;
  pendingEquipment=size_t(-1);
  }

void PlayerControl::actionFocus(Npc& other) {
  setTarget(&other);
  }

void PlayerControl::emptyFocus() {
  setTarget(nullptr);
  }

Focus PlayerControl::focus() const {
  return currentFocus;
  }

bool PlayerControl::hasActionFocus() const {
  if(!ctrl[Action::ActionGeneric])
    return false;
  return currentFocus.npc!=nullptr;
  }

bool PlayerControl::interact(Interactive &it) {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return false;
  auto pl = w->player();
  if(w->player()->isDown())
    return true;
  if(!canInteract())
    return false;
  if(it.isContainer()){
    inv.open(*pl,it);
    return true;
    }
  if(pl->setInteraction(&it)){
    }
  return true;
  }

bool PlayerControl::interact(Npc &other) {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return false;
  auto pl = w->player();
  if(pl->isDown())
    return true;
  if(!canInteract())
    return false;
  auto state = pl->bodyStateMasked();
  if(other.isDown()) {
    if(Gothic::settingsGetI("GAME","skipEmptyLoot")!=0 && !other.inventory().iterator(Inventory::T_Ransack).isValid())
      return true;
    if(state!=BS_STAND && state!=BS_SNEAK && state!=BS_SWIM && state!=BS_DIVE)
      return false;
    if(!inv.ransack(*w->player(),other))
      w->script().printNothingToGet();
    } else {
    if((state&BS_MAX)!=BS_NONE)
      return false;
    other.startDialog(*pl);
    }
  return true;
  }

bool PlayerControl::interact(Item &item) {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return false;
  auto pl = w->player();
  if(item.isTorchBurn() && pl->isUsingTorch())
    return false;
  if(pl->isDown())
    return true;
  if(!canInteract())
    return false;
  return pl->takeItem(item)!=nullptr;
  }

void PlayerControl::moveFocus(FocusAction act) {
  auto w = Gothic::inst().world();
  auto c = Gothic::inst().camera();
  if(w==nullptr || c==nullptr || currentFocus.npc==nullptr)
    return;

  auto vp  = c->viewProj();
  auto pos = currentFocus.npc->centerPosition();
  vp.project(pos);

  Npc* next = nullptr;
  auto npos = Tempest::Vec3();
  for(uint32_t i=0; i<w->npcCount(); ++i) {
    auto npc = w->npcById(i);
    if(npc->isPlayer())
      continue;
    auto p = npc->centerPosition();
    vp.project(p);

    if(std::abs(p.x)>1.f || std::abs(p.y)>1.f || p.z<0.f)
      continue;

    if(!w->testFocusNpc(npc))
      continue;

    if(act==ActLeft && p.x<pos.x && (next==nullptr || npos.x<p.x)) {
      npos = p;
      next = npc;
      }
    if(act==ActRight && p.x>pos.x && (next==nullptr || npos.x>p.x)) {
      npos = p;
      next = npc;
      }
    }

  if(next==nullptr)
    return;
  currentFocus.npc = next;
  }

void PlayerControl::toggleWalkMode() {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return;
  auto pl = w->player();
  if(controllerWalkApplied) {
    pl->setWalkMode(pl->walkMode()^WalkBit::WM_Walk);
    controllerWalkApplied=false;
    }
  pl->setWalkMode(pl->walkMode()^WalkBit::WM_Walk);
  }

void PlayerControl::toggleSneakMode() {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return;
  auto pl = w->player();
  if(pl->canSneak())
    pl->setWalkMode(pl->walkMode()^WalkBit::WM_Sneak);
  }

void PlayerControl::setSneaking(bool enabled) {
  auto pl=Gothic::inst().player();
  if(pl==nullptr || pl->isDown() || (enabled && !pl->canSneak())) return;
  auto mode=uint8_t(pl->walkMode());
  if(enabled) mode|=uint8_t(WalkBit::WM_Sneak);
  else mode&=~uint8_t(WalkBit::WM_Sneak);
  pl->setWalkMode(WalkBit(mode));
  }

bool PlayerControl::canInteract() const {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return false;
  auto pl = w->player();
  if(pl->weaponState()!=WeaponState::NoWeapon || pl->isAiBusy())
    return false;
  return true;
  }

void PlayerControl::clearMovementInput() {
  movement.reset();
  for(const auto action : {KeyCodec::Forward,KeyCodec::Back,KeyCodec::Left,KeyCodec::Right,KeyCodec::RotateL,KeyCodec::RotateR}) {
    ctrl[action] = false;
    controllerKeyReleases[action] = false;
    }
  }

void PlayerControl::clearInput() {
  touchAnalogMovement=false;
  touchTurn=0;
  controllerFinisher=nullptr;
  controllerFinishTime=0;
  controllerSwimming=false;
  swimJumpHeld=false;
  swimDiveStroke=false;
  controllerKeyReleases.fill(false);
  controllerReleaseAttack=false;
  controllerBlockPending=false;
  controllerEmptyAttack.cancel();
  controllerBowShot.cancel();
  pendingEquipment=size_t(-1);
  if(controllerWalkApplied) {
    if(auto pl=Gothic::inst().player())
      pl->setWalkMode(WalkBit(uint8_t(pl->walkMode()) & ~uint8_t(WalkBit::WM_Walk)));
    }
  controllerWalkApplied=false;
  controllerDirectional=false;
  controllerGroundStrafe=false;
  gamepadLX=0; gamepadLY=0;
  controllerTarget=nullptr;
  pendingInteractionUntil=0;
  movement.reset();
  std::memset(ctrl, 0,sizeof(ctrl));
  std::memset(actrl,0,sizeof(actrl));
  std::memset(wctrl,0,sizeof(wctrl));
  }

void PlayerControl::marvinF8(uint64_t dt) {
  auto w = Gothic::inst().world();
  if(w==nullptr || w->player()==nullptr)
    return;

  auto& pl  = *w->player();
  auto  pos = pl.position();
  float rot = pl.rotationRad();
  float s   = std::sin(rot), c = std::cos(rot);

  Tempest::Vec3 dp(c,0.8f,s);
  pos += dp*6000*float(dt)/1000.f;

  pl.changeAttribute(ATR_HITPOINTS,pl.attribute(ATR_HITPOINTSMAX),false);
  pl.changeAttribute(ATR_MANA,     pl.attribute(ATR_MANAMAX),     false);
  pl.clearState(false);
  pl.clearSpeed();
  pl.clearAiQueue();
  pl.setPosition(pos);
  pl.setInteraction(nullptr,true);
  pl.setAnim(AnimationSolver::Idle);

  if(auto c = Gothic::inst().camera())
    c->reset();
  }

void PlayerControl::marvinK(uint64_t dt) {
  auto w = Gothic::inst().world();
  if (w == nullptr || w->player() == nullptr)
    return;

  auto& pl = *w->player();
  auto  pos = pl.position();
  float rot = pl.rotationRad();
  float s = std::sin(rot), c = std::cos(rot);

  Tempest::Vec3 dp(c, 0.0f, s);
  pos += dp * 6000 * float(dt) / 1000.f;

  pl.clearState(false);
  pl.clearSpeed();
  pl.setPosition(pos);
  pl.setInteraction(nullptr,true);
  // pl.setAnim(AnimationSolver::Idle); // Original G2 behaviour: K doesn't stop running
  }

void PlayerControl::marvinO() {
  auto w = Gothic::inst().world();
  if (w == nullptr || w->player() == nullptr || w->player()->target() == nullptr)
    return;

  auto target = w->player()->target();

  w->setPlayer(target);
  }

Focus PlayerControl::findFocus(const Focus* prev) const {
  auto w = Gothic::inst().world();
  auto c = Gothic::inst().camera();
  if(w==nullptr)
    return Focus();
  if(w->player()!=nullptr && w->player()->isDown())
    return Focus();
  if(c!=nullptr && c->isCutscene())
    return Focus();
  if(!cacheFocus)
    prev = nullptr;

  if(prev)
    return w->findFocus(*prev);
  return w->findFocus(Focus());
  }

bool PlayerControl::tickCameraMove(uint64_t dt) {
  auto w = Gothic::inst().world();
  if(w==nullptr)
    return false;

  Npc*  pl     = w->player();
  auto  camera = Gothic::inst().camera();
  if(camera==nullptr || (pl!=nullptr && !camera->isFree()))
    return false;

  rotMouse = 0;
  if(ctrl[KeyCodec::Left] || (ctrl[KeyCodec::RotateL] && ctrl[KeyCodec::Jump])) {
    camera->moveLeft(dt);
    return true;
    }
  if(ctrl[KeyCodec::Right] || (ctrl[KeyCodec::RotateR] && ctrl[KeyCodec::Jump])) {
    camera->moveRight(dt);
    return true;
    }

  auto turningVal = movement.turnRightLeft.value();
  if(turningVal > 0.f)
    camera->rotateRight(dt);
  else if(turningVal < 0.f)
    camera->rotateLeft(dt);

  auto forwardVal = movement.forwardBackward.value();
  if(forwardVal > 0.f)
    camera->moveForward(dt);
  else if(forwardVal < 0.f)
    camera->moveBack(dt);
  return true;
  }

bool PlayerControl::tickMove(uint64_t dt) {
  auto w = Gothic::inst().world();
  if(w==nullptr)
    return false;
  const float dtF = float(dt)/1000.f;

  Npc*  pl     = w->player();
  auto  camera = Gothic::inst().camera();

  if(w->isCutsceneLock())
    clearInput();

  if(tickCameraMove(dt)) {
    controllerBowShot.cancel();
    return true;
    }

  if(ctrl[Action::K_F8] && Gothic::inst().isMarvinEnabled())
    marvinF8(dt);
  if(ctrl[Action::K_K] && Gothic::inst().isMarvinEnabled())
    marvinK(dt);
  cacheFocus = ctrl[Action::ActionGeneric] || controllerTarget!=nullptr || controllerBowShot.active();
  if(camera!=nullptr)
    camera->setLookBack(ctrl[Action::LookBack]);

  if(pl==nullptr)
    return true;

  if(controllerFinisher!=nullptr) {
    const auto ws=pl->weaponState();
    if(pl->target()!=controllerFinisher || !pl->canFinish(*pl->target()) || pl->isDown() ||
       pl->interactive()!=nullptr || (ws!=WeaponState::W1H && ws!=WeaponState::W2H)) {
      controllerFinisher=nullptr;
      }
    else if(dt>=controllerFinishTime) {
      actrl[ActKill]=true;
      controllerFinisher=nullptr;
      }
    else controllerFinishTime-=dt;
    }
  implMove(dt);
  if(controllerBlockPending) {
    // One parry attempt per button press, never a held auto-parry or a delayed attack interrupt.
    actrl[ActBack]=false;
    controllerBlockPending=false;
    }
  for(size_t i=0;i<controllerKeyReleases.size();++i)
    if(controllerKeyReleases[i]) releaseControllerKey(Action(i),true);

  float runAngle = pl->runAngle();
  if(runAngle!=0.f || std::fabs(runAngleDest)>0.01f) {
    const float speed = 35.f;
    if(runAngle<runAngleDest) {
      runAngle+=speed*dtF;
      if(runAngle>runAngleDest)
        runAngle = runAngleDest;
      pl->setRunAngle(runAngle);
      }
    else if(runAngle>runAngleDest) {
      runAngle-=speed*dtF;
      if(runAngle<runAngleDest)
        runAngle = runAngleDest;
      pl->setRunAngle(runAngle);
      }
    }

  rotMouseY = 0;
  return true;
  }

void PlayerControl::implMove(uint64_t dt) {
  auto  w         = Gothic::inst().world();
  Npc&  pl        = *w->player();
  float rot       = pl.rotation();
  float rotY      = pl.rotationY();
  // 100 / 200 according to some sources, yet my mesures are 90/180
  float rspeed    = (pl.weaponState()==WeaponState::NoWeapon ? 90.f : 180.f)*(float(dt)/1000.f);
  auto  ws        = pl.weaponState();
  auto  bs        = pl.bodyStateMasked();
  bool  allowRot  = !ctrl[KeyCodec::ActionGeneric] && pl.isRotationAllowed();

  Npc::Anim ani = Npc::Anim::Idle;

  if(controllerBowShot.active()) {
    const auto weapon=pl.inventory().activeWeapon();
    if((ws!=WeaponState::Bow && ws!=WeaponState::CBow) || weapon==nullptr || weapon->clsId()!=controllerBowWeapon ||
       !pl.hasAmmunition() || pl.isDown() || bs==BS_STUMBLE || pl.isAiBusy() || pl.interactive()!=nullptr ||
       !pl.isInState(ScriptFn()) || dlg.isActive() || pl.isFalling() || pl.isSlide() || pl.isInAir() ||
       pl.isJump() || pl.isJumpUp() || pl.isSwim() || pl.isDive() || pendingEquipment!=size_t(-1) ||
       std::any_of(std::begin(wctrl),std::end(wctrl),[](bool pending){ return pending; }))
      controllerBowShot.cancel();
    else
      controllerBowShot.advance(dt);
    }

  if(bs==BS_DEAD)
    return;
  if(bs==BS_UNCONSCIOUS)
    return;

  if(!pl.isAiQueueEmpty()) {
    runAngleDest = 0;
    return;
    }

  if(pl.interactive()!=nullptr) {
    runAngleDest = 0;
    implMoveMobsi(pl,dt);
    return;
    }

  if(pl.canSwitchWeapon()) {
    // Selecting the weapon already in hand must not start a sheathe/draw cycle.
    if(auto active=pl.activeWeapon(); active!=nullptr && active->clsId()==pendingEquipment)
      pendingEquipment=size_t(-1);
    if(pendingEquipment!=size_t(-1)) {
      if(pl.weaponState()!=WeaponState::NoWeapon) {
        pl.closeWeapon(false);
        return;
        }
      const auto id=pendingEquipment;
      pendingEquipment=size_t(-1);
      auto item=pl.getItem(id);
      if(item!=nullptr && item->checkCond(pl)) {
        if((item->mainFlag()&(ITM_CAT_NF|ITM_CAT_FF))!=0) {
          if(pl.currentMeleeWeapon()!=item && pl.currentRangedWeapon()!=item)
            pl.useItem(id,Item::NSLOT,false);
          if(pl.currentMeleeWeapon()==item) wctrl[WeaponMele]=true;
          if(pl.currentRangedWeapon()==item) wctrl[WeaponBow]=true;
          }
        else if(item->isSpellOrRune()) {
          // Quick slots may select a scroll/rune that has no numbered spell slot yet.
          if(!item->isEquipped()) {
            uint8_t slot=3;
            for(uint8_t i=0;i<8;++i)
              if(pl.inventory().currentSpell(i)==nullptr) { slot=uint8_t(3+i); break; }
            pl.useItem(id,slot,false);
            }
          for(uint8_t i=0;i<8;++i)
            if(pl.inventory().currentSpell(i)==item) wctrl[Weapon3+i]=true;
          }
        }
      }
    if(wctrl[WeaponClose]) {
      wctrl[WeaponClose] = !(pl.closeWeapon(false) || pl.isMonster());
      return;
      }
    if(wctrl[WeaponMele]) {
      bool ret=false;
      if(pl.currentMeleeWeapon()!=nullptr)
        ret = pl.drawWeaponMelee(); else
        ret = pl.drawWeaponFist();
      wctrl[WeaponMele] = !ret;
      wctrlLast         = WeaponMele;
      if(!wctrl[WeaponMele])
        return;
      }
    if(wctrl[WeaponBow]) {
      if(pl.currentRangedWeapon()!=nullptr) {
        wctrl[WeaponBow] = !pl.drawWeaponBow();
        wctrlLast        = WeaponBow;
        } else {
        wctrl[WeaponBow] = false;
        }
      if(!wctrl[WeaponBow])
        return;
      }
    for(uint8_t i=0;i<8;++i) {
      if(wctrl[Weapon3+i]){
        if(pl.inventory().currentSpell(i)!=nullptr){
          bool ret = pl.drawMage(uint8_t(3+i));
          wctrl[Weapon3+i] = !ret;
          wctrlLast = static_cast<WeaponAction>(Weapon3+i);
          if(ret) {
            if(auto spl = pl.inventory().currentSpell(i)) {
              Gothic::inst().onPrint(spl->description());
              }
            }
          } else {
          wctrl[Weapon3+i] = false;
          return;
          }
        }
      }
    }

  if(!pl.isInState(ScriptFn()) || dlg.isActive()) {
    runAngleDest = 0;
    return;
    }

  if(controllerSwimming && (pl.isSwim() || pl.isDive())) {
    implSwim(pl,dt);
    return;
    }

  int rotation = 0;
  if(controllerDirectional && controllerTarget==nullptr && gamepadLY!=0.f && allowRot && !pl.isAttackAnim()) {
    const float delta=std::remainder(controllerYaw-rot,360.f);
    rot+=MovementResponse::turn(delta,std::abs(gamepadLY),controllerTurnSpeed,controllerTurnBoost,float(dt)/1000.f);
    }
  if(allowRot) {
    if(touchTurn!=0.f) {
      rot-=controllerTurnSpeed*float(dt)/1000.f*touchTurn;
      rotation=touchTurn>0.f ? 1 : -1;
      rotMouse=0;
      }
    if(this->wantsToTurnLeft()) {
      rot += rspeed;
      rotation = -1;
      rotMouse=0;
      }
    if(this->wantsToTurnRight()) {
      rot -= rspeed;
      rotation = 1;
      rotMouse=0;
      }
    if(std::fabs(rotMouse)>0.f) {
      if(rotMouse>0)
        rotation = -1; else
        rotation = 1;
      rot += rotMouse;
      rotMouse  = 0;
      }
    rotY+=rotMouseY;
    } else {
    rotMouse  = 0;
    rotMouseY = 0;
    }

  pl.setDirectionY(rotY);
  if(pl.isFalling() || pl.isSlide() || pl.isInAir() || pl.isJump() || pl.isJumpUp()){
    pl.setDirection(rot);
    runAngleDest = 0;
    return;
    }

  if(casting) {
    if(controllerReleaseAttack) {
      actrl[ActForward]=false;
      controllerReleaseAttack=false;
      }
    if(!actrl[ActForward] || (Gothic::inst().version().game==1 && pl.attribute(ATR_MANA)==0)) {
      casting = false;
      pl.endCastSpell(true);
      }
    return;
    }

  if(ctrl[Action::K_ENTER]) {
    pl.transformBack();
    ctrl[Action::K_ENTER] = false;
    }

  if((ws==WeaponState::Bow || ws==WeaponState::CBow) && pl.hasAmmunition()) {
    if(actrl[ActGeneric] || actrl[ActForward] || controllerBowShot.active()) {
      if(auto other = pl.target()) {
        auto dp = other->position()-pl.position();
        pl.turnTo(dp.x,dp.z,true,dt);
        pl.aimBow();
        }
      else if(currentFocus.interactive!=nullptr) {
        auto dp = currentFocus.interactive->position()-pl.position();
        pl.turnTo(dp.x,dp.z,false,dt);
        pl.aimBow();
        }
      else {
        pl.aimBow();
        }

      if(actrl[ActLeft]) {
        moveFocus(ActLeft);
        actrl[ActLeft]  = false;
        }
      if(actrl[ActRight]) {
        moveFocus(ActRight);
        actrl[ActRight]  = false;
        }
      if(controllerBowShot.active()) {
        // shootBow also reports success for some non-shooting states; only aim states can fire.
        const auto aimState=pl.bodyStateMasked();
        if((aimState==BS_AIMNEAR || aimState==BS_AIMFAR) && pl.shootBow(currentFocus.interactive))
          controllerBowShot.fired();
        return;
        }
      if(!actrl[ActForward])
        return;
      }
    }

  if(ws==WeaponState::Mage) {
    if(actrl[ActGeneric] || actrl[ActForward] || ctrl[KeyCodec::ActionGeneric]) {
      if(auto other = pl.target()) {
        auto dp = other->centerPosition() - pl.centerPosition();
        pl.turnTo(dp.x,dp.z,true,dt);
        } else
      if(currentFocus.interactive!=nullptr) {
        auto dp = currentFocus.interactive->position()-pl.position();
        pl.turnTo(dp.x,dp.z,false,dt);
        }

      if(actrl[ActLeft]) {
        moveFocus(ActLeft);
        actrl[ActLeft]  = false;
        }
      if(actrl[ActRight]) {
        moveFocus(ActRight);
        actrl[ActRight]  = false;
        }
      if(!actrl[ActForward]) {
        pl.setAnim(Npc::Anim::Idle);
        return;
        }
      }
    }

  if(actrl[ActForward] || actrl[ActMove]) {
    assistMeleeAttack(pl);
    if(controllerReleaseAttack) {
      actrl[ActForward]=false;
      controllerReleaseAttack=false;
      }
    ctrl [Action::Forward] = actrl[ActMove];
    actrl[ActMove]         = false;
    if(ws!=WeaponState::Mage && !(g2Ctrl && (ws==WeaponState::Bow || ws==WeaponState::CBow))) {
      actrl[ActForward] = false;
      if(!ctrl[Action::Forward])
        movement.reset();
      }
    switch(ws) {
      case WeaponState::NoWeapon:
        break;
      case WeaponState::Fist:
        pl.fistShoot();
        return;
      case WeaponState::W1H:
      case WeaponState::W2H: {
        pl.swingSword();
        return;
        }
      case WeaponState::Bow:
      case WeaponState::CBow: {
        pl.shootBow(currentFocus.interactive);
        return;
        }
      case WeaponState::Mage: {
        casting = (pl.beginCastSpell()==Npc::BC_Invest);
        if(!casting)
          actrl[ActForward] = false;
        return;
        }
      }
    }

  if(actrl[ActKill]) {
    if((ws==WeaponState::W1H || ws==WeaponState::W2H) && pl.target()!=nullptr && pl.canFinish(*pl.target()))
      pl.finishingMove();
    actrl[ActKill] = false;
    }

  if(actrl[ActLeft] || actrl[ActRight] || actrl[ActBack]) {
    auto ws = pl.weaponState();
    if(ws==WeaponState::Fist) {
      if(actrl[ActBack])
        pl.blockFist();
      return;
      }
    else if(ws==WeaponState::W1H || ws==WeaponState::W2H) {
      if(actrl[ActLeft] || actrl[ActRight]) assistMeleeAttack(pl);
      if(actrl[ActLeft] && pl.swingSwordL()) {
        movement.strafeRightLeft.reset();
        }
      else if(actrl[ActRight] && pl.swingSwordR()) {
        movement.strafeRightLeft.reset();
        }
      else if(actrl[ActBack] && pl.blockSword()) {
        // movement.forwardBackward.reset();
        }

      actrl[ActLeft]  = false;
      actrl[ActRight] = false;
      // actrl[ActBack]  = false;
      return;
      }
    else if(ws==WeaponState::Mage) {
      if(actrl[ActLeft]) {
        moveFocus(ActLeft);
        actrl[ActLeft]  = false;
        }
      if(actrl[ActRight]) {
        moveFocus(ActRight);
        actrl[ActRight]  = false;
        }
      }
    }

  if(this->wantsToStrafeLeft()) {
    ani = Npc::Anim::MoveL;
    }
  else if(this->wantsToStrafeRight()) {
    ani = Npc::Anim::MoveR;
    }
  else if(this->wantsToMoveForward()) {
    if((pl.walkMode()&WalkBit::WM_Dive)!=WalkBit::WM_Dive) {
      ani = Npc::Anim::Move;
      }
    else if(pl.isDive()) {
      pl.setDirectionY(rotY - rspeed);
      return;
      }
    }
  else if(this->wantsToMoveBackward()) {
    if((pl.walkMode()&WalkBit::WM_Dive)!=WalkBit::WM_Dive) {
      ani = Npc::Anim::MoveBack;
      } else if(pl.isDive()) {
      pl.setDirectionY(rotY + rspeed);
      return;
      }
    }


  if(ctrl[Action::Jump]) {
    if(pl.bodyStateMasked()==BS_JUMP) {
      ani = Npc::Anim::Idle;
      }
    else if(pl.isDive()) {
      ani = Npc::Anim::Move;
      }
    else if(pl.isSwim()) {
      pl.startDive();
      }
    else if(pl.isInWater()) {
      auto& g  = w->script().guildVal();
      auto  gl = pl.guild();

      if(0<=gl && gl<GIL_MAX && pl.isStanding()) {
        MoveAlgo::JumpStatus jump;
        jump.anim   = Npc::Anim::JumpUp;
        jump.height = float(g.jumpup_height[gl])+pl.position().y;
        pl.startClimb(jump);
        }
      }
    else if(pl.isStanding()) {
      auto jump = pl.tryJump();
      if(!pl.isFalling() && !pl.isSlide() && jump.anim!=Npc::Anim::Jump){
        pl.startClimb(jump);
        return;
        }
      ani = Npc::Anim::Jump;
      }
    else if(!pl.isAttackAnim() && !pl.isCasting()) {
      ani = Npc::Anim::Jump;
      }
    }

  if(!pl.isCasting()) {
    if(ani==Npc::Anim::Jump) {
      pl.setAnimRotate(0);
      rotation = 0;
      }

    if(pl.isAttackAnim()) {
      if((ani==Npc::Anim::MoveL || ani==Npc::Anim::MoveR/* || ani==Npc::Anim::MoveBack*/) && pl.hasState(BS_RUN)) {
        ani = Npc::Anim::Idle;
        }

      if(!pl.hasState(BS_RUN) && ani==Npc::Anim::Idle) {
        // charge-run
        ani = Npc::Anim::NoAnim;
        }
      if((ani==Npc::Anim::MoveL || ani==Npc::Anim::MoveR) &&
          pl.hasState(BS_STAND) && pl.hasState(BS_HIT)) {
        // no charge to strafe transition
        ani = Npc::Anim::NoAnim;
        }
      }

    if(bs==BS_LIE) {
      ani = (ani==Npc::Anim::Move) ? Npc::Anim::Idle : Npc::Anim::NoAnim;
      rot = pl.rotation();
      }

    if(ani!=Npc::Anim::NoAnim)
      pl.setAnim(ani);
    }

  setAnimRotate(pl, rot, ani==Npc::Anim::Idle ? rotation : 0, movement.turnRightLeft.any(), dt);
  if(controllerTarget!=nullptr || actrl[ActGeneric] || ani==Npc::Anim::MoveL || ani==Npc::Anim::MoveR || pl.isFinishingMove()) {
    processAutoRotate(pl,rot,dt);
    }

  if(ani==Npc::Anim::Move && (rotation!=0 || rotY!=0)) {
    assignRunAngle(pl,rot,dt);
    } else {
    assignRunAngle(pl,pl.rotation(),dt);
    }
  pl.setDirection(rot);
  }

void PlayerControl::implSwim(Npc& pl, uint64_t dt) {
  const bool jump=ctrl[Action::Jump];
  if(!jump)
    swimDiveStroke=false;
  if(jump && !swimJumpHeld) {
    swimDiveStroke=pl.isSwim();
    if(swimDiveStroke)
      pl.startDive();
    }
  swimJumpHeld=jump;

  float magnitude=-gamepadLY;
  float yaw=controllerYaw;
  float pitch=pl.isDive() ? swimPitch : 0.f;
  if(jump && pl.isDive()) {
    // A fresh press underwater rises; the surface dive press must be released first.
    pitch=swimDiveStroke ? -40.f : 80.f;
    if(magnitude==0.f)
      yaw=pl.rotation();
    magnitude=1.f;
    }
  if(magnitude>0.f) {
    const float step=controllerTurnSpeed*float(dt)/1000.f;
    const float rotation=std::remainder(yaw-pl.rotation(),360.f);
    pl.setDirection(pl.rotation()+std::clamp(rotation,-step,step));
    const float elevation=pitch-pl.rotationY();
    pl.setDirectionY(pl.rotationY()+std::clamp(elevation,-step,step));
    }
  else if(pl.isSwim()) {
    pl.setDirectionY(0.f);
    }
  pl.setAnimRotate(0);
  pl.setRunAngle(0.f);
  runAngleDest=0.f;
  rotMouse=0.f;
  rotMouseY=0.f;
  pl.setAnim(magnitude>0.f ? Npc::Anim::Move : Npc::Anim::Idle);
  }

void PlayerControl::implMoveMobsi(Npc& pl, uint64_t /*dt*/) {
  // animation handled in MOBSI
  auto inter = pl.interactive();

  if(ctrl[KeyCodec::Back] && !inter->isLadder()) {
    pl.setInteraction(nullptr);
    return;
    }

  if(inter->needToLockpick(pl) && !inter->isCracked()) {
    return;
    }

  if(!inter->isLadder() && inter->isStaticState() && !inter->isDetachState(pl)) {
    auto stateId = inter->stateId();
    if(inter->canQuitAtState(pl,stateId))
      pl.setInteraction(nullptr,false);
    }

  if(inter->isLadder()) {
    if(ctrl[KeyCodec::ActionGeneric]) {
      inter->onKeyInput(KeyCodec::ActionGeneric);
      ctrl[KeyCodec::ActionGeneric] = false;
      }
    else if(wantsToMoveForward()) {
      inter->onKeyInput(KeyCodec::Forward);
      }
    else if(wantsToMoveBackward()) {
      inter->onKeyInput(KeyCodec::Back);
      }
    }
  }

void PlayerControl::processPickLock(Npc& pl, Interactive& inter, KeyCodec::Action k) {
  auto                   w             = Gothic::inst().world();
  auto&                  script        = w->script();
  const size_t           ItKE_lockpick = script.lockPickId();

  char ch = '\0';
  if(k==KeyCodec::Left || k==KeyCodec::RotateL)
    ch = 'L';
  else if(k==KeyCodec::Right || k==KeyCodec::RotateR)
    ch = 'R';
  else if(k==KeyCodec::Back) {
    quitPicklock(pl);
    return;
    }
  else
    return;

  auto cmp = inter.pickLockCode();
  while(pickLockProgress<cmp.size()) {
    auto c = cmp[pickLockProgress];
    if(c=='l' || c=='L' || c=='r' || c=='R')
      break;
    ++pickLockProgress;
    }

  if(pickLockProgress<cmp.size() && std::toupper(cmp[pickLockProgress])!=ch) {
    Feedback::play(Feedback::Effect::Reject);
    pickLockProgress = 0;
    const int32_t dex = Gothic::inst().version().game==2 ? pl.attribute(ATR_DEXTERITY) : (100 - pl.talentValue(TALENT_PICKLOCK));
    if(dex<=int32_t(script.rand(100)))  {
      script.invokePickLock(pl,0,1);
      pl.delItem(ItKE_lockpick,1);
      if(pl.inventory().itemCount(ItKE_lockpick)==0) {
        quitPicklock(pl);
        return;
        }
      } else {
      script.invokePickLock(pl,0,0);
      }
    } else {
    pickLockProgress++;
    if(pickLockProgress>=cmp.size()) {
      Feedback::play(Feedback::Effect::Confirm);
      script.invokePickLock(pl,1,1);
      inter.setAsCracked(true);
      pickLockProgress = 0;
      } else {
      script.invokePickLock(pl,1,0);
      Feedback::play(Feedback::Effect::Navigate);
      }
    }
  }

void PlayerControl::processLadder(Npc& pl, Interactive& inter, KeyCodec::Action key) {
  if(key!=KeyCodec::ActionGeneric && key!=KeyCodec::Forward && key!=KeyCodec::Back)
    return;

  ctrl[key] = true;
  inter.onKeyInput(key);
  }

void PlayerControl::quitPicklock(Npc& pl) {
  inv.close();
  pickLockProgress = 0;
  pl.setInteraction(nullptr);
  }

void PlayerControl::assignRunAngle(Npc& pl, float rotation, uint64_t dt) {
  float dtF    = (float(dt)/1000.f);
  auto  camera = Gothic::inst().camera();

  float dest = 0;
  if(camera!=nullptr && pl.walkMode()==WalkBit::WM_Run && pl.bodyState()==BS_RUN) {
    const float az   = camera->azimuth();
    const float maxV = 14.5f;
    dest = std::min(std::abs(az), maxV)*(az>=0 ? 1 : -1);
    }

  float a = std::min(dtF*5.f, 1.f);
  runAngleDest = runAngleDest*(1.f-a)+dest*a;
  }

void PlayerControl::setAnimRotate(Npc& pl, float rotation, int anim, bool force, uint64_t dt) {
  float dtF    = (float(dt)/1000.f);
  float angle  = pl.rotation();
  float dangle = (rotation-angle)/dtF;
  auto& wrld   = pl.world();

  if(std::fabs(dangle)<30.f && !force) // 30 deg per second threshold
    anim = 0;
  if(anim!=0 && pl.isAttackAnim())
    anim = 0;
  if(rotationAni==anim && anim!=0)
    force = true;
  if(!force && wrld.tickCount()<turnAniSmooth)
    return;
  turnAniSmooth = wrld.tickCount() + 100;
  rotationAni   = anim;
  pl.setAnimRotate(anim);
  }

void PlayerControl::processAutoRotate(Npc& pl, float& rot, uint64_t dt) {
  if(auto other = pl.target()) {
    if(pl.weaponState()==WeaponState::NoWeapon || pl.isFinishingMove()){
      pl.setTarget(nullptr);
      }
    else if(!pl.isAttack()) {
      auto  dp   = other->centerPosition() - pl.centerPosition();
      auto  gl   = pl.guild();
      float step = float(pl.world().script().guildVal().turn_speed[gl]);
      if(actrl[ActGeneric])
        step*=2.f;
      pl.rotateTo(dp.x,dp.z,step,AnimationSolver::TurnType::Std,dt);
      rot = pl.rotation();
      }
    }
  }
