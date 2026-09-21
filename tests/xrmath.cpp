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
  // Off-axis desktop HMDs: Index/Vive/WMR report tanAngleLeft != -tanAngleRight
  // and canted panels skew the vertical too, so the boresight of an eye is not
  // the centre of its image. A symmetric construction would silently place it
  // there and shear the stereo pair; pin the principal point of both eyes.
  const auto principal=[&](float al,float ar,float ad,float au) {
    const auto m=projection(al,ar,ad,au,2,100000); auto v=point(m,{0,0,10});
    return Vec3(v.x,v.y,0);
    };
  const auto centre=[](float lo,float hi) { return -(std::tan(hi)+std::tan(lo))/(std::tan(hi)-std::tan(lo)); };
  const auto nasal=principal(-0.9599f,0.7854f,-0.8727f,0.7854f); // ~ -55/45/-50/45 degrees
  near(nasal.x,centre(-0.9599f,0.7854f),"left eye boresight is off centre horizontally");
  near(nasal.y,-centre(-0.8727f,0.7854f),"canted boresight is off centre vertically and Y is flipped");
  const auto temporal=principal(-0.7854f,0.9599f,-0.8727f,0.7854f);
  near(temporal.x,-nasal.x,"mirrored eye mirrors the boresight, so the pair does not shear");
  const auto q=projection(-0.9599f,0.7854f,-0.8727f,0.7854f,2,100000);
  near(point(q,{std::tan(-0.9599f)*10,0,10}).x,-1,"asymmetric left edge");
  near(point(q,{std::tan(0.7854f)*10,0,10}).x,1,"asymmetric right edge");
  near(point(q,{0,-std::tan(0.7854f)*10,10}).y,-1,"asymmetric upper edge");
  near(point(q,{0,-std::tan(-0.8727f)*10,10}).y,1,"asymmetric lower edge");
  const auto symmetric=principal(-0.8f,0.8f,-0.8f,0.8f);
  near(symmetric.x,0,"a symmetric frustum still centres its boresight X");
  near(symmetric.y,0,"a symmetric frustum still centres its boresight Y");
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
