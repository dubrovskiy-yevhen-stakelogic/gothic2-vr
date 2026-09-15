#pragma once

#include <Tempest/Device>
#include <Tempest/Shader>

#include <zenkit/world/VobTree.hh>

#include "graphics/mesh/landscape.h"
#include "graphics/meshobjects.h"
#include "graphics/mesh/protomesh.h"
#include "graphics/pfx/pfxobjects.h"
#include "graphics/sky/sky.h"
#include "lightsource.h"
#include "lightgroup.h"
#include "sceneglobals.h"
#include "visualobjects.h"

class World;
class Camera;
class ParticleFx;
class PackedMesh;
class gtime;

class WorldView {
  public:
    WorldView(const PackedMesh& wmesh, std::string_view skyPreset);
    ~WorldView();

    const LightSource&        mainLight() const;
    const Tempest::Vec3&      ambientLight() const;
    std::pair<Tempest::Vec3, Tempest::Vec3> bbox() const;

    bool isInPfxRange(const Tempest::Vec3& pos) const;

    void preFrameUpdate(const Camera& camera,uint64_t tickCount,uint8_t fId,const Camera* shadowCamera=nullptr,bool secondEye=false);
    void postFrameupdate();
    VisualObjects::StageCpuStats stageCpu(uint64_t tickCount,bool windEnabled,uint64_t windPeriod) { return visuals.stageCpu(tickCount,windEnabled,windPeriod); }
    void discardCpuStage() { visuals.discardCpuStage(); }

    void prepareGlobals(Tempest::Encoder<Tempest::CommandBuffer> &cmd, uint8_t fId);
    struct UploadCpuStats {
      // CPU milliseconds for the most recently prepared eye.
      double scene=0,lights=0,instances=0,clusters=0,commands=0,buckets=0;
      double instancePack=0,clusterPack=0;
      bool instancesStaged=false,clustersStaged=false;
      };
    const UploadCpuStats& uploadCpuStats() const { return uploadCpu; }

    void setGbuffer(const Tempest::Texture2d& diffuse,
                    const Tempest::Texture2d& norm);
    void setShadowMaps (const Tempest::Texture2d* shadow[]);
    void setVirtualShadowMap(bool enabled,
                             const Tempest::ZBuffer& pageData,
                             const Tempest::StorageImage& pageTbl,
                             const Tempest::StorageImage& pageHiZ,
                             const Tempest::StorageBuffer& pageList);
    void setHiZ(const Tempest::Texture2d& hiZ);
    void setSceneImages(const Tempest::Texture2d& clr, const Tempest::Texture2d& depthAux, const Tempest::ZBuffer& depthNative);
    void setWindEnabled(bool enabled, uint64_t period);
    void setCameraObstructionFade(float distance, const Tempest::Vec4& target) { sGlobal.setCameraObstructionFade(distance, target); }

    void dbgLights      (DbgPainter& p) const;

    bool updateLights(const gtime gameTime);
    bool updateRtScene();

    void updateFrustrum (const Frustrum fr[]);
    void visibilityPass (Tempest::Encoder<Tempest::CommandBuffer>& cmd, int pass, bool skipShadows=false, bool reuseLeftSeed=false);
    void visibilityVsm  (Tempest::Encoder<Tempest::CommandBuffer>& cmd);

    void drawHiZ        (Tempest::Encoder<Tempest::CommandBuffer>& cmd, bool reuseLeftSeed=false);
    void drawShadow     (Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t frameId, uint8_t layer);
    void drawVsm        (Tempest::Encoder<Tempest::CommandBuffer>& cmd);
    void drawGBuffer    (Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t frameId);
    void drawWater      (Tempest::Encoder<Tempest::CommandBuffer>& cmd);
    void drawTranslucent(Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t frameId);

    MeshObjects::Mesh   addView      (std::string_view visual, int32_t headTex, int32_t teethTex, int32_t bodyColor);
    MeshObjects::Mesh   addView      (const ProtoMesh* visual);
    MeshObjects::Mesh   addItmView   (std::string_view visual, int32_t material);
    MeshObjects::Mesh   addAtachView (const ProtoMesh::Attach& visual, const int32_t version);
    MeshObjects::Mesh   addStaticView(const ProtoMesh* visual, bool staticDraw = false);
    MeshObjects::Mesh   addStaticView(std::string_view visual);
    MeshObjects::Mesh   addDecalView (const zenkit::VisualDecal& vob);
    LightGroup::Light   addLight     (const zenkit::VLight& vob, const uint64_t timeOffset);
    LightGroup::Light   addLight     (std::string_view preset, const uint64_t timeOffset);

    void                dbgClusters(Tempest::Painter& p, Tempest::Vec2 wsz);

    const SceneGlobals& sceneGlobals() const { return sGlobal; }
    void setVrIndexedObjects(bool enabled) { sGlobal.vrIndexedObjects=enabled; }
    void setVrBatchedObjects(bool enabled) { sGlobal.vrBatchedObjects=enabled; }
    void setVrTerrainLod(float distanceCm) { sGlobal.vrLodDistance=distanceCm; }
    void setVrObjectCull(float farCm, float smallFarCm, float minTan) { sGlobal.vrObjectFar=farCm; sGlobal.vrSmallObjectFar=smallFarCm; sGlobal.vrObjectMinTan=minTan; }
    void setVrSeedNextEye(bool enabled) { sGlobal.vrSeedNextEye=enabled; }
    void setVrSkipSeedCull(bool skip) { sGlobal.vrSkipSeedCull=skip; }
    // VR cached shadow (vr/vrshadowcache.h): world-aligned light matrices replace
    // the head-centred cascades (map 0 dynamic casters, map 1 the cached static
    // world); the static map is built through the V_Vsm view with buildMat.
    void setVrShadow(bool enabled, const Tempest::Matrix4x4& dynamicMat, const Tempest::Matrix4x4& staticMat,
                     const Tempest::Matrix4x4& buildMat, bool cullStatic, uint32_t slice, uint32_t slices, const Tempest::Vec4& params) {
      vrShadow.enabled=enabled; vrShadow.dynamicMat=dynamicMat; vrShadow.staticMat=staticMat;
      vrShadow.buildMat=buildMat; vrShadow.cullStatic=cullStatic; vrShadow.slice=slice; vrShadow.slices=slices; vrShadow.params=params;
      }
    // Balanced static slices: this frame's cluster range, the
    // weights it is built from, and the moved-caster rebuild request.
    void setVrShadowRange(bool balanced, uint32_t first, uint32_t last) { vrShadow.balanced=balanced; vrShadow.first=first; vrShadow.last=last; }
    void vrStaticShadowWeights(const Tempest::Matrix4x4& build, float width, bool objectsOnly, std::vector<uint32_t>& out) const {
      visuals.staticShadowWeights(build,width,objectsOnly,out);
      }
    uint32_t vrStaticCasterMoves() const { return visuals.staticCasterMoves(); }
    // Shadow tile early-out: false only when the NPC map's
    // frustum can receive no animated caster this frame.
    bool vrDynamicShadowCasters(const Frustrum& f) { return visuals.dynamicShadowCasters(f); }
    void setVrStaticCasterArea(const Tempest::Vec3& center, float radius) { visuals.setStaticCasterArea(center,radius); }
    uint64_t drawCommandGeneration() const { return visuals.commandGeneration(); }
    // VR 2002 static lighting: bake lit reference for the day curve, static lights off the list.
    void setVrStaticLighting(bool on) { vrStaticLighting=on; gLights.setVrStaticLighting(on); }
    size_t staticLightCount() const { return gLights.staticLightCount(); }
    void drawShadowDynamic(Tempest::Encoder<Tempest::CommandBuffer>& cmd);
    void drawShadowStatic (Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint32_t slice, uint32_t slices);
    const Sky&          sky() const { return gSky; }
    void setVrWeather(int mode) { gSky.setVrWeather(mode); }
    const Landscape&    landscape() const { return land; }
    LightGroup&         lights() { return gLights; }
    const LightGroup&   lights() const { return gLights; }
    const DrawClusters& clusters() const;
    const DrawCommands& drawCommands() const;
    const DrawBuckets&  drawBuckets() const;
    auto                instanceSsbo() const -> const Tempest::StorageBuffer&;

  private:
    std::pair<Tempest::Vec3, Tempest::Vec3> aabb;
    SceneGlobals  sGlobal;
    struct VrShadow {
      bool enabled=false, cullStatic=false, balanced=false;
      uint32_t slice=0, slices=1, first=0, last=0;
      Tempest::Matrix4x4 dynamicMat, staticMat, buildMat;
      Tempest::Vec4 params;
      } vrShadow;
    bool vrStaticLighting=false;
    Sky           gSky;
    LightGroup    gLights;
    VisualObjects visuals;
    UploadCpuStats uploadCpu;

    MeshObjects   objGroup;
    PfxObjects    pfxGroup;
    Landscape     land;

    bool needToUpdateCmd(uint8_t frameId) const;
    void invalidateCmd();

  friend class LightGroup::Light;
  friend class PfxEmitter;
  friend class TrlObjects;
  };
