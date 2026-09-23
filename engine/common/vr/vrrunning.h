#pragma once
namespace Vr {
class Running {
  public:
    bool update(bool down,bool enabled,bool hold) {
      if(!enabled || hold!=holdMode) active=false;
      if(enabled) {
        if(hold) active=down;
        else if(down && !previous) active=!active;
      }
      previous=down; holdMode=hold;
      return active;
    }
  private:
    bool active=false,previous=false,holdMode=false;
};
}
