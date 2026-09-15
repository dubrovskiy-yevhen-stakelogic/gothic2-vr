#include "vr/xrmath.h"
#include <cstdio>
#include <cstdlib>
using namespace Tempest;
using namespace XrMath;
static int checks=0;
static void near(float a,float b,const char* name) {
  ++checks;
  if(std::abs(a-b)>0.002f) { std::fprintf(stderr,"FAIL %s: %.6f != %.6f\n",name,a,b); std::exit(1); }
}
static Vec3 point(const Matrix4x4& m,Vec3 p) { m.project(p); return p; }
int main() {
  const auto identity=Matrix4x4::mkIdentity();
  Pose origin{};
  const auto center=eyeView(origin,origin,identity);
  near(point(center,{0,0,100}).z,100,"identity forward");
  Pose left{},right{}; left.position.x=-0.032f; right.position.x=0.032f;
  near(point(eyeView(left,origin,identity),{0,0,100}).x,3.2f,"left IPD sign and metres to centimetres");
  near(point(eyeView(right,origin,identity),{0,0,100}).x,-3.2f,"right IPD sign");
  Pose head{}; head.position={1,1.7f,2};
  auto ref=reference(head);
  auto view=eyeView(head,ref,identity);
  near(point(view,{0,0,100}).x,0,"initial room offset removed X");
  near(point(view,{0,0,100}).y,0,"initial room offset removed Y");
  head.position.y+=0.1f;
  near(point(eyeView(head,ref,identity),{0,0,100}).y,10,"standing up moves world down");
  head=origin; head.orientation={0,0.70710678f,0,0.70710678f};
  near(point(eyeView(head,origin,identity),{-100,0,0}).z,100,"left head turn sees left camera-space axis");
  ref=reference(head);
  near(point(eyeView(head,ref,identity),{0,0,100}).z,100,"yaw recenter");
  head=origin; head.orientation={0.38268343f,0,0,0.92387953f};
  near(reference(head).orientation.x,0,"recenter preserves pitch tracking");
  const float l=-0.7f,r=0.9f,b=-0.8f,t=0.6f;
  const auto p=projection(l,r,b,t,2,100000);
  near(point(p,{std::tan(l)*10,0,10}).x,-1,"left frustum edge");
  near(point(p,{std::tan(r)*10,0,10}).x,1,"right frustum edge");
  near(point(p,{0,-std::tan(t)*10,10}).y,-1,"upper frustum edge Vulkan Y");
  near(point(p,{0,-std::tan(b)*10,10}).y,1,"lower frustum edge Vulkan Y");
  near(point(p,{0,0,2}).z,0,"near maps to zero");
  near(point(p,{0,0,100000}).z,1,"far maps to one");
  SnapTurn snap;
  near(snap.update(1,true),0,"held on entry is blocked");
  near(snap.update(0,true),0,"neutral arms turn");
  near(snap.update(1,true),-30,"right snap");
  near(snap.update(1,true),0,"held stick does not repeat");
  near(snap.update(0,false),0,"focus loss disarms");
  near(snap.update(-1,true),0,"held across focus gain blocked");
  snap.update(0,true);
  near(snap.update(-1,true),30,"left snap after neutral");
  std::printf("PASS %d VR coordinate, projection and snap-turn checks\n",checks);
}
