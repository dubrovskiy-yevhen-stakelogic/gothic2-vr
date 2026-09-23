"""Exercise the actual gameplay visibility gate when entering/leaving water."""
from pathlib import Path
import sys

root=Path(__file__).resolve().parents[1]
source=(root/'engine/common/vr/vrgameplay.cpp').read_text()
def block(marker):
    start=source.index(marker);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

gate=next(line for line in source.splitlines() if line.strip().startswith('allowed=allowed && xr.focused()'))
swim_gate=next((line for line in source.splitlines() if 'const bool swimmingHands=' in line),'')
fixture=r'''
#include <array>
#include <cstdint>
#include <cstdio>
struct Hand { bool visible=false,anchored=false;int grip=-1,aim=-1;float squeeze=0,trigger=0; };
struct Player {
  bool down=false,swim=false,dive=false,interacting=false;
  bool isDown()const{return down;}bool isSwim()const{return swim;}bool isDive()const{return dive;}
  void* interactive()const{return interacting?(void*)this:nullptr;}
};
struct Xr {
  bool focus=true;bool tracked[2]={true,true};int frame=0;
  bool focused()const{return focus;}
  bool gripTracked(uint32_t i)const{return tracked[i] && focus;}
  int handWorld(uint32_t i,int,bool aim=false)const{return frame*10+int(i)*2+int(aim);}
  float gripValue(uint32_t i)const{return i==0?.25f:.8f;}
  struct Pad{float leftTrigger=.2f,rightTrigger=.9f;};Pad gamepad()const{return {};}
};
struct Gameplay {
  std::array<Hand,2> hands;int suspended=0,interactionPasses=0;
  struct {bool showHands=true;}settings;
  void suspend(){++suspended;}
  bool update(Player* player,Xr& xr,bool allowed,bool preview=false){
    bool changed=true;int base=0;
    for(auto& h:hands){h.visible=false;h.anchored=false;}
'''
fixture+=swim_gate+'\n'+gate+'\n'+block('if(!allowed) {suspend();')
fixture+=r'''
    ++interactionPasses;
    for(auto& h:hands)h.visible=settings.showHands;
    return changed;
  }
};
int checks=0,failures=0;
void check(bool value,const char* label){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
int main(){
  Player player;Xr xr;Gameplay game;
  game.update(&player,xr,true);check(game.hands[0].visible && game.interactionPasses==1,"land retains ordinary hands/interactions");
  player.swim=true;
  check(game.update(&player,xr,true),"swim branch preserves pending setting changes");
  check(game.hands[0].visible && game.hands[1].visible,"entering surface swim keeps both tracked hands visible");
  check(game.interactionPasses==1 && game.suspended==1,"surface swimming does not enable weapons/grabs");
  check(game.hands[0].grip==0 && game.hands[1].aim==3,"swimming draws raw tracked grip/aim poses");
  check(game.hands[0].squeeze==.25f && game.hands[1].trigger==.9f,"finger poses still follow controller buttons");
  player.swim=false;player.dive=true;xr.frame=4;game.hands[0].anchored=true;
  game.update(&player,xr,true);
  check(game.hands[0].visible && game.hands[1].visible && game.hands[0].grip==40,"diving updates hand poses every frame");
  check(!game.hands[0].anchored && game.interactionPasses==1,"dive clears weapon anchors without enabling interaction");
  xr.tracked[0]=false;game.update(&player,xr,true);
  check(!game.hands[0].visible && game.hands[1].visible,"tracking loss hides only the unavailable hand");
  xr.tracked[0]=true;game.update(&player,xr,true);check(game.hands[0].visible,"tracking recovery restores hand");
  game.settings.showHands=false;game.update(&player,xr,true);
  check(!game.hands[0].visible && !game.hands[1].visible,"Hands Off remains respected in water");
  game.settings.showHands=true;xr.focus=false;game.update(&player,xr,true);
  check(!game.hands[0].visible && !game.hands[1].visible,"focus loss still hides hands");
  xr.focus=true;player.down=true;game.update(&player,xr,true);check(!game.hands[0].visible,"dead or down player cannot show swim hands");
  player.down=false;player.interacting=true;game.update(&player,xr,true);check(!game.hands[0].visible,"scripted interaction gate remains intact");
  player.interacting=false;game.update(&player,xr,false);check(!game.hands[0].visible,"non-gameplay world/menu gate remains intact");
  game.update(&player,xr,false,true);check(game.interactionPasses==2,"existing settings preview path remains intact");
  player.dive=false;game.update(&player,xr,true);check(game.interactionPasses==3 && game.hands[0].visible,"leaving water resumes land path");
  std::printf("Swimming hands: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
'''
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
(out/'swimming-hands.cpp').write_text(fixture,encoding='utf-8')
