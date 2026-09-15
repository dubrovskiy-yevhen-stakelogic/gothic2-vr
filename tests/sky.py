"""Check the production celestial vertex math under asymmetric stereo projection."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
s=(root/'engine/shader/sky/sun.vert').read_text()
renderer=(root/'engine/common/graphics/renderer.cpp').read_text()
start=renderer.index('const bool angularSprite =',renderer.index('void Renderer::drawSunMoon('))
routing=renderer[start:renderer.index(';',start)+1]
start=s.index('vec3 center =');end=s.index('return;',start)
body=s[start:end]
body=body.replace('clip.xy','Vec2(clip.x,clip.y)').replace('inverse(push.viewProjectInv)*vec4(direction,0.0)','project4(inverse(push.viewProjectInv),Vec4(direction.x,direction.y,direction.z,0.f))')
body=body.replace('vec4(Vec2(clip.x,clip.y),clip.w,clip.w)','vec4(clip.x,clip.y,clip.w,clip.w)')
body=body.replace('vec3','Vec3').replace('vec4','Vec4').replace('vec2','Vec2').replace('1e-6','1e-6f')
fixture=r'''
#include "vr/vrinteractionmath.h"
#include "vr/xrmath.h"
#include <cstdio>
#include <cstdlib>
using namespace Tempest;
using Vr::cross;using Vr::dot;
Vec3 normalize(Vec3 v){return Vr::normalized(v);}
Matrix4x4 inverse(Matrix4x4 m){m.inverse();return m;}
Vec4 project4(const Matrix4x4& m,Vec4 v){m.project(v);return v;}
struct Push{Vec3 sunDir;Vec2 sunSz;Matrix4x4 viewProjectInv;};
Vec4 vertex(const Push& push,Vec2 v){Vec2 outPos;Vec4 gl_Position;
'''+body+r'''
return gl_Position;
}
int checks=0;
void test(bool ok,const char* name){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
bool angularRoute(bool isSun){
'''+routing+r'''
  return angularSprite;
}
int main(){
  test(angularRoute(true),"sun glow uses the world-space celestial path in VR");
  test(angularRoute(false),"moon retains its world-space celestial path in VR");
  for(Vec3 center:{normalize({-1,1,0}),normalize({.3f,.8f,.2f})})for(float extent:{100.f/1024.f,.4f}) {


  for(float yaw:{-40.f,0.f,40.f})for(float roll:{-180.f,-135.f,-90.f,-45.f,0.f,45.f,90.f,135.f,180.f})for(float shift:{-.12f,.12f}){
    const auto right=normalize(cross(Vec3(0,1,0),center)),down=cross(right,center);
    const Matrix4x4 base(right.x,right.y,right.z,0, down.x,down.y,down.z,0, center.x,center.y,center.z,0, 0,0,0,1);
    auto rotation=Matrix4x4::mkIdentity();rotation.rotateOY(yaw);rotation.rotateOZ(roll);
    const auto view=rotation*base;
    const auto projection=XrMath::projection(-.9f+shift,.9f+shift,-.85f,.9f,2,100000);
    Push push{center,Vec2(extent),inverse(projection*view)};
    for(Vec2 corner:{Vec2(-1,0),Vec2(1,0),Vec2(0,-1),Vec2(0,1)}){
      const auto clip=vertex(push,corner);
      test(std::isfinite(clip.w) && std::abs(clip.w)>.00001f,"celestial corner stays finite under gaze and roll");
      auto ray=Vec3(clip.x/clip.w,clip.y/clip.w,clip.z/clip.w);push.viewProjectInv.project(ray);ray=normalize(ray);
      const auto expected=normalize(center+right*(corner.x*extent)+down*(corner.y*extent));
      test((ray-expected).length()<.0001f,"texture axes stay fixed in world space through a full head roll");
      const auto angle=std::acos(std::clamp(dot(ray,center),-1.f,1.f));
      test(std::abs(angle-std::atan(extent))<.0001f,"celestial angular radius stays constant across gaze roll and both eye frustums");
    }
    const auto a=vertex(push,{-1,-1}),b=vertex(push,{1,1}),c=vertex(push,{1,-1});
    if(a.w>0 && b.w>0 && c.w>0){
      const Vec2 ab(b.x/b.w-a.x/a.w,b.y/b.w-a.y/a.w),ac(c.x/c.w-a.x/a.w,c.y/c.w-a.y/a.w);
      test(ab.x*ac.y-ab.y*ac.x<0,"celestial quad retains screen sprite winding for front-face culling");
    }
  }
  }
  std::printf("VR sky: %d production vertex checks passed\n",checks);
}
'''
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True);(out/'sky.cpp').write_text(fixture)
print('Extracted current angular celestial vertex transform')
