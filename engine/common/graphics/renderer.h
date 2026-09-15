#pragma once
#include <functional>

#include <Tempest/RenderPipeline>
#include <Tempest/CommandBuffer>
#include <Tempest/Matrix4x4>
#include <Tempest/Widget>
#include <Tempest/Device>
#include <Tempest/UniformBuffer>
#include <Tempest/VectorImage>

#include "worldview.h"
#include "vr/vrshadowcache.h"
#include "vr/vrskyrate.h"
#include "shaders.h"

class Camera;
class InventoryMenu;
class VideoWidget;

class Renderer final {
  public:
    Renderer();
    ~Renderer();

    void onWorldChanged();
    void setGizmo(bool enable, Tempest::Vec3 center);
    void setLightsHud(const Tempest::Texture2d* tex);

    void draw(Tempest::Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t fId,
              Tempest::VectorImage::Mesh& uiLayer, Tempest::VectorImage::Mesh& numOverlay,
              InventoryMenu &inventory, VideoWidget& video, float hdrPeakNits = 0);
    void draw(Tempest::Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t fId,
              WorldView& view, const Camera& camera,const Camera* shadowCamera=nullptr,bool secondEye=false,
              Tempest::Encoder<Tempest::CommandBuffer>* prep=nullptr, const std::function<void()>& onPrepared={});

    void dbgDraw(Tempest::Painter& painter);
    const Tempest::ZBuffer& vrDepthBuffer() const { return zbuffer; }
    // VR per-eye overlay (hands, held items): drawn inside the tonemapping
    // pass with its own cleared, discarded depth attachment, so it costs no
    // extra load/store of the eye image. Cleared by the caller after draw().
    struct VrOverlay { Tempest::ZBuffer* depth=nullptr; std::function<void(Tempest::Encoder<Tempest::CommandBuffer>&)> draw; };
    void setVrOverlay(VrOverlay overlay) { vrOverlay=std::move(overlay); }
    void setVrRenderScale(float scale) { vrRenderScale=scale; }
    // VR render scale as a sub-rectangle of the eye image: at
    // scale < 1 a world eye tonemaps at the internal size into the top-left
    // vrEyeRect(target) of its target (viewport, scissor, render area) and the
    // projection layer shows only that rectangle: no upscale pass, the fog fold
    // stays. setVrEyeTarget(true) applies to the next world draw only; menu and
    // cinema draws keep the upscale into the whole image. Gothic.ini [ENGINE]
    // vrScaleRectOff=1 restores the upscale for the eyes too.
    void setVrEyeTarget(bool eye) { vrEyeTarget=eye; }
    Tempest::Size vrEyeRect(Tempest::Size target) const;
    void setVrFogMode(int mode);
    void setVrIndexedObjects(bool enabled) { vrIndexedObjects=enabled; }
    void setVrBatchedObjects(bool enabled) { vrBatchedObjects=enabled; }
    // 0 = unlimited, 1 = 300 m, 2 = 200 m, 3 = 120 m (small objects a third of it)
    void setVrObjectDistance(int level) { vrObjectDistance=level; }
    void setVrTerrainLod(int level) { vrTerrainLod=level; }
    void setVrFoveation(int level) { vrFoveation=level; }
    void setVrStaticLighting(bool on) { vrStaticLighting=on; }
    void setVrHorizonHaze(bool enabled) { vrHorizonHaze=enabled; }
    void setVrFastLighting(bool enabled) { vrFastLighting=enabled; }
    void setVrLightDepth(bool enabled) { vrLightDepth=enabled; }
    bool vrUsesLightDepth() const { return vrLightDepthActive; }
    void setVrMergedTransparency(bool enabled) { vrMergedTransparency=enabled; }
    bool vrUsesMergedTransparency() const { return vrMergedTransparencyActive; }
    void setVrShadows(bool enabled) { if(vrShadows!=enabled) vrShadowClearNeeded=true; vrShadows=enabled; }
    void setVrLighting(bool enabled) { vrLighting=enabled; }
    void setVrLocalLights(bool enabled) { vrLocalLights=enabled; }
    void setVrTiledLights(bool enabled) { vrTiledLights=enabled; }
    void setVrSlabLights(bool enabled) { vrSlabLights=enabled; }
    bool vrUsesSlabLights() const { return vrSlabLightsActive; }
    // Per-eye slab route request from the VR window's measured-cost controller
    // (vr/lightroutecontroller.h); applied with the next prepareVisibleLights
    // of that eye when Light slabs is On.
    void  setVrSlabRoute(bool secondEye, bool slabs) { vrSlabRouteRequest[secondEye ? 1 : 0]=slabs; }
    // Camera-side prior of the last prepared eye: summed squared projected
    // coverage of the lights at or above the per-light threshold.
    float vrSlabGateWeight() const { return vrSlabGateWeights[vrSlabGateEye]; }
    bool  vrSlabRouteRequested() const { return vrSlabRouteRequest[vrSlabGateEye]; }
    // Diagnostic frames only, after the eye fence: per-light screen coverage
    // measured from the actual slab masks of the last prepared eye.
    struct VrLightSlabStats {
      struct Light { float range=0, distance=0, coverage=0; };
      uint32_t lights=0, tiles=0, litTiles=0, drawnTiles=0, maxPerTile=0;
      double   averagePerLitTile=0;
      std::vector<Light> detail;
      };
    VrLightSlabStats readVrLightSlabStats() const;
    uint32_t readVrSlabLightCount() const;
    void setVrSubgroupTiles(bool enabled) { vrSubgroupTiles=enabled; }
    void setVrUniformLights(bool enabled) { vrUniformLights=enabled; }
    bool vrUsesUniformLights() const { return vrUniformLightsReady; }
    void setVrStereoSeed(bool enabled) { vrStereoSeed=enabled; }
    bool vrUsesSubgroupTiles() const { return vrSubgroupTilesReady; }
    bool vrUsesTiledLights() const { return vrLightTilesReady; }
    uint32_t readVrVisibleLightCount() const;
    struct VrCpuStats { double update=0,uploads=0,encode=0; };
    VisualObjects::StageCpuStats stageVrCpu(WorldView& view,uint64_t tickCount);
    VrCpuStats vrCpuStats() const { return vrCpu; }
    void setVrDiagnostics(bool enabled) { vrDiagnostics=enabled; }
    struct FogWorkStats { uint32_t columns[33]={}, groups[33]={}; };
    FogWorkStats readVrFogWorkStats() const;

    Tempest::Attachment screenshoot(uint8_t frameId);

  private:
    VrCpuStats vrCpu;
    float vrRenderScale=0;
    int vrFogMode=-1;
    bool vrFastLighting=true;
    bool vrLightDepth=true, vrLightDepthActive=false;
    bool vrMergedTransparency=true, vrMergedTransparencyActive=false;
    bool vrIndexedObjects=true;
    bool vrBatchedObjects=true;
    int  vrObjectDistance=3;
    int  vrTerrainLod=2; // 0 full, 1 far (200 m), 2 near (120 m), 3 aggressive (80 m)
    // Fixed foveation (VK_EXT_fragment_density_map): 0 off, 1 low, 2 medium, 3 high
    int  vrFoveation=2;
    Tempest::Texture2d vrFdm; bool vrFdmValid=false; int vrFdmLevel=-1; uint32_t vrFdmW=0, vrFdmH=0; bool vrFdmLogged=false;
    void prepareVrFoveation(Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint32_t w, uint32_t h);
    VrOverlay vrOverlay;
    bool vrHorizonHaze=true;
    // VR: the reflections+fog composite pass folded into tonemapping (Gothic.ini
    // [ENGINE] vrFogFoldOff=1 restores the separate pass); active per frame when
    // the fog is the LQ/None path, the camera is above water and no upscale/AA.
    bool vrFogFold=true, vrFogFoldActive=false, vrFogFoldLogged=false;
    // vrScaleRect = the Gothic.ini switch (Android default on),
    // vrScaleRectActive = this draw tonemaps into the sceneLinear-sized
    // top-left rectangle of the eye target.
    bool vrScaleRect=false, vrEyeTarget=false, vrScaleRectActive=false, vrScaleRectLogged=false;
    Tempest::Size vrScaleRectSize;
    bool vrShadows=true, vrLighting=true, vrLocalLights=true, vrShadowClearNeeded=true;
    // VR cached shadow (vr/vrshadowcache.h): shadowMap[0] = animated casters
    // every frame, shadowMap[1] = the static world built over vrShadowSlicer.slices
    // frames into vrShadowBack (V_Vsm view) and swapped in at the next frame.
    Vr::ShadowSlicer       vrShadowSlicer;
    Vr::ShadowSlicer::Step vrShadowStep;
    Tempest::ZBuffer       vrShadowBack;
    Tempest::Matrix4x4     vrShadowFront, vrShadowBuilt, vrShadowDynamic, vrShadowStaticMat;
    bool  vrShadowActive=false, vrShadowSunUp=false, vrShadowFrontValid=false, vrShadowSwapPending=false, vrShadowLogged=false;
    // On demand with balanced slices: Gothic.ini
    // vrShadowOnDemandOff=1 restores the always-cycling slicer with raw ranges.
    bool  vrShadowBalanced=false;
    std::vector<uint32_t> vrShadowWeights, vrShadowBounds; // per-cluster weights, slice bounds of the cycle in progress
    uint32_t vrShadowReasons[Vr::ShadowSlicer::R_Count]={}, vrShadowRebuilds=0;
    float vrShadowStaticWidth=6400.f, vrShadowDynamicWidth=3200.f, vrShadowDepth=30000.f; // cm
    // Exact shadow early-out (lighting/shadow_tiles.comp): per
    // 8x8-texel tile the min/max depth of the tile and a 4-texel margin; the
    // direct light skips the PCF where a tile is all lit or all shadowed. Tile 1
    // is rebuilt from shadowMap[1] after each swap, so it cannot drift from it.
    // Gothic.ini vrShadowTilesOff=1; vrShadowNightSkipOff=1 keeps the shadow
    // variant at night (both maps are cleared then).
    Tempest::StorageImage vrShadowTile[Resources::ShadowLayers];
    bool  vrShadowTiles=true, vrShadowNightSkip=true, vrShadowTilesActive=false;
    bool  vrShadowTileDynamicValid=false; // vrShadowTile[0] holds this frame's map 0
    bool  vrShadowTileStaticValid=false;  // vrShadowTile[1] holds the current shadowMap[1]
    bool  vrShadowStaticClear=false;      // shadowMap[1] holds the night clear (all lit)
    bool  vrShadowDynamicEmpty=false;     // no animated caster can reach map 0 this frame (cleared only)
    uint32_t vrShadowTileBuilds=0;        // static tile builds (log)
    bool  vrBakedShadow=true; // Gothic.ini [ENGINE] vrBakedShadowOff=1 disables the landscape baked sun visibility (free)
    bool  vrStaticLighting=true; // Performance -> Lighting: 2002 static (bake lights the landscape, static lights off, no static shadow map)
    void prepareVrShadow(WorldView& wview, const Camera& shadowView, bool secondEye);
    void drawVrShadow(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& view);
    void buildVrShadowTiles(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const Tempest::ZBuffer& map, Tempest::StorageImage& tiles);
    bool vrLightsCompacted=false;
    bool vrTiledLights=true, vrLightTilesReady=false;
    bool vrSlabLights=true, vrSlabLightsActive=false;
    uint32_t vrSlabTileWidth=0, vrSlabTileHeight=0;
    Tempest::Vec3 vrSlabOrigin;
    // Split compact lists: vrVisibleLights holds the volume-route lights and
    // vrSlabLights the large-coverage lights; vrSlabDraw = {slab count, lit tiles,0,0}.
    Tempest::StorageBuffer vrSlabDraw, vrSlabList;
    // Per-light route threshold (screen-area fraction). The slab list itself
    // is enabled per eye by the VR window's measured-cost controller through
    // setVrSlabRoute; the camera-side weight (graphics/lightcoverage.h) is
    // reported as its prior and logged.
    float vrSlabCoverage=0.2f;
    bool  vrSlabRouteRequest[2]={true,true};
    float vrSlabGateWeights[2]={0,0};
    uint8_t vrSlabGateEye=0;
    Tempest::Matrix4x4 vrSlabView; // eye view of the last prepared slab list (diagnostic distances)
    bool vrSubgroupTiles=true, vrSubgroupTilesReady=false, vrStereoSeed=true;
    // VR: the second eye's HiZ from the first eye's final depth (hiz/hiz_stereo.comp,
    // vr/vrstereohiz.h) instead of its own occluder seed draw; Gothic.ini vrStereoHiZOff=1.
    bool vrStereoHiZ=true, vrStereoHiZActive=false, vrStereoHiZLogged=false;
    Tempest::Matrix4x4 vrLeftProj, vrLeftView; Tempest::Vec3 vrLeftOrigin; bool vrLeftEyeValid=false;
    void buildHiZStereo(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const Camera& camera);
    // VR: stash (sceneOpaque/sceneDepth) at half resolution; Gothic.ini vrStashHalfOff=1.
    bool vrStashHalf=true;
    // VR: reduced-rate sky LUTs, interleaved irradiance, the second eye applies
    // the first eye's exposure (vr/vrskyrate.h); Gothic.ini vrSkyRateOff=1.
    bool vrSkyRate=false, vrSkyRateLogged=false, vrExposureStored=false;
    Vr::SkyRate vrSkyRateState;
    Tempest::StorageBuffer vrExposureShared;
    bool vrUniformLights=true, vrUniformLightsReady=false;
    uint32_t vrDrawnLightCount=0;
    Tempest::StorageBuffer vrVisibleLights, vrLightDraw;
    Tempest::StorageBuffer vrLightTiles;
    bool vrDiagnostics=false;
    Tempest::StorageBuffer vrFogHistogram;
    enum Quality : uint8_t {
      None,
      VolumetricLQ,
      VolumetricHQ,
      Epipolar,
      PathTrace,
      };
    Tempest::Size internalResolution(Tempest::Size src) const;
    float         internalResolutionScale() const;

    void updateCamera(const WorldView& wview, const Camera &camera,const Camera* shadowCamera=nullptr);
    bool requiresTlas() const;
    bool requiresLightsTree() const;

    Tempest::StorageImage&  usesImage2d(Tempest::StorageImage& ret, Tempest::TextureFormat frm, uint32_t w, uint32_t h, bool mips = false);
    Tempest::StorageImage&  usesImage2d(Tempest::StorageImage& ret, Tempest::TextureFormat frm, Tempest::Size sz, bool mips = false);
    Tempest::StorageImage&  usesImage3d(Tempest::StorageImage& ret, Tempest::TextureFormat frm, uint32_t w, uint32_t h, uint32_t d, bool mips = false);
    Tempest::ZBuffer&       usesZBuffer(Tempest::ZBuffer&      ret, Tempest::TextureFormat frm, uint32_t w, uint32_t h);
    Tempest::Attachment&    usesAttachment(Tempest::Attachment&ret, Tempest::TextureFormat frm, Tempest::Size sz);
    Tempest::Attachment&    usesAttachment(Tempest::Attachment&ret, Tempest::TextureFormat frm, uint32_t w, uint32_t h);
    Tempest::StorageBuffer& usesSsbo(Tempest::StorageBuffer& ret, size_t size);
    Tempest::StorageBuffer& usesSsboInit(Tempest::StorageBuffer& ret, size_t size);
    Tempest::StorageBuffer& usesScratch(Tempest::StorageBuffer& ret, size_t size);

    void prepareUniforms(WorldView& wview, const Camera& camera);
    void resetViewport(Tempest::Size size, Tempest::Size fullRes);
    void resetShadowmap();
    void resetSkyFog();

    void prepareSky       (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview, const Vr::SkyRate::Plan& plan = Vr::SkyRate::Plan());
    void prepareSSAO      (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview);
    void prepareFog       (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview);
    void prepareIrradiance(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview);
    void prepareGi        (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview);
    void prepareExposure  (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview, uint8_t mode = 0); // 0 own, 1 own + store (first eye), 2 apply stored (second eye)
    void prepareEpipolar  (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview);

    void prepareLightsBvh (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview);
    void prepareVisibleLights(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview, bool secondEye);

    void prepareSurfels   (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview);
    void surfelsApply     (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview, int32_t tileSize, bool postPass);
    void surfelsBinning   (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview, int32_t tileSize, bool postPass);
    void surfelsTrace     (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview, Tempest::StorageBuffer& surfels, bool postPass);

    void drawHiZ          (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& view, bool reuseLeftSeed=false);
    void buildHiZ         (Tempest::Encoder<Tempest::CommandBuffer>& cmd);
    void drawVsm          (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& view);
    void drawRtsm         (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& view);
    void drawRtsmOmni     (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& view);
    void drawSwr          (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& view);
    void drawGBuffer      (Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t fId, WorldView& view);
    bool drawGWater       (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& view, bool merged=false);
    void drawShadowMap    (Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t fId, WorldView& view);
    void drawShadowResolve(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& view, bool combinedAmbient=false);
    void drawLights       (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& view);
    void drawSky          (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& view);
    void drawAmbient      (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& view);
    void drawTonemapping  (Tempest::Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawCMAA2        (Tempest::Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawReflections  (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawUnderwater   (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawFog          (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawSunMoon      (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawSunMoon      (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview, bool isSun);
    void drawGizmo        (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawLightsHud    (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);

    void drawSwRT         (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawPathtrace    (Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview, uint8_t fId);

    void stashSceneAux    (Tempest::Encoder<Tempest::CommandBuffer>& cmd);

    void drawRayQueryDbg  (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawProbesDbg    (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawSurfelsDbg   (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawProbesHitDbg (Tempest::Encoder<Tempest::CommandBuffer>& cmd);
    void drawVsmDbg       (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawSwrDbg       (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawRtsmDbg      (Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawHashDbg      (Tempest::Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);
    void drawLightTreeDbg (Tempest::Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview);

    void setupSettings();
    void toggleGi();
    void toggleVsm();
    void toggleRtsm();
    void togglePathtrace();

    struct Settings {
      uint32_t       shadowResolution   = 2048;
      bool           vsmEnabled         = false;
      bool           rtsmEnabled        = false;
      bool           swrEnabled         = false;
      bool           swrtEnabled        = false;
      bool           pathTraceEnabled   = false;

      bool           zEnvMappingEnabled = false;
      bool           zCloudShadowScale  = false;
      bool           ssaoHalfResolution = false;
      bool           fogHalfResolution  = false;
      float          cameraObstructionDistance = 0;
      bool           zFogRadial         = false;

      bool           zWindEnabled       = false;
      uint64_t       windPeriod         = 6000;

      GiMethod       giMethod           = GiMethod::None;
      bool           aaEnabled          = false;

      float          zVidBrightness     = 0.5;
      float          zVidContrast       = 0.5;
      float          zVidGamma          = 0.5;

      float          sunSize            = 0;
      float          moonSize           = 0;

      float          vidResIndex        = 0;
      float          renderScale        = 1;

      float          vsmMipBias         = 0.25; //TODO: set to lower, eventually
      } settings;

    Frustrum                  frustrum[SceneGlobals::V_Count];
    Tempest::Matrix4x4        proj, viewProj, viewProjLwc;
    Tempest::Matrix4x4        shadowMatrix[Resources::ShadowLayers];
    Tempest::Matrix4x4        shadowMatrixVsm;
    Tempest::Vec3             clipInfo;

    Tempest::Attachment       sceneLinear;
    Tempest::Attachment       hdrComposite;
    float                     hdrPeakRatio = 0;
    Tempest::ZBuffer          zbuffer, shadowMap[Resources::ShadowLayers];
    Tempest::ZBuffer          zbufferUi;

    Tempest::Attachment       sceneOpaque;
    Tempest::Attachment       sceneDepth;

    Tempest::Attachment       gbufDiffuse;
    Tempest::Attachment       gbufNormal;

    struct Shadow {
      Tempest::RenderPipeline* directLightPso = nullptr;
      } shadow;

    struct Lights {
      Tempest::RenderPipeline* directLightPso = nullptr;
      } lights;

    struct {
      Tempest::StorageBuffer bvh, tree;
      Tempest::StorageBuffer bvhMorton, bvhAlux, bvhCtrl;
      } lightsTree;

    struct Sky {
      Quality                quality       = Quality::None;
      bool                   fogHalfResolution = false;
        int                    vrFogMode = -2;

      Tempest::TextureFormat lutRGBFormat  = Tempest::TextureFormat::R11G11B10UF;
      Tempest::TextureFormat lutRGBAFormat = Tempest::TextureFormat::RGBA16F;

      Tempest::Sampler       sampler = Tempest::Sampler::bilinear();

      bool                   lutIsInitialized = false;
      Tempest::Attachment    transLut, multiScatLut, viewLut, viewCldLut;
      Tempest::StorageImage  cloudsLut, fogLut3D, fogLut3DMs;
      Tempest::StorageImage  occlusionLut, irradianceLut;
      } sky;

    struct SSAO {
      Tempest::TextureFormat    aoFormat = Tempest::TextureFormat::R8;
      Tempest::StorageImage     ssaoBuf;
      Tempest::StorageImage     ssaoBlur;
      } ssao;

    struct Cmaa2 {
      Tempest::StorageImage     workingEdges;
      Tempest::StorageBuffer    shapeCandidates;
      Tempest::StorageBuffer    deferredBlendLocationList;
      Tempest::StorageBuffer    deferredBlendItemList;
      Tempest::StorageImage     deferredBlendItemListHeads;
      Tempest::StorageBuffer    controlBuffer;
      Tempest::StorageBuffer    indirectBuffer;
      } cmaa2;

    struct {
      Tempest::StorageImage     hiZ;
      Tempest::StorageImage     hiZLeft; // VR: per-tile max of the first eye's final depth (hiz_stereo.comp input)
      Tempest::StorageBuffer    counter;
      } hiz;

    struct {
      const uint32_t            atlasDim  = 256; // sqrt(maxProbes)
      const uint32_t            maxProbes = atlasDim*atlasDim; // 65536

      Tempest::StorageBuffer    voteTable, hashTable, freeList;
      Tempest::StorageBuffer    probes;
      Tempest::StorageImage     probesGBuffDiff;
      Tempest::StorageImage     probesGBuffNorm;
      Tempest::StorageImage     probesGBuffRayT;
      Tempest::StorageImage     probesLighting;
      Tempest::StorageImage     probesLightingPrev;
      } gi;

    struct {
      const Tempest::IVec2      gbufTile   = {8};
      const uint32_t            gbufTilesX = 256;
      const uint32_t            gbufTilesY = 256;
      const uint32_t            maxSurfels = gbufTilesX*gbufTilesY;
      Tempest::StorageBuffer    surfels;

      Tempest::StorageImage     irrImage;
      Tempest::StorageImage     surfCnts, surfBins;
      Tempest::StorageBuffer    surfBinsCtrl, surfList;

      Tempest::StorageBuffer    gbuffFree;
      Tempest::StorageImage     gbuffDiff, gbuffNorm, gbuffHitT;
      } surf;

    struct {
      Tempest::Attachment       frame;
      uint32_t                  numFrames = 0;
      Tempest::Matrix4x4        mvpLast;
      } pt;

    struct {
      Tempest::StorageBuffer    epipoles;
      Tempest::StorageImage     epTrace;
      } epipolar;

    const int32_t VSM_PAGE_SIZE = 128;
    struct {
      Tempest::StorageImage     pageTbl;
      Tempest::StorageImage     pageHiZ;
      Tempest::ZBuffer          pageData;
      Tempest::StorageBuffer    pageList;
      Tempest::StorageBuffer    pageListTmp;
      Tempest::StorageBuffer    pageTblOmni;
      Tempest::StorageBuffer    visibleLights;

      Tempest::StorageImage     fogDbg;
      Tempest::StorageImage     vsmDbg;
      } vsm;

    struct {
      Tempest::StorageImage     outputImage;
      Tempest::StorageImage     outputImageClr;

      Tempest::StorageImage     pages;
      Tempest::StorageBuffer    visList;
      Tempest::StorageBuffer    posList;

      Tempest::StorageImage     meshTiles;
      Tempest::StorageImage     primTiles;

      Tempest::StorageBuffer    visibleLights;
      Tempest::StorageBuffer    drawTasks;
      Tempest::StorageImage     lightTiles;
      Tempest::StorageImage     lightBins, primTilesOmni;

      Tempest::StorageImage     dbg64, dbg32, dbg16, dbg8;
      } rtsm;

    struct {
      Tempest::StorageImage     outputImage;
      } swr;

    struct {
      Tempest::StorageImage     outputImage;
      } swrt;

    struct {
      Tempest::Vec3 center = {};
      bool          enable = false;
      } gizmo;

    struct {
      const Tempest::Texture2d* light = nullptr;
      } hud;

    Tempest::TextureFormat    shadowFormat  = Tempest::TextureFormat::Depth16;
    Tempest::TextureFormat    zBufferFormat = Tempest::TextureFormat::Depth16;

    Shaders&                  shaders = Shaders::inst(false);
  };
