#include "vr/vrtaa.h"
#include "vr/xrmath.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace Tempest;
void check(bool ok,const char* label) {if(!ok){std::puts(label);std::exit(1);}}
int main() {
  float worst=0,oldWorst=0;
  for(float worldOffset:{0.f,50000.f,200000.f,-200000.f})
    for(float eyeOffset:{-.03f,.03f}) {
      auto projection=XrMath::projection(-.8f+eyeOffset,.9f+eyeOffset,-.9f,.9f,10.f,100000.f);
      auto oldView=Matrix4x4::mkIdentity();oldView.rotateOY(35.f);
      auto newView=Matrix4x4::mkIdentity();newView.rotateOY(35.05f);
      auto oldVP=projection*oldView,newVP=projection*newView;
      const Vec3 oldOrigin(worldOffset,2000.f,worldOffset);
      const Vec3 newOrigin=oldOrigin+Vec3(.125f,.0625f,-.25f);
      auto m=Vr::taaReprojection(oldVP,newVP,oldOrigin,newOrigin);
      auto absoluteOld=oldVP;absoluteOld.translate(-oldOrigin);
      auto absoluteNew=newVP;absoluteNew.translate(-newOrigin);absoluteNew.inverse();
      auto unstable=absoluteOld*absoluteNew;
      check(Vr::taaReprojection(newVP,newVP,newOrigin,newOrigin)==Matrix4x4::mkIdentity(),"stationary camera must have exact identity reprojection");
      auto rotationInverse=newView;rotationInverse.inverse();
      for(float z:{100.f,1000.f,3000.f})for(float x:{-.3f,0.f,.3f}) {
        Vec3 relative(x*z,.1f*z,z);rotationInverse.project(relative);
        auto cur=newVP*Vec4(relative.x,relative.y,relative.z,1.f);
        const auto priorRelative=relative+(newOrigin-oldOrigin);
        auto expected=oldVP*Vec4(priorRelative.x,priorRelative.y,priorRelative.z,1.f);
        auto actual=m*cur,prior=unstable*cur;
        for(unsigned axis=0;axis<2;++axis) {
          float ref=axis==0?expected.x/expected.w:expected.y/expected.w;
          float val=axis==0?actual.x/actual.w:actual.y/actual.w;
          float old=axis==0?prior.x/prior.w:prior.y/prior.w;
          worst=std::max(worst,std::abs(val-ref)*1024.f);
          oldWorst=std::max(oldWorst,std::abs(old-ref)*1024.f);
        }
      }
    }
  std::printf("TAA reprojection max pixel error at 2048px: relative=%.6f old_absolute=%.6f\n",worst,oldWorst);
  check(worst<.02f,"relative reprojection must stay below 0.02 pixels for head motion in either eye");
  check(oldWorst>.1f,"fixture reproduces absolute world matrix precision loss");
}

