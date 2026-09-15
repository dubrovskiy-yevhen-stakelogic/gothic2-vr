#include "drawcommands.h"
#include "vr/vrshadowcache.h"

#include <Tempest/Log>
#include <unordered_set>

#include "graphics/mesh/submesh/animmesh.h"
#include "graphics/mesh/submesh/staticmesh.h"
#include "graphics/mesh/submesh/packedmesh.h"
#include "graphics/visualobjects.h"
#include "shaders.h"

#include "gothic.h"

using namespace Tempest;

static void usesSsbo(StorageBuffer& b, const size_t desiredSz) {
  if(b.byteSize()<desiredSz || b.byteSize()>=2*desiredSz) {
    Resources::recycle(std::move(b));
    b = Resources::device().ssbo(Tempest::Uninitialized, desiredSz);
    }
  }

bool DrawCommands::DrawCmd::isForwardShading() const {
  return Material::isForwardShading(alpha);
  }

bool DrawCommands::DrawCmd::isShadowmapRequired() const {
  return Material::isShadowmapRequired(alpha);
  }

bool DrawCommands::DrawCmd::isSceneInfoRequired() const {
  return Material::isSceneInfoRequired(alpha);
  }

bool DrawCommands::DrawCmd::isTextureInShadowPass() const {
  return Material::isTextureInShadowPass(alpha);
  }

bool DrawCommands::DrawCmd::isBindless() const {
  return bucketId==uint32_t(-1);
  }

bool DrawCommands::DrawCmd::isMeshShader() const {
  auto& opt = Gothic::inst().options();
  if(!opt.doMeshShading)
    return false;
  if(Material::isTesselated(alpha) && type==DrawCommands::Landscape && Resources::device().properties().tesselationShader)
    return false;
  return true;
  }


DrawCommands::DrawCommands(VisualObjects& owner, DrawBuckets& buckets, DrawClusters& clusters, const SceneGlobals& scene)
    : owner(owner), buckets(buckets), clusters(clusters), scene(scene), vsmSupported(Shaders::isVsmSupported()) {
  for(uint8_t v=0; v<SceneGlobals::V_Count; ++v) {
    views[v].viewport = SceneGlobals::VisCamera(v);
    }

  if(vsmSupported) {
    Tempest::DispatchIndirectCommand cmd = {2000,1,1};
    vsmIndirectCmd = Resources::device().ssbo(&cmd, sizeof(cmd));
    }
  // vsmSwrImage = Resources::device().image2d(TextureFormat::R16,  4096, 4096);
  // vsmSwrImage = Resources::device().image2d(TextureFormat::R32U, 4096, 4096);
  }

DrawCommands::~DrawCommands() {
  }

bool DrawCommands::isViewEnabled(SceneGlobals::VisCamera viewport) const {
  // VR cached shadow: the V_Vsm view builds the static map, V_Shadow1 is unused.
  if(viewport==SceneGlobals::V_Vsm && !(vsmSupported && scene.vsmEnabled) && !scene.vrStaticShadow)
    return false;
  if(viewport==SceneGlobals::V_Shadow1 && scene.vrStaticShadow)
    return false;
  if(viewport==SceneGlobals::V_Shadow0 && scene.shadowMap[0]->size()==Size(1,1))
    return false;
  if(viewport==SceneGlobals::V_Shadow1 && scene.shadowMap[1]->size()==Size(1,1))
    return false;
  return true;
  }

void DrawCommands::setBindings(Tempest::Encoder<CommandBuffer>& cmd, const DrawCmd& cx, SceneGlobals::VisCamera v) {
  static const DrawBuckets::Bucket nullBk;

  const auto  bId = cx.bucketId;
  const auto& bx  = cx.isBindless() ? nullBk : buckets.buckets()[bId];

  cmd.setBinding(L_Scene,    scene.uboGlobal[v]);
  cmd.setBinding(L_Payload,  views[v].visClusters);
  cmd.setBinding(L_Instance, owner.instanceSsbo());
  cmd.setBinding(L_Bucket,   buckets.ssbo());

  if(cx.isBindless()) {
    cmd.setBinding(L_Ibo, buckets.ibo());
    cmd.setBinding(L_Vbo, buckets.vbo());
    }
  else if(bx.staticMesh!=nullptr) {
    cmd.setBinding(L_Ibo, bx.staticMesh->ibo8);
    cmd.setBinding(L_Vbo, bx.staticMesh->vbo);
    }
  else {
    cmd.setBinding(L_Ibo, bx.animMesh->ibo8);
    cmd.setBinding(L_Vbo, bx.animMesh->vbo);
    }

  if(v==SceneGlobals::V_Main || cx.isTextureInShadowPass()) {
    if(cx.isBindless()) {
      cmd.setBinding(L_Diffuse, buckets.textures());
      }
    else if(bx.mat.hasFrameAnimation()) {
      uint64_t timeShift = 0;
      auto     frame     = size_t((timeShift+scene.tickCount)/bx.mat.texAniFPSInv);
      auto*    t         = bx.mat.frames[frame%bx.mat.frames.size()];
      cmd.setBinding(L_Diffuse, *t);
      }
    else {
      cmd.setBinding(L_Diffuse, *bx.mat.tex);
      }
    auto smp = SceneGlobals::isShadowView(v) ? Sampler::trillinear() : Sampler::anisotrophy();
    cmd.setBinding(L_Sampler, smp);
    }

  if(v==SceneGlobals::V_Main && cx.isShadowmapRequired()) {
    cmd.setBinding(L_Shadow0, *scene.shadowMap[0], Resources::shadowSampler());
    cmd.setBinding(L_Shadow1, *scene.shadowMap[1], Resources::shadowSampler());
    }

  if(cx.type==Morph && cx.isBindless()) {
    cmd.setBinding(L_MorphId,  buckets.morphId());
    cmd.setBinding(L_Morph,    buckets.morph());
    }
  else if(cx.type==Morph && bx.staticMesh!=nullptr) {
    cmd.setBinding(L_MorphId,  *bx.staticMesh->morph.index);
    cmd.setBinding(L_Morph,    *bx.staticMesh->morph.samples);
    }

  if(v==SceneGlobals::V_Main && cx.isSceneInfoRequired()) {
    cmd.setBinding(L_SceneClr, *scene.sceneColor, Sampler::bilinear(ClampMode::MirroredRepeat));
    cmd.setBinding(L_GDepth,   *scene.sceneDepth, Sampler::nearest (ClampMode::MirroredRepeat));
    }

  // Virtual shadow map bindings only: the VR cached shadow also draws through
  // the V_Vsm view with the plain shadow pipelines, and vsmPageList/lights are
  // null there (0.0.34 crashed on them).
  if(v==SceneGlobals::V_Vsm && scene.vsmEnabled) {
    cmd.setBinding(L_CmdOffsets, views[v].indirectCmd);
    cmd.setBinding(L_VsmPages,   *scene.vsmPageList);
    cmd.setBinding(L_Lights,     *scene.lights);
    }
  }

uint16_t DrawCommands::commandId(const Material& m, Type type, uint32_t bucketId, size_t indexFirst, size_t indexCount) {
  const bool bindlessSys = Gothic::inst().options().doBindless;
  const bool bindless    = bindlessSys && !m.hasFrameAnimation();

  uint32_t indexedFirst=0, indexedCount=0, indexedMeshlets=0;
  bool indexedBindless=false;
#if defined(__ANDROID__)
  const bool object = type==Static || type==Movable || type==Animated || type==Morph;
  const auto& caps=Resources::device().properties().indirect;
  if(object && !Gothic::options().doMeshShading && caps.indexed &&
     (m.alpha==Material::Solid || m.alpha==Material::AlphaTest) && !m.isGhost &&
     indexCount>0 && indexFirst%PackedMesh::MaxInd==0 && indexCount%PackedMesh::MaxInd==0) {
    const auto& bucket=buckets.buckets()[bucketId];
    const size_t available=bucket.staticMesh!=nullptr ? bucket.staticMesh->ibo.size() : bucket.animMesh->ibo.size();
    // One-off meshes can have a short real IBO and a padded meshlet payload.
    if(indexFirst<available && indexFirst<=UINT32_MAX && indexCount<=UINT32_MAX) {
      if(bindless) {
        indexedBindless=caps.count && caps.multiDraw && caps.firstInstance && buckets.enableIndexed(bucketId);
      } else {
        indexedFirst=uint32_t(indexFirst);
        indexedCount=uint32_t(std::min(indexCount,available-indexFirst));
        indexedMeshlets=uint32_t(indexCount/PackedMesh::MaxInd);
      }
      }
    }
#endif
  auto pMain    = Shaders::inst().materialPipeline(m, type, Shaders::T_Main,   bindless);
  auto pShadow  = Shaders::inst().materialPipeline(m, type, Shaders::T_Shadow, bindless);
  auto pVsm     = Shaders::inst().materialPipeline(m, type, Shaders::T_Vsm,    bindless);
  auto pHiZ     = Shaders::inst().materialPipeline(m, type, Shaders::T_Depth,  bindless);
  if(pMain==nullptr && pShadow==nullptr && pHiZ==nullptr)
    return uint16_t(-1);

  for(size_t i=0; i<cmd.size(); ++i) {
    if(cmd[i].pMain!=pMain || cmd[i].pShadow!=pShadow || cmd[i].pHiZ!=pHiZ)
      continue;
    if(!bindless && cmd[i].bucketId!=bucketId)
      continue;
    if(cmd[i].indexedBindless!=indexedBindless)
      continue;
    if(cmd[i].indexedFirst!=indexedFirst || cmd[i].indexedCount!=indexedCount || cmd[i].indexedMeshlets!=indexedMeshlets)
      continue;
    if(indexedCount>0 && cmd[i].type!=type)
      continue;
    return uint16_t(i);
    }

  auto ret = uint16_t(cmd.size());

  DrawCmd cx;
  cx.pMain       = pMain;
  cx.pShadow     = pShadow;
  cx.pVsm        = pVsm;
  cx.pHiZ        = pHiZ;
  cx.indexedFirst=indexedFirst;
  cx.indexedCount=indexedCount;
  cx.indexedMeshlets=indexedMeshlets;
  cx.indexedBindless=indexedBindless;
  if(indexedCount>0 || indexedBindless) {
    cx.pMainIndexed   = Shaders::inst().materialPipeline(m,type,Shaders::T_Main,bindless,true);
    cx.pShadowIndexed = Shaders::inst().materialPipeline(m,type,Shaders::T_Shadow,bindless,true);
    hasIndexedObjects=true;
    hasIndexedBindless|=indexedBindless;
    hasIndexedSlots|=indexedCount>0;
    }
  cx.bucketId    = bindless ? 0xFFFFFFFF : bucketId;
  cx.type        = type;
  cx.alpha       = m.alpha;
  cmd.push_back(std::move(cx));
  cmdDurtyBit = true;
  return ret;
  }

void DrawCommands::addClusters(uint16_t cmdId, uint32_t meshletCount) {
  cmdDurtyBit = true;
  cmd[cmdId].maxPayload += meshletCount;
  }

void DrawCommands::commit(Encoder<CommandBuffer>& enc) {
  bool cmdChg = false;
  for(auto& v:views) {
    if(isViewEnabled(v.viewport)) {
      cmdChg |= (v.indirectCmd.byteSize() != sizeof(IndirectCmd)*cmd.size());
      } else {
      Resources::recycle(std::move(v.indirectCmd));
      Resources::recycle(std::move(v.visClusters));
      Resources::recycle(std::move(v.indexedCmd));
      Resources::recycle(std::move(v.indexedBindlessCmd));
      Resources::recycle(std::move(v.vsmClusters));
      }
    }

  // Rebuilt command layouts/capacities make a previously recorded payload
  // unsuitable even when its buffers happen to keep the same byte size.
  if(cmdDurtyBit || cmdChg) {
    ++seedGeneration;
    stereoSeed.invalidate();
    }
  if(!cmdDurtyBit && !cmdChg)
    return;
  cmdDurtyBit = false;

  ord.resize(cmd.size());
  for(size_t i=0; i<cmd.size(); ++i)
    ord[i] = &cmd[i];
  std::sort(ord.begin(), ord.end(), [](const DrawCmd* l, const DrawCmd* r){
    return l->alpha < r->alpha;
    });

  size_t totalPayload = 0;
  bool   layChg       = false;
  for(auto& i:cmd) {
    layChg |= (i.firstPayload != uint32_t(totalPayload));
    i.firstPayload = uint32_t(totalPayload);
    totalPayload  += i.maxPayload;
    }

  totalPayload = (totalPayload + 0xFF) & ~size_t(0xFF);
  this->maxPayload = totalPayload;

  bool indexedCapacityChanged=false;
  if(hasIndexedBindless)
    for(const auto& v:views)
      if(isViewEnabled(v.viewport) && DrawCmd::supportsIndexedView(v.viewport) &&
         v.indexedBindlessCmd.byteSize()!=sizeof(Tempest::DrawIndexedIndirectCommand)*maxPayload)
        indexedCapacityChanged=true;
  if(!cmdChg && !layChg && !indexedCapacityChanged) {
    return;
    }

  std::vector<IndirectCmd> cx(cmd.size());
  for(size_t i=0; i<cmd.size(); ++i) {
    auto mesh = cmd[i].isMeshShader();

    cx[i].vertexCount   = PackedMesh::MaxInd;
    cx[i].writeOffset   = cmd[i].firstPayload;
    cx[i].firstVertex   = mesh ? 1 : 0;
    cx[i].firstInstance = mesh ? 1 : 0;
    }

  auto& device = Resources::device();
  size_t indexedGroups=0;
  for(const auto& c:cmd) if(c.indexedBindless) ++indexedGroups;
  if(hasIndexedBindless)
    Log::i("VR indexed bindless groups=",indexedGroups," totalGroups=",cmd.size()," maxPayload=",maxPayload,
           " nativeCount=",device.properties().indirect.count," firstInstance=",device.properties().indirect.firstInstance);
  if(hasIndexedSlots) {
    struct Range { uint32_t count,first,meshlets,reserved; };
    std::vector<Range> ranges;
    ranges.reserve(cmd.size());
    for(const auto& c:cmd) ranges.push_back({c.indexedCount,c.indexedFirst,c.indexedMeshlets,0});
    Resources::recycle(std::move(indexedRanges));
    indexedRanges=device.ssbo(ranges);
    }
  for(auto& v:views) {
    if(!isViewEnabled(v.viewport))
      continue;
    if(hasIndexedSlots && DrawCmd::supportsIndexedView(v.viewport) &&
       v.indexedCmd.byteSize()!=sizeof(Tempest::DrawIndexedIndirectCommand)*cmd.size()) {
      Resources::recycle(std::move(v.indexedCmd));
      v.indexedCmd=device.ssbo(Tempest::Uninitialized,sizeof(Tempest::DrawIndexedIndirectCommand)*cmd.size());
      }
    if(hasIndexedBindless && DrawCmd::supportsIndexedView(v.viewport) &&
       v.indexedBindlessCmd.byteSize()!=sizeof(Tempest::DrawIndexedIndirectCommand)*maxPayload) {
      Resources::recycle(std::move(v.indexedBindlessCmd));
      v.indexedBindlessCmd=device.ssbo(Tempest::Uninitialized,sizeof(Tempest::DrawIndexedIndirectCommand)*maxPayload);
    }
    if(v.indirectCmd.byteSize() != sizeof(IndirectCmd)*cmd.size()) {
      Resources::recycle(std::move(v.indirectCmd));
      v.indirectCmd = device.ssbo(cx.data(), sizeof(IndirectCmd)*cx.size());
      }
    else if(layChg) {
      auto staging = device.ssbo(BufferHeap::Upload, cx.data(), sizeof(IndirectCmd)*cx.size());

      enc.setBinding(0, v.indirectCmd);
      enc.setBinding(1, staging);
      enc.setPipeline(Shaders::inst().copyBuf);
      enc.dispatchThreads(staging.byteSize()/sizeof(uint32_t));

      Resources::recycle(std::move(staging));
      }
    }
  }

StereoSeedState::Snapshot DrawCommands::seedSnapshot() const {
  const auto size=scene.zbuffer->size();
  return {seedGeneration,scene.tickCount,cmd.size(),maxPayload,uint32_t(size.w),uint32_t(size.h)};
  }

bool DrawCommands::seedBuffersReady() const {
  const auto bytes=sizeof(IndirectCmd)*cmd.size();
  return bytes>0 && maxPayload>0 &&
         views[SceneGlobals::V_Main].indirectCmd.byteSize()==bytes &&
         views[SceneGlobals::V_HiZ].indirectCmd.byteSize()==bytes &&
         views[SceneGlobals::V_Main].visClusters.byteSize()>=maxPayload*4*sizeof(uint32_t);
  }

void DrawCommands::initIndirect(Encoder<CommandBuffer>& cmd, SceneGlobals::VisCamera viewport) {
  const uint32_t isMeshShader=(Gothic::options().doMeshShading ? 1 : 0);
  cmd.setBinding(T_Indirect,views[viewport].indirectCmd);
  cmd.setPushData(isMeshShader);
  cmd.setPipeline(Shaders::inst().clusterInit);
  cmd.dispatchThreads(this->cmd.size());
  }

void DrawCommands::visibilityPass(Encoder<CommandBuffer>& cmd, int pass, bool skipShadows, bool reuseLeftSeed) {
  static bool freeze = false;
  if(freeze)
    return;

  cmd.setFramebuffer({});
  if(pass==0) {
    seedReused=stereoSeed.beginEye(reuseLeftSeed,seedBuffersReady(),seedSnapshot());
    // With seedReused the V_HiZ buffers already hold this eye's seed: the
    // first eye's main pass (SEED_OUT variant) wrote its visible clusters that
    // pass the seed size rule there. Nothing to copy; the seed draw reads
    // V_HiZ as usual and the diagnostics read real counters.
    for(auto& v:views) {
      // Both eyes sample the first eye's head-centred shadow maps. Retain the
      // corresponding indirect buffers as well as skipping their unused rebuild.
      if(skipShadows && v.viewport>=SceneGlobals::V_Shadow0 && v.viewport<=SceneGlobals::V_ShadowLast)
        continue;
      if(v.viewport==SceneGlobals::V_Vsm && scene.vrStaticShadow && !scene.vrStaticShadowCull)
        continue; // the static map keeps its visible set until the next cull frame
      if(this->cmd.empty())
        continue;
      if(!isViewEnabled(v.viewport))
        continue;
      if(seedReused && (v.viewport==SceneGlobals::V_Main || v.viewport==SceneGlobals::V_HiZ))
        continue;
      initIndirect(cmd,v.viewport);
      }
    }
  else if(pass==1 && stereoSeed.isBorrowing()) {
    // The right seed has finished reading the left Main data. Its own main
    // visibility starts empty, and writes the payload only after those reads.
    initIndirect(cmd,SceneGlobals::V_Main);
    }

  for(uint8_t v=0; v<SceneGlobals::V_Count; ++v) {
    const auto viewport = SceneGlobals::VisCamera(v);
    if(skipShadows && viewport>=SceneGlobals::V_Shadow0 && viewport<=SceneGlobals::V_ShadowLast)
      continue;
    if(viewport==SceneGlobals::V_HiZ && pass!=0)
      continue;
    if(viewport==SceneGlobals::V_HiZ && (seedReused || scene.vrSkipSeedCull))
      continue;
    if(viewport!=SceneGlobals::V_HiZ && pass==0)
      continue;
    if(viewport==SceneGlobals::V_Vsm && !(scene.vrStaticShadow && scene.vrStaticShadowCull))
      continue;
    if(!isViewEnabled(viewport))
      continue;

    auto& view = views[viewport];

    const size_t visClustersSz = maxPayload*sizeof(uint32_t)*4;
    usesSsbo(view.visClusters, visClustersSz);

    struct Push {
      uint32_t firstMeshlet; uint32_t meshletCount; float znear;
      float objectFar, smallObjectFar, smallObjectRadius, minTanAngle, lodDistance;
      } push = {};
    push.firstMeshlet      = 0;
    push.meshletCount      = uint32_t(clusters.size());
    if(viewport==SceneGlobals::V_Vsm && scene.vrStaticShadow) {
      // VR static shadow map: one contiguous cluster range per frame. Balanced
      //: the renderer's range, sized by the casters inside the
      // map; the last slice also takes clusters appended since the cycle began.
      // Otherwise the raw index split of 0.0.37.
      uint32_t first=0, last=0;
      const uint32_t total = uint32_t(clusters.size());
      if(scene.vrStaticShadowBalanced) {
        last  = scene.vrStaticShadowSlice+1>=scene.vrStaticShadowSlices ? total : std::min(scene.vrStaticShadowLast, total);
        first = std::min(scene.vrStaticShadowFirst, last);
        } else {
        Vr::ShadowSlicer::range(scene.vrStaticShadowSlice, scene.vrStaticShadowSlices, total, first, last);
        }
      push.firstMeshlet = first;
      push.meshletCount = last-first;
      }
    push.znear             = scene.znear;
    push.objectFar         = scene.vrObjectFar;
    push.smallObjectFar    = scene.vrSmallObjectFar;
    push.smallObjectRadius = 750.f; // objects with a bounding radius under 7.5 m use the shorter distance
    push.minTanAngle       = scene.vrObjectMinTan;
    push.lodDistance       = scene.vrLodDistance;
    static_assert(sizeof(Push)==32);

    auto* pso = &Shaders::inst().visibilityPassSh;
    // The first eye's main pass also writes the second eye's occluder seed
    // (post-occlusion, size-filtered) into the V_HiZ buffers, which the seed
    // draw of this eye has already consumed at this point.
    const bool seedOut = viewport==SceneGlobals::V_Main && pass==1 && scene.vrSeedNextEye && !reuseLeftSeed &&
                         !Shaders::inst().visibilityPassHiZSeed.isEmpty() && isViewEnabled(SceneGlobals::V_HiZ);
    if(viewport==SceneGlobals::V_Main)
      pso = seedOut ? &Shaders::inst().visibilityPassHiZSeed : &Shaders::inst().visibilityPassHiZ;
    else if(viewport==SceneGlobals::V_HiZ)
      pso = &Shaders::inst().visibilityPassHiZCr;
    if(seedOut) {
      initIndirect(cmd,SceneGlobals::V_HiZ);
      usesSsbo(views[SceneGlobals::V_HiZ].visClusters, visClustersSz);
      }

    cmd.setBinding(T_Scene,    scene.uboGlobal[viewport]);
    cmd.setBinding(T_Payload,  view.visClusters);
    cmd.setBinding(T_Instance, owner.instanceSsbo());
    cmd.setBinding(T_Bucket,   buckets.ssbo());
    cmd.setBinding(T_Indirect, view.indirectCmd);
    cmd.setBinding(T_Clusters, clusters.ssbo());
    cmd.setBinding(T_HiZ,      *scene.hiZ);
    if(seedOut) {
      cmd.setBinding(T_SeedIndirect, views[SceneGlobals::V_HiZ].indirectCmd);
      cmd.setBinding(T_SeedPayload,  views[SceneGlobals::V_HiZ].visClusters);
      }
    cmd.setPushData(push);
    cmd.setPipeline(*pso);
    if(push.meshletCount>0)
      cmd.dispatchThreads(push.meshletCount);
    if(scene.vrIndexedObjects && hasIndexedObjects) prepareIndexed(cmd,viewport);
    }
  if(pass==1 && !reuseLeftSeed)
    stereoSeed.mainReady(seedSnapshot(),seedBuffersReady());
  }

DrawCommands::VisibilityStats DrawCommands::readVisibilityStats() const {
  VisibilityStats stats;
  stats.seedReused=seedReused;
  for(size_t i=0; i<clusters.size(); ++i) {
    const auto& c=clusters[i];
    if(c.r<=0 || c.commandId>=cmd.size() || cmd[c.commandId].pMain==nullptr)
      continue;
    const uint32_t count=c.meshletCount & 0xFFFFFFu; // top byte: terrain LOD level
    stats.active+=count;
    if(scene.frustrum[SceneGlobals::V_Main].testPoint(c.pos,c.r))
      stats.frustum+=count;
  }
  const auto& buffer=views[SceneGlobals::V_Main].indirectCmd;
  if(cmd.empty() || buffer.byteSize()!=cmd.size()*sizeof(IndirectCmd))
    return stats;
  std::vector<IndirectCmd> counts(cmd.size());
  Resources::device().readBytes(buffer,counts.data(),buffer.byteSize());
  for(size_t i=0; i<cmd.size(); ++i)
    if(cmd[i].pMain!=nullptr) {
      stats.visible+=counts[i].instanceCount;
      if(cmd[i].indexedPipeline(SceneGlobals::V_Main,scene.vrIndexedObjects,scene.vrBatchedObjects)!=nullptr)
        stats.indexedMainMeshlets+=counts[i].instanceCount;
      if(cmd[i].type==Landscape) stats.terrain+=counts[i].instanceCount;
      else stats.objectParts+=counts[i].instanceCount;
      // Reuse this diagnostic readback, matching drawCommon's submission guard.
      // PfxBucket particles/trails have a separate CPU draw path and are not
      // represented by these indirect commands.
      const auto& draw=cmd[i];
      if(draw.maxPayload==0) continue;
      const uint64_t n=counts[i].instanceCount;
      if(draw.usesBatchedObjects(SceneGlobals::V_Main,scene.vrBatchedObjects)) {
        stats.batchedMainMeshlets+=n;
        stats.batchedMainCommands+=(n>0 ? 1 : 0);
      }
      if(size_t(draw.alpha)<=size_t(Material::AdditiveLight)) {
        stats.mainMaterialMeshlets[draw.alpha]+=n;
        stats.mainMaterialCommands[draw.alpha]+=(n>0 ? 1 : 0);
      }
      if(draw.alpha!=Material::Solid && draw.alpha!=Material::AlphaTest) continue;
      switch(draw.type) {
        case Landscape: stats.gbufferTerrain+=n; break;
        case Static:    stats.gbufferStatic+=n; break;
        case Movable:   stats.gbufferMovable+=n; break;
        case Animated:  stats.gbufferAnimated+=n; break;
        case Morph:     stats.gbufferMorph+=n; break;
        case Pfx:       stats.gbufferPfx+=n; break;
      }
    }
  // A meshlet is not an object. Count distinct visible model-instance IDs
  // from the existing payload only on these infrequent diagnostic frames.
  if(stats.objectParts>0) {
    const auto& visible=views[SceneGlobals::V_Main].visClusters;
    std::vector<uint32_t> payload(visible.byteSize()/sizeof(uint32_t));
    Resources::device().readBytes(visible,payload.data(),visible.byteSize());
    std::unordered_set<uint32_t> instances;
    for(size_t i=0;i<cmd.size();++i) {
      if(cmd[i].pMain==nullptr || cmd[i].type==Landscape) continue;
      const uint64_t first=counts[i].writeOffset, end=first+counts[i].instanceCount;
      if(end>payload.size()/4) continue;
      for(uint64_t at=first;at<end;++at)
        if(payload[at*4]!=0xFFFFFFFFu) instances.insert(payload[at*4]);
    }
    stats.models=instances.size();
  }
  // Count only commands which the relevant draw function actually submits.
  // Payloads are already compacted by the GPU; no extra compute or readback is
  // needed in ordinary frames. Shared shadow counts remain valid in both eyes.
  auto readPass=[&](SceneGlobals::VisCamera viewport, bool seed,
                    uint64_t& total, uint64_t& terrain, uint64_t& objectParts) {
    const auto& indirect=views[viewport].indirectCmd;
    if(!isViewEnabled(viewport) || indirect.byteSize()!=counts.size()*sizeof(IndirectCmd))
      return;
    Resources::device().readBytes(indirect,counts.data(),indirect.byteSize());
    for(size_t i=0;i<cmd.size();++i) {
      const auto& draw=cmd[i];
      if(draw.maxPayload==0 || (draw.alpha!=Material::Solid && draw.alpha!=Material::AlphaTest))
        continue;
      if(seed) {
        if(draw.pHiZ==nullptr || (draw.type!=Landscape && draw.type!=Static))
          continue;
      } else if(draw.pShadow==nullptr) {
        continue;
      }
      total+=counts[i].instanceCount;
      if(!seed && draw.indexedPipeline(viewport,scene.vrIndexedObjects,scene.vrBatchedObjects)!=nullptr)
        stats.indexedShadowMeshlets[viewport-SceneGlobals::V_Shadow0]+=counts[i].instanceCount;
      if(draw.type==Landscape) terrain+=counts[i].instanceCount;
      else objectParts+=counts[i].instanceCount;
    }
  };
  readPass(SceneGlobals::V_HiZ,true,stats.seedVisible,stats.seedTerrain,stats.seedObjectParts);
  for(uint8_t layer=0;layer<Resources::ShadowLayers;++layer)
    readPass(SceneGlobals::VisCamera(SceneGlobals::V_Shadow0+layer),false,
             stats.shadowVisible[layer],stats.shadowTerrain[layer],stats.shadowObjectParts[layer]);
  return stats;
}

void DrawCommands::visibilityVsm(Encoder<CommandBuffer>& cmd) {
  auto& shaders = Shaders::inst();

  auto& view = views[SceneGlobals::V_Vsm];
  const size_t vsmMax = 1024*1024*4*4; // arbitrary: ~1k clusters per page
  const size_t visClustersSz = vsmMax*sizeof(uint32_t)*4;

  usesSsbo(view.vsmClusters, visClustersSz);
  usesSsbo(view.visClusters, visClustersSz);

  struct Push { uint32_t meshletCount; } push = {};
  push.meshletCount = uint32_t(clusters.size());

  cmd.setBinding(T_Scene,    scene.uboGlobal[SceneGlobals::V_Vsm]);
  cmd.setBinding(T_Payload,  view.vsmClusters); //unsorted clusters
  cmd.setBinding(T_Instance, owner.instanceSsbo());
  cmd.setBinding(T_Bucket,   buckets.ssbo());
  cmd.setBinding(T_Indirect, view.indirectCmd);
  cmd.setBinding(T_Clusters, clusters.ssbo());
  cmd.setBinding(T_Lights,   *scene.lights);
  cmd.setBinding(T_HiZ,      *scene.vsmPageHiZ);
  cmd.setBinding(T_VsmPages, *scene.vsmPageList);
  cmd.setBinding(9,          scene.vsmDbg);
  cmd.setPushData(&push, sizeof(push));
  cmd.setPipeline(shaders.vsmVisibilityPass);
  cmd.dispatchThreads(push.meshletCount, size_t(scene.vsmPageTbl->d() + 1));

  cmd.setBinding(1, view.vsmClusters);
  cmd.setBinding(2, view.visClusters);
  cmd.setBinding(3, view.indirectCmd);
  cmd.setBinding(4, *scene.vsmPageList);
  cmd.setBinding(5, vsmIndirectCmd);
  cmd.setPipeline(shaders.vsmPackDraw0);
  cmd.dispatch(1);

  cmd.setBinding(1, view.vsmClusters);
  cmd.setBinding(2, view.visClusters);
  cmd.setBinding(3, view.indirectCmd);
  cmd.setBinding(4, *scene.vsmPageList);
  cmd.setPipeline(shaders.vsmPackDraw1);
  cmd.dispatch(8096); // TODO: indirect
  // cmd.dispatchIndirect(vsmIndirectCmd, 0);
  }

void DrawCommands::drawVsm(Tempest::Encoder<Tempest::CommandBuffer>& cmd) {
  // return;
  struct Push { uint32_t commandId; } push = {};

  auto  viewId = SceneGlobals::V_Vsm;
  auto& view   = views[viewId];

  for(size_t i=0; i<ord.size(); ++i) {
    auto& cx = *ord[i];
    if(cx.alpha!=Material::Solid && cx.alpha!=Material::AlphaTest)
      continue;
    if(cx.pVsm==nullptr)
      continue;

    auto id  = size_t(std::distance(this->cmd.data(), &cx));
    push.commandId = uint32_t(id);

    // cmd.setUniforms(*cx.pVsm, desc[viewId], &push, sizeof(push));
    setBindings(cmd, cx, viewId);
    cmd.setPushData(push);
    cmd.setPipeline(*cx.pVsm);
    if(cx.isMeshShader())
      cmd.dispatchMeshIndirect(view.indirectCmd, sizeof(IndirectCmd)*id + sizeof(uint32_t)); else
      cmd.drawIndirect(view.indirectCmd, sizeof(IndirectCmd)*id);
    }

  if(false) {
    struct Push { uint32_t meshletCount; } push = {};
    push.meshletCount = uint32_t(clusters.size());

    cmd.setFramebuffer({});
    cmd.setBinding(0, *scene.vsmPageData);
    cmd.setBinding(1, scene.uboGlobal[SceneGlobals::V_Vsm]);
    cmd.setBinding(2, *scene.vsmPageList);
    cmd.setBinding(3, clusters.ssbo());
    cmd.setBinding(4, owner.instanceSsbo());
    cmd.setBinding(5, buckets.ibo());
    cmd.setBinding(6, buckets.vbo());
    cmd.setBinding(7, buckets.textures());
    cmd.setBinding(8, Sampler::bilinear());
    cmd.setPushData(&push, sizeof(push));
    cmd.setPipeline(Shaders::inst().vsmRendering);
    // const auto sz = Shaders::inst().vsmRendering.workGroupSize();
    cmd.dispatch(1024u);
    }
  }

void DrawCommands::drawHiZ(Tempest::Encoder<Tempest::CommandBuffer>& cmd, bool reuseLeftSeed) {
  // return;
  struct Push { uint32_t firstMeshlet; uint32_t meshletCount; } push = {};

  auto  viewId = SceneGlobals::V_HiZ;
  // A borrowed seed (stereo reuse) lives in the V_HiZ buffers too: the first
  // eye's main pass wrote it there, size-filtered like this eye's own seed.
  (void)reuseLeftSeed;
  auto& view   = views[viewId];

  for(size_t i=0; i<ord.size(); ++i) {
    auto& cx = *ord[i];
    if(cx.alpha!=Material::Solid && cx.alpha!=Material::AlphaTest)
      continue;
    if(cx.type!=Landscape && cx.type!=Static)
      continue;
    if(cx.pHiZ==nullptr)
      continue;

    auto id  = size_t(std::distance(this->cmd.data(), &cx));
    push.firstMeshlet = cx.firstPayload;
    push.meshletCount = cx.maxPayload;

    setBindings(cmd, cx, viewId);
    cmd.setPushData(push);
    cmd.setPipeline(*cx.pHiZ);
    if(cx.isMeshShader())
      cmd.dispatchMeshIndirect(view.indirectCmd, sizeof(IndirectCmd)*id + sizeof(uint32_t)); else
      cmd.drawIndirect(view.indirectCmd, sizeof(IndirectCmd)*id);
    }
  }

void DrawCommands::drawCommon(Tempest::Encoder<Tempest::CommandBuffer>& cmd, SceneGlobals::VisCamera viewId, Material::AlphaFunc func) {
  struct Push { uint32_t firstMeshlet; uint32_t meshletCount; } push = {};

  auto b = std::lower_bound(ord.begin(), ord.end(), func, [](const DrawCmd* l, Material::AlphaFunc f){
    return l->alpha < f;
    });
  auto e = std::upper_bound(ord.begin(), ord.end(), func, [](Material::AlphaFunc f, const DrawCmd* r){
    return f < r->alpha;
    });

  auto& view = views[viewId];
  for(auto i=b; i!=e; ++i) {
    auto& cx = **i;
    if(cx.alpha!=func)
      continue;

    if(cx.maxPayload==0)
      continue;

    const RenderPipeline* pso = nullptr;
    switch(viewId) {
      case SceneGlobals::V_Shadow0:
      case SceneGlobals::V_Shadow1:
      case SceneGlobals::V_Vsm:
        pso = cx.pShadow;
        break;
      case SceneGlobals::V_Main:
        pso = cx.pMain;
        break;
      case SceneGlobals::V_HiZ:
      case SceneGlobals::V_Count:
        break;
      }
    const auto* indexedPso=cx.indexedPipeline(viewId,scene.vrIndexedObjects,scene.vrBatchedObjects);
    if(pso==nullptr)
      continue;

    auto id  = size_t(std::distance(this->cmd.data(), &cx));
    push.firstMeshlet = cx.firstPayload;
    push.meshletCount = indexedPso!=nullptr ? cx.indexedMeshlets : cx.maxPayload;

    setBindings(cmd, cx, viewId);
    cmd.setPushData(push);
    cmd.setPipeline(indexedPso!=nullptr ? *indexedPso : *pso);
    if(indexedPso!=nullptr) {
      drawIndexed(cmd,cx,viewId,id);
      continue;
      }
    if(cx.isMeshShader())
      cmd.dispatchMeshIndirect(view.indirectCmd, sizeof(IndirectCmd)*id + sizeof(uint32_t)); else
      cmd.drawIndirect(view.indirectCmd, sizeof(IndirectCmd)*id);
    }
  }

// VR cached shadow (vr/vrshadowcache.h). The dynamic map takes the animated
// casters (skinned and morph meshes) every frame; the static map takes one
// contiguous slice of the landscape/static/movable commands per frame through
// the V_Vsm view. Particles cast no shadow in either map.
void DrawCommands::drawShadowFiltered(Encoder<CommandBuffer>& cmd, SceneGlobals::VisCamera viewId, bool animated, uint32_t first, uint32_t last) {
  struct Push { uint32_t firstMeshlet; uint32_t meshletCount; } push = {};
  auto&    view  = views[viewId];
  uint32_t index = 0;
  for(size_t i=0; i<ord.size(); ++i) {
    auto& cx = *ord[i];
    if(cx.alpha!=Material::Solid && cx.alpha!=Material::AlphaTest)
      continue;
    if(cx.type==Pfx)
      continue;
    const bool isAnimated = (cx.type==Animated || cx.type==Morph);
    if(isAnimated!=animated)
      continue;
    if(!animated && scene.vrStaticShadowObjectsOnly && cx.type==Landscape)
      continue; // 2002 static lighting: terrain self-shadowing comes from the bake
    const uint32_t at = index++;
    if(at<first || at>=last)
      continue;
    if(cx.maxPayload==0 || cx.pShadow==nullptr)
      continue;
    // The indexed routes are prepared for the V_Shadow views only.
    const auto* indexedPso = DrawCmd::supportsIndexedView(viewId) ? cx.indexedPipeline(viewId,scene.vrIndexedObjects,scene.vrBatchedObjects) : nullptr;
    auto id  = size_t(std::distance(this->cmd.data(), &cx));
    push.firstMeshlet = cx.firstPayload;
    push.meshletCount = indexedPso!=nullptr ? cx.indexedMeshlets : cx.maxPayload;
    setBindings(cmd, cx, viewId);
    cmd.setPushData(push);
    cmd.setPipeline(indexedPso!=nullptr ? *indexedPso : *cx.pShadow);
    if(indexedPso!=nullptr) {
      drawIndexed(cmd,cx,viewId,id);
      continue;
      }
    if(cx.isMeshShader())
      cmd.dispatchMeshIndirect(view.indirectCmd, sizeof(IndirectCmd)*id + sizeof(uint32_t)); else
      cmd.drawIndirect(view.indirectCmd, sizeof(IndirectCmd)*id);
    }
  }

uint32_t DrawCommands::staticShadowCommands() const {
  uint32_t n = 0;
  for(const auto* c:ord)
    if((c->alpha==Material::Solid || c->alpha==Material::AlphaTest) && c->type!=Animated && c->type!=Morph && c->type!=Pfx)
      ++n;
  return n;
  }

void DrawCommands::staticShadowWeights(const Matrix4x4& build, float width, bool objectsOnly, std::vector<uint32_t>& out) const {
  // The caster rule of drawShadowFiltered(animated=false), per command.
  std::vector<uint8_t> caster(cmd.size(), 0);
  for(size_t i=0; i<cmd.size(); ++i) {
    const auto& cx    = cmd[i];
    const bool  solid = (cx.alpha==Material::Solid || cx.alpha==Material::AlphaTest);
    const bool  type  = cx.type!=Animated && cx.type!=Morph && cx.type!=Pfx && !(objectsOnly && cx.type==Landscape);
    caster[i] = (solid && type && cx.maxPayload>0 && cx.pShadow!=nullptr) ? 1 : 0;
    }
  out.resize(clusters.size());
  for(size_t i=0; i<clusters.size(); ++i) {
    const auto& c = clusters[i];
    const bool  inMap = c.r>0 && c.commandId<cmd.size() && caster[c.commandId]!=0 &&
                        Vr::ShadowSlicer::touchesMap(build, c.pos, c.r, width);
    out[i] = Vr::ShadowSlicer::clusterWeight(inMap, c.meshletCount & 0xFFFFFFu); // top byte: terrain LOD level
    }
  }

void DrawCommands::drawShadowDynamic(Encoder<CommandBuffer>& cmd) {
  drawShadowFiltered(cmd, SceneGlobals::V_Shadow0, true, 0, 0xFFFFFFFFu);
  }

void DrawCommands::drawShadowStatic(Encoder<CommandBuffer>& cmd, uint32_t slice, uint32_t slices) {
  uint32_t first=0, last=0;
  Vr::ShadowSlicer::range(slice, slices, staticShadowCommands(), first, last);
  drawShadowFiltered(cmd, SceneGlobals::V_Vsm, false, first, last);
  }

void DrawCommands::prepareIndexed(Encoder<CommandBuffer>& cmd, SceneGlobals::VisCamera viewport) {
  // HiZ consumes the original batched indirect commands. Converting its payload
  // would waste compute work and force a barrier for data that no draw reads.
  if(!DrawCmd::supportsIndexedView(viewport))
    return;
  auto& view=views[viewport];
  if(hasIndexedSlots) {
    cmd.setBinding(0,view.indirectCmd);
    cmd.setBinding(1,indexedRanges);
    cmd.setBinding(2,view.indexedCmd);
    cmd.setPipeline(Shaders::inst().indexedObjects);
    cmd.dispatchThreads(this->cmd.size());
  }
  if(hasIndexedBindless) {
    cmd.setBinding(0,view.indirectCmd);
    cmd.setBinding(1,view.visClusters);
    cmd.setBinding(2,buckets.indexedRanges());
    cmd.setBinding(3,view.indexedBindlessCmd);
    cmd.setBinding(4,buckets.ibo());
    cmd.setPipeline(Shaders::inst().indexedBindless);
    for(uint32_t i=0;i<this->cmd.size();++i) {
      const auto& draw=this->cmd[i];
      if(!draw.indexedBindless || draw.maxPayload==0 || draw.indexedPipeline(viewport,true,scene.vrBatchedObjects)==nullptr) continue;
      cmd.setPushData(i);
      cmd.dispatchThreads(draw.maxPayload);
    }
  }
  }

void DrawCommands::drawIndexed(Encoder<CommandBuffer>& cmd, const DrawCmd& draw, SceneGlobals::VisCamera viewport, size_t id) {
  if(draw.indexedBindless) {
    auto& view=views[viewport];
    cmd.drawIndexedIndirectCount(buckets.indexedPool(),view.indexedBindlessCmd,
                                 draw.firstPayload*sizeof(Tempest::DrawIndexedIndirectCommand),
                                 view.indirectCmd,id*sizeof(IndirectCmd)+offsetof(IndirectCmd,instanceCount),draw.maxPayload);
    return;
  }
  const auto& bucket=buckets.buckets()[draw.bucketId];
  const auto& ibo=bucket.staticMesh!=nullptr ? bucket.staticMesh->ibo : bucket.animMesh->ibo;
  cmd.drawIndexedIndirect(ibo,views[viewport].indexedCmd,id*sizeof(Tempest::DrawIndexedIndirectCommand));
  }
