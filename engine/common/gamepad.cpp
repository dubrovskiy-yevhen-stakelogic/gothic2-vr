#include "mainwindow.h"
#include <Tempest/Log>
#include "vr/questxr.h"
#include "utils/feedback.h"
#include "gothic.h"
#include "utils/cameramath.h"
#include "utils/gthfont.h"
#include "world/objects/npc.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "game/quickslots.h"
#include <Tempest/Application>
#include <cmath>

using namespace Tempest;
using PadAction=GamepadBindings::Action;
using Context=GamepadBindings::Context;
using Phase=GamepadBindings::Phase;

void MainWindow::updateControllerOverlay() {
#if defined(GOTHIC2VR_CONTROLLER)
  const auto context=controllerContext();
  const bool enabled=controllerConnected && Gothic::settingsGetI("DEBUG","gamepadControls")!=0 &&
                     !video.isActive() && Gothic::inst().checkLoading()==Gothic::LoadState::Idle;
  const int key=enabled ? int(context)*2+int(player.isClassicCombat()) : -1;
  if(key==controllerOverlayContext) return;
  controllerOverlayContext=key;
  for(auto& group:controllerOverlayGroups) group.lines.clear();
  if(enabled) {
    constexpr const char* names[]={"Exploration","Classic combat","Modern combat","Bow / magic","Menus","Inventory","Radial wheel","Interaction"};
    controllerOverlayTitle=std::string("CONTROLS / ")+names[size_t(context)];
    const bool navigation=context==Context::UI || context==Context::Inventory || context==Context::EquipmentWheel;
    const char* titles[]={navigation ? "NAVIGATION" : "MOVEMENT","ACTIONS","SHORTCUTS","QUICK SLOTS","SPELL SLOTS"};
    for(size_t i=0;i<controllerOverlayGroups.size();++i) controllerOverlayGroups[i].title=titles[i];
    auto& movement=controllerOverlayGroups[size_t(GamepadBindings::HintGroup::Movement)].lines;
    const auto& options=controllerBindings.options;
    if(context==Context::EquipmentWheel)
      movement={"Either stick: Select","Release held button: Apply"};
    else if(context!=Context::UI && context!=Context::Inventory && context!=Context::Interaction) {
      movement.push_back(std::string(options.swapMovement ? "RS" : "LS")+": Move / turn (light input walks)");
      movement.push_back(std::string(options.swapCamera ? "LS" : "RS")+": Look / change locked target");
      }
    for(const auto& hint:controllerBindings.hints(context)) {
      auto keys=hint.keys;
      auto replace=[&](std::string_view from,std::string_view to) {
        size_t at=0;
        while((at=keys.find(from,at))!=std::string::npos) {
          keys.replace(at,from.size(),to);
          at+=to.size();
          }
        };
      replace("Hold:","Hold ");
      replace("LeftStick","LS ");
      replace("RightStick","RS ");
      replace("Dpad","D-pad ");
      replace("+"," + ");
      auto label=std::string(hint.label);
      if(hint.action>=PadAction::AssignUp && hint.action<=PadAction::AssignRight) {
        keys="Hold "+keys;
        label=label.substr(0,label.find(" ("));
        }
      controllerOverlayGroups[size_t(hint.group)].lines.push_back(keys+": "+label);
      }
    }
  update();
#endif
  }

void MainWindow::paintControllerOverlay(PaintEvent& event,int top) {
#if defined(GOTHIC2VR_CONTROLLER)
  if(controllerOverlayContext<0) return;
  const auto safe=safeArea();
  const int pad=std::max(12,int(16.f*uiScale()));
  // Let long bindings wrap instead of shrinking the whole guide to fit one line.
  const int columnWidth=std::max(1,(safe.w-2*pad)*42/100);
  constexpr int columns[]={0,1,0,1,1};
  float scale=1.4f*std::min(std::max(uiScale(),1.f),float(h())/720.f);
  auto requiredHeight=[&](float s) {
    const auto& font=Resources::font(s*0.92f);
    const int gap=std::max(4,int(6.f*s));
    int heights[2]={};
    for(size_t i=0;i<controllerOverlayGroups.size();++i) {
      const auto& group=controllerOverlayGroups[i];
      if(group.lines.empty()) continue;
      heights[columns[i]]+=Resources::font(s).pixelSize()+3*gap;
      for(const auto& text:group.lines)
        heights[columns[i]]+=font.textSize(columnWidth,text).h+gap;
      }
    return std::max(heights[0],heights[1])+Resources::font(s).pixelSize()+2*gap;
    };
  const int available=std::max(1,safe.h-top-3*pad);
  for(int i=0;i<20 && requiredHeight(scale)>available;++i) scale*=0.95f;
  const auto& heading=Resources::font(scale);
  const auto& font=Resources::font(scale*0.92f);
  const int gap=std::max(4,int(6.f*scale));
  const int baseline=safe.y+top+pad+heading.pixelSize();
  Painter painter(event);
  heading.drawTextShadow(painter,safe.x+pad,baseline,safe.w-2*pad,heading.pixelSize(),controllerOverlayTitle);
  int cursor[2]={baseline+heading.pixelSize()+2*gap,baseline+heading.pixelSize()+2*gap};
  for(size_t i=0;i<controllerOverlayGroups.size();++i) {
    const auto& group=controllerOverlayGroups[i];
    if(group.lines.empty()) continue;
    const int column=columns[i];
    const int x=column==0 ? safe.x+pad : safe.x+safe.w-pad-columnWidth;
    int y=cursor[column];
    heading.drawTextShadow(painter,x,y,columnWidth,heading.pixelSize(),group.title);
    painter.setPen(Pen(Color(0.843f,0.761f,0.631f,0.25f),Painter::Alpha,1.f));
    painter.drawLine(x,y+gap,x+columnWidth,y+gap);
    y+=font.pixelSize()+2*gap;
    for(const auto& text:group.lines) {
      const int height=font.textSize(columnWidth,text).h;
      font.drawTextShadow(painter,x,y,columnWidth,height,text);
      y+=height+gap;
      }
    cursor[column]=y+heading.pixelSize()-font.pixelSize();
    }
#else
  (void)event;
  (void)top;
#endif
  }

void MainWindow::controllerQuickSlot(size_t slot,bool toggleDraw) {
  auto pl=Gothic::inst().player();
  if(pl==nullptr || pl->isDown() || pl->isMonster() || pl->isAiBusy() || pl->interactive()!=nullptr ||
     pl->isSwim() || pl->isDive()) return;
  auto item=QuickSlots::resolve(*pl,slot);
  if(item==nullptr || !item->checkCond(*pl)) return;
  const auto category=QuickSlots::kind(*item);
  if(category==QuickSlots::Kind::Weapons || category==QuickSlots::Kind::Magic) {
    player.controllerEquip(item->clsId(),toggleDraw);
    return;
    }
  // Consumables and documents retain their normal scripted use restrictions.
  if(pl->weaponState()!=WeaponState::NoWeapon || !pl->canSwitchWeapon()) return;
  if(category==QuickSlots::Kind::Potions && !Gothic::settingsGetI("GAME","usePotionKeys")) return;
  pl->useItem(item->clsId());
  }

Context MainWindow::controllerContext() const {
  if(video.isActive() || rootMenu.isActive() || chapter.isActive() || document.isActive() || console.isActive()) return Context::UI;
  if(inventory.isWheelOpen()) return Context::EquipmentWheel;
  if(inventory.isOpen()==InventoryMenu::State::LockPicking) return Context::Interaction;
  if(inventory.isActive()) return Context::Inventory;
  if(dialogs.isActive()) return Context::UI;
  auto pl=Gothic::inst().player();
  if(pl==nullptr) return Context::UI;
  if(pl->interactive()!=nullptr) return Context::Interaction;
  if(pl->isSwim() || pl->isDive()) return Context::Gameplay;
  const auto ws=pl->weaponState();
  if(ws==WeaponState::Bow || ws==WeaponState::CBow || ws==WeaponState::Mage) return Context::Ranged;
  if(ws==WeaponState::NoWeapon) return Context::Gameplay;
#if defined(GOTHIC2VR_CONTROLLER)
  if(player.isClassicCombat() && !controllerExploration) return Context::ClassicMelee;
#endif
  return Context::ModernMelee;
  }

void MainWindow::controllerUiKey(Event::KeyType key,bool repeat,bool touchNavigation) {
  Widget* target=nullptr;
  if(video.isActive()) target=&video;
  else if(rootMenu.isActive()) target=&rootMenu;
  else if(chapter.isActive()) target=&chapter;
  else if(document.isActive()) target=&document;
  else if(console.isActive()) target=&console;
  else if(dialogs.isActive()) target=&dialogs;
  if(target==nullptr) return;
  if(target!=&rootMenu && (key==Event::K_Left || key==Event::K_Right)) {
    if(repeat || !touchNavigation) return;
    key=key==Event::K_Left ? Event::K_ESCAPE : Event::K_Return;
    }
  KeyEvent event(key,Event::M_NoModifier,repeat?Event::KeyRepeat:Event::KeyDown);
  // Dispatch only to the active layer. Ignored UI input must never reach gameplay.
  if(target==&rootMenu) {
    if(key==Event::K_Left || key==Event::K_Right) rootMenu.directionalInput(key==Event::K_Right,repeat,touchNavigation);
    else if(repeat) rootMenu.keyRepeatEvent(event); else rootMenu.keyDownEvent(event);
    }
  else if(target==&video) { if(!repeat) video.keyDownEvent(event); }
  else if(target==&chapter) { if(!repeat) chapter.keyDownEvent(event); }
  else if(target==&document) { if(!repeat) document.keyDownEvent(event); }
  else if(target==&dialogs) dialogs.keyDownEvent(event);
  else if(target==&console && key==Event::K_ESCAPE) console.close();
  // Console text input remains owned by the keyboard/IME.
  }

void MainWindow::controllerAction(const GamepadBindings::Event& event) {
#if defined(GOTHIC2VR_CONTROLLER)
  const auto action=event.action;
  const bool pressed=event.phase==Phase::Press;
  const bool repeat=event.phase==Phase::Repeat;
  const bool released=event.phase==Phase::Release || event.phase==Phase::Cancel;
  const auto context=controllerContext();
  if(released) {
#if defined(GOTHIC2VR_OPENXR)
    if(action==PadAction::Run && (event.phase==Phase::Cancel || vrMenu.settings.runHold))vrRunning=false;
#endif
    const bool cancel=event.phase==Phase::Cancel;
    if(action==PadAction::AttackForward) player.controllerCombat(0,false,cancel);
    if(action==PadAction::AttackLeft) player.controllerCombat(2,false,cancel);
    if(action==PadAction::AttackRight) player.controllerCombat(3,false,cancel);
    if(action==PadAction::Block) player.controllerCombat(1,false,cancel);
    if(action==PadAction::LookBehind) player.releaseControllerKey(KeyCodec::LookBack,cancel);
    if(action==PadAction::Jump) player.releaseControllerKey(KeyCodec::Jump,cancel);
    if(action==PadAction::Up) player.releaseControllerKey(KeyCodec::Forward,cancel);
    if(action==PadAction::Down || action==PadAction::Back) player.releaseControllerKey(KeyCodec::Back,cancel);
    if(action==PadAction::Accept) player.releaseControllerKey(KeyCodec::ActionGeneric,cancel);
    return;
    }
  if(!pressed && !repeat) return;
  if(context==Context::EquipmentWheel) {
    if(action==PadAction::PreviousPage) inventory.wheelPage(-1);
    if(action==PadAction::NextPage) inventory.wheelPage(1);
    if(action==PadAction::Cancel) { inventory.close(); wheelHeldMask=0; player.clearInput(); }
    return;
    }
  if(context==Context::Inventory) { inventory.controllerAction(int(action)); return; }
  if(context==Context::UI) {
    if(rootMenu.isActive() && !video.isActive()) {
      if(action==PadAction::Accept && pressed && rootMenu.overwriteSelectedSave()) return;
      if(action==PadAction::RenameSave && pressed) { rootMenu.renameSelectedSave(); return; }
      }
    if(action==PadAction::AdjustLeft || action==PadAction::AdjustRight) {
      if(rootMenu.isActive() && !video.isActive())
        rootMenu.adjustValue(action==PadAction::AdjustRight ? 1 : -1);
      return;
      }
    if(action==PadAction::DeleteSave && pressed && rootMenu.isActive() && !video.isActive()) {
      auto accept=controllerBindings.hint(PadAction::Accept,context);
      auto back=controllerBindings.hint(PadAction::Back,context);
      accept=accept.substr(0,accept.find(" / "));
      back=back.substr(0,back.find(" / "));
      rootMenu.requestDeleteSave(accept+": Delete    "+back+": Cancel");
      return;
      }
    Event::KeyType key=Event::K_NoKey;
    if(action==PadAction::Accept) key=Event::K_Return;
    if(action==PadAction::Back) key=Event::K_ESCAPE;
    if(action==PadAction::Up) key=Event::K_Up;
    if(action==PadAction::Down) key=Event::K_Down;
    if(action==PadAction::Left) key=Event::K_Left;
    if(action==PadAction::Right) key=Event::K_Right;
    if(key!=Event::K_NoKey) controllerUiKey(key,repeat);
    return;
    }
  auto& gothic=Gothic::inst();
  auto pl=gothic.player();
  auto world=gothic.world();
  auto camera=gothic.camera();
  if(pl==nullptr || world==nullptr || gothic.isPause() || world->isCutsceneLock() || (camera && camera->isCutscene())) return;
  auto key=[&](KeyCodec::Action a) { player.onKeyPressed(a,Event::K_NoKey,KeyCodec::Mapping::Secondary); };
  if(context==Context::Interaction) {
    // Each deliberate press turns the lock once; holding a direction must not consume the combination.
    if(repeat && inventory.isOpen()==InventoryMenu::State::LockPicking) return;
    if(action==PadAction::Up) key(KeyCodec::Forward);
    if(action==PadAction::Down || action==PadAction::Back) key(KeyCodec::Back);
    if(action==PadAction::Left) { key(KeyCodec::Left); player.releaseControllerKey(KeyCodec::Left); }
    if(action==PadAction::Right) { key(KeyCodec::Right); player.releaseControllerKey(KeyCodec::Right); }
    if(action==PadAction::Accept) key(KeyCodec::ActionGeneric);
    return;
    }
  if(repeat) return;
  if(action>=PadAction::QuickUp && action<=PadAction::QuickRight) {
    controllerQuickSlot(size_t(action)-size_t(PadAction::QuickUp));
    return;
    }
  if(action>=PadAction::WheelUp && action<=PadAction::WheelRight) {
    const auto slot=size_t(action)-size_t(PadAction::WheelUp);
    if(QuickSlots::kind(slot)==QuickSlots::Kind::Empty) return;
    player.clearInput();
    inventory.openQuickWheel(*pl,slot);
    if(inventory.isWheelOpen()) {
      wheelQuickSlot=slot;
      wheelHeldMask=event.mask;
      inventory.setWheelPageHint(controllerBindings.hint(PadAction::PreviousPage,Context::EquipmentWheel)+" / "+
                                controllerBindings.hint(PadAction::NextPage,Context::EquipmentWheel)+": pages");
      }
    return;
    }
  switch(action) {
    case PadAction::Interact: player.controllerInteract(controllerExploration); break;
    case PadAction::Back:
      if(pl->interactive()!=nullptr) key(KeyCodec::Back);
      break;
    case PadAction::Jump: key(KeyCodec::Jump); break;
    case PadAction::Walk: key(KeyCodec::Walk); break;
    case PadAction::Run:
#if defined(GOTHIC2VR_OPENXR)
      if(!pl->isDown())player.setSneaking(false);
#endif
      break;
    case PadAction::Sneak:
#if defined(GOTHIC2VR_OPENXR)
      if(!pl->isDown()) pl->setWalkMode(pl->walkMode()^WalkBit::WM_Sneak);
#else
      key(KeyCodec::Sneak);
#endif
      break;
    case PadAction::FirstPerson: key(KeyCodec::FirstPerson); break;
    case PadAction::LookBehind: key(KeyCodec::LookBack); break;
    case PadAction::DrawSheathe: key(KeyCodec::Weapon); break;
    case PadAction::LockTarget: key(KeyCodec::LockTarget); break;
    case PadAction::AttackForward: player.controllerCombat(0,true,false,controllerBindings.options.holdMs); break;
    case PadAction::AttackLeft: player.controllerCombat(2,true); break;
    case PadAction::AttackRight: player.controllerCombat(3,true); break;
    case PadAction::Block: player.controllerCombat(1,true); break;
    case PadAction::Finish: player.controllerCombat(6,true); break;
    case PadAction::HealthPotion: world->script().playerHotLameHeal(*pl); break;
    case PadAction::ManaPotion: world->script().playerHotLamePotion(*pl); break;
    case PadAction::Map: world->script().playerHotKeyScreenMap(*pl); break;
    case PadAction::QuickSave:
      if(gothic.isInGameAndAlive() && Gothic::settingsGetI("GAME","useQuickSaveKeys")) gothic.quickSave();
      break;
    case PadAction::QuickLoad:
      if(Gothic::settingsGetI("GAME","useQuickSaveKeys")) gothic.quickLoad();
      break;
    case PadAction::Inventory: player.clearInput(); inventory.open(*pl); break;
    case PadAction::SystemWheel:
    case PadAction::EquipmentWheel: {
      player.clearInput();
      inventory.openWheel(*pl,action==PadAction::SystemWheel ? InventoryMenu::WheelKind::System : InventoryMenu::WheelKind::Equipment);
      if(inventory.isWheelOpen()) {
        wheelQuickSlot=size_t(-1);
        wheelHeldMask=event.mask;
        const auto& b=controllerBindings;
        if(action==PadAction::EquipmentWheel)
          inventory.setWheelPageHint(b.hint(PadAction::PreviousPage,Context::EquipmentWheel)+" / "+
                                    b.hint(PadAction::NextPage,Context::EquipmentWheel)+": pages");
        }
      break;
      }
    case PadAction::Pause:
    case PadAction::CharacterStats:
    case PadAction::Journal: {
      const auto act=action==PadAction::Pause?KeyCodec::Escape:(action==PadAction::Journal?KeyCodec::Log:KeyCodec::Status);
      rootMenu.setMenu(action==PadAction::Pause?gothic.menuMain():(action==PadAction::Journal?"MENU_LOG":"MENU_STATUS"),act);
      rootMenu.showVersion(action==PadAction::Pause);
      rootMenu.setPlayer(*pl);
      player.clearInput();
      break;
      }
    default: break;
    }
#else
  (void)event;
#endif
  }

void MainWindow::tickGamepad() {
#if defined(GOTHIC2VR_CONTROLLER)
  const auto now=Application::tickCount();
  const auto pollDt=now-controllerLastPoll;
  const auto dt=std::min<uint64_t>(50,pollDt);
  controllerLastPoll=now;
#if defined(GOTHIC2VR_OPENXR)
  auto gp=QuestXr::inst().gamepad();
  const uint32_t runButtons[]={GamepadState::A,GamepadState::B,GamepadState::X,GamepadState::Select,GamepadState::L3,GamepadState::R3};
  bool runDown=false;
  for(size_t i=0;i<vrMenu.settings.mapping.size();++i)
    if(vrMenu.settings.mapping[i]==9 && (gp.buttons&runButtons[i]))runDown=true;
  struct InputGuard {
    Npc* npc; Vr::Running& run; bool& running; bool down,hold,accepted=false,waterInput=false;
    ~InputGuard() {
      if(!accepted) running=run.update(down,false,hold);
      if(npc && !waterInput) npc->setVrSwimInput({},0);
    }
  } inputGuard{Gothic::inst().player(),vrRunButton,vrRunning,runDown,vrMenu.settings.runHold};
  if(auto pl=Gothic::inst().player())pl->setVrLocomotionSpeed(1.f);
  // A device the runtime has just bound: Touch, Index and WMR keep the shipped
  // button map, a wand or the simple-controller floor falls back to the three
  // rows it can actually reach. An edited map is never overwritten, and the
  // fallback is not written to VR.ini, so plugging Touch back in restores it.
  if(const auto generation=QuestXr::inst().profileGeneration(); generation!=vrProfileGeneration) {
    vrProfileGeneration=generation;
    vrMenu.settings.adoptControllerDefaults(QuestXr::inst().reducedButtons());
  }
  const PadAction vrActions[]={PadAction::Count,PadAction::Jump,PadAction::Interact,PadAction::Inventory,PadAction::Journal,PadAction::Sneak,PadAction::LockTarget,PadAction::Walk,PadAction::DrawSheathe,PadAction::Run};
  std::array<PadAction,6> mapped;
  for(size_t i=0;i<mapped.size();++i)mapped[i]=vrActions[size_t(vrMenu.settings.mapping[i])];
  controllerBindings.setVrMapping(mapped);
  if(!gp.connected || !QuestXr::inst().focused())vrRunning=false;
  controllerFocused=QuestXr::inst().focused();
  if(tickVrMenu(gp,now)) {
    vrRunning=false;
    clearInput(); controllerBindings.reset(gp.buttons); controllerButtons=gp.buttons; controllerTriggers=0; controllerAxesBlocked=true;
    vrTurning.update(0,false,vrMenu.settings,0);
#if defined(__MOBILE_PLATFORM__)
    mobileUi.setTouchEnabled(false);
#endif
    return;
  }
  if(Gothic::inst().isInGame() && !rootMenu.isActive() && !dialogs.isActive() && !inventory.isActive() && !video.isActive() && !chapter.isActive() && !document.isActive() && !console.isActive()) {
    gp.buttons&=~(GamepadState::L1|GamepadState::R1);
    if(vrMenu.settings.interaction.physicalCombat) gp.leftTrigger=gp.rightTrigger=0;
  }
#else
  const auto gp=SystemApi::gamepadState();
#endif
  auto& options=controllerBindings.options;
  player.setMeleeAssist(options.meleeAssist,options.meleeAssistMaxAngle,options.meleeAssistMaxDistance);
  if(auto world=Gothic::inst().world()) world->setMeleeFocusRangeScale(options.meleeFocusRangeScale);
  // Track physical presence separately from focus so background disconnections are not lost.
  if(controllerWasPresent && !gp.connected)
    controllerDisconnectPending = Gothic::inst().isInGame() || Gothic::inst().checkLoading()!=Gothic::LoadState::Idle;
  controllerWasPresent = gp.connected && options.enabled;
  const bool connected=gp.connected && options.enabled && controllerFocused;
  Feedback::setGamepad(connected);
// The touch overlay is a mobile control scheme; PCVR has no TouchInput member
// (mainwindow.h), so every mobileUi call below belongs to the Quest alone.
#if defined(__MOBILE_PLATFORM__)
#if defined(GOTHIC2VR_OPENXR)
  mobileUi.setTouchEnabled(false);
#else
  mobileUi.setTouchEnabled(!connected && controllerFocused);
#endif
  if(touchWheelOwned && (!inventory.isWheelOpen() || Gothic::inst().isPause() ||
     Gothic::inst().checkLoading()!=Gothic::LoadState::Idle || rootMenu.isActive() ||
     dialogs.isActive() || video.isActive() || chapter.isActive() || document.isActive() || console.isActive()))
    mobileUi.cancelWheel();
  const auto touchPlayer = Gothic::inst().player();
  const auto touchWeapon = touchPlayer!=nullptr ? touchPlayer->weaponState() : WeaponState::NoWeapon;
  const auto touchCamera=Gothic::inst().camera();
  // Keep quickload gestures available after death; individual gameplay actions still check player state.
  mobileUi.setGesturesEnabled(touchPlayer!=nullptr && touchPlayer->interactive()==nullptr &&
                             touchCamera!=nullptr && !touchCamera->isCutscene() && !Gothic::inst().isPause() &&
                             Gothic::inst().checkLoading()==Gothic::LoadState::Idle);
  mobileUi.setDebugContext(player.isClassicCombat(),
                           video.isActive() || rootMenu.isActive() || chapter.isActive() ||
                           document.isActive() || dialogs.isActive() || (inventory.isActive() && !inventory.isWheelOpen()) || console.isActive(),
                           touchPlayer!=nullptr && touchPlayer->weaponState()!=WeaponState::NoWeapon &&
                           !Gothic::inst().isPause() && Gothic::inst().checkLoading()==Gothic::LoadState::Idle,
                           player.lockedTarget()!=nullptr,
                           (touchWeapon==WeaponState::Fist || touchWeapon==WeaponState::W1H || touchWeapon==WeaponState::W2H) &&
                           !Gothic::inst().isPause() && Gothic::inst().checkLoading()==Gothic::LoadState::Idle);
  mobileUi.setMenuAdjustment(rootMenu.canAdjustValue() && !video.isActive());
  mobileUi.setLockPicking(inventory.isOpen()==InventoryMenu::State::LockPicking);
  mobileUi.setSaveDeleteEnabled(rootMenu.canRequestDeleteSave() && !video.isActive() && !chapter.isActive() &&
                               !document.isActive() && !dialogs.isActive() && !inventory.isActive() && !console.isActive());
  mobileUi.tick();
#endif
  if(!connected && controllerConnected) {
    controllerAxesBlocked=true;
    player.clearInput(); controllerBindings.reset(); controllerButtons=0; controllerTriggers=0;
    if(inventory.isWheelOpen()) inventory.close();
    wheelStickInput.reset();
    wheelHeldMask=0;
    }
  controllerConnected=connected;
#if defined(__MOBILE_PLATFORM__)
  mobileUi.setDebugOverlay(!connected && Gothic::settingsGetI("DEBUG","touchControls")!=0);
#endif
  updateControllerOverlay();
  if(controllerDisconnectPending && controllerFocused && Gothic::inst().checkLoading()==Gothic::LoadState::Idle) {
    controllerDisconnectPending = false;
    if(auto pl = Gothic::inst().player(); pl!=nullptr && !rootMenu.isActive()) {
      rootMenu.setMenu(Gothic::inst().menuMain(),KeyCodec::Escape);
      rootMenu.showVersion(true);
      rootMenu.setPlayer(*pl);
      player.clearInput();
      controllerBindings.reset(controllerButtons);
      controllerAxesBlocked = true;
      }
    }
  auto camera=Gothic::inst().camera();
#if defined(__MOBILE_PLATFORM__)
  if(!connected) {
    const auto touchMove=mobileUi.movementAxis();
    const auto look=mobileUi.takeLookDelta();
    const bool touchUiActive = video.isActive() || rootMenu.isActive() || chapter.isActive() || document.isActive() ||
                               console.isActive() || dialogs.isActive() || inventory.isActive();
    if(touchUiActive || !controllerFocused || Gothic::inst().isPause() || camera==nullptr || camera->isCutscene() ||
       Gothic::inst().checkLoading()!=Gothic::LoadState::Idle) {
      mobileUi.setAnalogMovement(false);
      if(!touchMovementBlocked)
        player.clearInput();
      touchMovementBlocked = true;
      player.setGamepadAxis(0.f,0.f);
      touchLookIdle = 0;
      return;
      }
    if(touchMove==PointF())
      touchMovementBlocked = false;
    if(touchPlayer!=nullptr && touchPlayer->interactive()!=nullptr && touchPlayer->interactive()->isLadder()) {
      mobileUi.setAnalogMovement(true);
      const auto raw=touchMovementBlocked ? PointF() : touchMove;
      const auto axis=controllerBindings.movementAxis(0.f,raw.y,true);
      player.setGamepadAxis(0.f,axis.second);
      return;
      }
    const bool swimming = touchPlayer!=nullptr && (touchPlayer->isSwim() || touchPlayer->isDive());
    const bool locked = !swimming && player.lockedTarget()!=nullptr;
    const bool classicAction = player.isClassicCombat() && player.isPressed(KeyCodec::ActionGeneric);
    const bool targetMovement = locked && !classicAction;
    const bool analogMovement = !classicAction || swimming;
    mobileUi.setClassicAction(classicAction);
    mobileUi.setAnalogMovement(analogMovement);
    if(analogMovement) {
      // Remove keyboard-style turning without clearing target lock or held combat actions.
      if(targetMovement || swimming)
        player.clearMovementInput();
      const auto raw = touchMovementBlocked ? PointF() : touchMove;
      const auto axis = controllerBindings.movementAxis(raw.x,raw.y,true);
      if(swimming)
        player.setControllerSwim(axis.first,axis.second,camera->spin().y,camera->spin().x,options.movementTurnSpeed);
      else if(targetMovement)
        player.setControllerMovement(axis.first,axis.second,camera->spin().y,
                                     controllerBindings.automaticWalk(raw.x,raw.y,true,true),options.movementTurnSpeed);
      else {
        const auto ground=controllerBindings.touchMovementAxis(raw.x,raw.y);
        player.setTurnMovement(ground.first,ground.second,controllerBindings.automaticWalk(0.f,raw.y,false,true),options.touchTurnSpeed);
        }
      }
    else {
      player.setGamepadAxis(0.f,0.f);
      }
    const float dtSec=float(dt)/1000.f;
    float yaw=float(look.x)*300.f/float(std::max(w(),1));
    float pitch=float(look.y)*220.f/float(std::max(h(),1));
    const float sensitivity=Gothic::settingsGetF("GAME","mouseSensitivity")/0.5f;
    yaw*=sensitivity; pitch*=sensitivity;
    if(Gothic::settingsGetI("GAME","camLookaroundInverse")) pitch=-pitch;
    if(mobileUi.isLooking() || look!=Point()) touchLookIdle=0;
    else touchLookIdle+=dt;
    camera->onRotateMouse(PointF(pitch,locked ? 0.f : -yaw));
    const float movement = touchMovementBlocked ? 0.f : std::max(std::abs(touchMove.x),std::abs(touchMove.y));
    const float followSpeed=camera->followSpeed();
    if(auto pl = Gothic::inst().player(); pl!=nullptr && !swimming && !player.isPressed(KeyCodec::LookBack) &&
       !camera->isFirstPerson() && (locked || (touchLookIdle>uint64_t(800.f/followSpeed) && movement>0.35f))) {
      const float follow = CameraMath::followYawDelta(camera->spin().y,pl->rotation(),dtSec*followSpeed,options.cameraSmoothing,
                                                    locked ? 1.f : movement);
      camera->onRotateMouse(PointF(0.f,follow));
      }
    return;
    }
#endif
  auto trigger=[&](float value,uint32_t bit) {
    if(value>=((controllerTriggers&bit)?options.triggerRelease:options.triggerPress)) controllerTriggers|=bit;
    else controllerTriggers&=~bit;
    };
  uint32_t axes=0;
  auto directions=[&](float x,float y,int shift) {
    if(y< -0.55f) axes|=1u<<shift;
    if(y> 0.55f) axes|=1u<<(shift+1);
    if(x< -0.55f) axes|=1u<<(shift+2);
    if(x> 0.55f) axes|=1u<<(shift+3);
    };
  auto dispatch=[&](const auto& sample) {
    trigger(sample.leftTrigger,1u<<14); trigger(sample.rightTrigger,1u<<15);
    axes=0;
    directions(sample.leftStickX,sample.leftStickY,16); directions(sample.rightStickX,sample.rightStickY,20);
    controllerButtons=sample.buttons|controllerTriggers|axes;
    controllerExploration=(controllerButtons&options.explorationModifier)!=0;
    auto context=controllerContext();
    if(gp.overflow || Gothic::inst().checkLoading()!=Gothic::LoadState::Idle) {
      controllerAxesBlocked=true;
      controllerBindings.reset(controllerButtons); player.clearInput(); return;
      }
    if(inventory.isWheelOpen() && (controllerButtons&wheelHeldMask)!=wheelHeldMask) {
      const auto selected=inventory.wheelSelection();
      const auto kind=inventory.currentWheelKind();
      const auto quickSlot=wheelQuickSlot;
      wheelQuickSlot=size_t(-1);
      inventory.close(); wheelHeldMask=0;
      player.clearInput(); controllerBindings.reset(controllerButtons);
      if(selected!=size_t(-1)) {
        if(kind==InventoryMenu::WheelKind::System) applySystemWheelSelection(selected);
        else if(kind==InventoryMenu::WheelKind::Equipment) {
          Feedback::play(Feedback::Effect::Confirm);
          if(quickSlot<4) {
            auto pl=Gothic::inst().player();
            auto item=pl!=nullptr ? pl->getItem(selected) : nullptr;
            if(item!=nullptr && QuickSlots::assign(*pl,quickSlot,*item)) controllerQuickSlot(quickSlot,false);
            }
          else player.controllerEquip(selected);
          }
        }
      return;
      }
    auto events=controllerBindings.update(controllerButtons,context,now);
    for(auto& event:events) {
      controllerAction(event);
      if(controllerContext()!=context) { controllerAxesBlocked=true; break; }
      }
    };
  if(!gp.overflow) for(auto& sample:gp.changes) dispatch(sample);
  dispatch(gp);

  auto deadZone=[&](float x,float y) {
    const float magnitude=std::sqrt(x*x+y*y);
    if(magnitude<=options.deadZone) return PointF();
    const float scale=(std::min(magnitude,1.f)-options.deadZone)/(1.f-options.deadZone)/magnitude;
    return PointF(x*scale,y*scale);
    };
  const auto left=deadZone(gp.leftStickX,gp.leftStickY), right=deadZone(gp.rightStickX,gp.rightStickY);
  if(inventory.isWheelOpen()) {
    const auto stick=wheelStickInput.update(left.x,left.y,right.x,right.y);
    inventory.wheelMove(stick.first,stick.second,now);
    }
  else wheelStickInput.reset();
  const auto context=controllerContext();
#if defined(GOTHIC2VR_OPENXR)
  const bool vrNavigation=context==Context::UI || context==Context::Inventory || context==Context::EquipmentWheel ||
                          context==Context::Interaction || Gothic::inst().isPause() || camera==nullptr || camera->isCutscene();
  const float snap=vrTurning.update(gp.rightStickX,!vrNavigation && QuestXr::inst().focused(),vrMenu.settings,float(dt)/1000.f);
  if(snap!=0 && camera) camera->onRotateMouse(PointF(0,snap));
#endif
  if(context==Context::UI || context==Context::Inventory || context==Context::EquipmentWheel || context==Context::Interaction ||
     Gothic::inst().isPause() || camera==nullptr || camera->isCutscene()) {
    controllerAxesBlocked=true;
    player.setGamepadAxis(0,0);
    // Ladders consume vertical movement even though other interactions suppress analog steering.
    auto pl=Gothic::inst().player();
    if(context==Context::Interaction && !Gothic::inst().isPause() && camera!=nullptr && !camera->isCutscene() &&
       pl!=nullptr && pl->interactive()!=nullptr && pl->interactive()->isLadder()) {
      const float y=options.swapMovement ? gp.rightStickY : gp.leftStickY;
      const auto axis=controllerBindings.movementAxis(0.f,y);
      player.setGamepadAxis(0.f,axis.second);
      }
    return;
    }
  if(controllerAxesBlocked) {
    if(left!=PointF() || right!=PointF()) {
      player.setGamepadAxis(0,0);
      return;
      }
    controllerAxesBlocked=false;
    }
  const PointF raw=options.swapMovement ? PointF(gp.rightStickX,gp.rightStickY) : PointF(gp.leftStickX,gp.leftStickY);
  const auto movement=controllerBindings.movementAxis(raw.x,raw.y);
  const PointF move(movement.first,movement.second);
#if defined(GOTHIC2VR_OPENXR)
  const float yaw=camera->spin().y+(vrMenu.settings.headMovement?QuestXr::inst().headYawDegrees():0.f);
  inputGuard.accepted=connected && !gp.overflow && Gothic::inst().checkLoading()==Gothic::LoadState::Idle;
  vrRunning=vrRunButton.update(runDown,inputGuard.accepted,vrMenu.settings.runHold);
  // Native WM_Walk is unusually slow. Use the normal locomotion animation
  // with a brisk baseline; L3 applies the player's running-speed setting.
  if(auto pl=Gothic::inst().player()) {
    // A save from the previous default can retain WM_Walk after controller state resets.
    if((pl->walkMode()&WalkBit::WM_Walk)!=WalkBit::WM_Run)
      pl->setWalkMode(WalkBit(uint8_t(pl->walkMode()) & ~uint8_t(WalkBit::WM_Walk)));
    pl->setVrLocomotionSpeed(vrRunning?vrMenu.settings.runSpeed:.72f);
    if(int(vrRunning)!=vrRunningLogged) {
      // Diagnostic for the "L3 run does nothing" report: the raw stick clicks and the mapping.
      Tempest::Log::i("VR run ",vrRunning?"on":"off"," L3=",(gp.buttons&GamepadState::L3)!=0," R3=",(gp.buttons&GamepadState::R3)!=0,
                      " map4=",vrMenu.settings.mapping[4]," map5=",vrMenu.settings.mapping[5]," walkMode=",int(pl->walkMode()));
      vrRunningLogged=int(vrRunning);
      }
  }
  if(auto pl=Gothic::inst().player(); pl && inputGuard.accepted && !pl->isDown()) {
    auto& xr=QuestXr::inst();
    const auto base=camera->vrBaseView(pl->position()+Vec3(0,vrEyeHeight,0),camera->spin().y);
    pl->setVrSwimInput(xr.swimInput(base,pl->position().y,vrEyeHeight),float(pollDt)/1000.f);
    inputGuard.waterInput=true;
    if(pl->isSwim() || pl->isDive()) {
      // Physical strokes own travel; keep the native swim animation/climb controller.
      player.setControllerSwim(0,0,camera->spin().y+xr.headYawDegrees(),0,options.movementTurnSpeed);
      return;
    }
  }
  player.setControllerMovement(move.x,move.y,yaw,false,options.movementTurnSpeed);
  return;
#endif
  auto look=options.swapCamera?left:right;
  const float magnitude=std::max(std::abs(raw.x),std::abs(raw.y));
  auto pl=Gothic::inst().player();
  const bool swimming=pl!=nullptr && (pl->isSwim() || pl->isDive());
  const bool lockedGround=player.lockedTarget()!=nullptr && !swimming;
  if(swimming)
    player.setControllerSwim(move.x,move.y,camera->spin().y,camera->spin().x,options.movementTurnSpeed);
  else if(!lockedGround) {
    const auto ground=controllerBindings.turnMovementAxis(raw.x,raw.y);
    const float travel=ground.second!=0.f ? std::hypot(raw.x,raw.y) : 0.f;
    const bool automaticWalk=controllerBindings.automaticWalk(0.f,travel,false);
    player.setTurnMovement(ground.first,ground.second,automaticWalk,options.movementTurnSpeed);
    }
  else
    player.setControllerMovement(move.x,move.y,camera->spin().y,
                                 controllerBindings.automaticWalk(raw.x,raw.y,true),options.movementTurnSpeed);
  const float dtSec=float(dt)/1000.f;
  const float sensitivity=Gothic::settingsGetF("GAME","mouseSensitivity")/0.5f;
  const float inverse=Gothic::settingsGetI("GAME","camLookaroundInverse")?-1.f:1.f;
  const bool locked=player.lockedTarget()!=nullptr;
  // Lock owns horizontal tracking, but vertical look remains under player control.
  camera->onRotateMouse(PointF(look.y*140.f*dtSec*sensitivity*inverse,locked?0.f:-look.x*180.f*dtSec*sensitivity));
  if(locked) {
    if(std::abs(look.x)<options.switchReset) controllerSwitchReady=true;
    if(controllerSwitchReady && std::abs(look.x)>options.switchThreshold && now-controllerLastSwitch>=options.switchCooldownMs) {
      player.switchControllerTarget(look.x>0); controllerSwitchReady=false; controllerLastSwitch=now;
      }
    }
  if(look!=PointF()) controllerLookIdle=0; else controllerLookIdle+=dt;
  const float followSpeed=camera->followSpeed();
  if(pl && !swimming && !player.isPressed(KeyCodec::LookBack) && !camera->isFirstPerson() &&
     (player.lockedTarget()!=nullptr || (options.cameraAssist && magnitude>0.35f &&
                                       controllerLookIdle>uint64_t(800.f/followSpeed)))) {
    // Gentle movement should produce gentle camera assistance, without disabling stationary lock tracking.
    const float strength=player.lockedTarget()!=nullptr?1.f:magnitude;
    const float yaw=CameraMath::followYawDelta(camera->spin().y,pl->rotation(),dtSec*followSpeed,options.cameraSmoothing,strength);
    camera->onRotateMouse(PointF(0,yaw));
    }
#endif
  }
