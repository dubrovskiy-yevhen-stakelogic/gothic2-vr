"""Test physical OpenXR Y through the production adapter and menu interception."""
from pathlib import Path
import sys
r=Path(__file__).resolve().parents[1]
window=(r/'engine/common/vr/vrwindow.cpp').read_text()
start=window.index('  Vr::Input in;');end=window.index('  const bool before=vrMenu.visible;',start)
input_adapter=window[start:end]
xr=(r/'engine/common/vr/questxr.cpp').read_text()
y_binding=next(line for line in xr.splitlines() if 'button(secondary,h==0?' in line)
gameplay=(r/'engine/common/vr/vrgameplay.cpp').read_text()
start=gameplay.index('  const auto stickClicks=');end=gameplay.index('  // Resolve release for both hands',start)
physical_chord=gameplay[start:end]
code=r'''
#include <Tempest/SystemApi>
#include "vr/vrcontrols.h"
#include "utils/gamepadbindings.h"
#include <cstdio>
#include <cstdlib>
using Tempest::GamepadState;
struct QuestXr {
  static QuestXr& inst(){static QuestXr x;return x;}
  bool focused(){return true;}
  float gripValue(uint32_t){return .9f;}
};
Vr::Input adapt(const GamepadState& pad){
'''+input_adapter+r'''
  return in;
}
uint32_t physicalY(){
  GamepadState pad;const uint32_t h=0;const int secondary=1;
  auto button=[&](int,uint32_t mask){pad.buttons|=mask;};
'''+y_binding+r'''
  return pad.buttons;
}
bool interactionBlocked(const GamepadState& pad){auto& xr=QuestXr::inst();
'''+physical_chord+r'''
  return chord;
}
int checks=0;
void check(bool ok,const char* label){++checks;if(!ok){std::printf("FAIL %s\n",label);std::exit(1);}}
int main(){
  using GB=GamepadBindings;using Action=GB::Action;
  GB bindings;bindings.setVrMapping({Action::Jump,Action::Interact,Action::Inventory,Action::Journal,Action::Run,Action::Sneak});
  Vr::Menu menu;GamepadState pad;pad.connected=true;uint64_t now=0;
  int journal=0,gameMenu=0;
  auto tick=[&]{
    now+=20;const bool consumed=menu.update(adapt(pad),now);
    if(consumed){if(menu.action==Vr::Menu::OpenGameMenu)++gameMenu;bindings.reset(pad.buttons);}
    else for(auto event:bindings.update(pad.buttons,GB::Context::Gameplay,now))
      if(event.action==Action::Journal && event.phase==GB::Phase::Press)++journal;
    return consumed;
  };
  tick();tick();pad.buttons=GamepadState::L1|GamepadState::R1;tick();
  pad.buttons|=physicalY();
  check(adapt(pad).yButton,"physical OpenXR Y reaches the shortcut adapter");
  check(tick() && gameMenu==1 && journal==0,"grips+physical Y is consumed before Journal dispatch");
  check(interactionBlocked(pad),"physical Y chord blocks held-item interactions");
  tick();check(gameMenu==1 && journal==0,"holding physical shortcut cannot repeat or leak to Journal");
  pad.buttons=GamepadState::L1|GamepadState::R1;tick();
  pad.buttons|=physicalY();tick();check(gameMenu==2 && journal==0,"fresh Y while gripping reopens only game menu");
  pad.buttons=0;tick();tick();pad.buttons=physicalY();
  check(!tick() && journal==1 && gameMenu==2,"physical Y alone still opens Journal");
  pad.buttons=0;tick();pad.buttons=GamepadState::L1|physicalY();tick();
  check(journal==2 && gameMenu==2,"one grip plus physical Y retains normal mapping");
  std::printf("OpenXR menu input: %d production-adapter checks passed\n",checks);
}
'''
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
(out/'menu-input.cpp').write_text(code,encoding='utf-8',newline='\n')
