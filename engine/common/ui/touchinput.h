#pragma once

#include <Tempest/Widget>

#include <functional>
#include <unordered_map>
#include "utils/multifingertap.h"
#include "utils/touchadjustment.h"
#include "utils/panelnavigation.h"

namespace Tempest { class Painter; }

class TouchInput : public Tempest::Widget {
  public:
    enum class Command : uint8_t {
      Up,
      Down,
      Left,
      Right,
      Accept,
      Back,
      Jump,
      Weapon,
      Inventory,
      LockTarget,
      TapAccept,
      Block,
      SneakOn,
      SneakOff,
      FirstPerson,
      LookBehind,
      QuickSave,
      QuickLoad,
      DeleteSave,
      HealthPotion,
      ManaPotion,
      };

    using CommandHandler = std::function<void(Command,bool)>;
    enum class WheelPhase : uint8_t { Begin, Move, Apply, Cancel };
    using WheelHandler = std::function<bool(Command,WheelPhase,Tempest::Point)>;
    using AdjustmentHandler = std::function<void(int)>;

    explicit TouchInput(CommandHandler command, WheelHandler wheel, AdjustmentHandler adjust);

    void            paintEvent(Tempest::PaintEvent& e) override;
    void            resizeEvent(Tempest::SizeEvent& e) override;
    void            mouseDownEvent(Tempest::MouseEvent& e) override;
    void            mouseDragEvent(Tempest::MouseEvent& e) override;
    void            mouseUpEvent(Tempest::MouseEvent& e) override;
    void            mouseWheelEvent(Tempest::MouseEvent& e) override;

    void            setTouchEnabled(bool enabled);
    void            setGesturesEnabled(bool enabled);
    void            setSaveDeleteEnabled(bool enabled);
    void            setAnalogMovement(bool enabled);
    void            setClassicAction(bool held);
    void            setDebugOverlay(bool enabled);
    void            setDebugLeftInset(int inset);
    void            setDebugSafeArea(Tempest::Rect area);
    void            setMenuAdjustment(bool enabled);
    void            setLockPicking(bool enabled);
    void            setDebugContext(bool classicCombat, bool uiActive, bool canLock, bool locked, bool canBlock);
    void            tick();
    Tempest::PointF movementAxis() const;
    Tempest::Point  takeLookDelta();
    bool            isLooking() const;
    void            cancelWheel();

  private:
    int debugLeftInset = 0;
    Tempest::Rect debugSafeArea;

    enum class Role : uint8_t {
      Move,
      Look,
      Adjust,
      Button,
      Gesture,
      MultiTap,
      };

    struct Touch {
      Role           role = Role::Look;
      Tempest::Point anchor;
      Tempest::Point last;
      Command        command = Command::Accept;
      uint64_t       pressedAt = 0;
      bool           pendingAction = false;
      bool           pendingButton = false;
      bool           actionSent = false;
      bool           pendingWheel = false;
      bool           wheelMoved = false;
      };

    void updateMovement(const Tempest::Point& pos);
    void setDirection(Command command, bool pressed);
    void reset();
    void startWheel(int pointer);
    void moveWheel(Touch& touch, Tempest::Point pos);
    void adjustValue(Touch& touch, Tempest::Point pos);
    bool tryGesture(int pointer, const Touch& touch);
    void updateGesture();
    bool gestureActive() const { return gestureFirst>=0 || gestureSecond>=0; }
    void releaseGesture();
    void captureTap(int pointer, const Touch& touch);
    float tapSlop() const;
    int  movementRadius() const;
    Tempest::Rect buttonRect(size_t index) const;
    bool blockVisible() const;
    Tempest::Rect blockRect() const;
    void drawBlock(Tempest::Painter& p) const;
    void drawPanelControls(Tempest::Painter& p) const;
    Tempest::Rect panelButtonRect(int index) const;
    int panelButtonAt(Tempest::Point pos) const;
    bool panelMenuEnabled() const;
    PanelNavigation panelNavigation;

    static constexpr int LookBoundaryPercent = 84;
    static constexpr Command Buttons[] = {Command::Back,Command::Inventory,Command::Jump,Command::Weapon,Command::Accept};
    static constexpr float DirectionThreshold = 0.35f;
    static constexpr uint64_t ActionHoldMs = 180;
    static constexpr uint64_t WheelHoldMs = 400;

    CommandHandler command;
    WheelHandler wheel;
    AdjustmentHandler adjustment;
    TouchAdjustment adjustmentDrag;
    int             adjustmentPointer = -1;
    bool            menuAdjustment = false;
    std::unordered_map<int,Touch> touches;
    Tempest::PointF moveAxis;
    Tempest::Point  lookDelta;
    int             movePointer = -1;
    int             lookPointer = -1;
    int             blockPointer = -1;
    int             wheelPointer = -1;
    int             gestureFirst = -1;
    int             gestureSecond = -1;
    uint64_t        gestureStarted = 0;
    bool            gestureFired = false;
    bool            gestureLookBehind = false;
    bool            gesturesEnabled = false;
    MultiFingerTap  multiTap;
    bool            tapCaptured = false;
    bool            saveDeleteEnabled = false;
    bool            touchEnabled = true;
    bool            analogMovement = false;
    bool            debugOverlay = false;
    bool            classicCombat = true;
    bool            classicAction = false;
    bool            uiActive = false;
    bool            lockPicking = false;
    bool            canLock = false;
    bool            targetLocked = false;
    bool            canBlock = false;
    bool            directions[4] = {};
  };

