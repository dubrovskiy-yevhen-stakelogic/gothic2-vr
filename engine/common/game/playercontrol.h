#pragma once

#include "utils/bufferedshot.h"

#include "world/focus.h"
#include "utils/keycodec.h"
#include "utils/attacktap.h"
#include "constants.h"

#include <array>

class DialogMenu;
class InventoryMenu;
class DbgPainter;
class World;
class Interactive;
class Npc;
class Item;
class Camera;
class Gothic;

class PlayerControl final {
  public:
    PlayerControl(DialogMenu& dlg, InventoryMenu& inv);
    ~PlayerControl();

    void  onKeyPressed (KeyCodec::Action a, Tempest::Event::KeyType key, KeyCodec::Mapping mapping);
    void  onKeyReleased(KeyCodec::Action a, KeyCodec::Mapping mapping);
    bool  isPressed(KeyCodec::Action a) const;
    void  setGamepadAxis(float lx, float ly);
    void  setTurnMovement(float turn, float forward, bool walk, float turnSpeed);
    void  setControllerMovement(float x, float y, float cameraYaw, bool walk, float turnSpeed, float turnBoost=0.f);
    void  setControllerSwim(float x, float y, float cameraYaw, float cameraPitch, float turnSpeed);
    void  controllerCombat(int direction, bool pressed, bool cancel=false, uint64_t holdMs=400);
    void  setMeleeAssist(bool enabled, float maxAngle, float maxDistance);
    void  releaseControllerKey(KeyCodec::Action action, bool cancel=false);
    void  controllerInteract(bool sheath);
    void  controllerEquip(size_t item, bool toggleDraw=false);
    void  toggleTargetLock();
    void  switchControllerTarget(bool right);
    Npc*  lockedTarget() const { return controllerTarget; }
    bool  isControllerMoving() const { return controllerDirectional && gamepadLY!=0.f; }
    bool  isClassicCombat() const { return !g2Ctrl; }
    void  setSneaking(bool enabled);
    void  onRotateMouse(float dAngleX, float dAngleY);

    void  drawVobRay(DbgPainter& p) const;

    void  changeZoom(int delta);
    void  tickFocus();
    void  clearFocus();

    bool  interact(Interactive& it);
    bool  interact(Npc&         other);
    bool  interact(Item&        item);

    void  clearInput();
    void  clearMovementInput();

    void  setTarget(Npc* other);
    void  actionFocus(Npc& other);
    void  emptyFocus();

    Focus focus() const;
    bool  hasActionFocus() const;

    bool  tickMove(uint64_t dt);
    bool  tickCameraMove(uint64_t dt);

  private:
    enum WeaponAction : uint8_t {
      WeaponClose,
      WeaponMele,
      WeaponBow,
      Weapon3,
      Weapon4,
      Weapon5,
      Weapon6,
      Weapon7,
      Weapon8,
      Weapon9,
      Weapon10,

      Last,
      };

    enum FocusAction : uint8_t {
      ActForward=0,
      ActBack   =1,
      ActLeft   =2,
      ActRight  =3,
      ActGeneric=4,
      ActMove   =5,
      ActKill   =6,
      };

    using Action=KeyCodec::Action;

    struct AxisStatus { 
        /// Main direction (e.g. W or Up arrow)
        std::array<bool, KeyCodec::NumMappings> main;
        
        /// Reverse direction (e.g. S or Down arrow)
        std::array<bool, KeyCodec::NumMappings> reverse;

        /// Current axis value (scale from -1 to 1)
        auto value() const -> float {
          return
              (this->anyMain() ? 1.f : 0.f)
            + (this->anyReverse() ? -1.f : 0.f);
          }

        /// True if only one of directions is active
        /// (e.g. false if none or both directions are active).
        auto any() const -> bool {
          return this->value() != 0;
          }

        void reset() {
          this->main.fill(false);
          this->reverse.fill(false);
          }

      private:
        /// Is any key pressed that activates the main direction
        /// (e.g. W or Up Arrow in Forward-Backward axis)
        auto anyMain() const -> bool {
          for(auto elem : main) {
            if(elem) return true;
            }
          return false;
          }
        
        /// Is any key pressed that activates the reverse direction
        /// (e.g. S or Down arrow in Forward-Backward axis)
        auto anyReverse() const -> bool {
          for(auto elem : reverse) {
            if(elem) return true;
            }
          return false;
          }
      };

    struct MovementStatus {
      AxisStatus forwardBackward;  
      AxisStatus strafeRightLeft;
      AxisStatus turnRightLeft;

      /// Resets all axes to their default state.
      void reset() {
        this->forwardBackward.reset();
        this->strafeRightLeft.reset();
        this->turnRightLeft.reset();
        }
      } movement;
    
    bool           ctrl[Action::Last]={};
    std::array<bool,Action::Last> controllerKeyReleases={};
    bool           wctrl[WeaponAction::Last]={};
    bool           actrl[7]={};

    WeaponAction   wctrlLast = WeaponAction::WeaponMele; //!< Reminder for weapon toggle.

    bool           cacheFocus=false;
    Focus          currentFocus;
    float          rotMouse=0;
    float          rotMouseY=0;
    float          gamepadLX=0;
    float          gamepadLY=0;
    float          touchTurn=0;
    bool           touchAnalogMovement=false;
    bool           controllerGroundStrafe=false;
    bool           controllerDirectional=false;
    bool           controllerReleaseAttack=false;
    bool           controllerBlockPending=false;
    AttackTap      controllerEmptyAttack;
    BufferedShot   controllerBowShot;
    size_t         controllerBowWeapon=size_t(-1);
    bool           controllerWalkApplied=false;
    bool           controllerSwimming=false;
    bool           swimJumpHeld=false;
    bool           swimDiveStroke=false;
    float          swimPitch=0;
    Npc*           controllerFinisher=nullptr;
    uint64_t       controllerFinishTime=0;
    float          controllerYaw=0;
    float          controllerTurnSpeed=180.f;
    float          controllerTurnBoost=0.f;
    Npc*           controllerTarget=nullptr;
    bool           meleeAssist=false;
    float          meleeAssistMaxAngle=90.f;
    float          meleeAssistMaxDistance=300.f;
    Focus          pendingInteraction;
    uint64_t       pendingInteractionUntil=0;
    size_t         pendingEquipment=size_t(-1);
    bool           casting = false;
    size_t         pickLockProgress = 0;

    float          runAngleDest   = 0.f;
    uint64_t       turnAniSmooth  = 0;
    int            rotationAni    = 0;
    bool           g2Ctrl         = false;

    DialogMenu&    dlg;
    InventoryMenu& inv;

    void           setupSettings();
    void           applyControllerWalk(bool walk);
    bool           canInteract() const;
    void           marvinF8(uint64_t dt);
    void           marvinK(uint64_t dt);
    void           marvinO();
    void           toggleWalkMode();
    void           toggleSneakMode();
    void           moveFocus(FocusAction act);
    Focus          findFocus(const Focus* prev) const;

    void           clrDraw();
    void           implMove(uint64_t dt);
    void           implSwim(Npc& pl, uint64_t dt);
    void           implMoveMobsi(Npc& pl, uint64_t dt);
    void           processPickLock(Npc& pl, Interactive& inter, KeyCodec::Action key);
    void           processLadder(Npc& pl, Interactive& inter, KeyCodec::Action key);
    void           quitPicklock(Npc& pl);
    void           setPos(std::array<float,3> a, uint64_t dt, float speed);
    void           assignRunAngle(Npc& pl, float rotation, uint64_t dt);
    void           setAnimRotate (Npc& pl, float rotation, int anim, bool force, uint64_t dt);
    void           processAutoRotate(Npc& pl, float& rot, uint64_t dt);
    void           assistMeleeAttack(Npc& pl);


    //////////////////////////////////
    // Helper functions for movement
    //////////////////////////////////

    auto wantsToMoveForward() const -> bool {
      return movement.forwardBackward.value() > 0.f || gamepadLY < -((controllerDirectional || touchAnalogMovement) ? 0.f : 0.2f);
      }
    auto wantsToMoveBackward() const -> bool {
      return movement.forwardBackward.value() < 0.f || gamepadLY > ((controllerDirectional || touchAnalogMovement) ? 0.f : 0.2f);
      }

    auto wantsToStrafeRight() const -> bool {
      return movement.strafeRightLeft.value() > 0.f || gamepadLX > (controllerGroundStrafe ? 0.f : 0.2f);
      }
    auto wantsToStrafeLeft() const -> bool {
      return movement.strafeRightLeft.value() < 0.f || gamepadLX < -(controllerGroundStrafe ? 0.f : 0.2f);
      }

    auto wantsToTurnRight() const -> bool {
      return movement.turnRightLeft.value() > 0.f;
      }
    auto wantsToTurnLeft() const -> bool {
      return movement.turnRightLeft.value() < 0.f;
      }

    /// @brief Analyses the input action mapping and updates the movement status accordingly.
    ///        Meant to be used when key is pressed or released.
    /// @param actionMapping - the pressed/released action
    /// @param pressed - true if the key was pressed, false if it was released
    auto handleMovementAction(KeyCodec::ActionMapping actionMapping, bool pressed) -> void;
  };
