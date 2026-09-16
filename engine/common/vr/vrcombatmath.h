#pragma once
#include "vrinteractionmath.h"

namespace Vr {
// A small cone with a near-field margin makes tiny items selectable without
// requiring a pixel-perfect ray. Lower score is closer to the gaze centre.
inline float gazeScore(Vec3 head,Vec3 forward,Vec3 point) {
  if(!finite(head)||!finite(forward)||!finite(point))return -1;
  forward=normalized(forward);const auto delta=point-head;const float along=dot(delta,forward);
  if(along<=0)return -1;
  const float width=std::min(35.f,9.f+along*.14f),side=(delta-forward*along).length();
  return side>width?-1.f:side/width+along*.0005f;
}
// Interaction gaze tangent to a point; negative when the point is behind the head.
inline float gazeTangent(Vec3 head,Vec3 forward,Vec3 point) {
  if(!finite(head)||!finite(forward)||!finite(point))return -1;
  forward=normalized(forward);const auto delta=point-head;const float along=dot(delta,forward);
  if(along<=0)return -1;
  return (delta-forward*along).length()/std::max(along,50.f);
}
// NPC interaction cone: fifteen degrees to acquire, twenty-two to retain, with a 15 cm near-field margin.
inline float npcConeScore(Vec3 head,Vec3 forward,Vec3 point,bool retained) {
  if(!finite(head)||!finite(forward)||!finite(point))return -1;
  forward=normalized(forward);const auto delta=point-head;const float along=dot(delta,forward);
  if(along<=0)return -1;
  const float side=(delta-forward*along).length();
  if(side>15.f+along*(retained?.404026f:.267949f))return -1;
  return side/std::max(along,50.f)+along*.0005f;
}
// Scores the point of the NPC body axis (shins to head) closest to the gaze ray.
// points: that point, then head/chest probes inside the cone for line-of-sight tests.
inline float npcGazeScore(Vec3 head,Vec3 forward,Vec3 feet,Vec3 top,bool retained,Vec3* points=nullptr,size_t* count=nullptr) {
  if(count)*count=0;
  if(!finite(head)||!finite(forward)||!finite(feet)||!finite(top))return -1;
  forward=normalized(forward);
  const auto axis=top-feet;const float length2=dot(axis,axis);
  float t=.7f;
  if(length2>1.f) {
    const auto w0=head-feet;const float b=dot(forward,axis),d=dot(forward,w0),e=dot(axis,w0),denominator=length2-b*b;
    if(denominator>1e-3f*length2)t=(e-b*d)/denominator;
  }
  t=std::clamp(t,.15f,1.f);
  const auto nearest=feet+axis*t;
  const float score=npcConeScore(head,forward,nearest,retained);
  if(score<0)return -1;
  if(points && count) {
    points[(*count)++]=nearest;
    for(float height:{.9f,.55f})if(std::abs(height-t)>.05f) {
      const auto probe=feet+axis*height;
      if(npcConeScore(head,forward,probe,retained)>=0)points[(*count)++]=probe;
    }
  }
  return score;
}
// HUD-only angular selection: twenty degrees to acquire, twenty-eight to retain.
inline float healthGazeScore(Vec3 head,Vec3 forward,Vec3 point,float units,bool retained) {
  if(!finite(head) || !finite(forward) || !finite(point) || !std::isfinite(units) || units<=0)return -1;
  const auto delta=point-head;forward=normalized(forward);
  const float distance=delta.length(),along=dot(delta,forward);
  if(along<=0 || distance>20.f*units)return -1;
  const float side=(delta-forward*along).length();
  if(side>.10f*units+along*(retained?.531709f:.363970f))return -1;
  return side/std::max(along,.1f*units)+.015f*distance/(20.f*units);
}
// Damage multiplier for weapons whose requirements are not met.
constexpr float unqualifiedWeaponDamage=.25f;
inline int32_t penalizedDamage(int32_t value,bool qualified) {
  return qualified || value<=0 ? value : int32_t(float(value)*unqualifiedWeaponDamage);
}
enum class ReleaseAction { Keep,Stow,Drop };
inline ReleaseAction releaseAction(bool allowed,bool tracked,float grip,bool lock,bool atHolster) {
  if(!allowed || !tracked || !std::isfinite(grip) || grip>.25f)return ReleaseAction::Keep;
  if(atHolster)return ReleaseAction::Stow;
  return lock?ReleaseAction::Keep:ReleaseAction::Drop;
}
// Grip release must stay open confirmMs; releases are blocked after a frame gap. Velocity is taken on the first open frame.
struct ReleaseDebounce {
  static constexpr uint64_t confirmMs=40,gapMs=200,blockMs=300;
  bool pending=false;uint64_t since=0;Vec3 velocity{};
  void reset(){pending=false;velocity={};}
  bool confirm(bool open,uint64_t now,uint64_t blockedUntil,Vec3 currentVelocity) {
    if(!open || now<blockedUntil){reset();return false;}
    if(!pending){pending=true;since=now;velocity=currentVelocity;}
    return now-since>=confirmMs;
  }
};
// Finite segment against the actual oriented NPC bounds, including an origin
// already inside. The legacy projectile ray deliberately keeps its own policy.
inline bool meleeContact(Vec3 from,Vec3 to,Vec3 center,Vec3 radius,float yaw,float& fraction) {
  if(!finite(from)||!finite(to)||!finite(center)||!finite(radius)||!std::isfinite(yaw) ||
     radius.x<=0 || radius.y<=0 || radius.z<=0)return false;
  const float c=std::cos(yaw),s=std::sin(yaw);
  auto local=[&](Vec3 v){return Vec3((v.x*c+v.z*s)/radius.x,v.y/radius.y,(-v.x*s+v.z*c)/radius.z);};
  const auto o=local(from-center),d=local(to-from);
  const float cc=dot(o,o)-1.f;if(cc<=0){fraction=0;return true;}
  const float a=dot(d,d),b=dot(o,d),disc=b*b-a*cc;
  if(a<1e-12f || disc<0)return false;
  const float t=(-b-std::sqrt(disc))/a;
  if(t<0 || t>1)return false;
  fraction=t;return true;
}
struct Guard {
  Vec3 base{},tip{},head{},forward{0,0,1};float units=100;
  uint64_t until=0;int hand=-1;
  bool blocks(Vec3 attacker,uint64_t now) const {
    if(hand<0 || hand>1 || now>until || !finite(base)||!finite(tip)||!finite(head)||!finite(attacker)||
       !std::isfinite(units)||units<=0)return false;
    auto incoming=attacker-head;incoming.y=0;const float distance=incoming.length()/units;
    auto blade=(tip-base)/units;const float length=blade.length();
    if(distance<.05f || distance>3.f || length<.20f)return false;
    incoming=normalized(incoming);blade=blade/length;
    if(dot(incoming,normalized(Vec3(forward.x,0,forward.z)))<.25f ||
       std::abs(dot(blade,incoming))>.45f || std::abs(blade.y)>.65f)return false;
    // Nearest point of the blade to the line of attack must cover the torso.
    const auto a=(base-head)/units,span=(tip-base)/units;
    const auto side=normalized(cross(Vec3(0,1,0),incoming));
    const float lateral=dot(a,side),slope=dot(span,side);
    const float t=std::abs(slope)>.001f?std::clamp(-lateral/slope,0.f,1.f):.5f;
    const auto p=a+span*t;
    return dot(p,incoming)>.05f && dot(p,incoming)<1.f && p.y>-.75f && p.y<.25f && std::abs(dot(p,side))<.3f;
  }
};
// Use raw tracking velocity for release, transformed into the current game
// basis. Camera translation and snap turns must not add throw energy.
struct ReleaseMotion {
  Vec3 previous{},velocity{};uint64_t stamp=0;bool valid=false;
  void reset(){valid=false;velocity={};}
  void update(Vec3 tracking,uint64_t now,bool tracked) {
    if(!tracked || !finite(tracking)){reset();return;}
    const float dt=valid&&now>stamp?float(now-stamp)*.001f:0;
    if(dt>=.001f && dt<=.1f) {
      auto v=(tracking-previous)/dt;const float speed=v.length();
      if(speed>12.f)velocity={};else velocity=velocity*.35f+v*.65f;
    } else velocity={};
    previous=tracking;stamp=now;valid=true;
  }
};
}
