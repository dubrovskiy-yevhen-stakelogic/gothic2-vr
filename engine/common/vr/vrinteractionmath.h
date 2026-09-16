#pragma once
#include <Tempest/Matrix4x4>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <map>

namespace Vr {
using Vec3=Tempest::Vec3;
using Matrix=Tempest::Matrix4x4;
inline Vec3 origin(const Matrix& m) { return {m[3][0],m[3][1],m[3][2]}; }
inline Vec3 axis(const Matrix& m,int i) { return {m[size_t(i)][0],m[size_t(i)][1],m[size_t(i)][2]}; }
inline Vec3 normalized(Vec3 v,Vec3 fallback={0,0,1}) { const float l=v.length(); return std::isfinite(l)&&l>0.0001f?v/l:fallback; }
inline float dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a,Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline bool finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
struct ItemCalibration {
  Vec3 offset{},rotation{};
  float grip=-1.f; // negative: authored model origin; otherwise fraction of the long axis
  float scale=1.f,stringHeight=.84f,stringCenter=0.f,stringSide=0.f,stringDepth=0.f;
  void sanitize() {
    grip=std::isfinite(grip)?std::clamp(grip,-1.f,1.f):-1.f;
    scale=std::isfinite(scale)?std::clamp(scale,.05f,3.f):1.f;
    stringHeight=std::isfinite(stringHeight)?std::clamp(stringHeight,.10f,3.f):.84f;
    stringCenter=std::isfinite(stringCenter)?std::clamp(stringCenter,-1.f,1.f):0.f;
    stringSide=std::isfinite(stringSide)?std::clamp(stringSide,-1.f,1.f):0.f;
    stringDepth=std::isfinite(stringDepth)?std::clamp(stringDepth,-1.f,1.f):0.f;
    if(!finite(offset))offset={};if(!finite(rotation))rotation={};
    offset.x=std::clamp(offset.x,-2.f,2.f);offset.y=std::clamp(offset.y,-2.f,2.f);offset.z=std::clamp(offset.z,-2.f,2.f);
    rotation.x=std::clamp(rotation.x,-180.f,180.f);rotation.y=std::clamp(rotation.y,-180.f,180.f);rotation.z=std::clamp(rotation.z,-180.f,180.f);
  }
};
inline ItemCalibration mirrorCalibration(ItemCalibration c) {
  c.offset.x=-c.offset.x;c.rotation.y=-c.rotation.y;c.rotation.z=-c.rotation.z;c.stringSide=-c.stringSide;return c;
}
struct HandBasis {Vec3 forward,right,up;};
inline HandBasis handBasis(const Matrix& grip,const Matrix& aim,bool leftHand,bool anchored) {
  const auto forward=normalized(axis(aim,2));
  auto up=axis(grip,0)*(leftHand?-1.f:1.f);
  up=normalized(up-forward*dot(up,forward),-axis(grip,1));
  auto right=normalized(cross(up,forward));
  // A socket's grip Z equals its aim Z: their perpendicular dot has no sign.
  // Only tracked controller poses supply an independent orientation reference.
  if(!anchored && dot(right,axis(grip,2))<-.001f)right=-right;
  return {forward,right,up};
}
inline Matrix handPalmPose(const Matrix& grip,const Matrix& aim,bool leftHand,bool socket) {
  const auto basis=handBasis(grip,aim,leftHand,socket);
  auto pose=grip;const Vec3 axes[]={basis.right,basis.up,basis.forward};
  for(size_t i=0;i<3;++i){pose[i][0]=axes[i].x;pose[i][1]=axes[i].y;pose[i][2]=axes[i].z;}
  return pose;
}
inline Matrix relativeHandPalm(const Matrix& weapon,const Matrix& grip,const Matrix& aim,bool leftHand) {
  auto inverse=weapon;inverse.inverse();
  return inverse*handPalmPose(grip,aim,leftHand,false);
}
inline std::string rangedDefaultKey(bool crossbow,std::string_view domain={}) {
  std::string key=crossbow?"DEFAULT_CROSSBOW":"DEFAULT_BOW";
  if(domain=="HOL")return key+"_HOL";
  if(!domain.empty())key+="_"+std::string(domain);
  return key+"_R";
}
inline ItemCalibration defaultRangedCalibration(bool crossbow,std::string_view domain={}) {
  ItemCalibration c;
  if(crossbow){if(domain=="SUP")c.offset.z=-.04f;return c;}
  if(domain.empty())c.rotation.y=-90;
  if(domain=="AIM")c.rotation.y=90;
  if(domain=="SUP" || domain=="STR"){c.offset.x=-.2f;c.rotation.y=90;}
  return c;
}
inline ItemCalibration defaultTwoHandSupport() {return {{-.01f,.04f,-.19f},{90,0,0}};}
inline ItemCalibration defaultMeleeGrip() {return {{-.02f,.075f,.06f},{-77.f,0,0},-1};}
inline Matrix itemPose(Vec3 position,Vec3 forward,Vec3 up,const ItemCalibration& calibration,float units) {
  forward=normalized(forward);up=normalized(up,{0,1,0});
  if(std::abs(dot(up,forward))>.97f)up={1,0,0};
  auto right=normalized(cross(up,forward));up=normalized(cross(forward,right));
  auto m=Matrix::mkIdentity();Vec3 axes[]={right,up,forward};
  for(size_t i=0;i<3;++i){m[i][0]=axes[i].x;m[i][1]=axes[i].y;m[i][2]=axes[i].z;}
  m[3][0]=position.x;m[3][1]=position.y;m[3][2]=position.z;
  m.translate(calibration.offset*units);m.rotateOX(calibration.rotation.x);m.rotateOY(calibration.rotation.y);m.rotateOZ(calibration.rotation.z);return m;
}
// One model transform for rendering and physical blade endpoints. Gothic weapons
// author their attachment origin; a bounding-box percentage is an explicit override.
inline Matrix weaponModelMatrix(Vec3 low,Vec3 high,int kind,const Matrix& pose,float grip=-1.f,float scale=1.f,bool leftHand=false) {
  const Vec3 size=high-low;const float lengths[]={size.x,size.y,size.z};
  int longest=0;
  // Native crossbows share authored axes; their width/depth ordering varies by model.
  if(kind!=4)for(int i=1;i<3;++i)if(lengths[i]>lengths[longest])longest=i;
  const float mins[]={low.x,low.y,low.z},maxs[]={high.x,high.y,high.z};
  const float sign=(kind==0 || kind==3) && std::abs(mins[longest])>std::abs(maxs[longest])?-1.f:1.f;
  Vec3 basis[3];const float side=kind==2 && leftHand?-1.f:1.f;
  const auto right=axis(pose,0)*side,up=axis(pose,1),forward=axis(pose,2)*side;
  if(kind==2){basis[longest]=up;basis[(longest+1)%3]=forward;basis[(longest+2)%3]=right;}
  else {basis[longest]=forward*sign;basis[(longest+1)%3]=right*sign;basis[(longest+2)%3]=up;}
  scale=std::isfinite(scale)?std::clamp(scale,.05f,3.f):1.f;
  for(auto& v:basis)v*=scale;
  Vec3 anchor=(kind==0 || kind==2 || kind==3)?Vec3():(low+high)*.5f;
  if(grip>=0){anchor=(low+high)*.5f;float* values[]={&anchor.x,&anchor.y,&anchor.z};*values[longest]=mins[longest]+lengths[longest]*grip;}
  auto m=Matrix::mkIdentity();
  for(size_t i=0;i<3;++i){m[i][0]=basis[i].x;m[i][1]=basis[i].y;m[i][2]=basis[i].z;}
  const auto p=origin(pose)-basis[0]*anchor.x-basis[1]*anchor.y-basis[2]*anchor.z;
  m[3][0]=p.x;m[3][1]=p.y;m[3][2]=p.z;return m;
}
inline Vec3 rotateBetween(Vec3 v,Vec3 from,Vec3 to) {
  from=normalized(from);to=normalized(to);const float c=std::clamp(dot(from,to),-1.f,1.f);
  if(c>.9999f)return v;
  if(c<-.9999f){auto a=normalized(cross(from,std::abs(from.y)<.9f?Vec3(0,1,0):Vec3(1,0,0)));return a*(2*dot(a,v))-v;}
  const auto k=cross(from,to);return v+cross(k,v)+cross(k,cross(k,v))/(1+c);
}
inline bool twoHandDirection(Vec3 primary,Vec3 support,float units,Vec3& forward) {
  const auto span=primary-support;const float distance=span.length();
  if(!finite(span) || distance<.06f*units || distance>.65f*units)return false;
  forward=span/distance;return true;
}
// Ray and occupancy callbacks keep the same placement policy testable without a world.
template<class Ray,class Occupied>
bool enemySpawnPoint(Vec3 player,Vec3 forward,Vec3 right,Ray ray,Occupied occupied,Vec3& spawn) {
  for(float distance:{260.f,200.f,140.f}) {
    for(float angle:{0.f,-.52f,.52f,-1.04f,1.04f}) {
      const auto chest=player+Vec3(0,75,0);
      const auto candidate=chest+(forward*std::cos(angle)+right*std::sin(angle))*distance;
      const auto wall=ray(chest,candidate);
      if(wall.hasCol && (wall.v-candidate).length()>=3.f)continue;
      const auto floor=ray(candidate,candidate-Vec3(0,400,0));
      if(!floor.hasCol || floor.n.y<.5f || std::abs(floor.v.y-player.y)>120.f || occupied(floor.v))continue;
      spawn=floor.v+Vec3(0,5,0);return true;
    }
  }
  return false;
}
struct HolsterSettings {
  ItemCalibration meleeDefault=defaultMeleeGrip();
  std::map<std::string,ItemCalibration> calibration;
  bool enabled=true,showHands=true,showHolsters=true,physicalCombat=true,gripLock=false,pickupHighlight=true,bowSight=true,ignoreWeaponRequirements=false;
  float radius=0.23f,pickupRadius=0.35f,swingSpeed=2.2f,pickupHighlightRange=6.f;
  std::array<Vec3,4> offsets={Vec3(.23f,-.45f,.10f),Vec3(-.12f,-.32f,.15f),Vec3(-.26f,-.14f,-.20f),Vec3(.24f,-.08f,-.22f)};
  std::array<std::string,4> items{}; // empty means first compatible inventory item
  void sanitize() {
    meleeDefault.sanitize();
    for(auto& entry:calibration)entry.second.sanitize();
    radius=std::isfinite(radius)?std::clamp(radius,.10f,.40f):.23f;
    pickupRadius=std::isfinite(pickupRadius)?std::clamp(pickupRadius,.10f,.65f):.35f;
    pickupHighlightRange=std::isfinite(pickupHighlightRange)?std::clamp(pickupHighlightRange,1.f,20.f):6.f;
    swingSpeed=std::isfinite(swingSpeed)?std::clamp(swingSpeed,1.f,5.f):2.2f;
    for(auto& p:offsets) { if(!finite(p)) p={0,-.4f,0};p.x=std::clamp(p.x,-.65f,.65f);p.y=std::clamp(p.y,-1.1f,.25f);p.z=std::clamp(p.z,-.65f,.65f); }
  }
};
inline void storeRangedDefaults(HolsterSettings& settings,bool crossbow,const std::array<ItemCalibration,3>& profiles,bool leftHand) {
  const std::string_view domains[]={"","AIM","SUP"};
  for(size_t i=0;i<3;++i)settings.calibration[rangedDefaultKey(crossbow,domains[i])]=leftHand?mirrorCalibration(profiles[i]):profiles[i];
}
inline bool assignHolsterItem(HolsterSettings& settings,int point,std::string name) {
  if(point<0 || point>=4)return false;
  const auto previous=settings.items[size_t(point)];
  if(name!="EMPTY" && !name.empty())for(int i=0;i<4;++i)
    if(i!=point && settings.items[size_t(i)]==name)settings.items[size_t(i)]=previous==name?"EMPTY":previous;
  settings.items[size_t(point)]=std::move(name);return true;
}
inline int holsterPointForPickup(bool melee,bool ranged) { return melee?0:ranged?2:-1; }
// Free: "", "EMPTY" or an unowned item; items already assigned keep their holster.
inline bool holsterFreeForPickup(const HolsterSettings& settings,int point,const std::string& name,bool pointOwned) {
  if(point<0 || point>=4 || name.empty())return false;
  for(const auto& assigned:settings.items)if(assigned==name)return false;
  const auto& current=settings.items[size_t(point)];
  return current.empty() || current=="EMPTY" || !pointOwned;
}
inline bool swapHolsterItems(HolsterSettings& settings,int from,int to) {
  if(from<0 || from>=4 || to<0 || to>=4 || from==to)return false;
  std::swap(settings.items[size_t(from)],settings.items[size_t(to)]);return true;
}
struct BodyFrame {
  Vec3 head{},forward{0,0,1},right{1,0,0};
  void update(const Matrix& headWorld) {
    head=origin(headWorld);auto f=axis(headWorld,2);f.y=0;
    if(f.length()>.15f) forward=normalized(f);
    right=normalized(cross(Vec3(0,1,0),forward),{1,0,0});
  }
  Vec3 point(Vec3 offset,float units) const { return head+(right*offset.x+Vec3(0,offset.y,0)+forward*offset.z)*units; }
  Vec3 local(Vec3 world,float units) const { auto d=(world-head)/units;return {dot(d,right),d.y,dot(d,forward)}; }
  bool placeHolster(HolsterSettings& settings,int slot,Vec3 hand,float units) const {
    if(slot<0 || slot>=4 || !finite(hand) || !std::isfinite(units) || units<=0)return false;
    const auto p=local(hand,units);
    if(!finite(p) || std::abs(p.x)>.65f || p.y< -1.1f || p.y>.25f || std::abs(p.z)>.65f)return false;
    settings.offsets[size_t(slot)]=p;return true;
  }
  int closest(Vec3 hand,const HolsterSettings& s,float units) const {
    if(!s.enabled) return -1;
    int best=-1;float distance=s.radius*units;
    for(int i=0;i<4;++i) {float d=(hand-point(s.offsets[size_t(i)],units)).length();if(d<distance) {best=i;distance=d;} }
    return best;
  }
};
struct ButtonEdge {
  bool ready=false,down=false,pressed=false,released=false;
  void update(float value,bool valid) {
    pressed=released=false;
    if(!valid || !std::isfinite(value)) { ready=false;down=false;return; }
    if(!ready) { if(value<.25f) ready=true;return; }
    const bool next=value>(down?.35f:.65f);
    pressed=next&&!down;released=!next&&down;down=next;
  }
};
// Body-local and raw tracking samples reject locomotion and camera discontinuities.
// Each stroke can contact once; a return stroke rearms without a stationary hold.
struct Swing {
  Vec3 previousTracking{},previousGrip{},strokeDirection{};float strokeTravel=0,reverseTravel=0,contactTravel=0;
  Vec3 previous{},previousBodyForward{0,0,1},previousHead{};
  uint64_t stamp=0,coolUntil=0;bool valid=false,armed=false,active=false,contactReady=false;
  float speed=0;bool continuous=false;
  void reset() { valid=armed=active=continuous=contactReady=false;speed=strokeTravel=reverseTravel=contactTravel=0;strokeDirection={};coolUntil=0; }
  void contact() { active=false; }
  bool update(Vec3 tip,const BodyFrame& body,float units,uint64_t now,bool allowed,float threshold,const Vec3* tracking=nullptr,const Vec3* gripTracking=nullptr) {
    const Vec3 local=body.local(tip,units);
    if(!allowed || !finite(local) || (tracking && !finite(*tracking)) || (gripTracking && !finite(*gripTracking))) {reset();return false;}
    const float dt=valid&&now>stamp?float(now-stamp)*.001f:0;
    const float jump=(body.head-previousHead).length()/units;
    const bool continuity=valid && dt>=.001f && dt<=.1f && jump<.4f && dot(body.forward,previousBodyForward)>.94f;
    const auto movement=tracking?*tracking-previousTracking:local-previous;
    const float travel=std::min((local-previous).length(),movement.length());
    speed=continuity?travel/dt:0;
    const float gripTravel=gripTracking?(*gripTracking-previousGrip).length():0.f;
    const bool gripContinuous=!gripTracking || (continuity && gripTravel<.30f && gripTravel/dt<15.f);
    continuous=continuity && gripContinuous && travel<(gripTracking?2.f:.5f) && speed<(gripTracking?80.f:24.f);
    // Entry/exit collision state owns melee debouncing. A fresh entry must not depend
    // on the direction, cooldown or consumed animation phase of an earlier stroke.
    contactTravel=continuous && speed>=.7f?std::min(contactTravel+travel,.2f):0.f;
    contactReady=continuous && speed>=threshold && (!tracking || contactTravel>=.035f);
    if(!continuous) {armed=active=false;strokeTravel=reverseTravel=0;strokeDirection={};}
    else if(speed<.7f) {
      active=false;armed=now>=coolUntil;strokeTravel=reverseTravel=0;strokeDirection={};
    } else {
      const auto direction=normalized(movement);
      if(strokeDirection.length()<.01f) {
        armed=true;strokeDirection=direction;
      } else if(dot(direction,strokeDirection)<-.25f) {
        reverseTravel+=travel;
        if(reverseTravel>=.035f) {
          active=false;armed=true;strokeTravel=reverseTravel-travel;reverseTravel=0;strokeDirection=direction;
        }
      } else reverseTravel=0;
      if(armed)strokeTravel+=travel;
    }
    const bool strike=continuous && armed && speed>=threshold && now>=coolUntil && (!tracking || strokeTravel>=.035f);
    if(strike) {armed=false;active=true;coolUntil=now+280;}
    if(tracking)previousTracking=*tracking;
    if(gripTracking)previousGrip=*gripTracking;
    previous=local;previousHead=body.head;previousBodyForward=body.forward;stamp=now;valid=true;
    return strike;
  }
};
}
