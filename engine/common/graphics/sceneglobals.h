#pragma once

#include <Tempest/Matrix4x4>
#include <Tempest/Vec>

#include "graphics/dynamic/frustrum.h"
#include "rtscene.h"
#include "resources.h"

class Sky;
class WorldView;

class SceneGlobals final {
  public:
    SceneGlobals();
    ~SceneGlobals();

    enum VisCamera : uint8_t {
      V_Shadow0    = 0,
      V_Shadow1    = 1,
      V_ShadowLast = 1,
      V_Main       = 2,
      V_HiZ        = 3,
      V_Vsm        = 4,
      V_Count
      };

    static bool isShadowView(VisCamera v);

    void setViewProject(const Tempest::Matrix4x4& view, const Tempest::Matrix4x4& proj,
                        float zNear, float zFar,
                        const Tempest::Matrix4x4 *sh);
    void setViewLwc(const Tempest::Matrix4x4& view, const Tempest::Matrix4x4& proj, const Tempest::Matrix4x4 *sh);
    void setViewVsm(const Tempest::Matrix4x4& view, const Tempest::Matrix4x4& viewLwc);
    void setSky(const Sky& s);
    void setWorld(const WorldView& wview);
    void setUnderWater(bool w);
    void setCameraObstructionFade(float distance, const Tempest::Vec4& target);
    void setVrShadowParams(const Tempest::Vec4& p) { uboGlobalCpu.vrShadowParams = p; }
    void setVrStaticLight(const Tempest::Vec4& p) { uboGlobalCpu.vrStaticLight = p; }

    void setTime(uint64_t time);
    void commitUbo(uint8_t fId);
    void prepareGlobals(Tempest::Encoder<Tempest::CommandBuffer> &cmd, uint8_t frameId);

    void setResolution(uint32_t w, uint32_t h);
    void setHiZ(const Tempest::Texture2d& hiZ);
    void setShadowMap(const Tempest::Texture2d* tex[]);

    void setVirtualShadowMap(bool enabled,
                             const Tempest::ZBuffer&       vsmPageData,
                             const Tempest::StorageImage&  pageTbl,
                             const Tempest::StorageImage&  pageHiZ,
                             const Tempest::StorageBuffer& vsmPageList);

    const Tempest::Matrix4x4& viewProject() const;
    const Tempest::Matrix4x4& viewProjectInv() const;
    const Tempest::Matrix4x4& viewShadow(uint8_t view) const;
    const Tempest::Vec3       clipInfo() const;

    const Tempest::Matrix4x4  viewProjectLwc() const;
    const Tempest::Matrix4x4  viewProjectLwcInv() const;

    uint64_t                          tickCount = 0;
    const Tempest::Texture2d*         shadowMap[2] = {};

    Tempest::Matrix4x4                view, proj;
    Tempest::Matrix4x4                viewLwc;
    Tempest::Vec3                     originLwc;
    float                             znear = 0;

    const Tempest::Texture2d*         sceneColor   = &Resources::fallbackBlack();
    const Tempest::Texture2d*         sceneDepth   = &Resources::fallbackBlack();
    const Tempest::Texture2d*         zbuffer      = &Resources::fallbackBlack();

    const Tempest::Texture2d*         gbufDiffuse  = &Resources::fallbackBlack();
    const Tempest::Texture2d*         gbufNormals  = &Resources::fallbackBlack();

    const Tempest::Texture2d*         hiZ          = &Resources::fallbackTexture();

    const Tempest::StorageBuffer*     lights        = nullptr;

    const Tempest::Texture2d*         vsmPageData   = nullptr;
    const Tempest::StorageImage*      vsmPageTbl    = nullptr;
    const Tempest::StorageImage*      vsmPageHiZ    = nullptr;
    const Tempest::StorageBuffer*     vsmPageList   = nullptr;
    bool                              vsmEnabled    = false;
    bool                              vrIndexedObjects = false;
    bool                              vrBatchedObjects = true;
    // VR object draw distance (cm; 0 = unlimited), shorter distance for small
    // objects, minimum angular size, and whether this eye's main pass writes
    // the next eye's occluder seed (materials/visibility_pass.comp).
    float                             vrObjectFar = 0, vrSmallObjectFar = 0, vrObjectMinTan = 0;
    float                             vrLodDistance = 0; // terrain LOD: level 1 beyond it, level 2 beyond twice it (cm); 0 = full detail
    bool                              vrSeedNextEye = false;
    bool                              vrSkipSeedCull = false; // second eye with the stereo HiZ: no occluder seed at all
    // VR cached shadow (vr/vrshadowcache.h): the V_Vsm view builds the static
    // map (visibility pass on cull frames only), V_Shadow1 is unused.
    bool                              vrStaticShadow = false;
    bool                              vrStaticShadowCull = false;
    uint32_t                          vrStaticShadowSlice = 0, vrStaticShadowSlices = 1; // cluster range culled this frame
    bool                              vrStaticShadowObjectsOnly = false; // 2002 static lighting: the landscape casts no shadow (its own shadows are in the bake)
    // Balanced slices: this frame's cluster range from the renderer
    // (Vr::ShadowSlicer::balancedBounds) instead of the raw index split.
    bool                              vrStaticShadowBalanced = false;
    uint32_t                          vrStaticShadowFirst = 0, vrStaticShadowLast = 0;
    Tempest::StorageImage             vsmDbg;

    struct UboGlobal final {
      Tempest::Matrix4x4              viewProject;
      Tempest::Matrix4x4              viewProjectInv;
      Tempest::Matrix4x4              viewShadow[Resources::ShadowLayers];
      Tempest::Matrix4x4              viewProjectLwcInv;
      Tempest::Matrix4x4              viewShadowLwc[Resources::ShadowLayers];
      Tempest::Matrix4x4              viewVirtualShadow;
      Tempest::Matrix4x4              viewVirtualShadowLwc;
      Tempest::Matrix4x4              viewProject2VirtualShadow;
      Tempest::Vec4                   vsmDdx, vsmDdy;
      Tempest::Matrix4x4              view, project, projectInv;
      Tempest::Vec3                   sunDir        = {0,0,1};
      float                           waveAnim      = 0;
      Tempest::Vec3                   lightAmb      = {1,1,1};
      float                           exposure      = 1;
      Tempest::Vec3                   lightCl       = {0,0,0};
      float                           GSunIntensity = 0;
      Tempest::Vec4                   frustrum[6];
      Tempest::Vec3                   clipInfo;
      uint32_t                        tickCount32 = 0;
      Tempest::Vec3                   camPos;
      float                           isNight = 0;
      Tempest::Vec2                   screenResInv;
      Tempest::Vec2                   closeupShadowSlice;

      Tempest::Vec3                   pfxLeft  = {};
      uint32_t                        underWater = 0;
      Tempest::Vec3                   pfxTop   = {};
      float                           luminanceMed = 0;
      Tempest::Vec3                   pfxDepth = {};
      float                           plPosY = {};
      Tempest::Point                  hiZTileSize = {};
      Tempest::Point                  screenRes = {};
      Tempest::Vec2                   cloudsDir[2] = {};

      float                           probeGridBias = 3;
      float                           cameraFadeNear2 = 0;
      float                           cameraFadeFar2 = 0;
      float                           cameraFadePadding = 0;
      Tempest::Vec4                    cameraFadeTarget = {};
      Tempest::Vec4                   vrShadowParams = {}; // x: VR cached shadow sampling on, y: static map edge fade start, z: baked sun visibility, w: 2002 static lighting
      Tempest::Vec4                   vrStaticLight  = {}; // x: 1/litRef^2.2 of the bake, y: reference n.d of the baked sun
      };

    Tempest::UniformBuffer<UboGlobal> uboGlobalPf[Resources::MaxFramesInFlight][V_Count];
    Tempest::StorageBuffer            uboGlobal[V_Count];

    Frustrum                          frustrum[V_Count];

    bool                              zWindEnabled = false;
    Tempest::Vec2                     windDir = {0,1};
    uint64_t                          windPeriod = 6000;

    RtScene                           rtScene;

  private:
    UboGlobal                         uboGlobalCpu;
  };

