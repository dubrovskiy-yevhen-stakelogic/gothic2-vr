#include "vr/vrcontrols.h"
#include "vr/vrrunning.h"
#include "vr/vrswimming.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
int checks=0;
void check(bool ok,const char* name) { ++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);} }
int main() {
  Vr::Running run;
  check(!run.update(false,true,false),"toggle begins walking");
  check(run.update(true,true,false),"one press starts running");
  check(run.update(true,true,false),"held press cannot toggle twice");
  check(run.update(false,true,false),"release retains running");
  check(!run.update(true,true,false),"second press stops running");
  run.update(false,true,false);run.update(true,true,false);
  check(!run.update(true,false,false),"menu/focus loss clears latch");
  check(!run.update(true,true,false),"held button on resume does not start running");
  run.update(false,true,false);
  check(run.update(true,true,false),"fresh press after resume works");
  check(!run.update(false,true,true),"changing to hold clears old latch");
  check(run.update(true,true,true),"hold mode runs while pressed");
  check(!run.update(false,true,true),"hold mode stops on release");
  Vr::Settings s;check(!s.runHold,"old profiles default to toggle");
  s.runHold=true;std::ostringstream out;s.write(out);std::istringstream saved(out.str());
  Vr::Settings restored;restored.read(saved);check(restored.runHold,"hold preference round trips");
  Vr::Menu menu;Vr::Input in;in.focused=true;uint64_t now=0;
  auto step=[&]{return menu.update(in,now+=20);};
  auto release=[&]{in={};in.focused=true;step();};
  step();menu.visible=true;menu.page=Vr::Menu::Page::Locomotion;
  for(int i=0;i<20;++i){in.x=(i%2)?1:-1;step();check(!menu.changed && menu.selected==0,"horizontal stick never changes a value or selection");}
  release();in.x=1;in.y=1;step();check(menu.selected==1 && !menu.changed,"diagonal scroll selects without changing values");
  in.y=.2f;now+=500;step();check(menu.selected==1 && !menu.changed,"horizontal tail after scroll cannot edit");
  release();in.trigger=1;step();check(menu.changed && menu.settings.snapAngle==45,"right trigger increases");
  release();in.secondaryTrigger=1;step();check(menu.changed && menu.settings.snapAngle==30,"left trigger decreases");
  release();const auto rows=menu.rows();menu.selected=int(std::find(rows.begin(),rows.end(),Vr::Menu::RunMode)-rows.begin());
  in.trigger=1;step();check(menu.settings.runHold && menu.changed,"run mode reachable in locomotion");
  now+=1000;step();check(menu.settings.runHold && !menu.changed,"holding trigger cannot oscillate run mode");
  release();in.a=true;step();check(!menu.settings.runHold,"A returns to toggle");
  Vr::Swimming swim;Vr::SwimInput input;input.enabled=true;input.valid[0]=input.valid[1]=true;
  input.forward={0,0,-1};input.direction={0,1,0};
  swim.sample(input,1.f/60);
  for(int i=1;i<=30;++i){input.relative[0].z=input.relative[1].z=i*.02f;swim.sample(input,1.f/60);}
  auto v=swim.advance(1.f/60,10,false);check(v.y>.5f && v.z==0,"GTA vertical impulse maps to Gothic Y-up");
  swim.sample({},0);check(!swim.input.enabled,"disabled input clears swim ownership");
  swim.sample(input,1.f/60);v=swim.advance(1.f/60,10,false);
  check(v.y<0 && std::abs(v.y)<.01f,"resume has no stored impulse or synthetic stroke");
  input.direction={0,-1,0};swim.sample(input,1.f);v=swim.advance(1.f/60,10,false);
  check(v.y<0 && std::abs(v.y)<.01f,"long frame cannot generate a stroke");
  Vr::Swimming once,repeated;input=Vr::SwimInput{};
  input.enabled=true;input.valid[0]=input.valid[1]=true;input.forward={0,0,-1};input.direction={0,0,1};
  for(int i=0;i<=30;++i) {
    input.poseTime=1000000000ull+uint64_t(i)*16000000ull;
    input.relative[0].z=input.relative[1].z=i*.02f;
    once.sample(input,.016f);
    for(int poll=0;poll<8;++poll) repeated.sample(input,.002f);
  }
  const auto one=once.advance(.016f,10,false),many=repeated.advance(.016f,10,false);
  check(std::abs(one.z-many.z)<.00001f && one.z>1,"duplicate gamepad polls do not change physical stroke strength");
  std::printf("PASS: %d locomotion input checks\n",checks);
}
