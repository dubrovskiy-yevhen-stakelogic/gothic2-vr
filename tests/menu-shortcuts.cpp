#include "vr/vrcontrols.h"
#include <cstdio>
#include <cstdlib>
int checks=0;
void check(bool ok,const char* label){++checks;if(!ok){std::printf("FAIL %s\n",label);std::exit(1);}}
int main(){
  Vr::Menu menu;Vr::Input in;in.focused=true;uint64_t now=0;
  auto step=[&]{return menu.update(in,now+=20);};
  auto release=[&]{in={};in.focused=true;step();};
  step();in.leftClick=true;
  check(!step() && !menu.visible,"single L3 retains gameplay mapping");
  in.rightClick=true;
  check(step() && menu.visible && menu.action==-1,"L3+R3 opens only VR menu");
  for(int i=0;i<10;++i)check(step() && menu.visible,"held clicks cannot retoggle");
  in.rightClick=false;step();in.rightClick=true;step();
  check(menu.visible,"partial chord release cannot retoggle through the gate");
  release();in.leftClick=in.rightClick=true;step();
  check(!menu.visible,"fresh click chord closes VR menu");
  in.leftClick=in.rightClick=false;in.rightX=.8f;
  check(step(),"turning remains blocked until right stick is centered");
  release();check(!step(),"neutral resumes gameplay");
  in.leftGrip=in.rightGrip=in.yButton=true;
  check(step() && menu.action==Vr::Menu::OpenGameMenu && !menu.visible,"both grips+Y requests only game menu");
  check(step() && menu.action==-1,"holding game shortcut cannot repeat or leak journal action");
  in.yButton=false;step();in.yButton=true;
  check(step() && menu.action==Vr::Menu::OpenGameMenu,"fresh Y press works while grips remain held");
  release();in.yButton=true;
  check(!step() && menu.action==-1,"Y alone retains journal mapping");
  in.leftGrip=true;check(!step() && menu.action==-1,"one grip+Y retains gameplay mapping");
  release();in.rightClick=true;check(!step(),"R3 alone retains crouch mapping");
  release();in.leftClick=in.rightClick=true;step();release();
  in.leftGrip=in.rightGrip=in.yButton=true;step();
  check(menu.action==Vr::Menu::OpenGameMenu && !menu.visible,"game shortcut leaves VR settings before opening game menu");
  release();in.leftClick=in.rightClick=in.leftGrip=in.rightGrip=in.yButton=true;step();
  check(menu.visible && menu.action==-1,"overlapping shortcuts have one deterministic VR action");
  release();in.focused=false;step();in.focused=true;in.leftGrip=in.rightGrip=in.yButton=true;
  check(step() && menu.action==-1,"held game chord across focus gain cannot fire");
  release();in.leftGrip=in.rightGrip=in.yButton=true;step();
  check(menu.action==Vr::Menu::OpenGameMenu,"game shortcut re-arms after neutral focus recovery");
  release();in.focused=false;step();in.focused=true;in.leftClick=in.rightClick=true;step();
  check(!menu.visible,"held VR chord across focus gain cannot fire");
  release();in.leftClick=in.rightClick=true;step();check(menu.visible,"VR shortcut re-arms after focus recovery");
  std::printf("Menu shortcuts: %d checks passed\n",checks);
}
