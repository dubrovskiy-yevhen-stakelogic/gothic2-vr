#pragma once
#if defined(GOTHIC2VR_OPENXR)
#include "vrcontrols.h"
#include "vrcombatmath.h"
#include "vrbowmath.h"
#include <vector>
#include <array>
class World;
class Npc;
class Item;
class Focus;
class QuestXr;
namespace Vr {
class Gameplay {
  public:
    struct Hand {
      Matrix grip=Matrix::mkIdentity(),aim=Matrix::mkIdentity(),palmPose=Matrix::mkIdentity();
      bool visible=false,anchored=false;float squeeze=0,trigger=0;
      void anchor(bool leftHand) {anchored=true;palmPose=handPalmPose(grip,aim,leftHand,true);}
    };
    struct Visual { std::string mesh; Vec3 position,forward,up;int kind=0;float grip=-1.f,scale=1.f;bool leftHand=false,replaceBowString=false; };
    std::array<Hand,2> hands;
    struct Highlight {std::string mesh;Matrix matrix;};
    std::vector<Highlight> highlights;
    std::vector<Visual> visuals;
    std::vector<std::pair<Vec3,Vec3>> lines,aimLines;
    BodyFrame body;
    float units=100;
    bool raining=false;
    std::string notice;
    uint64_t noticeUntil=0;
    void queue(int row,int direction,int point) { if(actions.size()<32) actions.push_back({row,direction,point}); }
    // Menu actions (spawn, give, time, weather) run only after the whole
    // previous frame completed: they disable the early left eye.
    bool hasQueuedActions() const { return !actions.empty(); }
    bool update(World* world,QuestXr& xr,Menu& menu,const Matrix& base,uint64_t now,bool allowed);
    std::string label(Menu::Row row,const Menu& menu) const;
    void suspend();
    Npc* healthTarget(World& world,const Focus& focus,uint64_t now);
  private:
    static constexpr size_t None=size_t(-1);
    struct Action {int row,direction,point;};
    struct Entry {size_t id;std::string name;int category;std::string display;int value=0;};
    struct Held {size_t id=None;int slot=-1,holster=-1;Swing swing;Vec3 previousBase,previousTip;uint64_t strikeUntil=0,shootAfter=0;bool autoArrow=false,insideTarget=false;};
    struct ReturnItem {Item* item=nullptr;uint64_t time=0;};
    std::vector<ReturnItem> returning;
    std::array<Held,2> held;
    std::array<ReleaseMotion,2> releaseMotion;
    std::array<ReleaseDebounce,2> releaseDebounce;
    uint64_t releaseFrame=0,releaseBlockedUntil=0;
    std::array<ButtonEdge,2> grips,triggers;
    std::array<int,2> hovered={-1,-1};
    std::vector<Action> actions;
    std::vector<Entry> items,enemies;
    World* context=nullptr;
    Npc* contextPlayer=nullptr;
    size_t symbolCount=0;
    int enemyCategory=1,itemCategory=1,enemyIndex=0,itemIndex=0,quantity=1,hour=12,weather=0;
    size_t calibrationItem=None;bool calibrating=false;
    std::string itemKey(size_t id,int hand,std::string_view domain={}) const;
    ItemCalibration profile(size_t id,int hand,const HolsterSettings& settings,std::string_view domain={}) const;
    void selectCalibration(Npc& player,Menu& menu,int direction=0);
    int nockHand=-1,supportHand=-1,swordMain=-1;
    Matrix swordPalmLocal=Matrix::mkIdentity();
    float drawLength=0;
    bool drawing=false;
    BowGesture bowGesture;
    Vec3 healthGazeDirection{0,0,1};
    Npc* hudTargetNpc=nullptr;
    World* hudTargetWorld=nullptr;
    Npc* recentTarget=nullptr;
    uint64_t targetUntil=0,twoHandNoticeAfter=0,requirementNoticeAfter=0;
    void rememberTarget(Npc* target,uint64_t now) { recentTarget=target;targetUntil=now+4000; }
    uint64_t roofCheck=0;
    Vec3 roofPosition;
    std::string calibrationKey(int hand) const;
    ItemCalibration calibrationFor(int hand,const HolsterSettings& settings) const;
    void returnItems(World& world,Npc& player,uint64_t now);
    void catalog(World& world);
    std::vector<const Entry*> filtered(bool npc) const;
    bool seedRangedDefaults(HolsterSettings& settings) const;
    bool autoHolsterPickup(Npc& player,HolsterSettings& settings,size_t id,uint64_t now);
    bool actionsTick(World& world,Menu& menu,uint64_t now,QuestXr& xr,const Matrix& base);
    const Item* resolve(Npc& player,int slot,const HolsterSettings& settings) const;
    static bool compatible(const Item& item,int slot);
    void message(std::string text,uint64_t now) { notice=std::move(text);noticeUntil=now+4000; }
};
}
#endif
