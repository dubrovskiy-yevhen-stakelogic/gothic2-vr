#include "vrhandrenderer.h"
#if defined(GOTHIC2VR_OPENXR)
#include <Tempest/MemReader>
#include <Tempest/Pixmap>
#include <Tempest/Log>
#include "graphics/mesh/protomesh.h"
#include "shader.h"
#include "vrassets.h"
using namespace Tempest;
namespace Vr {
namespace {
Resources::Vertex vertex(Vec3 p,Vec3 n,Vec2 uv={0,0},uint32_t color=0xffffffff) {
  return {{p.x,p.y,p.z},{n.x,n.y,n.z},{uv.x,uv.y},color};
}
Matrix itemMatrix(const Gameplay::Visual& v,const ProtoMesh& mesh) {
  const auto bounds=mesh.bbox();
  return weaponModelMatrix(bounds[0],bounds[1],v.kind,itemPose(v.position,v.forward,v.up,{},1.f),v.grip,v.scale,v.leftHand);
}
}
void HandRenderer::init(Device& device) {
  const char* file="vrhands/BigHandLeft.uxrh";
  try {
    for(int i=0;i<2;++i) {
      file=i==0?"vrhands/BigHandLeft.uxrh":"vrhands/BigHandRight.uxrh";
      auto& h=hand[size_t(i)];h.asset=HandAsset::read(Platform::asset(file));
      h.ibo=device.ibo(h.asset.indices);std::vector<Resources::Vertex> empty(h.asset.vertices.size());for(auto& vbo:h.vbo) vbo=device.vbo(BufferHeap::Upload,empty);
    }
    file="vrhands/BigHandsAlbedo.png";
    auto bytes=Platform::asset(file);MemReader reader(bytes);albedo=device.texture(Pixmap(reader));
  } catch(const std::exception& error) {
    // Hands are cosmetic. Name the file and the full path it was looked for at,
    // then leave the game running without them. failed is set before the log so
    // a path that cannot be rendered as text still disables hands; prepare()
    // never calls init() again, so this is reported exactly once. The
    // half-built meshes are dropped: nothing downstream may see a vertex count
    // without a matching vertex buffer.
    failed=true;for(auto& h:hand) {h.asset=HandAsset();h.visible=false;}
    Log::e("VR hands disabled - ",error.what(),"; expected \"",file,"\" at \"",Platform::assetPath(file),"\"");
    return;
  }
  auto vs=GothicShader::get("vr_hands.vert.sprv"),fs=GothicShader::get("vr_hands.frag.sprv");
  const auto v=device.shader(vs.data,vs.len),f=device.shader(fs.data,fs.len);
  RenderState state;state.setCullFaceMode(RenderState::CullMode::NoCull);state.setZTestMode(RenderState::ZTestMode::LEqual);state.setZWriteEnabled(true);
  pipeline=device.pipeline(Triangles,state,v,f);
  state.setBlendSource(RenderState::BlendMode::SrcAlpha);state.setBlendDest(RenderState::BlendMode::OneMinusSrcAlpha);state.setZWriteEnabled(false);
  linePipeline=device.pipeline(Triangles,state,v,f);
  const auto pv=GothicShader::get("vr_pickup.vert.sprv"),pf=GothicShader::get("vr_pickup.frag.sprv");
  state.setBlendDest(RenderState::BlendMode::One);
  pickupPipeline=device.pipeline(Triangles,state,device.shader(pv.data,pv.len),device.shader(pf.data,pf.len));
  initialized=true;Log::i("VR Vice City hands loaded: ",hand[0].asset.vertices.size()," / ",hand[1].asset.vertices.size());
}
void HandRenderer::prepare(Device& device,const Gameplay& gameplay,uint64_t now,uint8_t slot) {
  objects.clear();lineCount=0;for(auto& h:hand)h.visible=false;
  if(failed) return;
  slot&=1u;
  try {
    if(!initialized) init(device);
    if(!initialized) return; // asset load failed; init() logged where it looked and set failed
    // Writes only this frame's vertex set. The other set may still be read by
    // the previous frame's right eye while the early left eye is recorded.
    for(int i=0;i<2;++i) {
      const auto& input=gameplay.hands[size_t(i)];auto& mesh=hand[size_t(i)];mesh.visible=input.visible;
      if(!mesh.visible) continue;
      const auto palm=input.anchored?input.palmPose:handPalmPose(input.grip,input.aim,i==0,false);
      const auto forward=axis(palm,2),right=axis(palm,0),up=axis(palm,1);
      auto weights=HandAsset::weights(input.squeeze,input.trigger);
      std::vector<Resources::Vertex> vertices;vertices.reserve(mesh.asset.vertices.size());
      for(const auto& source:mesh.asset.vertices) {
        Vec3 p,n;
        for(size_t k=0;k<4;++k) {p+=Vec3(source.position[k][0],source.position[k][1],source.position[k][2])*weights[k];n+=Vec3(source.normal[k][0],source.normal[k][1],source.normal[k][2])*weights[k];}
        const auto world=origin(palm)+(forward*(p.x-.055f)+right*p.z-up*p.y)*gameplay.units;
        vertices.push_back(vertex(world,normalized(forward*n.x+right*n.z-up*n.y),{source.uv[0],source.uv[1]}));
      }
      mesh.vbo[slot].update(vertices);
    }
    for(const auto& visual:gameplay.visuals) if(!visual.mesh.empty()) {
      if(auto mesh=visual.kind==2 && visual.replaceBowString?Resources::loadVrBowMesh(visual.mesh):Resources::loadMesh(visual.mesh)) objects.push_back({mesh,itemMatrix(visual,*mesh)});
    }
    for(const auto& mark:gameplay.highlights)if(auto mesh=Resources::loadMesh(mark.mesh))objects.push_back({mesh,mark.matrix,true});
    std::vector<Resources::Vertex> vertices;
    auto line=[&](Vec3 a,Vec3 b,float width,uint32_t color) {
      const auto side=gameplay.body.right*width;const Vec3 n(0,1,0);
      for(auto p:{a-side,b-side,b+side,a-side,b+side,a+side})vertices.push_back(vertex(p,n,{0,0},color));
    };
    for(const auto& l:gameplay.lines)line(l.first,l.second,.14f,0xffd0d0d0);
    for(const auto& l:gameplay.aimLines)line(l.first,l.second,.28f,0xff2020ff);
    if(gameplay.raining) {
      const auto& body=gameplay.body;const float t=float(now%100000)*.001f;
      for(int i=0;i<96;++i) {
        float x=float((i*37)%101)/101.f*6.f-3.f,z=float((i*61)%103)/103.f*6.f-3.f;
        const float y=2.f-std::fmod(t*5.f+float(i)*.193f,4.f);
        auto p=body.head+Vec3(x,y,z)*gameplay.units;
        line(p,p+Vec3(.015f,-.14f,.02f)*gameplay.units,.09f,0x5080a0c0);
      }
    }
    lineCount=vertices.size();
    auto& lines=lineVbo[slot];
    if(lineCount>lines.size()) lines=device.vbo(BufferHeap::Upload,vertices);else if(lineCount)lines.update(vertices);
  } catch(const std::exception& error) {Log::e("VR hands disabled after render setup error: ",error.what());failed=true;objects.clear();lineCount=0;for(auto& h:hand)h.visible=false;}
}
ZBuffer* HandRenderer::depthBuffer(uint32_t width,uint32_t height) {
  if(!initialized || failed) return nullptr;
  if(depth.w()!=int(width) || depth.h()!=int(height)) depth=Resources::device().zbuffer(TextureFormat::Depth32F,width,height);
  return &depth;
}
void HandRenderer::drawInPass(const ZBuffer& sceneDepth,Encoder<CommandBuffer>& cmd,const Matrix& vp,const Matrix& relativeVp,Vec3 cameraOrigin,Vec2 clipPlanes,uint8_t slot,Size view) {
  slot&=1u;
  if(!hasContent()) return;
  cmd.setPipeline(pipeline);cmd.setBinding(1,sceneDepth,Sampler::nearest(ClampMode::ClampToEdge));
  struct Push {Matrix mvp;Vec4 light;Vec4 viewport;Vec4 tint;};
  // gl_FragCoord -> scene depth UV over the pass viewport (render scale < 1:
  // the eye sub-rectangle, ; otherwise the whole target).
  const int vw=view.w>0?view.w:depth.w(), vh=view.h>0?view.h:depth.h();
  Push push{vp,Vec4(.2f,.8f,.3f,.1f),Vec4(1.f/float(vw),1.f/float(vh),0,0),Vec4(1,1,1,1)};
  cmd.setBinding(0,albedo);cmd.setPushData(push);
  for(const auto& h:hand) if(h.visible)cmd.draw(h.vbo[slot],h.ibo);
  for(const auto& object:objects) {
    cmd.setPipeline(object.highlight?pickupPipeline:pipeline);
    push.tint=object.highlight?Vec4(.1f,.85f,1.f,.75f):Vec4(1,1,1,1);push.viewport.z=clipPlanes.y/(clipPlanes.y-clipPlanes.x);push.viewport.w=-clipPlanes.x*push.viewport.z;
    const auto& mesh=*object.mesh;
    auto drawMesh=[&](const ProtoMesh::Attach& part,const Matrix& transform) {
      if(part.vbo.isEmpty() || part.ibo.isEmpty()) return;
      if(object.highlight) {
        auto relative=transform;
        const auto position=origin(transform)-cameraOrigin;
        relative[3][0]=position.x;relative[3][1]=position.y;relative[3][2]=position.z;
        push.mvp=relativeVp*relative;
      } else push.mvp=vp*transform;
      const Vec3 light=normalized(Vec3(.2f,.8f,.3f));
      push.light=Vec4(dot(axis(transform,0),light),dot(axis(transform,1),light),dot(axis(transform,2),light),.1f);
      cmd.setPushData(push);
      for(const auto& sub:part.sub) if(sub.material.tex && sub.iboLength) {cmd.setBinding(0,*sub.material.tex);cmd.draw(part.vbo,part.ibo,sub.iboOffset,sub.iboLength);}
    };
    for(size_t i=0;i<mesh.attach.size();++i) {
      const bool bound=std::any_of(mesh.nodes.begin(),mesh.nodes.end(),[&](const auto& n){return n.attachId==i;});
      if(!bound) drawMesh(mesh.attach[size_t(i)],object.matrix);
    }
    for(const auto& node:mesh.nodes) if(node.attachId<mesh.attach.size()) drawMesh(mesh.attach[node.attachId],object.matrix*node.transform);
  }
  if(lineCount) {
    cmd.setPipeline(linePipeline);push.tint=Vec4(1,1,1,1);push.mvp=vp;push.light=Vec4(0,1,0,0);cmd.setPushData(push);
    cmd.setBinding(0,*Resources::loadTexture(Color(1,1,1,1)));cmd.setBinding(1,sceneDepth,Sampler::nearest(ClampMode::ClampToEdge));cmd.draw(lineVbo[slot],0,lineCount);
  }
}
}
#endif
