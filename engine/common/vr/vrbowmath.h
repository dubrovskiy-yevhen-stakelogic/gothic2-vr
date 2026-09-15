#pragma once
#include "vrinteractionmath.h"
namespace Vr {
inline float bowPower(float draw) {return std::clamp(draw/.60f,0.f,1.f);}
inline float bowSpeedScale(float power) {return .35f+.65f*std::clamp(power,0.f,1.f);}
// Only the bow hand controls aim. The string hand supplies axial travel.
inline Matrix bowShotPose(const Matrix& bow,const ItemCalibration& aim,Vec3 stringRest,Vec3,float units) {
  const auto calibrated=itemPose(origin(bow),axis(bow,2),axis(bow,1),aim,units);
  return itemPose(stringRest+axis(calibrated,2)*.08f*units,axis(calibrated,2),axis(calibrated,1),{},units);
}
inline float bowAxialDraw(Vec3 rest,Vec3 hand,Vec3 forward,float units) {
  if(!finite(rest) || !finite(hand) || !std::isfinite(units) || units<=0)return 0;
  return dot(rest-hand,normalized(forward))/units;
}
inline Vec3 bowNockPoint(Vec3 rest,Vec3 forward,float travel,float units) {
  return rest-normalized(forward)*(std::clamp(travel,0.f,.6f)*units);
}
inline float arrowNockGrip(Vec3 low,Vec3 high) {
  const auto size=high-low;const float lengths[]={size.x,size.y,size.z};
  const float mins[]={low.x,low.y,low.z},maxs[]={high.x,high.y,high.z};
  int longest=0;for(int i=1;i<3;++i)if(lengths[i]>lengths[longest])longest=i;
  return std::abs(mins[longest])>std::abs(maxs[longest])?1.f:0.f;
}
inline Vec3 bowStringOffset(const Matrix& bow,const ItemCalibration& model,float units) {
  return (axis(bow,0)*model.stringSide+axis(bow,1)*model.stringCenter+axis(bow,2)*model.stringDepth)*units;
}
inline std::array<Vec3,2> bowStringTips(const Matrix& bow,const ItemCalibration& model,float units) {
  const auto center=origin(bow)+bowStringOffset(bow,model,units);
  const auto span=axis(bow,1)*(model.stringHeight*.5f*units);
  return {center+span,center-span};
}
inline bool handTransferReach(Vec3 receiver,Vec3 donor,float units) {
  return finite(receiver) && finite(donor) && std::isfinite(units) && units>0 &&
         (receiver-donor).length()<=.10f*units;
}
struct BowGesture {
  enum Event {None,Nocked,Fired,Cancelled};bool active=false;float nockDistance=0;
  void reset(){active=false;nockDistance=0;}
  Event update(bool tracked,bool nearString,bool pressed,bool released,float draw) {
    if(!tracked || !std::isfinite(draw)){bool was=active;reset();return was?Cancelled:None;}
    if(!active){if(pressed && nearString){active=true;nockDistance=draw;return Nocked;}return None;}
    if(draw>.95f){reset();return Cancelled;}
    if(released){const bool pulled=draw-nockDistance>=.08f;reset();return pulled?Fired:Cancelled;}
    return None;
  }
};
// Match Gothic's gravity integration, including its one-step downward term.
inline Vec3 arrowPath(Vec3 origin,Vec3 velocity,float seconds,float gravity=980.f) {
  return origin+velocity*seconds-Vec3(0,.5f*gravity*seconds*(seconds+.016f),0);
}
}
