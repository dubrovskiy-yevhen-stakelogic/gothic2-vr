#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

// Game-specific bindings are independent of the platform input backend.
class GamepadBindings final {
  public:
    enum class Context { Gameplay, ClassicMelee, ModernMelee, Ranged, UI, Inventory, EquipmentWheel, Interaction };
    enum class Action {
      Interact, Back, Jump, Journal, Inventory, Pause, Sneak, Walk, LockTarget,
      DrawSheathe, EquipmentWheel, Map, HealthPotion, ManaPotion, CharacterStats,
      FirstPerson, LookBehind, QuickSave, QuickLoad, AttackForward, AttackLeft,
      AttackRight, Block, Finish, Accept, Up, Down, Left, Right,
      PreviousPage, NextPage, Cancel, LeftPanel, RightPanel, TakeStack, Drop,
      Spell3, Spell4, Spell5, Spell6, Spell7, Spell8, Spell9, Spell10,
      DeleteSave, AdjustLeft, AdjustRight, SystemWheel,
      QuickUp, QuickDown, QuickLeft, QuickRight,
      WheelUp, WheelDown, WheelLeft, WheelRight,
      AssignUp, AssignDown, AssignLeft, AssignRight,
      RenameSave, Run,
      Count
      };
    enum class Phase { Press, Release, Repeat, Cancel };
    struct Event { Action action; Phase phase; uint32_t mask; };
    struct Options {
      bool enabled = true;
      uint32_t explorationModifier = 1u<<4;
      float deadZone = 0.20f;
      float movementDeadZone = 0.28f;
      float touchMovementDeadZone = 0.15f;
      float movementExponent = 1.5f;
      float movementTurnSpeed = 180.f;
      float touchTurnSpeed = 180.f;
      float walkThreshold = 0.60f;
      float touchWalkThreshold = 0.35f;
      float walkHysteresis = 0.04f;
      float triggerPress = 0.55f;
      float triggerRelease = 0.40f;
      bool swapMovement = false;
      bool swapCamera = false;
      uint64_t holdMs = 400;
      uint64_t repeatDelayMs = 350;
      uint64_t repeatMs = 150;
      float switchThreshold = 0.65f;
      float switchReset = 0.25f;
      uint64_t switchCooldownMs = 250;
      float cameraSmoothing = 0.20f;
      bool cameraAssist = true;
      bool meleeAssist = true;
      float meleeAssistMaxAngle = 90.f;
      float meleeAssistMaxDistance = 300.f;
      float meleeFocusRangeScale = 0.f;
      } options;

    GamepadBindings();
    void setVrMapping(const std::array<Action,6>& actions);
    std::vector<std::string> load(std::istream& input);
    static std::string defaults();
    static uint32_t button(std::string_view name);
    std::string hint(Action action, Context context) const;
    enum class HintGroup { Movement, Actions, Shortcuts, QuickSlots, Spells };
    struct Hint { Action action; std::string keys; std::string_view label; HintGroup group; };
    std::vector<Hint> hints(Context context) const;
    std::vector<Event> update(uint32_t buttons, Context context, uint64_t now);
    void reset(uint32_t held = 0);
    std::pair<float,float> movementAxis(float x, float y, bool touch=false) const;
    std::pair<float,float> touchMovementAxis(float x, float y) const;
    std::pair<float,float> turnMovementAxis(float x, float y, bool touch=false) const;
    static std::pair<float,float> targetMovementAxis(float x, float y);
    bool automaticWalk(float rawX, float rawY, bool targetRelative, bool touch=false);

  private:
    struct Binding {
      Action action = Action::Count;
      uint32_t mask = 0;
      uint32_t trigger = 0;
      bool hold = false;
      std::string text;
      };
    struct Section { std::string name; std::vector<Binding> bindings; };
    struct Press {
      Binding binding;
      Binding hold;
      uint64_t started = 0;
      uint64_t repeat = 0;
      bool active = false;
      bool pending = false;
      };
    std::vector<Binding> bindings(Context context) const;
    std::vector<Section> sections;
    std::array<Press,24> presses;
    uint32_t previous = 0;
    uint32_t blocked = 0;
    Context lastContext = Context::Gameplay;
    bool initialized = false;
    bool vrMapping=false;
    std::array<Action,6> vrActions{};
    bool automaticWalking = true;
  };
