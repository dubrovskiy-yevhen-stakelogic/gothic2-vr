"""Exercise production camera and movement callers across VR and flat builds."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)

def method(path, signature):
    source = (root / path).read_text(encoding='utf-8')
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

fixture = r'''
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include "utils/movementresponse.h"
#include "utils/swiminput.h"
struct Vec3 {float x=0,y=0,z=0;};
enum class WalkBit : uint8_t {WM_Walk=1};
enum class WeaponState {NoWeapon,Fist,W1H,W2H};
struct Camera {
  enum Mode {Normal}; Vec3 angle; bool cutscene=false,free=false;
  Vec3 spin() const{return angle;} void setSpin(Vec3 v){angle=v;}
  bool isFree() const{return free;} bool isCutscene() const{return cutscene;}
  bool isFirstPerson() const{return true;} bool isMarvin() const{return false;}
  bool isToggleEnabled() const{return false;} void setMode(Mode){}
  void setTarget(Vec3){} void tick(uint64_t){}
};
struct Npc {
  float yaw=0; bool down=false; void* mob=nullptr; WeaponState weapon=WeaponState::NoWeapon;
  float rotation()const{return yaw;} float rotationY()const{return 0;}
  bool isDown()const{return down;} bool isDive()const{return false;} bool isSwim()const{return false;}
  void* interactive()const{return mob;} WeaponState weaponState()const{return weapon;}
  Vec3 cameraBone(bool)const{return {};}
  WalkBit walkMode()const{return WalkBit(0);} void setWalkMode(WalkBit){}
};
struct Gothic {
  Camera cam; Npc npc; static Gothic& inst(){static Gothic g;return g;}
  Camera* camera(){return &cam;} Npc* player(){return &npc;}
};
namespace GamepadBindings {std::pair<float,float> targetMovementAxis(float x,float y){return {x,y};}}
struct PlayerControl {
  float touchTurn=0,controllerTurnSpeed=0,controllerTurnBoost=0,gamepadLX=0,gamepadLY=0,controllerYaw=0;
  bool touchAnalogMovement=false,controllerSwimming=false,swimJumpHeld=false,swimDiveStroke=false;
  bool controllerDirectional=false,controllerGroundStrafe=false; Npc* controllerTarget=nullptr;
  bool controllerWalkApplied=false; float swimPitch=0;
  struct Focus {Npc* npc=nullptr;} target; Focus& focus(){return target;}
  void applyControllerWalk(bool){}
  void setControllerMovement(float,float,float,bool,float,float);
  void setControllerSwim(float,float,float,float,float);
};
namespace Event {enum {ButtonLeft};}
namespace SystemApi {bool isFullscreen(int){return true;}}
struct MainWindow {
  PlayerControl player; bool mouseP[1]={false};
  struct Dialogs {bool active=false;bool isActive(){return active;}bool isMobsiDialog(){return false;}
    void dialogCamera(Camera& c){c.angle.y=123;}} dialogs;
  struct Inventory {bool isActive(){return false;}} inventory;
  int hwnd(){return 0;} Camera::Mode solveCameraMode(){return Camera::Normal;}
  void tickCamera(uint64_t);
};
int checks=0;
void check(bool ok,const char* label){++checks;if(!ok){std::printf("FAIL %s\n",label);std::exit(1);}}
'''
fixture += method('engine/common/mainwindow.cpp', 'void MainWindow::tickCamera(uint64_t dt)')
fixture += '\n' + method('engine/common/game/playercontrol.cpp', 'void PlayerControl::setControllerMovement(')
fixture += '\n' + method('engine/common/game/playercontrol.cpp', 'void PlayerControl::setControllerSwim(')
fixture += r'''
int main(){
  auto& g=Gothic::inst(); MainWindow w;
  for(float heading : {0.f,45.f,-90.f,179.f}) for(float zeroX : {0.f,-0.f}) for(float zeroY : {0.f,-0.f}) {
    // Physical swimming passes neutral axes; hand strokes own translation.
    w.player.setControllerSwim(zeroX,zeroY,heading,0,180);
    const float swimYaw=w.player.controllerYaw;
    std::printf("neutral swim heading=%.1f body=%.1f\n",heading,swimYaw);
    check(std::abs(std::remainder(swimYaw-heading,360.f))<.001f,"neutral swim must preserve head yaw");
    check(w.player.gamepadLY==0 && w.player.swimPitch==0,"neutral swim has no stick propulsion or dive pitch");
    w.player.setControllerMovement(0,-1,heading,false,180,0);
    check(std::abs(std::remainder(w.player.controllerYaw-swimYaw,360.f))<.001f,"shore handoff starts facing walking direction");
  }
  for(float heading : {0.f,45.f,-90.f}) {
    w.player.setControllerSwim(0,-1,heading,30,180);
    check(std::abs(w.player.controllerYaw-heading)<.001f && std::abs(w.player.swimPitch+30)<.001f,"forward stick retains camera pitch steering");
    w.player.setControllerSwim(1,0,heading,0,180);
    check(std::abs(w.player.controllerYaw-(heading-90))<.001f,"right stick retains sideways steering");
    w.player.setControllerSwim(0,1,heading,0,180);
    check(std::abs(std::abs(std::remainder(w.player.controllerYaw-heading,360.f))-180)<.001f,"backward stick retains reverse steering");
  }
#if defined(GOTHIC2VR_OPENXR) || defined(__ANDROID__)
  for(float head : {30.f,90.f,-45.f}) for(float stickX : {0.f,1.f,-1.f}) {
    g.cam.angle={};g.npc.yaw=0;
    for(int frame=0;frame<600;++frame){
      w.player.setControllerMovement(stickX,-1,g.cam.spin().y+head,false,180,0);
      const float delta=std::remainder(w.player.controllerYaw-g.npc.yaw,360.f);
      g.npc.yaw+=MovementResponse::turn(delta,std::abs(w.player.gamepadLY),180,0,1.f/72);
      w.tickCamera(14);
    }
    std::printf("head=%.0f stickX=%.0f cameraYaw=%.2f bodyYaw=%.2f\n",head,stickX,g.cam.angle.y,g.npc.yaw);
    check(std::abs(g.cam.angle.y)<0.001f,"walking must not feed body rotation back into camera yaw");
    check(std::abs(std::remainder(w.player.controllerYaw-g.npc.yaw,360.f))<=2.01f,"body settles at requested movement heading");
  }
  g.cam.angle.y=60;g.npc.yaw=0;w.tickCamera(14);
  check(g.cam.angle.y==60,"explicit stick look remains owned by input");
  g.npc.weapon=WeaponState::W1H;w.player.target.npc=&g.npc;w.tickCamera(14);
  check(g.cam.angle.y==60,"melee focus does not seize VR yaw");
#else
  for(float facing : {30.f,-90.f,120.f}) {
    g.cam.angle.y=0;g.npc.yaw=facing;w.tickCamera(14);
    check(g.cam.angle.y==facing,"flat desktop follows character facing");
  }
  g.npc.weapon=WeaponState::W1H;w.player.target.npc=&g.npc;g.npc.yaw=75;w.tickCamera(14);
  check(g.cam.angle.y==75,"flat melee focus retains original facing");
#endif
  w.player.target.npc=nullptr;g.npc.weapon=WeaponState::NoWeapon;
  g.cam.angle.y=42;g.npc.yaw=90;g.npc.mob=&w;w.tickCamera(14);
  check(g.cam.angle.y==42,"interaction yaw remains unchanged");g.npc.mob=nullptr;
  g.npc.down=true;w.tickCamera(14);check(g.cam.angle.y==42,"downed player yaw unchanged");g.npc.down=false;
  w.dialogs.active=true;w.tickCamera(14);check(g.cam.angle.y==123,"dialog camera retains priority");w.dialogs.active=false;
  g.cam.cutscene=true;g.cam.angle.y=17;w.tickCamera(14);check(g.cam.angle.y==17,"cutscene camera untouched");
  std::printf("Camera yaw: %d production-caller checks passed\n",checks);
}
'''
(out / 'camera-yaw.cpp').write_text(fixture, encoding='utf-8')
