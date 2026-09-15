#pragma once

#include <Tempest/RenderPipeline>
#include <Tempest/Shader>
#include <Tempest/Device>
#include <future>
#include <list>

#include "graphics/drawcommands.h"
#include "material.h"
#include "game/constants.h"

class Shaders {
  public:
    Shaders();
    ~Shaders();

    void waitCompiler();

    enum PipelineType: uint8_t {
      T_Depth,
      T_Shadow,
      T_Vsm,
      T_Main,
      };

    static Shaders& inst(bool waitCompiler = true);
    static bool isVsmSupported();
    static bool isRtsmSupported();
    static bool isLightsTreeSupported();
    static bool isGi1Supported();
    static bool isGi2Supported();

    Tempest::RenderPipeline  lightsReference, lightsFar;
    Tempest::RenderPipeline  lightsTiled, lightsTiledUniform;
    Tempest::RenderPipeline  lightsSlab, lightsSlabUniform;
    Tempest::RenderPipeline  lights, lightsRq, lightsVsm;
    Tempest::RenderPipeline  directLight,  directLightSh, directLightRq;
    Tempest::RenderPipeline  directAmbient, directAmbientSh;
    // VR shadow tile early-out: the SHADOW_MAP variants with
    // the min/max tile test, and the tile build.
    Tempest::RenderPipeline  directLightShTiles, directAmbientShTiles;
    Tempest::ComputePipeline shadowTiles;
    Tempest::RenderPipeline  unlit;
    Tempest::RenderPipeline  ambientLight, ambientLightSsao, ambientLightSurf;

    Tempest::ComputePipeline indexedObjects, indexedBindless;
    Tempest::ComputePipeline lightVisibilityInit, lightVisibility;
    Tempest::ComputePipeline lightTiles;
    Tempest::ComputePipeline lightTilesSubgroup;
    Tempest::ComputePipeline lightTilesSlab;
    Tempest::ComputePipeline copyBuf;
    Tempest::ComputePipeline copyImg;
    Tempest::ComputePipeline patch;
    Tempest::RenderPipeline  copy, downscale;
    Tempest::RenderPipeline  stash;
    Tempest::RenderPipeline  bink;

    Tempest::ComputePipeline ssao, ssaoBlur, ssaoHalf, ssaoUpsample;

    Tempest::ComputePipeline irradiance;

    // Scalable and Production Ready Sky and Atmosphere
    Tempest::RenderPipeline  skyTransmittance, skyMultiScattering;
    Tempest::RenderPipeline  skyViewLut, skyViewCldLut, sky, skySep;
    Tempest::RenderPipeline  fog;
    Tempest::RenderPipeline  fog3dHQ;
    Tempest::RenderPipeline  sun;
    Tempest::ComputePipeline cloudsLut, fogOcclusion;
    Tempest::ComputePipeline fogViewLut3d, fogViewLutSep, vrFogProbe;
    Tempest::ComputePipeline skyExposure;
    Tempest::ComputePipeline skyExposureStore, skyExposureApply; // VR: first eye stores, second eye applies

    Tempest::RenderPipeline  skyPathTrace;

    Tempest::RenderPipeline  underwaterT, underwaterS;
    Tempest::RenderPipeline  waterReflection, waterReflectionSSR;

    Tempest::RenderPipeline  tonemapping, tonemappingUpscale;
    Tempest::RenderPipeline  tonemappingFog; // VR: reflections + fog composite folded into the tonemapping pass
    Tempest::RenderPipeline  hdrOutput;

    // AA
    Tempest::ComputePipeline cmaa2EdgeColor2x2Presets[uint32_t(AaPreset::PRESETS_COUNT)];
    Tempest::ComputePipeline cmaa2ProcessCandidates;
    Tempest::RenderPipeline  cmaa2DeferredColorApply2x2;

    // HUD
    Tempest::RenderPipeline  gizmo, lightImposter;

    // HiZ
    Tempest::ComputePipeline hiZPot, hiZMip, hiZStereo;

    // Cluster
    Tempest::ComputePipeline clusterInit, clusterPatch;
    Tempest::ComputePipeline visibilityPassSh, visibilityPassHiZ, visibilityPassHiZCr, visibilityPassHiZSeed;

    // RT/RQ
    Tempest::RenderPipeline  rtDbg;
    Tempest::RenderPipeline  rtPathtrace;
    Tempest::RenderPipeline  hashDbg;

    // GI
    Tempest::RenderPipeline  probeDbg, probeHitDbg;
    Tempest::ComputePipeline probeInit, probeClear, probeClearHash, probeMakeHash;
    Tempest::ComputePipeline probeVote, probePrune, probeAlocation;
    Tempest::ComputePipeline probeTrace, probeLighting;
    Tempest::RenderPipeline  probeAmbient;

    // GI-Surf
    Tempest::RenderPipeline  surfDbg;
    Tempest::ComputePipeline surfBinClear, surfBinPass, surfBinAlloc, surfBinSort;

    Tempest::ComputePipeline surfAlloc, surfApply, surfFList;
    Tempest::ComputePipeline surfUpdate, surfCulling, surfDecimate, surfCompact;

    Tempest::ComputePipeline surfPathtrace, surfRaycast, surLighting;

    // Lights tree
    Tempest::ComputePipeline lightsMorton, lightsTopology, lightsTree, lightsBvh;
    Tempest::RenderPipeline  lightsTreeDbg;

    // Epipolar
    Tempest::ComputePipeline fogEpipolarVsm;
    Tempest::ComputePipeline fogEpipolarOcclusion;

    // Virtual shadow
    Tempest::ComputePipeline vsmVisibilityPass;
    Tempest::ComputePipeline vsmClear, vsmClearOmni, vsmCullLights, vsmMarkPages, vsmMarkOmniPages, vsmPostprocessOmni;
    Tempest::ComputePipeline vsmTrimPages, vsmSortPages, vsmListPages, vsmClumpPages, vsmAllocPages, vsmAlloc2Pages, vsmMergePages;
    Tempest::ComputePipeline vsmPackDraw0, vsmPackDraw1;
    Tempest::ComputePipeline vsmFogPages, vsmFogShadow, vsmFogTrace;
    Tempest::RenderPipeline  vsmFog;
    Tempest::RenderPipeline  vsmDirectLight;
    Tempest::RenderPipeline  vsmDbg;

    Tempest::ComputePipeline vsmRendering;

    // RTSM (Experimental)
    Tempest::RenderPipeline  rtsmDirectLight;

    Tempest::ComputePipeline rtsmClear, rtsmPages, rtsmFogPages, rtsmHiZ;
    Tempest::ComputePipeline rtsmCulling, rtsmPosition;
    Tempest::ComputePipeline rtsmMeshletCull, rtsmPrimCull;
    Tempest::ComputePipeline rtsmRaster;

    Tempest::ComputePipeline rtsmClearOmni;
    Tempest::ComputePipeline rtsmCullLights, rtsmCompactLights, rtsmCullingOmni;
    Tempest::ComputePipeline rtsmPositionOmni;
    Tempest::ComputePipeline rtsmMeshletOmni, rtsmBackfaceOmni;
    Tempest::ComputePipeline rtsmLightsOmni, rtsmBboxesOmni, rtsmCompactOmni, rtsmTaskOmni;
    Tempest::ComputePipeline rtsmPrimOmni, rtsmRasterOmni;

    Tempest::ComputePipeline rtsmRendering, rtsmRenderingOmni; //reference
    Tempest::RenderPipeline  rtsmDbg;

    Tempest::ComputePipeline swRaytracing, swRaytracing64;
    Tempest::ComputePipeline swRaytracing8;

    // Software rendering
    Tempest::ComputePipeline swRendering;
    Tempest::RenderPipeline  swRenderingDbg;

    // Inventory
    Tempest::RenderPipeline  inventory;

    const Tempest::RenderPipeline* materialPipeline(const Material& desc, DrawCommands::Type t, PipelineType pt, bool bindless, bool indexed=false) const;

  private:
    struct Entry {
      Tempest::RenderPipeline pipeline;
      Material::AlphaFunc     alpha        = Material::Solid;
      DrawCommands::Type      type         = DrawCommands::Static;
      PipelineType            pipelineType = PipelineType::T_Main;
      bool                    bindless     = false;
      bool                    trivial      = false;
      bool                    indexed      = false;
      };

    void                     compileKeyShaders();
    void                     compileShaders();

    Tempest::RenderPipeline  postEffect(std::string_view name);
    Tempest::RenderPipeline  postEffect(std::string_view name, Tempest::RenderState::ZTestMode ztest);
    Tempest::RenderPipeline  postEffect(std::string_view vs, std::string_view fs, Tempest::RenderState::ZTestMode ztest = Tempest::RenderState::ZTestMode::LEqual);
    Tempest::ComputePipeline computeShader(std::string_view name);
    Tempest::RenderPipeline  fogShader (std::string_view name);
    Tempest::RenderPipeline  inWaterShader   (std::string_view name, bool isScattering);
    Tempest::RenderPipeline  reflectionShader(std::string_view name, bool hasMeshlets);
    Tempest::RenderPipeline  ambientLightShader(std::string_view name);

    static Shaders* instance;

    std::future<void>        deferredCompilation;
    mutable std::list<Entry> materials;
  };
