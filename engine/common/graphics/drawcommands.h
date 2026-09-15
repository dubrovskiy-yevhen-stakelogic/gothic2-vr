#pragma once

#include <Tempest/RenderPipeline>
#include <Tempest/StorageBuffer>
#include <vector>

#include "sceneglobals.h"
#include "stereoseed.h"

class VisualObjects;
class DrawBuckets;
class DrawClusters;

class DrawCommands {
  public:
    enum Type : uint8_t {
      Landscape,
      Static,
      Movable,
      Animated,
      Pfx,
      Morph,
      };

    struct DrawCmd {
      const Tempest::RenderPipeline* pMain        = nullptr;
      const Tempest::RenderPipeline* pShadow      = nullptr;
      const Tempest::RenderPipeline* pVsm         = nullptr;
      const Tempest::RenderPipeline* pHiZ         = nullptr;
      const Tempest::RenderPipeline* pMainIndexed  = nullptr;
      const Tempest::RenderPipeline* pShadowIndexed= nullptr;
      uint32_t                       indexedFirst = 0;
      uint32_t                       indexedCount = 0;
      uint32_t                       indexedMeshlets = 0;
      bool                           indexedBindless = false;
      Type                           type         = Type::Landscape;

      Material::AlphaFunc            alpha        = Material::Solid;
      uint32_t                       firstPayload = 0;
      uint32_t                       maxPayload   = 0;

      // bindfull only
      uint32_t                       bucketId     = 0;

      bool                           isForwardShading() const;
      bool                           isShadowmapRequired() const;
      bool                           isSceneInfoRequired() const;
      bool                           isTextureInShadowPass() const;
      bool                           isBindless() const;
      bool                           isMeshShader() const;

      static constexpr bool supportsIndexedView(SceneGlobals::VisCamera view) {
        // Quest's depth seed is faster as one instanced draw per pipeline group.
        // Keep conversion/allocation and drawing on the same main/shadow policy.
        return view==SceneGlobals::V_Main || view==SceneGlobals::V_Shadow0 || view==SceneGlobals::V_Shadow1;
        }

      bool usesBatchedObjects(SceneGlobals::VisCamera view, bool enabled) const {
        // On Adreno, one instanced draw per material group beats hundreds of
        // tiny indexed draws. Keep other mesh types, slots and shadows intact.
        return enabled && view==SceneGlobals::V_Main && indexedBindless &&
               (type==Static || type==Movable) &&
               (alpha==Material::Solid || alpha==Material::AlphaTest);
        }

      const Tempest::RenderPipeline* indexedPipeline(SceneGlobals::VisCamera view, bool enabled, bool batchedObjects=false) const {
        if(!enabled || !supportsIndexedView(view) || usesBatchedObjects(view,batchedObjects))
          return nullptr;
        return view==SceneGlobals::V_Main ? pMainIndexed : pShadowIndexed;
        }
      };

    DrawCommands(VisualObjects& owner, DrawBuckets& buckets, DrawClusters& clusters, const SceneGlobals& scene);
    ~DrawCommands();

    const DrawCmd& operator[](size_t i) const { return cmd[i]; }
    size_t   maxMeshlets() const { return maxPayload; }
    bool     stereoSeedReused() const { return seedReused; }

    void     commit(Tempest::Encoder<Tempest::CommandBuffer>& cmd);
    uint16_t commandId(const Material& m, Type type, uint32_t bucketId, size_t indexFirst=0, size_t indexCount=0);
    void     addClusters(uint16_t cmdId, uint32_t meshletCount);

    void     visibilityPass(Tempest::Encoder<Tempest::CommandBuffer>& cmd, int pass, bool skipShadows=false, bool reuseLeftSeed=false);
    // VR cached shadow (vr/vrshadowcache.h): animated casters into V_Shadow0
    // every frame, one slice of the static commands into the V_Vsm view.
    void     drawShadowDynamic(Tempest::Encoder<Tempest::CommandBuffer>& cmd);
    void     drawShadowStatic (Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint32_t slice, uint32_t slices);
    uint32_t staticShadowCommands() const;
    // Per-cluster weights for balanced static-map slices: the
    // casters of drawShadowFiltered(animated=false) touching the map footprint.
    void     staticShadowWeights(const Tempest::Matrix4x4& build, float width, bool objectsOnly, std::vector<uint32_t>& out) const;
    uint64_t commandGeneration() const { return seedGeneration; } // bumps when the command list or its layout changes
    void     visibilityVsm(Tempest::Encoder<Tempest::CommandBuffer>& cmd);

    struct VisibilityStats {
      uint64_t active=0, frustum=0, visible=0, terrain=0, objectParts=0, models=0;
      uint64_t mainMaterialMeshlets[Material::AdditiveLight+1]={};
      uint64_t mainMaterialCommands[Material::AdditiveLight+1]={};
      uint64_t gbufferTerrain=0, gbufferStatic=0, gbufferMovable=0;
      uint64_t gbufferAnimated=0, gbufferMorph=0, gbufferPfx=0;
      uint64_t indexedMainMeshlets=0, indexedShadowMeshlets[Resources::ShadowLayers]={};
      uint64_t batchedMainMeshlets=0, batchedMainCommands=0;
      uint64_t seedVisible=0, seedTerrain=0, seedObjectParts=0;
      bool seedReused=false;
      uint64_t shadowVisible[Resources::ShadowLayers]={};
      uint64_t shadowTerrain[Resources::ShadowLayers]={};
      uint64_t shadowObjectParts[Resources::ShadowLayers]={};
    };
    // Diagnostic only: call after the graphics fence. Reads existing indirect
    // commands; no per-cluster atomics or readbacks in normal frames.
    VisibilityStats readVisibilityStats() const;

    void     drawHiZ(Tempest::Encoder<Tempest::CommandBuffer>& cmd, bool reuseLeftSeed=false);
    void     drawCommon(Tempest::Encoder<Tempest::CommandBuffer>& cmd, SceneGlobals::VisCamera viewId, Material::AlphaFunc func);

    void     drawVsm(Tempest::Encoder<Tempest::CommandBuffer>& cmd);

  private:
    enum TaskLinkpackage : uint8_t {
      T_Scene      = 0,
      T_Payload    = 1,
      T_Instance   = 2,
      T_Bucket     = 3,
      T_Indirect   = 4,
      T_Clusters   = 5,
      T_Lights     = 6,
      T_HiZ        = 7,
      T_VsmPages   = 8,
      T_CmdOffsets = 9,
      T_SeedIndirect = 10, // next eye's occluder seed (SEED_OUT main pass)
      T_SeedPayload  = 11,
      };

    enum UboLinkpackage : uint8_t {
      L_Scene      = 0,
      L_Payload    = 1,
      L_Instance   = 2,
      L_Pfx        = L_Instance,
      L_Bucket     = 3,
      L_Ibo        = 4,
      L_Vbo        = 5,
      L_Diffuse    = 6,
      L_Sampler    = 7,
      L_Shadow0    = 8,
      L_Shadow1    = 9,
      L_MorphId    = 10,
      L_Morph      = 11,
      L_SceneClr   = 12,
      L_GDepth     = 13,
      L_CmdOffsets = 14,
      L_VsmPages   = L_Shadow0,
      L_Lights     = L_Shadow1,
      };

    struct IndirectCmd {
      uint32_t vertexCount   = 0;
      uint32_t instanceCount = 0;
      uint32_t firstVertex   = 0;
      uint32_t firstInstance = 0;
      uint32_t writeOffset   = 0;
      };

    struct View {
      SceneGlobals::VisCamera viewport = SceneGlobals::V_Main;
      Tempest::StorageBuffer  visClusters, indirectCmd, indexedCmd, indexedBindlessCmd;
      Tempest::StorageBuffer  vsmClusters;
      };

    bool                     isViewEnabled(SceneGlobals::VisCamera v) const;
    void                     drawShadowFiltered(Tempest::Encoder<Tempest::CommandBuffer>& cmd, SceneGlobals::VisCamera viewId, bool animated, uint32_t first, uint32_t last);

    void                     setBindings(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const DrawCmd& cx, SceneGlobals::VisCamera viewId);

    VisualObjects&           owner;
    DrawBuckets&             buckets;
    DrawClusters&            clusters;
    const SceneGlobals&      scene;
    size_t                   maxPayload = 0;

    std::vector<DrawCmd>     cmd;
    std::vector<DrawCmd*>    ord;
    bool                     cmdDurtyBit = false;
    View                     views[SceneGlobals::V_Count];
    StereoSeedState          stereoSeed;
    uint64_t                 seedGeneration=0;
    bool                     seedReused=false;
    StereoSeedState::Snapshot seedSnapshot() const;
    bool                     seedBuffersReady() const;
    void                     initIndirect(Tempest::Encoder<Tempest::CommandBuffer>& cmd, SceneGlobals::VisCamera view);

    Tempest::StorageBuffer   indexedRanges;
    bool                     hasIndexedObjects=false, hasIndexedBindless=false, hasIndexedSlots=false;
    void prepareIndexed(Tempest::Encoder<Tempest::CommandBuffer>& cmd, SceneGlobals::VisCamera view);
    void drawIndexed(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const DrawCmd& draw, SceneGlobals::VisCamera view, size_t id);

    const bool               vsmSupported;
    Tempest::StorageBuffer   vsmIndirectCmd;
  };
