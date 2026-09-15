#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

class MultiFingerTap {
  public:
    enum class Action { None, QuickSave, QuickLoad, DeleteSave };
    static constexpr uint64_t JoinMs = 350;
    static constexpr uint64_t ReleaseMs = 800;

    static Action action(int fingers, bool saveMenu) {
      if(saveMenu) return fingers==3 ? Action::DeleteSave : Action::None;
      if(fingers==3) return Action::QuickSave;
      if(fingers==4) return Action::QuickLoad;
      return Action::None;
      }

    void down(int pointer, float x, float y, uint64_t now, bool eligible) {
      if(contacts.empty()) {
        started=now;
        peak=0;
        valid=true;
        releasing=false;
        }
      if(contacts.contains(pointer)) { valid=false; return; }
      contacts.emplace(pointer,Contact{x,y});
      peak=std::max(peak,int(contacts.size()));
      valid &= eligible && !releasing && now-started<=JoinMs && peak<=4;
      }

    void move(int pointer, float x, float y, float slop) {
      const auto it=contacts.find(pointer);
      if(it!=contacts.end() && std::hypot(x-it->second.x,y-it->second.y)>slop)
        valid=false;
      }

    int up(int pointer, uint64_t now) {
      if(contacts.erase(pointer)==0) return 0;
      releasing=true;
      if(!contacts.empty() || !valid || now-started>ReleaseMs) return 0;
      return peak==3 || peak==4 ? peak : 0;
      }

    bool ready() const { return valid && !releasing && contacts.size()>=3; }
    bool joining(uint64_t now) const { return valid && !releasing && !contacts.empty() && now-started<=JoinMs; }
    bool empty() const { return contacts.empty(); }
    void reset() { contacts.clear(); valid=false; }

  private:
    struct Contact { float x,y; };
    std::unordered_map<int,Contact> contacts;
    uint64_t started=0;
    int peak=0;
    bool valid=false;
    bool releasing=false;
  };
