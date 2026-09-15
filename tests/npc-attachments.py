"""Exercise the actual NPC animation caller with a moving skeleton fixture."""
from pathlib import Path
import sys
import hashlib
root=Path(__file__).resolve().parents[1]
source=(root/'engine/common/world/objects/npc.cpp').read_text()
signature='void Npc::updateAnimation(uint64_t dt, bool force)'
start=source.index(signature);brace=source.index('{',start);depth=1;end=brace+1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
method=source[start:end]
header=(root/'engine/common/world/objects/npc.h').read_text()
assert 'MdlVisual                      visual;' in header, 'Reassess fixture: NPC visual ownership changed'
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
fixture=r'''
#include <Tempest/Matrix4x4>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
using namespace Tempest;
struct Camera {bool isFree() const {return false;}};
struct Gothic {static Gothic& inst() {static Gothic g;return g;} Camera* camera(){return nullptr;}};
class Npc;
struct Visual {
  Matrix4x4 position=Matrix4x4::mkIdentity();float attached=0,headBone=0;bool changed=true;int syncs=0;
  const Matrix4x4& transform() const{return position;}
  void setObjMatrix(Matrix4x4 m,bool){position=m;}
  bool updateAnimation(Npc*,void*,int&,uint64_t,bool force){if(changed || force)headBone=position[3][0]+5;return changed||force;}
  void syncAttaches(){attached=headBone;++syncs;}
};
class Npc {
public:
  enum {TR_Pos=1,TR_Rot=2};uint32_t durtyTranform=TR_Pos;Vec3 lastGroundNormal={0,1,0};float x=0,y=0,z=0;int owner=0;
  Visual visual;struct Sound{void setPosition(float,float,float){}} sfxWeapon;
  bool isPlayer() const {return false;}Vec3 groundNormal() const {return {0,1,0};}
  Matrix4x4 mkPositionMatrix() const {auto m=Matrix4x4::mkIdentity();m.translate(x,y,z);return m;}
  void updateAnimation(uint64_t dt,bool force);
};
void check(bool ok,const char* label) {if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
'''
fixture+=method+r'''
int main() {
  Npc npc;npc.x=10;npc.updateAnimation(14,false);
  check(npc.visual.attached==15,"head follows initial pose");
  npc.x=150;npc.durtyTranform=Npc::TR_Pos;npc.updateAnimation(14,false);
  check(npc.visual.attached==155 && npc.visual.syncs==2,"walking NPC carries its head");
  npc.visual.changed=false;npc.durtyTranform=0;npc.updateAnimation(0,false);
  check(npc.visual.syncs==2,"unchanged pose avoids duplicate synchronization");
  npc.updateAnimation(0,true);check(npc.visual.syncs==3,"forced pose refresh carries attachments");
  std::puts("NPC attachments: 4 production-caller checks passed");
}
'''
(out/'npc.cpp').write_text(fixture)
(out/'source-sha256.txt').write_text(hashlib.sha256(method.encode()).hexdigest()+'\n')
print('Extracted current Npc::updateAnimation for attachment regression')
