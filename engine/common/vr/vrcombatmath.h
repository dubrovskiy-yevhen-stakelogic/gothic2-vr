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
enum class ReleaseAction { Keep,Stow,Drop };
inline ReleaseAction releaseAction(bool allowed,bool tracked,float grip,bool lock,bool atHolster) {
  if(!allowed || !tracked || !std::isfinite(grip) || grip>.25f)return ReleaseAction::Keep;
  if(atHolster)return ReleaseAction::Stow;
  return lock?ReleaseAction::Keep:ReleaseAction::Drop;
}
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
