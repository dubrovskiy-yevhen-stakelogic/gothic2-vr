"""Run production VR eye water classification independently of legacy camera state."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
source=(root/'engine/common/camera.cpp').read_text()
def method(signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
fixture=r'''
#include <Tempest/Matrix4x4>
#include <Tempest/Vec>
#include <limits>
#include <cstdio>
#include <cstdlib>
using namespace Tempest;
struct Physics {
  float surface=100;bool roof=false;Vec3 queried;float padding=-1;
  struct Hit{float wdepth;};
  Hit waterRay(Vec3 eye,float step){queried=eye;padding=step;return {roof?-std::numeric_limits<float>::infinity():surface};}
};
struct World {Physics physics;Physics* physic(){return &physics;}};
struct Gothic {
  World worldData;bool loaded=true;
  static Gothic& inst(){static Gothic g;return g;}
  World* world(){return loaded?&worldData:nullptr;}
};
struct Camera {
  bool vrViewActive=false,vrInWater=false,inWater=false;
  Matrix4x4 vrView,vrProjection;Vec3 vrOrigin;
  bool isInWater()const;
  void setVrView(const Matrix4x4&,const Matrix4x4&);
};
int checks=0;
void check(bool v,const char* label){++checks;if(!v){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
void eye(Camera& c,float height){Matrix4x4 view,projection;view.identity();projection.identity();view.translate(0,-height,0);c.setVrView(view,projection);}
'''
fixture+=method('bool Camera::isInWater() const')+'\n'+method('void Camera::setVrView(')
fixture+=r'''
int main(){
  Camera c;auto& g=Gothic::inst();
  eye(c,90);check(c.isInWater(),"first submerged VR eye ignores dry legacy camera");
  check(g.worldData.physics.queried.y==90 && g.worldData.physics.padding==0,"water probe uses actual eye without NPC step padding");
  c.inWater=true;eye(c,110);check(!c.isInWater(),"resurfacing ignores stale underwater parity");
  eye(c,99);check(c.isInWater(),"submerged left eye");eye(c,101);check(!c.isInWater(),"right eye above surface classified separately");
  eye(c,100);check(!c.isInWater(),"eye exactly at surface is dry");
  g.worldData.physics.roof=true;eye(c,50);check(!c.isInWater(),"no water through cave roof");
  g.loaded=false;eye(c,50);check(!c.isInWater(),"unloaded world clears VR water state");
  c.vrViewActive=false;check(c.isInWater(),"flat mode retains original camera state");
  std::printf("PASS: %d camera water checks\n",checks);
}
'''
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
(out/'camera-water.cpp').write_text(fixture,encoding='utf-8')
