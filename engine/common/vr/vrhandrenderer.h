#pragma once
#if defined(GOTHIC2VR_OPENXR)
#include "vrgameplay.h"
#include "uxrh.h"
#include "resources.h"
#include <Tempest/Device>
#include <Tempest/Encoder>
#include <Tempest/RenderPipeline>
class ProtoMesh;
namespace Vr {
class HandRenderer {
  public:
    // `slot` is the frame's command slot (cmdId): the host-mapped vertex
    // buffers are double-buffered by frame so the next frame's early left eye
    // can write its set while the previous right eye still reads the other.
    void prepare(Tempest::Device& device,const Gameplay& gameplay,uint64_t now,uint8_t slot);
    // Draws inside an already set render pass whose depth attachment is
    // depthBuffer() cleared to 1 (the renderer's tonemapping pass); scene
    // occlusion is sampled from sceneDepth in the fragment shader. `view` is
    // the pass viewport the fragment coordinates run over (the eye image
    // sub-rectangle at render scale < 1); empty = depthBuffer().
    void drawInPass(const Tempest::ZBuffer& sceneDepth,Tempest::Encoder<Tempest::CommandBuffer>& cmd,const Matrix& viewProjection,const Matrix& relativeViewProjection,Vec3 cameraOrigin,Tempest::Vec2 clipPlanes,uint8_t slot,Tempest::Size view=Tempest::Size());
    Tempest::ZBuffer* depthBuffer(uint32_t width,uint32_t height);
    bool hasContent() const { return initialized && !failed && (!objects.empty() || hand[0].visible || hand[1].visible || lineCount!=0); }
  private:
    bool initialized=false,failed=false;
    struct HandMesh {HandAsset asset;Tempest::VertexBuffer<Resources::Vertex> vbo[2];Tempest::IndexBuffer<uint16_t> ibo;bool visible=false;};
    HandMesh hand[2];
    Tempest::Texture2d albedo;
    Tempest::ZBuffer depth;
    Tempest::RenderPipeline pipeline,linePipeline,pickupPipeline;
    Tempest::VertexBuffer<Resources::Vertex> lineVbo[2];
    size_t lineCount=0;
    struct Object {const ProtoMesh* mesh;Matrix matrix;bool highlight=false;};
    std::vector<Object> objects;
    void init(Tempest::Device& device);
};
}
#endif
