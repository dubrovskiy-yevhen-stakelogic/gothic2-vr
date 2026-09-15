#pragma once

#include <cstdint>

// Release-to-activate navigation for a ray pointer. Dragging off, losing focus,
// resizing or changing input context cancels a press instead of accepting a menu.
class PanelNavigation {
  public:
    static constexpr int Count = 6;
    void press(int pointer, int button) {
      if(owner<0 && button>=0 && button<Count) { owner=pointer; selected=button; }
      }
    bool owns(int pointer) const { return owner==pointer && owner>=0; }
    int release(int pointer, int button) {
      if(!owns(pointer)) return -1;
      const int result=button==selected ? selected : -1;
      cancel();
      return result;
      }
    void cancel() { owner=-1; selected=-1; }
    int pressedButton() const { return selected; }
    bool scroll(uint64_t now) {
      if(scrolled && now-lastScroll<180) return false;
      scrolled=true; lastScroll=now; return true;
      }
  private:
    int owner=-1;
    int selected=-1;
    bool scrolled=false;
    uint64_t lastScroll=0;
  };
