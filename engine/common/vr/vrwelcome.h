#pragma once
#include <cstdint>

namespace Vr {
// The saved acknowledgement arms this gate only for a new installation.
struct Welcome {
  bool visible=false,shown=false,waitForRelease=false;
  uint32_t previousButtons=0;
  bool update(bool eligible,bool focused,bool moving,uint32_t buttons) {
    const auto pressed=buttons&~previousButtons;
    previousButtons=buttons;
    if(!focused)return visible || waitForRelease;
    if(waitForRelease){waitForRelease=buttons!=0;return true;}
    if(!shown && eligible && moving){shown=true;visible=true;return true;}
    if(!visible)return false;
    if(pressed!=0){visible=false;waitForRelease=true;}
    return true; // Consume the dismissing press as well as the opening movement.
  }
};
}
