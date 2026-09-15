#include "touchinput.h"

#include <Tempest/Painter>
#include <Tempest/Application>
#include <Tempest/Log>

#include "gothic.h"
#include "resources.h"
#include "utils/gthfont.h"
#include "utils/twofingerswipe.h"
#include "utils/touchmovement.h"

#include <algorithm>
#include <cmath>

using namespace Tempest;

TouchInput::TouchInput(CommandHandler command, WheelHandler wheel, AdjustmentHandler adjust)
  :command(std::move(command)),wheel(std::move(wheel)),adjustment(std::move(adjust)) {
  }

void TouchInput::setMenuAdjustment(bool enabled) {
  if(menuAdjustment==enabled) return;
  menuAdjustment=enabled;
  if(!enabled && adjustmentPointer>=0) {
    touches.erase(adjustmentPointer);
    adjustmentPointer=-1;
    adjustmentDrag.reset();
    }
  update();
  }

void TouchInput::setLockPicking(bool enabled) {
  if(lockPicking==enabled) return;
  lockPicking=enabled;
  update();
  }

void TouchInput::setDebugLeftInset(int inset) {
  if(debugLeftInset==inset)
    return;
  debugLeftInset = inset;
  update();
  }

void TouchInput::setDebugSafeArea(Rect area) {
  if(debugSafeArea==area) return;
  debugSafeArea=area;
  update();
  }

void TouchInput::paintEvent(Tempest::PaintEvent& e) {
  {
  Painter panel(e);
  drawPanelControls(panel);
  }
  if(!debugOverlay)
    return;

  Painter p(e);
  if(blockVisible())
    drawBlock(p);
  const auto gold = Color(0.843f,0.761f,0.631f,0.28f);
  const auto active = Color(1.f,0.85f,0.4f,0.95f);
  const float scale = 1.4f*std::min(Gothic::interfaceScale(this),float(h())/720.f);
  const auto& font = Resources::font(scale);
  const auto& detail = Resources::font(scale*0.92f);
  const int pad = std::max(8,int(12*scale));
  const int line = font.pixelSize();
  const int moveEnd = w()/2;
  const int lookEnd = (w()*LookBoundaryPercent)/100;
  p.setPen(Pen(gold,Painter::Alpha,std::max(1.f,scale)));
  p.setBrush(gold);
  p.drawLine(moveEnd,0,moveEnd,h());
  p.drawLine(lookEnd,0,lookEnd,h());

  auto label = [&](int x,int y,int width,std::string_view text) {
    font.drawTextShadow(p,x,y,width,2*line,text,AlignHCenter);
    };
  const auto safe=debugSafeArea.isEmpty() ? Rect(0,0,w(),h()) : debugSafeArea;
  const int leftX=safe.x+pad;
  const int leftWidth=moveEnd-pad-leftX, rightX=moveEnd+pad, rightWidth=lookEnd-moveEnd-2*pad;
  const int baseline=safe.y+pad+line;
  font.drawTextShadow(p,leftX+debugLeftInset,baseline,std::max(1,leftWidth-debugLeftInset),line,uiActive ? "NAVIGATE" : "MOVE / TURN");
  font.drawTextShadow(p,rightX,baseline,rightWidth,line,uiActive ? (menuAdjustment ? "ADJUST VALUE" : "NAVIGATE") : "LOOK");
  detail.drawTextShadow(p,leftX,baseline+2*line,leftWidth,line,
                        uiActive ? "Touch controls / Menus" : (classicCombat ? "Touch controls / Classic combat" : "Touch controls / Modern combat"));
  if(!uiActive)
    detail.drawTextShadow(p,rightX,baseline+2*line,rightWidth,line,"Drag to move the camera");
  else
    detail.drawTextShadow(p,rightX,baseline+2*line,rightWidth,2*line,
                          lockPicking ? "Left / right: turn pick" : (menuAdjustment ? "Drag left / right" : "Right: accept / Left: back"));
  int legendY=baseline+5*line;
  auto legend=[&](std::string_view text,bool heading=false) {
    const auto& f=heading ? font : detail;
    f.drawTextShadow(p,leftX,legendY,leftWidth,2*line,text);
    legendY+=f.textSize(leftWidth,text).h+std::max(4,int(6.f*scale));
    };
  if(!touchEnabled)
    legend("Gamepad active / Keyboard available");
  else if(lockPicking) {
    legend("LOCKPICKING",true);
    legend("Left / right: turn pick");
    legend("Center between turns");
    legend("Down / Back: stop picking");
    }
  else if(uiActive) {
    legend("NAVIGATION",true);
    legend("Up / down: select");
    legend("Left: back / Right: accept");
    if(saveDeleteEnabled) legend("3-finger tap: delete save");
    }
  else if(gesturesEnabled) {
    legend("GESTURES",true);
    legend("Left side: 2 fingers down / up - sneak / stand");
    legend("Right side: 2 fingers up - first person");
    legend("Right side: 2 fingers down + hold - look behind");
    legend("Anywhere: 2 fingers left / right - health / mana");
    legend("Anywhere: 3-finger tap - save / 4-finger tap - load");
    if(classicCombat) {
      legendY+=line;
      legend("CLASSIC COMBAT",true);
      legend("Hold Action + movement up / sides: attack");
      legend("Hold Action + pull straight down: block");
      }
    }

  const char* names[] = {"Back / Menu","Inventory","Jump","Draw / Sheathe",
                        uiActive ? "Accept" : (classicCombat ? "Action" : "Use / Attack")};
  for(int i=0;i<5;++i) {
    const auto rect = buttonRect(size_t(i));
    bool pressed = false;
    for(const auto& [id,touch]:touches)
      pressed |= touch.role==Role::Button && touch.command==Buttons[i];
    if(pressed) {
      p.setBrush(Color(0.8f,0.6f,0.2f,0.18f));
      p.drawRect(rect);
      }
    p.setBrush(gold);
    p.drawLine(rect.x,rect.y,rect.x+rect.w,rect.y);
    if(i==2) {
      p.drawLine(rect.x,rect.y,rect.x,rect.y+rect.h);
      p.drawLine(rect.x,rect.y+rect.h,rect.x+rect.w,rect.y+rect.h);
      }
    const int textLeft=std::max(rect.x,safe.x)+pad;
    const int textRight=std::min(rect.x+rect.w,safe.x+safe.w)-pad;
    const int textY=std::max(rect.y,safe.y)+(std::min(rect.y+rect.h,safe.y+safe.h)-std::max(rect.y,safe.y))/2;
    label(textLeft,textY,textRight-textLeft,names[i]);
    if(i==4 && canLock && touchEnabled && !uiActive)
      detail.drawTextShadow(p,textLeft,textY+line,textRight-textLeft,2*line,targetLocked ? "Drag: unlock" : "Drag: lock",AlignHCenter);
    if((i==0 || i==1 || i==3) && touchEnabled && !uiActive)
      detail.drawTextShadow(p,textLeft,textY+line,textRight-textLeft,2*line,
                           i==0 ? "Hold: System" : (i==1 ? "Hold: Character" : "Hold: Equipment"),AlignHCenter);
    }

  auto cross = [&](Point pos,int radius) {
    p.drawLine(pos.x-radius,pos.y,pos.x+radius,pos.y);
    p.drawLine(pos.x,pos.y-radius,pos.x,pos.y+radius);
    };
  auto box = [&](Point pos,int radius) {
    p.drawLine(pos.x-radius,pos.y-radius,pos.x+radius,pos.y-radius);
    p.drawLine(pos.x+radius,pos.y-radius,pos.x+radius,pos.y+radius);
    p.drawLine(pos.x+radius,pos.y+radius,pos.x-radius,pos.y+radius);
    p.drawLine(pos.x-radius,pos.y+radius,pos.x-radius,pos.y-radius);
    };
  for(const auto& [id,touch]:touches) {
    p.setPen(Pen(active,Painter::Alpha,2.f));
    p.setBrush(active);
    cross(touch.anchor,pad);
    p.drawLine(touch.anchor,touch.last);
    box(touch.last,pad);
    if(touch.role==Role::Move) {
      const int radius = movementRadius();
      p.setPen(Pen(gold,Painter::Alpha,2.f));
      p.setBrush(gold);
      box(touch.anchor,radius);
      box(touch.anchor,int(float(radius)*DirectionThreshold));
      if(classicCombat && canBlock && !uiActive) {
        const int top=int(float(radius)*0.65f);
        const int topWidth=int(float(top)*0.40f), bottomWidth=int(float(radius)*0.40f);
        p.drawLine(touch.anchor.x-topWidth,touch.anchor.y+top,touch.anchor.x+topWidth,touch.anchor.y+top);
        p.drawLine(touch.anchor.x-topWidth,touch.anchor.y+top,touch.anchor.x-bottomWidth,touch.anchor.y+radius);
        p.drawLine(touch.anchor.x+topWidth,touch.anchor.y+top,touch.anchor.x+bottomWidth,touch.anchor.y+radius);
        p.drawLine(touch.anchor.x-bottomWidth,touch.anchor.y+radius,touch.anchor.x+bottomWidth,touch.anchor.y+radius);
        }
      }
    }
  }

bool TouchInput::panelMenuEnabled() const {
#if defined(__ANDROID__)
  return touchEnabled && uiActive;
#else
  return false;
#endif
  }

Rect TouchInput::panelButtonRect(int index) const {
  const auto safe=debugSafeArea.isEmpty() ? Rect(0,0,w(),h()) : debugSafeArea;
  const int pad=std::max(3,safe.w/160);
  const int height=std::clamp(safe.h/9,36,80);
  const int left=safe.x+(safe.w*index)/PanelNavigation::Count+pad;
  const int right=safe.x+(safe.w*(index+1))/PanelNavigation::Count-pad;
  return Rect(left,safe.y+safe.h-height-pad,right-left,height);
  }

int TouchInput::panelButtonAt(Point pos) const {
  for(int i=0;i<PanelNavigation::Count;++i)
    if(panelButtonRect(i).contains(pos)) return i;
  return -1;
  }

void TouchInput::drawPanelControls(Painter& p) const {
#if defined(__ANDROID__)
  if(!touchEnabled || debugOverlay) return;
  const float scale=std::max(0.6f,std::min(float(w())/640.f,float(h())/480.f));
  const auto& font=Resources::font(scale);
  if(uiActive) {
    const char* labels[]={"UP","DOWN","LEFT","RIGHT","OK","BACK"};
    for(int i=0;i<PanelNavigation::Count;++i) {
      const auto r=panelButtonRect(i);
      p.setBrush(panelNavigation.pressedButton()==i ? Color(0.40f,0.29f,0.12f,0.95f) : Color(0.06f,0.07f,0.09f,0.90f));
      p.drawRect(r);
      font.drawTextShadow(p,r.x,r.y+(r.h+font.pixelSize())/2,r.w,font.pixelSize(),labels[i],AlignHCenter);
      }
    const auto r=panelButtonRect(0);
    font.drawTextShadow(p,0,r.y-6,w(),font.pixelSize(),"Point at these buttons and pull the trigger",AlignHCenter);
    return;
    }
  const char* labels[]={"Menu","Items","Jump","Draw","Use"};
  for(size_t i=0;i<std::size(Buttons);++i) {
    const auto r=buttonRect(i);
    p.setBrush(Color(0.06f,0.07f,0.09f,0.36f));
    p.drawRect(r);
    font.drawTextShadow(p,r.x,r.y+(r.h+font.pixelSize())/2,r.w,font.pixelSize(),labels[i],AlignHCenter);
    }
  font.drawTextShadow(p,0,h()-font.pixelSize(),w()/2,font.pixelSize(),"Hold trigger + drag: move",AlignHCenter);
#else
  (void)p;
#endif
  }

void TouchInput::mouseWheelEvent(MouseEvent& e) {
  e.accept();
  if(!panelMenuEnabled() || e.delta==0 || !panelNavigation.scroll(Application::tickCount())) return;
  const auto action=e.delta>0 ? Command::Up : Command::Down;
  command(action,true);
  command(action,false);
  }

void TouchInput::resizeEvent(Tempest::SizeEvent&) {
  // A moved layout must not apply the selection from a gesture in the old viewport.
  reset();
  }

void TouchInput::mouseDownEvent(Tempest::MouseEvent& e) {
  if(panelMenuEnabled()) {
    const int button=panelButtonAt(e.pos());
    if(button>=0) {
      panelNavigation.press(e.mouseID,button);
      e.accept();
      update();
      return;
      }
    }
  if(!touchEnabled) {
    // Consume gameplay touches instead of forwarding them as desktop mouse input.
    // Android's text editor receives its own input outside this widget.
    e.accept();
    return;
    }

  Touch touch;
  touch.anchor = e.pos();
  touch.last   = e.pos();
  touch.pressedAt = Application::tickCount();

  int button = -1;
  for(size_t i=0;i<std::size(Buttons);++i)
    if(buttonRect(i).contains(e.pos()))
      button = int(i);

  const bool onBlock=blockVisible() && blockRect().contains(e.pos());
  multiTap.down(e.mouseID,float(e.x),float(e.y),touch.pressedAt,
                (saveDeleteEnabled || (gesturesEnabled && !uiActive)) && wheelPointer<0);
  if(tapCaptured || multiTap.ready()) {
    captureTap(e.mouseID,touch);
    return;
    }
  if(wheelPointer>=0 || gestureActive())
    return;
  if(tryGesture(e.mouseID,touch))
    return;

  if(onBlock) {
    if(blockPointer>=0)
      return;
    blockPointer = e.mouseID;
    touch.role = Role::Button;
    touch.command = Command::Block;
    touch.pendingButton = multiTap.joining(touch.pressedAt);
    if(!touch.pendingButton) {
      touch.actionSent = true;
      command(Command::Block,true);
      }
    }
  else if(button>=0) {
    touch.role = Role::Button;
    touch.command = Buttons[button];
    touch.pendingAction = touch.command==Command::Accept && canLock && !uiActive;
    touch.pendingWheel = !uiActive && (touch.command==Command::Weapon || touch.command==Command::Back ||
                                      touch.command==Command::Inventory);
    if(!touch.pendingAction && !touch.pendingWheel) {
      touch.pendingButton = multiTap.joining(touch.pressedAt);
      if(!touch.pendingButton) {
        touch.actionSent = true;
        command(touch.command,true);
        }
      }
    }
  else if(uiActive && menuAdjustment && e.x>=w()/2) {
    if(adjustmentPointer>=0) return;
    touch.role=Role::Adjust;
    adjustmentPointer=e.mouseID;
    adjustmentDrag.reset();
    }
  else if(e.x<w()/2 || uiActive) {
    if(movePointer>=0)
      return;
    touch.role = Role::Move;
    movePointer = e.mouseID;
    }
  else {
    if(lookPointer>=0)
      return;
    touch.role = Role::Look;
    lookPointer = e.mouseID;
    }
  touches[e.mouseID] = touch;
  if(debugOverlay || blockVisible())
    update();
  }

void TouchInput::mouseDragEvent(Tempest::MouseEvent& e) {
  if(panelNavigation.owns(e.mouseID)) { e.accept(); return; }
  multiTap.move(e.mouseID,float(e.x),float(e.y),tapSlop());
  auto it = touches.find(e.mouseID);
  if(it==touches.end())
    return;
  auto& touch = it->second;
  if(touch.role==Role::MultiTap) {
    touch.last=e.pos();
    update();
    return;
    }
  if(touch.role==Role::Gesture) {
    touch.last=e.pos();
    updateGesture();
    update();
    return;
    }
  if(wheelPointer>=0) {
    if(e.mouseID==wheelPointer) {
      moveWheel(touch,e.pos());
      }
    return;
    }
  if(touch.pendingAction) {
    const auto delta = e.pos()-touch.anchor;
    const float distance = std::hypot(float(delta.x),float(delta.y));
    const float threshold = float(std::max(32,std::min(w(),h())/18));
    if(Application::tickCount()-touch.pressedAt>=ActionHoldMs && !multiTap.joining(Application::tickCount())) {
      touch.pendingAction = false;
      touch.actionSent = true;
      command(Command::Accept,true);
      }
    else if(distance>=threshold) {
      touch.pendingAction = false;
      touch.command = Command::LockTarget;
      command(Command::LockTarget,true);
      }
    }
  if(touch.role==Role::Move)
    updateMovement(e.pos());
  else if(touch.role==Role::Adjust) {
    adjustValue(touch,e.pos());
    return;
    }
  else if(touch.role==Role::Look) {
    lookDelta += e.pos()-touch.last;
    }
  touch.last = e.pos();
  if(debugOverlay || blockVisible())
    update();
  }

void TouchInput::mouseUpEvent(Tempest::MouseEvent& e) {
  if(panelNavigation.owns(e.mouseID)) {
    const int button=panelNavigation.release(e.mouseID,panelMenuEnabled() ? panelButtonAt(e.pos()) : -1);
    e.accept();
    if(button>=0) {
      constexpr Command actions[]={Command::Up,Command::Down,Command::Left,Command::Right,Command::Accept,Command::Back};
      const auto action=actions[button];
      Log::i("Panel navigation: ",button);
      command(action,true);
      command(action,false);
      }
    update();
    return;
    }
  multiTap.move(e.mouseID,float(e.x),float(e.y),tapSlop());
  const int tap=multiTap.up(e.mouseID,Application::tickCount());
  if(tapCaptured) {
    touches.erase(e.mouseID);
    if(multiTap.empty()) {
      tapCaptured=false;
      // Decide only after every finger is lifted: four fingers must never save first.
      switch(MultiFingerTap::action(tap,saveDeleteEnabled)) {
        case MultiFingerTap::Action::QuickSave:  command(Command::QuickSave,true); break;
        case MultiFingerTap::Action::QuickLoad:  command(Command::QuickLoad,true); break;
        case MultiFingerTap::Action::DeleteSave: command(Command::DeleteSave,true); break;
        case MultiFingerTap::Action::None: break;
        }
      }
    update();
    return;
    }
  // Recognize long holds even when Android batches the last move and release.
  tick();
  auto it = touches.find(e.mouseID);
  if(it==touches.end())
    return;
  if(it->second.role==Role::Gesture) {
    it->second.last=e.pos();
    updateGesture();
    releaseGesture();
    if(e.mouseID==gestureFirst) gestureFirst=-1;
    if(e.mouseID==gestureSecond) gestureSecond=-1;
    touches.erase(it);
    update();
    return;
    }
  if(e.mouseID==wheelPointer) {
    const auto action=it->second.command;
    moveWheel(it->second,e.pos());
    wheelPointer=-1;
    touches.erase(it);
    wheel(action,WheelPhase::Apply,e.pos());
    update();
    return;
    }
  if(it->second.role==Role::Adjust) {
    adjustValue(it->second,e.pos());
    adjustmentPointer=-1;
    adjustmentDrag.reset();
    touches.erase(e.mouseID);
    update();
    return;
    }
  if(it->second.role==Role::Move) {
    movePointer = -1;
    moveAxis = PointF();
    setDirection(Command::Up,false);
    setDirection(Command::Down,false);
    setDirection(Command::Left,false);
    setDirection(Command::Right,false);
    }
  else if(it->second.role==Role::Look) {
    lookPointer = -1;
    }
  else {
    if(it->second.command==Command::Block)
      blockPointer = -1;
    if(it->second.pendingWheel || it->second.pendingButton) {
      const auto action=it->second.command;
      touches.erase(it);
      command(action,true);
      command(action,false);
      update();
      return;
      }
    if(it->second.pendingAction)
      command(Command::TapAccept,true);
    else if(it->second.actionSent)
      command(it->second.command,false);
    }
  touches.erase(it);
  if(debugOverlay || blockVisible())
    update();
  }

void TouchInput::setTouchEnabled(bool enabled) {
  if(touchEnabled==enabled)
    return;
  touchEnabled = enabled;
  if(!enabled)
    reset();
  update();
  }

void TouchInput::setGesturesEnabled(bool enabled) {
  if(gesturesEnabled==enabled) return;
  gesturesEnabled=enabled;
  if(!enabled && (gestureActive() || tapCaptured)) reset();
  update();
  }

void TouchInput::setSaveDeleteEnabled(bool enabled) {
  if(saveDeleteEnabled==enabled) return;
  // Never reinterpret a tap that started in another menu or in gameplay.
  reset();
  saveDeleteEnabled=enabled;
  update();
  }

void TouchInput::setDebugOverlay(bool enabled) {
  if(debugOverlay==enabled)
    return;
  debugOverlay = enabled;
  update();
  }

void TouchInput::setAnalogMovement(bool enabled) {
  if(analogMovement==enabled)
    return;
  analogMovement = enabled;
  for(size_t i=0;i<4;++i)
    setDirection(Command(i),false);
  if(!enabled && movePointer>=0)
    updateMovement(touches.at(movePointer).last);
  }

void TouchInput::setClassicAction(bool held) {
  if(classicAction==held) return;
  classicAction=held;
  if(movePointer>=0) updateMovement(touches.at(movePointer).last);
  }

void TouchInput::setDebugContext(bool classic, bool ui, bool lockAllowed, bool locked, bool blockAllowed) {
  if(classicCombat==classic && uiActive==ui && canLock==lockAllowed && targetLocked==locked && canBlock==blockAllowed)
    return;
  classicCombat = classic;
  if(uiActive!=ui) panelNavigation.cancel();
  if(uiActive!=ui)
    for(auto& [id,touch]:touches) touch.pendingButton=false;
  uiActive = ui;
  canLock = lockAllowed;
  targetLocked = locked;
  canBlock = blockAllowed;
  if(uiActive && (gestureActive() || tapCaptured)) reset();
  if(uiActive || !canLock) {
    for(auto& [id,touch]:touches)
      touch.pendingAction = false;
    }
  if(uiActive)
    for(auto& [id,touch]:touches)
      touch.pendingWheel=false;
  if(!blockVisible()) {
    for(auto& [id,touch]:touches) {
      if(touch.command==Command::Block && touch.actionSent) {
        touch.actionSent = false;
        command(Command::Block,false);
        }
      }
    blockPointer = -1;
    }
  update();
  }

Rect TouchInput::buttonRect(size_t index) const {
  // Hit testing and the optional debug overlay use exactly the same bounds.
  const int left = w()*LookBoundaryPercent/100;
  const int width = w()-left;
  constexpr int menuHeightPercent = 23;
  const int jumpTop = h()*(2*menuHeightPercent)/100;
  const int attackTop = h()*64/100;
  if(index<2) {
    const int top=h()*int(index)*menuHeightPercent/100;
    const int bottom=h()*int(index+1)*menuHeightPercent/100;
    return Rect(left,top,width,bottom-top);
    }
  if(index==4)
    return Rect(left,attackTop,width,h()-attackTop);
  const int sideWidth=std::min(width,h()*30/100);
  return Rect(index==2 ? left-sideWidth : left,jumpTop,
              index==2 ? sideWidth : width,attackTop-jumpTop);
  }

bool TouchInput::blockVisible() const {
  return touchEnabled && !classicCombat && !uiActive && canBlock;
  }

Rect TouchInput::blockRect() const {
  // Keep Block beside Attack, with a shared top edge and enough room for a thumb.
  const auto attack = buttonRect(4);
  const auto jump = buttonRect(2);
  return Rect(jump.x,attack.y,jump.w,attack.h);
  }

void TouchInput::drawBlock(Painter& p) const {
  const auto rect = blockRect();
  const float scale = 1.4f*std::min(Gothic::interfaceScale(this),float(h())/720.f);
  const bool pressed = blockPointer>=0;
  if(pressed) {
    p.setBrush(Color(0.8f,0.6f,0.2f,0.18f));
    p.drawRect(rect);
    }
  const auto gold = Color(0.843f,0.761f,0.631f,0.28f);
  p.setBrush(gold);
  p.setPen(Pen(gold,Painter::Alpha,std::max(1.f,scale)));
  // Jump and Attack already draw the shared edges.
  p.drawLine(rect.x,rect.y+rect.h,rect.x,rect.y);

  const auto& font = Resources::font(scale);
  font.drawTextShadow(p,rect.x,rect.y+(rect.h+font.pixelSize())/2,rect.w,font.pixelSize(),"Block",AlignHCenter);
  }

void TouchInput::tick() {
  const auto now = Application::tickCount();
  if(tapCaptured) return;
  if(wheelPointer>=0) {
    auto& touch=touches.at(wheelPointer);
    moveWheel(touch,touch.last);
    return;
    }
  for(auto& [id,touch]:touches) {
    // Short button taps fire on release; reserve the joining window for a multi-finger gesture.
    if(multiTap.joining(now)) continue;
    if(touch.pendingWheel && now-touch.pressedAt>=WheelHoldMs) {
      startWheel(id);
      return;
      }
    if(touch.pendingAction && now-touch.pressedAt>=ActionHoldMs) {
      touch.pendingAction = false;
      touch.actionSent = true;
      command(Command::Accept,true);
      }
    if(touch.pendingButton) {
      touch.pendingButton=false;
      touch.actionSent=true;
      command(touch.command,true);
      return;
      }
    }
  }

void TouchInput::startWheel(int pointer) {
  auto touch=touches.at(pointer);
  // Release other held controls before the wheel becomes modal.
  reset();
  touch.pendingWheel=false;
  touch.pendingAction=false;
  touch.actionSent=false;
  // Start from the finger's position when the wheel opens, so hold-time drift stays neutral.
  touch.anchor=touch.last;
  touch.wheelMoved=false;
  touches[pointer]=touch;
  if(wheel(touch.command,WheelPhase::Begin,touch.anchor))
    wheelPointer=pointer;
  update();
  }

void TouchInput::moveWheel(Touch& touch, Point pos) {
  touch.last=pos;
  const auto delta=pos-touch.anchor;
  touch.wheelMoved |= std::hypot(float(delta.x),float(delta.y))>=12.f;
  // Ignore initial finger jitter, but always use the final release position after dragging.
  if(touch.wheelMoved)
    wheel(touch.command,WheelPhase::Move,pos);
  if(debugOverlay)
    update();
  }

void TouchInput::cancelWheel() {
  if(wheelPointer>=0)
    reset();
  }

PointF TouchInput::movementAxis() const {
  return moveAxis;
  }

Point TouchInput::takeLookDelta() {
  auto ret = lookDelta;
  lookDelta = Point();
  return ret;
  }

bool TouchInput::isLooking() const {
  return lookPointer>=0;
  }

void TouchInput::adjustValue(Touch& touch, Point pos) {
  const int delta=pos.x-touch.last.x;
  touch.last=pos;
  const int steps=adjustmentDrag.drag(delta,std::max(8,std::min(w(),h())/40));
  if(steps!=0 && uiActive && menuAdjustment) adjustment(steps);
  if(debugOverlay) update();
  }

void TouchInput::updateMovement(const Point& pos) {
  auto it = touches.find(movePointer);
  if(it==touches.end())
    return;
  const auto  delta  = pos-it->second.anchor;
  const float radius = float(movementRadius());
  moveAxis.x = std::clamp(float(delta.x)/radius,-1.f,1.f);
  moveAxis.y = std::clamp(float(delta.y)/radius,-1.f,1.f);

  if(analogMovement)
    return;
  if(uiActive) {
    const bool vertical=std::abs(moveAxis.y)>=std::abs(moveAxis.x);
    const bool requested[]={vertical && moveAxis.y < -DirectionThreshold,vertical && moveAxis.y > DirectionThreshold,
                            !vertical && moveAxis.x < -DirectionThreshold,!vertical && moveAxis.x > DirectionThreshold};
    for(size_t i=0;i<4;++i) if(!requested[i]) setDirection(Command(i),false);
    for(size_t i=0;i<4;++i) if(requested[i]) setDirection(Command(i),true);
    return;
    }
  if(classicCombat && classicAction && canBlock && !uiActive) {
    using Direction=TouchMovement::Direction;
    const auto direction=TouchMovement::classicDirection(moveAxis.x,moveAxis.y,directions[size_t(Command::Down)]);
    const bool requested[]={direction==Direction::Forward,direction==Direction::Back,
                            direction==Direction::Left,direction==Direction::Right};
    // Release the old chord before pressing the new one: Gothic releases clear combat actions.
    for(size_t i=0;i<4;++i) if(!requested[i]) setDirection(Command(i),false);
    for(size_t i=0;i<4;++i) if(requested[i]) setDirection(Command(i),true);
    return;
    }
  setDirection(Command::Up,   moveAxis.y < -DirectionThreshold);
  setDirection(Command::Down, moveAxis.y >  DirectionThreshold);
  setDirection(Command::Left, moveAxis.x < -DirectionThreshold);
  setDirection(Command::Right,moveAxis.x >  DirectionThreshold);
  }

int TouchInput::movementRadius() const {
  return std::max(80,std::min(w(),h())/6);
  }

void TouchInput::setDirection(Command value, bool pressed) {
  const auto id = size_t(value);
  if(id>=4 || directions[id]==pressed)
    return;
  directions[id] = pressed;
  command(value,pressed);
  }

void TouchInput::reset() {
  panelNavigation.cancel();
  multiTap.reset();
  tapCaptured=false;
  releaseGesture();
  gestureFirst=-1;
  gestureSecond=-1;
  gestureFired=false;
  if(wheelPointer>=0) {
    const auto action=touches.at(wheelPointer).command;
    wheelPointer=-1;
    wheel(action,WheelPhase::Cancel,Point());
    }
  for(auto& touch:touches)
    if(touch.second.role==Role::Button && touch.second.actionSent)
      command(touch.second.command,false);
  for(size_t i=0;i<4;++i)
    setDirection(Command(i),false);
  touches.clear();
  adjustmentPointer=-1;
  adjustmentDrag.reset();
  moveAxis = PointF();
  lookDelta = Point();
  movePointer = -1;
  lookPointer = -1;
  blockPointer = -1;
  analogMovement = false;
  classicAction = false;
  }

bool TouchInput::tryGesture(int pointer, const Touch& second) {
  if(!gesturesEnabled || uiActive || touches.size()!=1) return false;
  auto& [firstId,first]=*touches.begin();
  if(first.role!=Role::Move && first.role!=Role::Look && first.role!=Role::Button) return false;
  const auto travel=first.last-first.anchor;
  const float slop=tapSlop();
  if(!TwoFingerSwipe::canPair(second.pressedAt-first.pressedAt,std::hypot(float(travel.x),float(travel.y)),slop))
    return false;
  gestureFirst=firstId;
  gestureSecond=pointer;
  gestureStarted=second.pressedAt;
  gestureFired=false;
  if(first.role==Role::Button && first.actionSent)
    command(first.command,false);
  first.actionSent=false;
  first.pendingAction=false;
  first.pendingButton=false;
  first.pendingWheel=false;
  blockPointer=-1;
  first.role=Role::Gesture;
  auto touch=second;
  touch.role=Role::Gesture;
  touches[pointer]=touch;
  movePointer=-1;
  lookPointer=-1;
  moveAxis=PointF();
  lookDelta=Point();
  for(size_t i=0;i<4;++i) setDirection(Command(i),false);
  update();
  return true;
  }

void TouchInput::updateGesture() {
  if(gestureFired || gestureFirst<0 || gestureSecond<0) return;
  const auto& first=touches.at(gestureFirst);
  const auto& second=touches.at(gestureSecond);
  const auto a=first.last-first.anchor, b=second.last-second.anchor;
  const float threshold=float(std::max(48,std::min(w(),h())/18));
  const auto elapsed=Application::tickCount()-gestureStarted;
  const int horizontal=TwoFingerSwipe::horizontalDirection(float(a.x),float(a.y),float(b.x),float(b.y),threshold,elapsed);
  if(horizontal!=0) {
    gestureFired=true;
    command(horizontal<0 ? Command::HealthPotion : Command::ManaPotion,true);
    return;
    }
  // Vertical gestures use one side's movement/view meaning; potion swipes may span both sides.
  if((first.anchor.x<w()/2)!=(second.anchor.x<w()/2)) return;
  const int direction=TwoFingerSwipe::direction(float(a.x),float(a.y),float(b.x),float(b.y),threshold,
                                               elapsed);
  if(direction==0) return;
  gestureFired=true;
  if(first.anchor.x>=w()/2) {
    if(direction<0) command(Command::FirstPerson,true);
    else {
      gestureLookBehind=true;
      command(Command::LookBehind,true);
      }
    }
  else command(direction<0 ? Command::SneakOff : Command::SneakOn,true);
  }

void TouchInput::releaseGesture() {
  if(gestureLookBehind) {
    gestureLookBehind=false;
    command(Command::LookBehind,false);
    }
  }

float TouchInput::tapSlop() const {
  return float(std::max(24,std::min(w(),h())/30));
  }

void TouchInput::captureTap(int pointer, const Touch& touch) {
  releaseGesture();
  gestureFirst=-1;
  gestureSecond=-1;
  gestureFired=false;
  tapCaptured=true;
  touches[pointer]=touch;
  for(auto& [id,held]:touches) {
    if(held.role==Role::Button && held.actionSent)
      command(held.command,false);
    held.actionSent=false;
    held.pendingAction=false;
    held.pendingButton=false;
    held.pendingWheel=false;
    held.role=Role::MultiTap;
    }
  blockPointer=-1;
  movePointer=-1;
  lookPointer=-1;
  moveAxis=PointF();
  lookDelta=Point();
  for(size_t i=0;i<4;++i) setDirection(Command(i),false);
  update();
  }
