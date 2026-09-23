#include "renderer.h"
#include "vr/vrstereohiz.h"

#include "vr/vrfoveation.h"

#include <Tempest/Color>
#include <Tempest/Fence>
#include <Tempest/Log>
#include <Tempest/StorageImage>
#include <cassert>
#include <bit>
#include <chrono>
#include "graphics/lightcoverage.h"
#if defined(GOTHIC2VR_OPENXR)
#include "vr/vrprofiler.h"
#endif

#include "ui/inventorymenu.h"
#include "ui/videowidget.h"
#include "camera.h"
#include "gothic.h"
#include "world/objects/npc.h"
#include "utils/string_frm.h"

using namespace Tempest;

static const bool skyPathTrace = false;

static uint32_t nextPot(uint32_t x) {
  x--;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  x++;
  return x;
  }

static float smoothstep(float edge0, float edge1, float x) {
  float t = std::min(std::max((x - edge0) / (edge1 - edge0), 0.f), 1.f);
  return t * t * (3.f - 2.f * t);
  };

static float linearstep(float edge0, float edge1, float x) {
  float t = std::min(std::max((x - edge0) / (edge1 - edge0), 0.f), 1.f);
  return t;
  };

static Size tileCount(Size sz, int s) {
  sz.w = (sz.w+s-1)/s;
  sz.h = (sz.h+s-1)/s;
  return sz;
  }

Renderer::Renderer() {
  auto& device = Resources::device();

  static const TextureFormat sfrm[] = {
    TextureFormat::Depth16,
    TextureFormat::Depth24x8,
    TextureFormat::Depth32F,
    };

  for(auto& i:sfrm) {
    if(device.properties().hasDepthFormat(i) && device.properties().hasSamplerFormat(i)) {
      shadowFormat = i;
      break;
      }
    }

  static const TextureFormat zfrm[] = {
    TextureFormat::Depth32F,
    TextureFormat::Depth24x8,
    TextureFormat::Depth16,
    };
  for(auto& i:zfrm) {
    if(device.properties().hasDepthFormat(i) && device.properties().hasSamplerFormat(i)){
      zBufferFormat = i;
      break;
      }
    }

  // crappy rasbery-pi like hardware
  if(!device.properties().hasStorageFormat(sky.lutRGBAFormat))
    sky.lutRGBAFormat = Tempest::TextureFormat::RGBA8;
  if(!device.properties().hasStorageFormat(sky.lutRGBFormat))
    sky.lutRGBFormat = Tempest::TextureFormat::RGBA8;

  Log::i("GPU = ",device.properties().name);
  Log::i("Depth format = ", Tempest::formatName(zBufferFormat), " Shadow format = ", Tempest::formatName(shadowFormat));

  Gothic::inst().onSettingsChanged.bind(this,&Renderer::setupSettings);
  Gothic::inst().toggleGi  .bind(this, &Renderer::toggleGi);
  Gothic::inst().toggleVsm .bind(this, &Renderer::toggleVsm);
  Gothic::inst().toggleRtsm.bind(this, &Renderer::toggleRtsm);

  Gothic::inst().togglePathtrace.bind(this, &Renderer::togglePathtrace);

  settings.giMethod         = Gothic::options().doGi;
  settings.vsmEnabled       = Gothic::options().doVirtualShadow;
  settings.rtsmEnabled      = Gothic::options().doSoftwareShadow;
  settings.swrEnabled       = Gothic::options().swRenderingPreset>0;
  settings.swrtEnabled      = Gothic::options().doSoftwareRT;

  sky.sampler = Tempest::Sampler::bilinear();
  sky.sampler.vClamp = ClampMode::ClampToEdge;

  sky.cloudsLut     = device.image2d   (sky.lutRGBAFormat,  2,  1);
  sky.transLut      = device.attachment(sky.lutRGBFormat, 256, 64);
  sky.multiScatLut  = device.attachment(sky.lutRGBFormat,  32, 32);
  sky.viewLut       = device.attachment(Tempest::TextureFormat::RGBA32F, 128, 64);
  sky.viewCldLut    = device.attachment(Tempest::TextureFormat::RGBA32F, 512, 256);
  sky.irradianceLut = device.image2d(TextureFormat::RGBA32F, 3,2);

  setupSettings();
  }

Renderer::~Renderer() {
  Gothic::inst().onSettingsChanged.ubind(this,&Renderer::setupSettings);
  }

void Renderer::setupSettings() {
#if defined(__ANDROID__)
  {
    // VR cached shadow (Gothic.ini [ENGINE], optional): frames per static map
    // rebuild, static and dynamic map widths in cm.
    const int   slices = Gothic::settingsGetI("ENGINE","vrShadowSlices");
    const float sw     = Gothic::settingsGetF("ENGINE","vrShadowStaticWidth");
    const float dw     = Gothic::settingsGetF("ENGINE","vrShadowDynamicWidth");
    vrShadowSlicer.slices = uint32_t(std::clamp(slices<=0 ? 12 : slices, 1, 32));
    vrShadowStaticWidth   = sw>0.f ? std::clamp(sw, 2000.f, 40000.f) : 6400.f;
    vrShadowDynamicWidth  = dw>0.f ? std::clamp(dw, 1000.f, 20000.f) : 2400.f;
    // Baked landscape sun visibility: on unless Gothic.ini [ENGINE] vrBakedShadowOff=1.
    vrBakedShadow = Gothic::settingsGetI("ENGINE","vrBakedShadowOff")==0;
    vrFogFold     = Gothic::settingsGetI("ENGINE","vrFogFoldOff")==0;
    // Render scale < 1 as a projection-layer sub-rectangle;
    // vrScaleRectOff=1 restores the Lanczos upscale into the whole eye image.
    vrScaleRect   = Gothic::settingsGetI("ENGINE","vrScaleRectOff")==0;
    // Stereo HiZ (0.0.42-0.0.44) measured worse than the seed draw on the Quest
    // (captures 042-044: weaker culling in dense areas, right eye +0.6 ms); opt-in.
    vrStereoHiZ   = Gothic::settingsGetI("ENGINE","vrStereoHiZ")!=0;
    vrStashHalf   = Gothic::settingsGetI("ENGINE","vrStashHalfOff")==0;
    // Static shadow map on demand with balanced slices;
    // vrShadowOnDemandOff=1 restores the always-cycling slicer with raw
    // cluster ranges (0.0.34-0.0.47). Optional: vrShadowRebuildMove (cm),
    // vrShadowRebuildSeconds, vrShadowMovedCasterOff=1.
    vrShadowSlicer.onDemand = Gothic::settingsGetI("ENGINE","vrShadowOnDemandOff")==0;
    vrShadowBalanced        = vrShadowSlicer.onDemand;
    const int moveCm  = Gothic::settingsGetI("ENGINE","vrShadowRebuildMove");
    const int seconds = Gothic::settingsGetI("ENGINE","vrShadowRebuildSeconds");
    vrShadowSlicer.moveCm        = moveCm>0 ? std::clamp(float(moveCm), 100.f, 0.4f*vrShadowStaticWidth) : 800.f;
    vrShadowSlicer.intervalMs    = uint64_t(seconds>0 ? std::clamp(seconds, 1, 120) : 10)*1000u;
    vrShadowSlicer.movedCasterMs = Gothic::settingsGetI("ENGINE","vrShadowMovedCasterOff")!=0 ? 0 : 1000;
    // Reduced-rate sky LUTs and the shared exposure (vr/vrskyrate.h).
    vrSkyRate       = Gothic::settingsGetI("ENGINE","vrSkyRateOff")==0;
    vrSkyRateLogged = false;
    vrSkyRateState.reset();
    // Exact shadow early-out: min/max tile maps let the direct
    // light skip the PCF on all-lit / all-shadowed tiles, and skip the NPC map
    // when no animated caster can reach it; vrShadowTilesOff=1 restores the
    // plain PCF. vrShadowNightSkipOff=1 keeps the SHADOW_MAP variant while the
    // sun is below the horizon (both maps are cleared then: every tap is lit).
    vrShadowTiles     = Gothic::settingsGetI("ENGINE","vrShadowTilesOff")==0;
    vrShadowNightSkip = Gothic::settingsGetI("ENGINE","vrShadowNightSkipOff")==0;
  }
#endif
  const int shadowResolution = Gothic::settingsGetI("ENGINE", "shadowMapResolution");
  switch(shadowResolution) {
    case 512:
    case 1024:
    case 1536:
    case 2048:
      settings.shadowResolution = uint32_t(shadowResolution);
      break;
    default:
      // Missing, malformed and unsupported values retain the original shadow quality.
      settings.shadowResolution = 2048;
      break;
    }
  settings.zEnvMappingEnabled = Gothic::settingsGetI("ENGINE","zEnvMappingEnabled")!=0;
#if defined(GOTHIC2VR_OPENXR)
  // SSAO (zCloudShadowScale) exceeds the Quest stereo GPU budget
  settings.zCloudShadowScale  = false;
#else
  settings.zCloudShadowScale  = Gothic::settingsGetI("ENGINE","zCloudShadowScale") !=0;
#endif
  settings.ssaoHalfResolution = Gothic::settingsGetI("ENGINE","ssaoHalfResolution")!=0;
  settings.fogHalfResolution  = Gothic::settingsGetI("ENGINE","fogHalfResolution")==1;
  const float fadeDistance = Gothic::settingsGetF("ENGINE","cameraObstructionFadeDistance");
  settings.cameraObstructionDistance = Gothic::settingsGetI("ENGINE","cameraObstructionFade")!=0 && std::isfinite(fadeDistance)
                                      ? std::clamp(fadeDistance,50.f,500.f) : 0.f;
  settings.zFogRadial         = Gothic::settingsGetI("RENDERER_D3D","zFogRadial")!=0;
  {
    // wind
    settings.zWindEnabled = Gothic::inst().settingsGetI("ENGINE","zWindEnabled")!=0;

    const float period  = Gothic::inst().settingsGetF("ENGINE","zWindCycleTime");
    const float periodV = Gothic::inst().settingsGetF("ENGINE","zWindCycleTimeVar");
    settings.windPeriod = uint64_t((period+periodV)*1000.f);
    if(settings.windPeriod<=0) {
      settings.windPeriod   = 1;
      settings.zWindEnabled = false;
      }
  }

  settings.zVidBrightness     = Gothic::settingsGetF("VIDEO","zVidBrightness");
  settings.zVidContrast       = Gothic::settingsGetF("VIDEO","zVidContrast");
  settings.zVidGamma          = Gothic::settingsGetF("VIDEO","zVidGamma");

  settings.sunSize            = Gothic::settingsGetF("SKY_OUTDOOR","zSunSize");
  settings.moonSize           = Gothic::settingsGetF("SKY_OUTDOOR","zMoonSize");
  if(settings.sunSize<=1)
    settings.sunSize = 200;
  if(settings.moonSize<=1)
    settings.moonSize = 400;

  settings.vidResIndex = Gothic::inst().settingsGetF("INTERNAL","vidResIndex");
  settings.renderScale = settings.vidResIndex==0 ? 1.f : (settings.vidResIndex==1 ? 0.75f : 0.5f);
#if defined(__ANDROID__)
  settings.renderScale = float(std::clamp(Gothic::settingsGetI("ENGINE","renderScale"),50,100))/100.f;
  settings.vidResIndex = settings.renderScale==1.f ? 0 : 1;
#endif
  settings.aaEnabled   = (Gothic::options().aaPreset>0) && (settings.vidResIndex==0);

  // direct lighting
  if(settings.rtsmEnabled)
    shadow.directLightPso = &shaders.rtsmDirectLight;
  else if(settings.vsmEnabled)
    shadow.directLightPso = &shaders.vsmDirectLight; //TODO: naming
  else if(Gothic::options().doRayQuery && Resources::device().properties().descriptors.nonUniformIndexing &&
           settings.shadowResolution>0)
    shadow.directLightPso = &shaders.directLightRq;
  else if(settings.shadowResolution>0)
    shadow.directLightPso = &shaders.directLightSh;
  else
    shadow.directLightPso = &shaders.directLight;

  // point-lights
  if(settings.vsmEnabled)
    lights.directLightPso = &shaders.lightsVsm;
  else if(Gothic::options().doRayQuery && Resources::device().properties().descriptors.nonUniformIndexing)
    lights.directLightPso = &shaders.lightsRq;
  else
    lights.directLightPso = &shaders.lights;

  const auto gi = settings.giMethod;
  if(gi==GiMethod::Probes && Shaders::isGi1Supported() && Gothic::options().doRayQuery) {
    settings.giMethod = GiMethod::Probes;
    }
  else if(gi==GiMethod::IrrC && Shaders::isGi2Supported() && Gothic::options().doRayQuery) {
    settings.giMethod = GiMethod::IrrC;
    }
  else {
    settings.giMethod = GiMethod::None;
    }

  if(settings.giMethod!=GiMethod::None) {
    // need a projective shadow, for gi
    resetShadowmap();
    }

  resetSkyFog();
  resetShadowmap();
  }

void Renderer::toggleGi() {
  auto& device = Resources::device();
  if(!Gothic::options().doRayQuery)
    return;

  if(settings.giMethod==GiMethod::None && Gothic::options().doGi!=GiMethod::None)
    settings.giMethod = Gothic::options().doGi;
  else if(settings.giMethod==GiMethod::None && Shaders::isGi1Supported())
    settings.giMethod = GiMethod::Probes; // default for now
  else
    settings.giMethod = GiMethod::None;

  device.waitIdle();
  setupSettings();
  }

void Renderer::toggleVsm() {
  if(!Shaders::isVsmSupported())
    return;

  settings.vsmEnabled = !settings.vsmEnabled;

  auto& device = Resources::device();
  device.waitIdle();

  setupSettings();
  }

void Renderer::toggleRtsm() {
  if(!Shaders::isRtsmSupported())
    return;

  settings.rtsmEnabled = !settings.rtsmEnabled;
  setupSettings();
  }

void Renderer::togglePathtrace() {
  settings.pathTraceEnabled = !settings.pathTraceEnabled;
  pt.numFrames = 0;
  setupSettings();
  }

void Renderer::onWorldChanged() {
  sky.lutIsInitialized = false;
  resetSkyFog();
  }

void Renderer::setGizmo(bool enable, Tempest::Vec3 center) {
  gizmo.enable = enable;
  gizmo.center = center;
  }

void Renderer::setLightsHud(const Tempest::Texture2d* tex) {
  hud.light = tex;
  }

void Renderer::updateCamera(const WorldView& wview, const Camera& camera,const Camera* shadowCamera) {
  const auto& shadowView=shadowCamera?*shadowCamera:camera;
  proj        = camera.projective();
  viewProj    = camera.viewProj();
  viewProjLwc = camera.viewProjLwc();

  for(size_t i=0; i<Resources::ShadowLayers; ++i)
    shadowMatrix[i] = shadowView.viewShadow(wview.mainLight().dir(),i);
  shadowMatrixVsm = shadowView.viewShadowVsm(wview.mainLight().dir());

  auto zNear = camera.zNear();
  auto zFar  = camera.zFar();
  clipInfo.x = zNear*zFar;
  clipInfo.y = zNear-zFar;
  clipInfo.z = zFar;
  }

bool Renderer::requiresTlas() const {
  if(!Gothic::options().doRayQuery)
    return false;

  if(settings.giMethod!=GiMethod::None || settings.pathTraceEnabled)
    return true;
  if(!(settings.rtsmEnabled || settings.vsmEnabled))
    return true;
  return false;
  }

bool Renderer::requiresLightsTree() const {
  if(!Shaders::isLightsTreeSupported())
    return false;
  if(settings.giMethod==GiMethod::IrrC || settings.pathTraceEnabled)
    return true;
  return false;
  }

StorageImage& Renderer::usesImage3d(Tempest::StorageImage& ret, Tempest::TextureFormat frm, uint32_t w, uint32_t h, uint32_t d, bool mips) {
  if(ret.format()==frm && uint32_t(ret.w())==w && uint32_t(ret.h())==h && uint32_t(ret.d())==d && bool(ret.mipCount()>1)==mips)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().image3d(frm, w, h, d, mips);
  return ret;
  }

StorageImage& Renderer::usesImage2d(Tempest::StorageImage& ret, Tempest::TextureFormat frm, uint32_t w, uint32_t h, bool mips) {
  if(ret.format()==frm && uint32_t(ret.w())==w && uint32_t(ret.h())==h && ret.d()==1 && bool(ret.mipCount()>1)==mips)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().image2d(frm, w, h, mips);
  return ret;
  }

StorageImage& Renderer::usesImage2d(Tempest::StorageImage& ret, Tempest::TextureFormat frm, Tempest::Size size, bool mips) {
  if(ret.format()==frm && ret.size()==size && ret.d()==1 && (ret.mipCount()>1)==mips)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().image2d(frm, size, mips);
  return ret;
  }

ZBuffer& Renderer::usesZBuffer(Tempest::ZBuffer& ret, Tempest::TextureFormat frm, uint32_t w, uint32_t h) {
  if(textureCast<Texture2d&>(ret).format()==frm && uint32_t(ret.w())==w && uint32_t(ret.h())==h)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().zbuffer(frm, w, h);
  return ret;
  }

Attachment& Renderer::usesAttachment(Tempest::Attachment& ret, Tempest::TextureFormat frm, Tempest::Size size) {
  if(textureCast<Texture2d&>(ret).format()==frm && ret.size()==size)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().attachment(frm, size);
  return ret;
  }

Attachment& Renderer::usesAttachment(Tempest::Attachment& ret, Tempest::TextureFormat frm, uint32_t w, uint32_t h) {
  if(textureCast<Texture2d&>(ret).format()==frm && uint32_t(ret.w())==w && uint32_t(ret.h())==h)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().attachment(frm, w, h);
  return ret;
  }

StorageBuffer& Renderer::usesSsbo(Tempest::StorageBuffer& ret, size_t size) {
  if(ret.byteSize()==size)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().ssbo(Uninitialized, size);
  return ret;
  }

StorageBuffer& Renderer::usesSsboInit(Tempest::StorageBuffer& ret, size_t size) {
  if(ret.byteSize()==size)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().ssbo(nullptr, size);
  return ret;
  }

StorageBuffer& Renderer::usesScratch(Tempest::StorageBuffer& ret, size_t size) {
  if(ret.byteSize()>=size)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().ssbo(Uninitialized, size);
  return ret;
  }

void Renderer::prepareUniforms(WorldView& wview, const Camera& camera) {
  const Texture2d* sh[Resources::ShadowLayers] = {};
  for(size_t i=0; i<Resources::ShadowLayers; ++i)
    if(!shadowMap[i].isEmpty()) {
      sh[i] = &textureCast<const Texture2d&>(shadowMap[i]);
      }
  wview.setShadowMaps(sh);
  wview.setVirtualShadowMap(settings.vsmEnabled, vsm.pageData, vsm.pageTbl, vsm.pageHiZ, vsm.pageList);

  wview.setHiZ(textureCast<const Texture2d&>(hiz.hiZ));
  wview.setGbuffer(textureCast<const Texture2d&>(gbufDiffuse), textureCast<const Texture2d&>(gbufNormal));
  wview.setSceneImages(textureCast<const Texture2d&>(sceneOpaque), textureCast<const Texture2d&>(sceneDepth), zbuffer);
  wview.setWindEnabled(settings.zWindEnabled, settings.windPeriod);
  Vec4 fadeTarget = {};
  const auto player = Gothic::inst().player();
  if(player!=nullptr && !camera.isFirstPerson() && !camera.isCutscene() && !camera.isFree()) {
    auto center = player->centerPosition();
    camera.view().project(center);
    // Keep a 90 cm faded core, followed by a 90 cm soft edge.
    fadeTarget = Vec4(center.x, center.y, center.z, 180.f);
    }
  wview.setCameraObstructionFade(settings.pathTraceEnabled ? 0.f : settings.cameraObstructionDistance, fadeTarget);
  }

void Renderer::resetViewport(Tempest::Size res, Tempest::Size fullRes) {
  auto& device = Resources::device();
  device.waitIdle();

  const uint32_t w = uint32_t(res.w);
  const uint32_t h = uint32_t(res.h);

  sceneLinear = device.attachment(TextureFormat::R11G11B10UF,w,h);

  if(settings.aaEnabled) {
    cmaa2.workingEdges               = device.image2d(TextureFormat::R8, (w + 1) / 2, h);
    cmaa2.shapeCandidates            = device.ssbo(Tempest::Uninitialized, w * h / 4 * sizeof(uint32_t));
    cmaa2.deferredBlendLocationList  = device.ssbo(Tempest::Uninitialized, (w * h + 3) / 6 * sizeof(uint32_t));
    cmaa2.deferredBlendItemList      = device.ssbo(Tempest::Uninitialized, w * h * sizeof(uint32_t));
    cmaa2.deferredBlendItemListHeads = device.image2d(TextureFormat::R32U, (w + 1) / 2, (h + 1) / 2);
    cmaa2.controlBuffer              = device.ssbo(nullptr, 5 * sizeof(uint32_t));
    cmaa2.indirectBuffer             = device.ssbo(nullptr, sizeof(DispatchIndirectCommand) + sizeof(DrawIndirectCommand));
    }

  zbuffer = device.zbuffer(zBufferFormat,w,h);
  if(res!=fullRes)
    zbufferUi = device.zbuffer(zBufferFormat, fullRes); else
    zbufferUi = ZBuffer();

  uint32_t pw = nextPot(w);
  uint32_t ph = nextPot(h);

  uint32_t hw = pw;
  uint32_t hh = ph;
  while(hw>64 || hh>64) {
    hw = std::max(1u, (hw+1)/2u);
    hh = std::max(1u, (hh+1)/2u);
    }

  hiz.hiZ       = device.image2d(TextureFormat::R16,  hw, hh, true);
  hiz.hiZLeft   = device.image2d(TextureFormat::R16,  hw, hh, false);
  hiz.counter   = device.ssbo(nullptr, sizeof(uint32_t));

  {
#if defined(__ANDROID__)
    // VR: the stash feeds water refraction and translucency only; half resolution
    // halves its load and store (about 0.45 ms per eye at full resolution).
    const uint32_t sw = vrStashHalf ? std::max(1u,(w+1)/2) : w, sh = vrStashHalf ? std::max(1u,(h+1)/2) : h;
#else
    const uint32_t sw = w, sh = h;
#endif
    sceneOpaque   = device.attachment(TextureFormat::R11G11B10UF,sw,sh);
    sceneDepth    = device.attachment(TextureFormat::R32F,       sw,sh);
  }

  gbufDiffuse   = device.attachment(TextureFormat::RGBA8,w,h);
  gbufNormal    = device.attachment(TextureFormat::R32U, w,h);

  ssao     = decltype(ssao)();
  epipolar = decltype(epipolar)();
  vsm      = decltype(vsm)();
  rtsm     = decltype(rtsm)();

  // volumetric sky
  sky.occlusionLut = StorageImage();

  // software renderer
  swr.outputImage = StorageImage();

  // swrt
  swrt.outputImage = StorageImage();

  resetSkyFog();
  }

void Renderer::resetShadowmap() {
  vrShadowClearNeeded=true;
  auto& device = Resources::device();

  Log::i("Shadow map resolution = ", settings.shadowResolution);

  for(int i=0; i<Resources::ShadowLayers; ++i)
    Resources::recycle(std::move(shadowMap[i]));

  const bool forceSm1 = (settings.giMethod==GiMethod::Probes || settings.giMethod==GiMethod::IrrC ||
                         settings.pathTraceEnabled || sky.quality==PathTrace);
  for(int i=0; i<Resources::ShadowLayers; ++i) {
    if(!(i==1 && forceSm1)) {
      if(settings.vsmEnabled && !(settings.rtsmEnabled && i==1))
        continue; //TODO: support vsm in gi code
      if(settings.rtsmEnabled && !(sky.quality!=None && i==1))
        continue; //TODO: support vsm in gi code
      }
#if defined(__ANDROID__)
    // VR cached shadow: map 0 holds the NPC casters within vrShadowDynamicWidth
    // (24 m) and needs half the texels of the 64 m static map for the same
    // texel size.
    const uint32_t res = (i==0) ? std::max<uint32_t>(settings.shadowResolution/2, 256) : settings.shadowResolution;
    shadowMap[i] = device.zbuffer(shadowFormat, res, res);
#else
    shadowMap[i] = device.zbuffer(shadowFormat, settings.shadowResolution, settings.shadowResolution);
#endif
    }
#if defined(__ANDROID__)
  // VR cached shadow: the back buffer of the static map.
  Resources::recycle(std::move(vrShadowBack));
  if(!shadowMap[1].isEmpty())
    vrShadowBack = device.zbuffer(shadowFormat, settings.shadowResolution, settings.shadowResolution);
  vrShadowSlicer.reset();
  vrShadowStep        = {};
  vrShadowBounds.clear();
  vrShadowFrontValid  = false;
  vrShadowSwapPending = false;
  vrShadowLogged      = false;
  // Shadow tiles: the new maps' content is unknown, so both
  // tile maps are rebuilt from them before the direct light reads them.
  vrShadowTileDynamicValid = false;
  vrShadowTileStaticValid  = false;
  vrShadowStaticClear      = false;
  vrShadowDynamicEmpty     = false;
#endif
  }

void Renderer::setVrFogMode(int mode) {
  mode=std::clamp(mode,0,1);
  if(vrFogMode==mode) return;
  vrFogMode=mode; resetSkyFog();
  Log::i("VR fog: ",mode==0?"Mobile atmosphere":"Volumetric sunshafts");
}

void Renderer::resetSkyFog() {
  auto& device = Resources::device();

  {
    auto q = Quality::VolumetricLQ;
    if(!settings.zFogRadial) {
      q = Quality::VolumetricLQ;
      } else {
      q = Quality::VolumetricHQ;
      // q = Quality::Epipolar;
      }

    if(vrFogMode==0) q=Quality::VolumetricLQ;
    if(skyPathTrace)
      q = PathTrace;

    const bool halfResolution = (settings.fogHalfResolution || vrFogMode==0) && q!=PathTrace;
    if(sky.quality==q && sky.fogHalfResolution==halfResolution && sky.vrFogMode==vrFogMode) {
      return;
      }

    sky.quality = q;
    sky.fogHalfResolution = halfResolution;
    sky.vrFogMode = vrFogMode;
  }

  Resources::recycle(std::move(sky.fogLut3D));
  Resources::recycle(std::move(sky.fogLut3DMs));

  sky.lutIsInitialized = false;

  // Reduce only the angular sampling of the lighting volume.
  // Keep all depth steps and the separate sunshaft occlusion calculation unchanged.
  const uint32_t divisor = sky.fogHalfResolution ? 2 : 1;
  switch(sky.quality) {
    case None:
    case VolumetricLQ:
      sky.fogLut3D   = vrFogMode==0 ? device.image3d(sky.lutRGBAFormat,64,32,32) :
                                    device.image3d(sky.lutRGBAFormat,160/divisor,90/divisor,64);
      break;
    case VolumetricHQ:
      // Fog lighting and occlusion are decoupled.
      sky.fogLut3D   = device.image3d(sky.lutRGBFormat,  128/divisor, 64/divisor, 32);
      sky.fogLut3DMs = device.image3d(sky.lutRGBAFormat, 128/divisor, 64/divisor, 32);
      break;
    case Epipolar:
      sky.fogLut3D   = device.image3d(sky.lutRGBFormat,  128/divisor, 64/divisor, 32);
      sky.fogLut3DMs = device.image3d(sky.lutRGBAFormat, 128/divisor, 64/divisor, 32);
      break;
    case PathTrace:
      break;
    }
  if(sky.quality!=PathTrace)
    Log::i("Fog lighting volume = ", sky.fogLut3D.w(), "x", sky.fogLut3D.h(), "x", sky.fogLut3D.d());
  }

void Renderer::prepareSky(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview, const Vr::SkyRate::Plan& plan) {
  auto& scene   = wview.sceneGlobals();
  // VR reduced rate (vr/vrskyrate.h): the plan names the LUTs
  // this frame draws; the default plan (desktop, path tracing) draws all.
  const bool drawView = plan.viewLut  || !sky.lutIsInitialized;
  const bool drawCld  = plan.cloudLut || !sky.lutIsInitialized;
  if(!drawView && !drawCld)
    return;

  cmd.setDebugMarker("Sky LUT");
  if(!sky.lutIsInitialized) {
    sky.lutIsInitialized = true;

    cmd.setFramebuffer({});
    cmd.setBinding(0, sky.cloudsLut);
    cmd.setBinding(5, *wview.sky().cloudsDay()  .lay[0], Sampler::trillinear());
    cmd.setBinding(6, *wview.sky().cloudsDay()  .lay[1], Sampler::trillinear());
    cmd.setBinding(7, *wview.sky().cloudsNight().lay[0], Sampler::trillinear());
    cmd.setBinding(8, *wview.sky().cloudsNight().lay[1], Sampler::trillinear());
    cmd.setPipeline(shaders.cloudsLut);
    cmd.dispatchThreads(size_t(sky.cloudsLut.w()), size_t(sky.cloudsLut.h()));

    auto sz = Vec2(float(sky.transLut.w()), float(sky.transLut.h()));
    cmd.setFramebuffer({{sky.transLut, Tempest::Discard, Tempest::Preserve}});
    cmd.setBinding(5, *wview.sky().cloudsDay()  .lay[0], Sampler::trillinear());
    cmd.setBinding(6, *wview.sky().cloudsDay()  .lay[1], Sampler::trillinear());
    cmd.setBinding(7, *wview.sky().cloudsNight().lay[0], Sampler::trillinear());
    cmd.setBinding(8, *wview.sky().cloudsNight().lay[1], Sampler::trillinear());
    cmd.setPushData(&sz, sizeof(sz));
    cmd.setPipeline(shaders.skyTransmittance);
    cmd.draw(nullptr, 0, 3);

    sz = Vec2(float(sky.multiScatLut.w()), float(sky.multiScatLut.h()));
    cmd.setFramebuffer({{sky.multiScatLut, Tempest::Discard, Tempest::Preserve}});
    cmd.setBinding(0, sky.transLut, sky.sampler);
    cmd.setPushData(&sz, sizeof(sz));
    cmd.setPipeline(shaders.skyMultiScattering);
    cmd.draw(nullptr, 0, 3);
    }

  if(drawView) {
    auto sz = Vec2(float(sky.viewLut.w()), float(sky.viewLut.h()));
    cmd.setFramebuffer({{sky.viewLut, Tempest::Discard, Tempest::Preserve}});
    cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, sky.transLut,     sky.sampler);
    cmd.setBinding(2, sky.multiScatLut, sky.sampler);
    cmd.setBinding(3, sky.cloudsLut,    Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setPushData(&sz, sizeof(sz));
    cmd.setPipeline(shaders.skyViewLut);
    cmd.draw(nullptr, 0, 3);
    }

  if(drawCld) {
    auto sz = Vec2(float(sky.viewCldLut.w()), float(sky.viewCldLut.h()));
    cmd.setFramebuffer({{sky.viewCldLut, Tempest::Discard, Tempest::Preserve}});
    cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, sky.viewLut, sky.sampler);
    cmd.setBinding(2, *wview.sky().cloudsDay()  .lay[0], Sampler::trillinear());
    cmd.setBinding(3, *wview.sky().cloudsDay()  .lay[1], Sampler::trillinear());
    cmd.setBinding(4, *wview.sky().cloudsNight().lay[0], Sampler::trillinear());
    cmd.setBinding(5, *wview.sky().cloudsNight().lay[1], Sampler::trillinear());
    cmd.setPushData(&sz, sizeof(sz));
    cmd.setPipeline(shaders.skyViewCldLut);
    cmd.draw(nullptr, 0, 3);
    }
  }

void Renderer::draw(Attachment& output, Encoder<CommandBuffer>& cmd, uint8_t fId,
                    VectorImage::Mesh& uiLayer, VectorImage::Mesh& numOverlay,
                    InventoryMenu& inventory, VideoWidget& video, float hdrPeakNits) {
  vrLightDepthActive=false;
  vrMergedTransparencyActive=false;
  // Menus can render before any world has waited for the asynchronous shader compiler.
  if(hdrPeakNits>0)
    shaders.waitCompiler();
  // Compose the existing SDR UI at paper white, not at the display's peak brightness.
  const float paperWhite = std::min(200.f, hdrPeakNits);
  hdrPeakRatio = hdrPeakNits>0 ? hdrPeakNits/paperWhite : 0.f;
  auto& result = hdrPeakNits>0 ? usesAttachment(hdrComposite, TextureFormat::RGBA16F, output.size()) : output;
  auto wview  = Gothic::inst().worldView();
  auto camera = Gothic::inst().camera();

  if(!video.isActive() && wview!=nullptr && camera!=nullptr) {
    draw(result, cmd, fId, *wview, *camera);
    cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}});
    } else {
    cmd.setFramebuffer({{result, Vec4(), Tempest::Preserve}});
    }
  cmd.setDebugMarker("UI");
  uiLayer.draw(cmd);

  if(inventory.isOpen()!=InventoryMenu::State::Closed) {
    auto& zb = (zbufferUi.isEmpty() ? zbuffer : zbufferUi);
    cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}},{zb, 1.f, Tempest::Preserve});
    cmd.setDebugMarker("Inventory");
    inventory.draw(cmd);

    cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}});
    cmd.setDebugMarker("Inventory-counters");
#if !defined(__ANDROID__)
    numOverlay.draw(cmd);
#endif
    }
#if defined(__ANDROID__)
  // The Android overlay also contains the FPS counter outside the inventory.
  if(!video.isActive())
    numOverlay.draw(cmd);
#endif
  if(hdrPeakNits>0) {
    cmd.setDebugMarker("HDR output");
    cmd.setFramebuffer({{output, Tempest::Discard, Tempest::Preserve}});
    cmd.setBinding(0, hdrComposite, Sampler::nearest(ClampMode::ClampToEdge));
    cmd.setPushData(Vec2(paperWhite, hdrPeakNits));
    cmd.setPipeline(shaders.hdrOutput);
    cmd.draw(nullptr, 0, 3);
    }
  // Save thumbnails and other offscreen draws remain SDR.
  hdrPeakRatio = 0;
  }

void Renderer::dbgDraw(Tempest::Painter& p) {
  static bool dbg = false;
  if(!dbg)
    return;

  std::vector<const Texture2d*> tex;
  //tex.push_back(&textureCast(swr.outputImage));
  //tex.push_back(&textureCast(hiz.hiZ));
  //tex.push_back(&textureCast(hiz.smProj));
  //tex.push_back(&textureCast(hiz.hiZSm1));
  //tex.push_back(&textureCast(shadowMap[1]));
  //tex.push_back(&textureCast<const Texture2d&>(shadowMap[0]));
  //tex.push_back(&textureCast<const Texture2d&>(vsm.pageData));
  //tex.push_back(&textureCast<const Texture2d&>(swrt.outputImage));
  tex.push_back(&textureCast<const Texture2d&>(surf.irrImage));

  static int size = 400;
  int left = 10;
  for(auto& t:tex) {
    p.setBrush(Brush(*t,Painter::NoBlend,ClampMode::ClampToBorder));
    auto sz = Size(p.brush().w(),p.brush().h());
    if(sz.isEmpty())
      continue;
    while(sz.w<size && sz.h<size) {
      sz.w *= 2;
      sz.h *= 2;
      }
    while(sz.w>size*2 || sz.h>size*2) {
      sz.w = (sz.w+1)/2;
      sz.h = (sz.h+1)/2;
      }
    p.drawRect(left,50,sz.w,sz.h,
               0,0,p.brush().w(),p.brush().h());
    left += (sz.w+10);
    }
  }

VisualObjects::StageCpuStats Renderer::stageVrCpu(WorldView& view,uint64_t tickCount) {
  return view.stageCpu(tickCount,settings.zWindEnabled,settings.windPeriod);
  }

void Renderer::draw(Tempest::Attachment& result, Encoder<CommandBuffer>& cmd, uint8_t fId, WorldView& wview, const Camera& camera,const Camera* shadowCamera,bool secondEye,
                    Encoder<CommandBuffer>* prep, const std::function<void()>& onPrepared) {
  // Split left eye: everything up to the GBuffer (uploads, visibility, sky LUT,
  // HiZ, shadow maps, irradiance) goes to `prep` when given; onPrepared submits
  // it so the GPU starts while the rest is still recorded.
  Encoder<CommandBuffer>& pc = prep!=nullptr ? *prep : cmd;
  // the eye-target request covers this draw only (consumed
  // first, so a failed eye cannot leak it into a menu/cinema draw).
  const bool eyeTarget = vrEyeTarget;
  vrEyeTarget       = false;
  vrScaleRectActive = false;
  vrLightDepthActive=false;
  vrMergedTransparencyActive=false;
#if defined(GOTHIC2VR_OPENXR)
  double cpuStart=Vr::milliseconds(); vrCpu={};
#endif
  //FIXME: those also needed to be refactor away
  const auto world     = Gothic::inst().world();
  const auto gameTime  = world!=nullptr ? world->time() : gtime(8,0);
  const auto tickCount = world!=nullptr ? world->tickCount() : uint64_t();

  shaders.waitCompiler();

  const auto res = internalResolution(result.size());
  if(res!=zbuffer.size()) {
    resetViewport(res, result.size());
    }
#if defined(__ANDROID__)
  if(eyeTarget) {
    // Render scale < 1: tonemap the internal size into the
    // top-left rectangle of the eye image; the projection layer's imageRect is
    // that rectangle (vrwindow -> QuestXr::setEyeRect, the same vrEyeRect()).
    // Capture 025: the upscale pass cost +0.93 ms per pair at 0.7 and the lost
    // fog fold 0.3 ms per eye (capture 040). Scale 1: the old path, unchanged.
    const auto rect   = vrEyeRect(result.size());
    vrScaleRectActive = rect!=result.size();
    if(!vrScaleRectLogged) {
      vrScaleRectLogged = true;
      Log::i("VR scale rect: ", vrScaleRect ? (settings.aaEnabled ? "off (CMAA2 on: upscale path)" : "active (render scale < 1 tonemaps into an eye image sub-rectangle, fog fold kept)")
                                           : "off (vrScaleRectOff=1): render scale < 1 upscales into the whole eye image");
      }
    if(vrScaleRectActive && rect!=vrScaleRectSize)
      Log::i("VR scale rect: eye ", rect.w, "x", rect.h, " of ", result.w(), "x", result.h(), " (render scale ", internalResolutionScale(), ")");
    vrScaleRectSize = vrScaleRectActive ? rect : Size();
    }
#else
  (void)eyeTarget;
#endif

  if(!secondEye && requiresTlas())
    wview.updateRtScene();
  if(!secondEye)
    wview.updateLights(gameTime);

  if(!secondEye && requiresLightsTree())
    prepareLightsBvh(cmd, wview);

  updateCamera(wview, camera,shadowCamera);

#if defined(__ANDROID__)
  prepareVrShadow(wview, shadowCamera ? *shadowCamera : camera, secondEye);
#endif
  static bool updFr = true;
  if(updFr){
#if defined(__ANDROID__)
    if(vrShadowActive) {
      // Cached shadow: dynamic map through V_Shadow0, the static build through V_Vsm.
      if(vrShadowSunUp) {
        frustrum[SceneGlobals::V_Shadow0].make(shadowMatrix[0],shadowMap[0].w(),shadowMap[0].h(),Frustrum::DepthRange::ZeroToOne);
        frustrum[SceneGlobals::V_Vsm].make(vrShadowSlicer.build,vrShadowBack.w(),vrShadowBack.h(),Frustrum::DepthRange::ZeroToOne);
        } else {
        frustrum[SceneGlobals::V_Shadow0].clear();
        frustrum[SceneGlobals::V_Vsm].clear();
        }
      frustrum[SceneGlobals::V_Shadow1].clear();
      } else
#endif
    {
    if(wview.mainLight().dir().y>Camera::minShadowY) {
      frustrum[SceneGlobals::V_Shadow0].make(shadowMatrix[0],shadowMap[0].w(),shadowMap[0].h(),Frustrum::DepthRange::ZeroToOne);
      frustrum[SceneGlobals::V_Shadow1].make(shadowMatrix[1],shadowMap[1].w(),shadowMap[1].h(),Frustrum::DepthRange::ZeroToOne);
      } else {
      frustrum[SceneGlobals::V_Shadow0].clear();
      frustrum[SceneGlobals::V_Shadow1].clear();
      }
    frustrum[SceneGlobals::V_Vsm] = frustrum[SceneGlobals::V_Shadow1]; //TODO: remove
    }
    frustrum[SceneGlobals::V_Main].make(viewProj,zbuffer.w(),zbuffer.h(),Frustrum::DepthRange::ZeroToOne);
    frustrum[SceneGlobals::V_HiZ] = frustrum[SceneGlobals::V_Main];
    wview.updateFrustrum(frustrum);
    }

#if defined(__ANDROID__)
  wview.setVrIndexedObjects(vrIndexedObjects);
  wview.setVrBatchedObjects(vrBatchedObjects);
  {
    // Object draw distance: 300 m is the community D3D11 renderer's default,
    // 120 m the original engine's zone far plane. Objects smaller than 7.5 m
    // in radius use a third of the distance. Objects below 4 px of bounding
    // radius are dropped at any distance (angular size from the eye's focal
    // length in pixels).
    static const float objectFar[4] = {0.f, 30000.f, 20000.f, 12000.f};
    const float far   = objectFar[std::clamp(vrObjectDistance,0,3)];
    const float focal = std::abs(camera.projective().at(0,0))*float(std::max(1,zbuffer.w()))*0.5f;
    wview.setVrObjectCull(far, far/3.f, (far>0.f && focal>0.f) ? 4.f/focal : 0.f);
    // Terrain level of detail: 60 m tiles beyond the distance use the first
    // clustered level, beyond twice the distance the second (Performance ->
    // Terrain detail); only when the landscape was packed with the levels.
    static const float lodNear[4] = {0.f, 20000.f, 12000.f, 8000.f};
    wview.setVrTerrainLod(wview.landscape().lodLevels()>=2 ? lodNear[std::clamp(vrTerrainLod,0,3)] : 0.f);
    // Stereo HiZ: the second eye builds its HiZ from the first eye's final
    // depth (0.0.42) but still draws the first eye's occluder seed: capture
    // 043 showed the seed draw doubles as the GBuffer's depth pre-pass (right
    // GBuffer 1.26 -> 2.14 ms without it, at equal visible meshlet counts).
    vrStereoHiZActive = vrStereoHiZ && !shaders.hiZStereo.isEmpty() && !hiz.hiZLeft.isEmpty();
    wview.setVrSeedNextEye(!secondEye && vrStereoSeed);
    wview.setVrSkipSeedCull(false);
    if(!secondEye) { vrLeftProj = camera.projective(); vrLeftView = camera.view(); vrLeftOrigin = camera.originLwc(); vrLeftEyeValid = true; }
  }
#endif
  prepareUniforms(wview, camera);
  wview.preFrameUpdate(camera, tickCount, fId,shadowCamera,secondEye);
#if defined(GOTHIC2VR_OPENXR)
  vrCpu.update=Vr::milliseconds()-cpuStart; cpuStart=Vr::milliseconds();
#endif
  wview.prepareGlobals(pc,fId);
#if defined(GOTHIC2VR_OPENXR)
  vrCpu.uploads=Vr::milliseconds()-cpuStart; cpuStart=Vr::milliseconds();
#endif

  if(settings.pathTraceEnabled) {
    drawPathtrace(cmd, wview, fId);
    cmd.setDebugMarker("Tonemapping");
    drawTonemapping(result, cmd, wview);
    wview.postFrameupdate();
    return;
    }

  wview.visibilityPass(pc, 0, secondEye || !vrShadows, secondEye && vrStereoSeed);
  // Sky LUTs, first eye only. VR reduced rate (vr/vrskyrate.h):
  // the plan names the LUTs this frame draws; the default plan draws all.
  Vr::SkyRate::Plan skyPlan;
#if defined(__ANDROID__)
  if(!secondEye) {
    if(vrSkyRate) {
      Vr::SkyRate::Inputs in;
      in.sunDir       = wview.sky().sunLight().dir();
      // The view LUT's plPosY, exactly as SceneGlobals::setWorld computed it in
      // preFrameUpdate: the near-plane centre (2 cm ahead of the eye), so head
      // pitch moves it too; the camera origin missed that (review R2).
      Vec3 plPos = Vec3(0,0,0);
      wview.sceneGlobals().viewProjectInv().project(plPos);
      float plPosY = plPos.y/100.f;
      plPosY += (-wview.bbox().first.y)/100.f;
      in.height       = std::clamp(plPosY, 0.f, 1000.f);
      in.sunIntensity = wview.sky().sunIntensity();
      in.night        = wview.sky().isNight();
      skyPlan = vrSkyRateState.plan(in, !sky.lutIsInitialized);
      }
    if(!vrSkyRateLogged) {
      Log::i(vrSkyRate ? "VR sky rate: active (view LUT on change or every 8th frame, cloud LUT every 2nd frame, irradiance on the other frames, second eye applies the first eye's exposure)"
                       : "VR sky rate: off (vrSkyRateOff=1)");
      vrSkyRateLogged = true;
      }
    }
#endif
  if(!secondEye) prepareSky(pc, wview, skyPlan);

#if defined(__ANDROID__)
  if(secondEye && vrStereoHiZActive && vrLeftEyeValid) {
    buildHiZStereo(pc, camera);                     // HiZ for culling, from the first eye's depth
    drawHiZ (pc, wview, secondEye && vrStereoSeed); // depth pre-pass only (clears depth, no HiZ rebuild)
    } else
#endif
  {
  drawHiZ (pc, wview, secondEye && vrStereoSeed);
  buildHiZ(pc);
  }

  wview.visibilityPass(pc, 1, secondEye || !vrShadows, secondEye && vrStereoSeed);
  // Shadow maps and the irradiance LUT do not depend on this eye's GBuffer.
  if(!secondEye) drawShadowMap(pc, fId, wview);
  if(!secondEye && skyPlan.irradiance) prepareIrradiance(pc, wview);
  pc.setFramebuffer({});
  if(prep!=nullptr && onPrepared) {
    // InstanceStorage::commit uploads object patches on a worker. The prep
    // buffer reads those patches, so its early submit needs the same upload
    // join as the main buffer at the end of the world draw.
    wview.postFrameupdate();
    onPrepared();
  }
#if defined(__ANDROID__)
  // Fixed foveation (Performance -> Foveation): the density map is attached
  // only to passes that clear or discard their attachments (GBuffer, lighting,
  // stash, tonemapping). Passes that load an existing image (water, fog
  // composite) run without it: 0.0.28 showed black bands there.
  prepareVrFoveation(cmd, uint32_t(sceneLinear.w()), uint32_t(sceneLinear.h()));
#endif
  drawGBuffer(cmd, fId, wview);
  cmd.setFramebuffer({});

  prepareEpipolar(cmd, wview);

  if(vrShadows) {
    drawVsm(cmd, wview);
    drawSwr(cmd, wview);
    drawRtsm(cmd, wview);
    drawRtsmOmni(cmd, wview);
    }

  drawSwRT(cmd, wview);

  // prepareGlobals uploads fresh per-eye lighting, including exposure=1.
  // Exposure also transforms sunColor/ambient in that buffer. Apply it after
  // EVERY upload; sharing the sky LUT does not preserve these GPU-written fields.
#if defined(__ANDROID__)
  // VR: the first eye also stores its result; the second eye
  // applies it to its own freshly uploaded buffer instead of recomputing it
  // (same LUTs, same sky fields: exact).
  const uint8_t exposureMode = !vrSkyRate ? 0 : (!secondEye ? 1 : (vrExposureStored ? 2 : 0));
  vrExposureStored = (exposureMode==1);
  prepareExposure(cmd, wview, exposureMode);
#else
  prepareExposure(cmd, wview);
#endif
  if(vrLighting) prepareSSAO(cmd, wview);
  prepareFog (cmd, wview);
  cmd.setFramebuffer({});
  cmd.setDebugMarker("After fog compute");
  if(vrLighting) {
    prepareGi(cmd, wview);
    prepareSurfels(cmd, wview);
    }
  prepareVisibleLights(cmd, wview, secondEye);

  cmd.setFramebuffer({});
  cmd.setDebugMarker(vrLighting ? "Lighting+Sky" : "Unlit+Sky");
  if(vrLighting && vrDiagnostics) {
    // Diagnostic frames (capture 045: the first eye's direct light costs
    // ~0.65 ms more than the second eye's indoors, in every capture since
    // 009): an empty pass first absorbs any wait on earlier work, then the
    // direct light is timed alone.
    cmd.setFramebuffer({{sceneLinear, Tempest::Discard, Tempest::Preserve}}, {zbuffer, Tempest::Readonly});
    cmd.setFramebuffer({});
    cmd.setDebugMarker("Probe direct light");
    cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}}, {zbuffer, Tempest::Readonly});
    } else
    cmd.setFramebuffer({{sceneLinear, Tempest::Discard, Tempest::Preserve}}, {zbuffer, Tempest::Readonly});
  bool combinedAmbient = false;
#if defined(__ANDROID__)
  combinedAmbient = vrFastLighting && !settings.zCloudShadowScale && settings.giMethod==GiMethod::None &&
                    (shadow.directLightPso==&shaders.directLight || shadow.directLightPso==&shaders.directLightSh);
#endif
  if(vrLighting) {
    drawShadowResolve(cmd, wview, combinedAmbient);
    if(!combinedAmbient) {
      if(vrDiagnostics) {
        cmd.setFramebuffer({});
        cmd.setDebugMarker("Probe ambient");
        cmd.setFramebuffer({{sceneLinear,Tempest::Preserve,Tempest::Preserve}}, {zbuffer,Tempest::Readonly});
        }
      drawAmbient(cmd, wview);
      }
    if(vrDiagnostics) {
      // Separate render passes only on diagnostic frames. Tile GPUs can
      // defer fragment work; markers inside one pass do not isolate it.
      cmd.setFramebuffer({});
      cmd.setDebugMarker("Probe local lights");
      cmd.setFramebuffer({{sceneLinear,Tempest::Preserve,Tempest::Preserve}}, {zbuffer,Tempest::Readonly});
    }
    drawLights(cmd, wview);
    if(vrDiagnostics) {
      cmd.setFramebuffer({});
      cmd.setDebugMarker("Probe sky");
      cmd.setFramebuffer({{sceneLinear,Tempest::Preserve,Tempest::Preserve}}, {zbuffer,Tempest::Readonly});
    }
    }
#if defined(__ANDROID__)
  else {
    cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
    cmd.setPipeline(shaders.unlit);
    cmd.draw(nullptr, 0, 3);
    }
#endif
  drawSky(cmd, wview);
  drawLightTreeDbg(sceneLinear, cmd, wview);

  stashSceneAux(cmd);
  cmd.setFragmentDensityMap(nullptr); // water, translucent and fog composite load sceneLinear: no density map

  bool mergeTransparency=false;
#if defined(__ANDROID__)
  mergeTransparency=vrMergedTransparency && Resources::device().properties().render.independentBlend;
#endif
  const bool waterPass=drawGWater(cmd, wview, mergeTransparency);
  vrMergedTransparencyActive=mergeTransparency && waterPass;
  if(!vrMergedTransparencyActive) {
    cmd.setFramebuffer({});
    cmd.setDebugMarker("Translucent+sun");
    cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}}, {zbuffer, Tempest::Preserve, Tempest::Preserve});
    cmd.setDebugMarker("Sun&Moon");
    }
  // The Vulkan MRT output masks preserve Water's diffuse/normal attachments
  // when subsequent shaders write only HDR. Scene sampling uses the stash.
  drawSunMoon(cmd, wview);
  if(!vrMergedTransparencyActive) cmd.setDebugMarker("Translucent");
  wview.drawTranslucent(cmd, fId);

  //drawHashDbg(sceneLinear, cmd, wview);
  drawProbesDbg(cmd, wview);
  drawProbesHitDbg(cmd);
  drawSurfelsDbg(cmd, wview);
  drawVsmDbg(cmd, wview);
  drawSwrDbg(cmd, wview);
  drawRtsmDbg(cmd, wview);
  drawRayQueryDbg(cmd, wview);
  //
  drawLightsHud(cmd, wview);

  vrFogFoldActive = false;
#if defined(__ANDROID__)
  // Fold the reflections and the LQ fog composite into the tonemapping pass
  // (lighting/tonemapping_fog.frag): one load/store of the eye image less.
  // Native resolution, or render scale < 1 through the eye sub-rectangle
  //: the fold's gl_FragCoord texel addressing starts at 0,0.
  vrFogFoldActive = vrFogFold && !camera.isInWater() && (sky.quality==None || sky.quality==VolumetricLQ) &&
                    !settings.aaEnabled && (internalResolutionScale()>=0.999f || vrScaleRectActive) && !shaders.tonemappingFog.isEmpty();
  if(!vrFogFoldLogged) { Log::i("VR fog fold: ",vrFogFoldActive ? "active" : "off"); vrFogFoldLogged = true; }
#endif
  if(!vrFogFoldActive) {
  cmd.setFramebuffer({});
  cmd.setDebugMarker("Reflections+fog composite");
  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}});
  drawReflections(cmd, wview);
  drawGizmo(cmd, wview);
  if(camera.isInWater()) {
    cmd.setDebugMarker("Underwater");
    drawUnderwater(cmd, wview);
    } else {
    cmd.setDebugMarker("Fog");
    drawFog(cmd, wview);
    }
  }

  cmd.setFramebuffer({});
#if defined(__ANDROID__)
  if(!settings.aaEnabled) cmd.setFragmentDensityMap(vrFdmValid ? &vrFdm : nullptr); // tonemapping discards its target
#endif
  if(settings.aaEnabled) {
    cmd.setDebugMarker("CMAA2 & Tonemapping");
    drawCMAA2(result, cmd, wview);
    } else {
    cmd.setDebugMarker("Tonemapping");
    drawTonemapping(result, cmd, wview);
    }

  //drawRayQueryDbg(cmd, wview);
  //drawHashDbg(result, cmd, wview);

  cmd.setFramebuffer({});
  cmd.setFragmentDensityMap(nullptr);
  cmd.setDebugMarker("World end");
  wview.postFrameupdate();
#if defined(GOTHIC2VR_OPENXR)
  vrCpu.encode=Vr::milliseconds()-cpuStart;
#endif
  }

void Renderer::prepareVrFoveation(Encoder<CommandBuffer>& cmd, uint32_t w, uint32_t h) {
  auto& device = Resources::device();
  const auto& fd = device.properties().fragmentDensity;
  if(!vrFdmLogged) {
    vrFdmLogged = true;
    Log::i("VR foveation: fragment density map ", fd.supported ? "supported" : "unsupported", " texel ", fd.minTexel, "-", fd.maxTexel, " px");
    }
  const int level = fd.supported ? std::clamp(vrFoveation,0,3) : 0;
  if(level!=vrFdmLevel || w!=vrFdmW || h!=vrFdmH) {
    vrFdmLevel = level; vrFdmW = w; vrFdmH = h;
    vrFdm = Texture2d(); vrFdmValid = false;
    if(level>0 && w>0 && h>0) {
      const uint32_t texel = std::clamp<uint32_t>(32u, std::max<uint32_t>(fd.minTexel,1u), std::max<uint32_t>(fd.maxTexel,1u));
      vrFdm = device.fragmentDensityMap(Vr::foveationMap(level,w,h,texel));
      vrFdmValid = vrFdm.w()>0;
      Log::i("VR foveation level ", level, ": map ", Vr::foveationMapSize(w,texel), "x", Vr::foveationMapSize(h,texel),
             " texel ", texel, " px for ", w, "x", h, vrFdmValid ? "" : " (creation failed, off)");
      }
    }
  cmd.setFragmentDensityMap(vrFdmValid ? &vrFdm : nullptr);
  }

void Renderer::drawTonemapping(Attachment& result, Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  struct Push {
    float brightness = 0;
    float contrast   = 1;
    float gamma      = 1.f/2.2f;
    float mul        = 1;
    float hdrPeak    = 0;
    float padding[3] = {};
    };

  Push p;
  p.hdrPeak = hdrPeakRatio;
  p.brightness = (settings.zVidBrightness - 0.5f)*0.1f;
  p.contrast   = std::max(1.5f - settings.zVidContrast, 0.01f);
  p.gamma      = p.gamma/std::max(2.0f*settings.zVidGamma,  0.01f);

  static float mul = 0.f;
  if(mul>0)
    p.mul = mul;

  // One output pixel per sceneLinear texel: native resolution or the eye
  // sub-rectangle; otherwise the Lanczos upscale.
  const bool native = internalResolutionScale()>=0.999f || vrScaleRectActive;
  auto& pso = vrFogFoldActive ? shaders.tonemappingFog : (native ? shaders.tonemapping : shaders.tonemappingUpscale);
#if defined(__ANDROID__)
  if(vrFogFoldActive && vrHorizonHaze) {
    // Horizon haze band as in drawFog, carried in the unused hdr slots.
    const float far = wview.sceneGlobals().clipInfo().z;
    p.padding[0] = far*0.55f;
    p.padding[1] = far*0.95f;
    }
#endif
  const bool overlay = vrOverlay.depth!=nullptr && vrOverlay.draw;
  const int  rectW = sceneLinear.w(), rectH = sceneLinear.h();
  // only the internal-size top-left rectangle of the eye
  // image is loaded, drawn and stored; the hands overlay (full-size depth)
  // draws through the same viewport.
  if(vrScaleRectActive)
    cmd.setRenderArea(Rect(0, 0, rectW, rectH));
  if(overlay)
    cmd.setFramebuffer({ {result, Tempest::Discard, Tempest::Preserve} }, {*vrOverlay.depth, 1.f, Tempest::Discard});
  else
    cmd.setFramebuffer({ {result, Tempest::Discard, Tempest::Preserve} });
  if(vrScaleRectActive) {
    cmd.setViewport(0, 0, rectW, rectH);
    cmd.setScissor (0, 0, rectW, rectH);
    }
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  // This Lanczos implementation pairs adjacent weights using bilinear sampling.
  // Keep nearest sampling at native resolution.
  const auto sampler = native ? Sampler::nearest(ClampMode::ClampToEdge) :
                                Sampler::bilinear(ClampMode::ClampToEdge);
  cmd.setBinding(1, sceneLinear, sampler);
  if(vrFogFoldActive) {
    auto& scene = wview.sceneGlobals();
    cmd.setBinding(2, zbuffer,       Sampler::nearest());
    cmd.setBinding(3, sky.fogLut3D,  Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setBinding(4, sky.viewLut,   sky.sampler);
    cmd.setBinding(5, sceneOpaque,   Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setBinding(6, gbufDiffuse,   Sampler::nearest (ClampMode::ClampToEdge));
    cmd.setBinding(7, gbufNormal,    Sampler::nearest (ClampMode::ClampToEdge));
    cmd.setBinding(8, sceneDepth,    Sampler::nearest (ClampMode::ClampToEdge));
    cmd.setBinding(9, sky.viewCldLut, sky.sampler);
    (void)scene;
    }
  cmd.setPushData(p);
  cmd.setPipeline(pso);
  cmd.draw(nullptr, 0, 3);
  if(overlay) {
    cmd.setDebugMarker("VR hands / held items");
    vrOverlay.draw(cmd);
    }
  }

void Renderer::drawCMAA2(Tempest::Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  auto&          pso             = shaders.cmaa2EdgeColor2x2Presets[Gothic::options().aaPreset];
  const IVec3    inputGroupSize  = pso.workGroupSize();
  const IVec3    outputGroupSize = inputGroupSize - IVec3(2, 2, 0);
  const uint32_t groupCountX     = uint32_t((sceneLinear.w() + outputGroupSize.x * 2 - 1) / (outputGroupSize.x * 2));
  const uint32_t groupCountY     = uint32_t((sceneLinear.h() + outputGroupSize.y * 2 - 1) / (outputGroupSize.y * 2));

  cmd.setFramebuffer({});
  cmd.setBinding(0, sceneLinear, Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(1, cmaa2.workingEdges);
  cmd.setBinding(2, cmaa2.shapeCandidates);
  cmd.setBinding(3, cmaa2.deferredBlendLocationList);
  cmd.setBinding(4, cmaa2.deferredBlendItemList);
  cmd.setBinding(5, cmaa2.deferredBlendItemListHeads);
  cmd.setBinding(6, cmaa2.controlBuffer);
  cmd.setBinding(7, cmaa2.indirectBuffer);

  // detect edges
  cmd.setPipeline(pso);
  cmd.dispatch(groupCountX, groupCountY, 1);

  // process candidates pass
  cmd.setPipeline(shaders.cmaa2ProcessCandidates);
  cmd.dispatchIndirect(cmaa2.indirectBuffer, 0);

  // deferred color apply
  struct Push {
    float brightness = 0;
    float contrast   = 1;
    float gamma      = 1.f/2.2f;
    float mul        = 1;
    float hdrPeak    = 0;
    float padding[3] = {};
    };

  Push p;
  p.hdrPeak = hdrPeakRatio;
  p.brightness = (settings.zVidBrightness - 0.5f)*0.1f;
  p.contrast   = std::max(1.5f - settings.zVidContrast, 0.01f);
  p.gamma      = p.gamma/std::max(2.0f*settings.zVidGamma,  0.01f);

  static float mul = 0.f;
  if(mul>0)
    p.mul = mul;

  auto& psoTone = (internalResolutionScale()>=0.999f) ? shaders.tonemapping : shaders.tonemappingUpscale;
  cmd.setFramebuffer({{result, Tempest::Discard, Tempest::Preserve}});
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  const auto sampler = internalResolutionScale()>=0.999f ? Sampler::nearest() : Sampler::bilinear(ClampMode::ClampToEdge);
  cmd.setBinding(1, sceneLinear, sampler);
  cmd.setPushData(&p, sizeof(p));
  cmd.setPipeline(psoTone);
  cmd.draw(nullptr, 0, 3);

  cmd.setBinding(0, sceneLinear);
  cmd.setBinding(1, cmaa2.workingEdges);
  cmd.setBinding(2, cmaa2.shapeCandidates);
  cmd.setBinding(3, cmaa2.deferredBlendLocationList);
  cmd.setBinding(4, cmaa2.deferredBlendItemList);
  cmd.setBinding(5, cmaa2.deferredBlendItemListHeads);
  cmd.setBinding(6, cmaa2.controlBuffer);
  cmd.setPushData(&p, sizeof(p));
  cmd.setPipeline(shaders.cmaa2DeferredColorApply2x2);
  cmd.drawIndirect(cmaa2.indirectBuffer, 3*sizeof(uint32_t));
  }

void Renderer::drawFog(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  auto& scene = wview.sceneGlobals();

  switch(sky.quality) {
    case None:
    case VolumetricLQ: {
      cmd.setBinding(0, sky.fogLut3D, Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(1, sky.fogLut3D, Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(2, zbuffer, Sampler::nearest()); // NOTE: wanna here depthFetch from gles2
      cmd.setBinding(3, scene.uboGlobal[SceneGlobals::V_Main]);
      cmd.setBinding(4, sky.viewLut, sky.sampler);
      {
        // Horizon haze: geometry fades to the sky colour over the last part
        // of the view range, so the far plane is not a hard edge (VR setting;
        // off on desktop). View depths in cm.
        struct Push { float hazeStart, hazeEnd; } push = {0.f, 0.f};
#if defined(__ANDROID__)
        if(vrHorizonHaze) {
          const float far = scene.clipInfo().z;
          push = {far*0.55f, far*0.95f};
          }
#endif
        cmd.setPushData(push);
      }
      cmd.setPipeline(shaders.fog);
      break;
      }
    case VolumetricHQ: {
      cmd.setBinding(0, sky.fogLut3D,   Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(1, sky.fogLut3DMs, Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(2, zbuffer,        Sampler::nearest());
      cmd.setBinding(3, scene.uboGlobal[SceneGlobals::V_Main]);
      cmd.setBinding(4, sky.occlusionLut);
      cmd.setPipeline(shaders.fog3dHQ);
      break;
      }
    case Epipolar: {
      //cmd.setBinding(0, sky.fogLut3D,   Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
      cmd.setBinding(1, zbuffer,        Sampler::nearest());
      cmd.setBinding(2, vsm.fogDbg,     Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(3, epipolar.epipoles);
      cmd.setBinding(4, sky.fogLut3DMs, Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setPipeline(shaders.vsmFog);
      break;
      }
    case PathTrace:
      return;
    }
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawSunMoon(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  drawSunMoon(cmd, wview, false);
  drawSunMoon(cmd, wview, true);
  }

void Renderer::drawSunMoon(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview, bool isSun) {
  auto& scene = wview.sceneGlobals();
  auto& sun   = wview.sky().sunLight();
  auto  m     = scene.viewProject();
  auto  d     = sun.dir();

  if(!isSun) {
    // fixed pos for now
    d = Vec3::normalize({-1,1,0});
    }

  auto  dx = d;
  float w  = 0;
  m.project(dx.x, dx.y, dx.z, w);

#if defined(GOTHIC2VR_OPENXR)
  const bool angularSprite = true;
#else
  const bool angularSprite = false;
#endif
  if(!angularSprite && dx.z<=0)
    return;

  struct Push {
    Tempest::Vec2      pos;
    Tempest::Vec2      size;
    Tempest::Vec3      sunDir;
    float              GSunIntensity = 0;
    Tempest::Matrix4x4 viewProjectInv;
    uint32_t           isSun = 0;
    } push;
  push.pos     = angularSprite ? Vec2(0) : Vec2(dx.x,dx.y)/dx.z;
  push.size.x  = 2.f/float(zbuffer.w());
  push.size.y  = 2.f/float(zbuffer.h());

  const float GSunIntensity  = wview.sky().sunIntensity();
  const float GMoonIntensity = wview.sky().moonIntensity();

  const float scale          = internalResolutionScale();
  const float sunSize        = settings.sunSize  * scale;
  const float moonSize       = settings.moonSize * scale;
  const float intencity      = isSun ? 0.07f : 0.4f;

  push.size          *= isSun ? sunSize : (moonSize*0.25f);
  push.GSunIntensity  = isSun ? (GSunIntensity*intencity) : (GMoonIntensity*intencity);
  push.isSun          = (isSun ? 1u : 0u) | (angularSprite ? 2u : 0u);
  if(angularSprite) {
    // World-space angular extent is independent of eye resolution and gaze.
    push.pos = Vec2(0);
    push.size = Vec2((isSun ? settings.sunSize : settings.moonSize*.25f)/1024.f);
  }
  push.sunDir         = d;
  push.viewProjectInv = scene.viewProjectLwcInv();

  // HACK
  if(isSun) {
    float day = sun.dir().y;
    float stp = linearstep(-0.07f, 0.03f, day);
    push.GSunIntensity *= stp*stp*4.f;
    } else {
    push.GSunIntensity *= wview.sky().isNight();
    }
  // push.GSunIntensity *= exposure;

  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, isSun ? wview.sky().sunImage() : wview.sky().moonImage());
  cmd.setBinding(2, sky.transLut, sky.sampler);
  cmd.setPushData(push);
  cmd.setPipeline(shaders.sun);
  cmd.draw(nullptr, 0, 6);
  }

void Renderer::drawGizmo(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  if(!gizmo.enable)
    return;

  auto  cen = gizmo.center;
  float w   = 1;
  auto  vp  = wview.sceneGlobals().viewProject();
  vp.project(cen.x, cen.y, cen.z, w);

  cmd.setPushData(gizmo.center);
  cmd.setDebugMarker("Hud-Gizmo");
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, zbuffer);
  cmd.setPipeline(shaders.gizmo);
  cmd.draw(nullptr, 0, 36, 0, 3);
  }

void Renderer::drawLightsHud(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  if(hud.light==nullptr)
    return;
  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}}, {zbuffer, Tempest::Preserve, Tempest::Preserve});
  auto& ssbo = wview.lights().lightsSsbo();
  cmd.setDebugMarker("Hud-Lights");
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, ssbo);
  cmd.setBinding(2, *hud.light);
  cmd.setPipeline(shaders.lightImposter);
  cmd.draw(nullptr, 0, 6, 0, wview.lights().size());
  }

void Renderer::drawSwRT(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  if(!settings.swrtEnabled)
    return;

  const auto& scene     = wview.sceneGlobals();
  const auto& bvh       = wview.landscape().bvh();
  const auto  originLwc = scene.originLwc;

  if(swrt.outputImage.size()!=zbuffer.size()) {
    Resources::recycle(std::move(swrt.outputImage));
    auto& device = Resources::device();
    // swrt.outputImage = device.image2d(TextureFormat::R32U, zbuffer.size());
    swrt.outputImage = device.image2d(TextureFormat::RGBA8, zbuffer.size());
    }

  cmd.setFramebuffer({});
  cmd.setDebugMarker("Raytracing");
  //cmd.setPipeline(shaders.swRaytracing8);
  cmd.setPipeline(shaders.swRaytracing);
  cmd.setPushData(&originLwc, sizeof(originLwc));
  cmd.setBinding(0, swrt.outputImage);
  cmd.setBinding(1, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, gbufDiffuse);
  cmd.setBinding(3, gbufNormal);
  cmd.setBinding(4, zbuffer);
  cmd.setBinding(5, bvh);

  cmd.dispatchThreads(swrt.outputImage.size());
  }

void Renderer::stashSceneAux(Encoder<CommandBuffer>& cmd) {
  auto& device = Resources::device();
  if(!device.properties().hasSamplerFormat(zBufferFormat))
    return;
  cmd.setFramebuffer({});
  cmd.setDebugMarker("Stash scene");
  cmd.setFramebuffer({{sceneOpaque, Tempest::Discard, Tempest::Preserve}, {sceneDepth, Tempest::Discard, Tempest::Preserve}});
  cmd.setDebugMarker("Stash scene");
  cmd.setBinding(0, sceneLinear,Sampler::nearest());
  cmd.setBinding(1, zbuffer,    Sampler::nearest());
  const int32_t stashScale = std::max(1, int32_t(sceneLinear.w()/std::max(1,sceneOpaque.w())));
  cmd.setPushData(stashScale);
  cmd.setPipeline(shaders.stash);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawVsmDbg(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  static bool enable = false;
  if(!enable || !settings.vsmEnabled)
    return;

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}});
  cmd.setDebugMarker("VSM-dbg");
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
  cmd.setBinding(2, gbufNormal,  Sampler::nearest());
  cmd.setBinding(3, zbuffer,     Sampler::nearest());
  cmd.setBinding(4, vsm.pageTbl);
  cmd.setBinding(5, vsm.pageList);
  cmd.setBinding(6, vsm.pageData);
  cmd.setBinding(8, wview.sceneGlobals().vsmDbg);
  cmd.setPushData(&settings.vsmMipBias, sizeof(settings.vsmMipBias));
  cmd.setPipeline(shaders.vsmDbg);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawSwrDbg(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  static bool enable = true;
  if(!enable || !settings.swrEnabled)
    return;

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}});
  cmd.setDebugMarker("SWR-dbg");
  cmd.setBinding(0, swr.outputImage);
  cmd.setBinding(1, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, gbufDiffuse, Sampler::nearest());
  cmd.setBinding(3, gbufNormal,  Sampler::nearest());
  cmd.setBinding(4, zbuffer,     Sampler::nearest());
  cmd.setPipeline(shaders.swRenderingDbg);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawRtsmDbg(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  static bool enable = false;
  if(!enable || !settings.rtsmEnabled)
    return;

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}});
  cmd.setDebugMarker("RTSM-dbg");
#if 1
  cmd.setBinding(0, rtsm.dbg32);
#else
  cmd.setBinding(0, rtsm.primBins);
  cmd.setBinding(1, rtsm.posList);
  cmd.setBinding(2, rtsm.pages);
#endif
  cmd.setPipeline(shaders.rtsmDbg);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawHashDbg(Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  static bool enable = true;
  if(!enable)
    return;

  auto& scene = wview.sceneGlobals();

  struct Push {
    Vec3 originLwc;
    } push;
  push.originLwc = scene.originLwc;

  cmd.setDebugMarker("Hash-dbg");
  cmd.setPushData(push);
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, gbufNormal);
  cmd.setBinding(3, zbuffer);

  cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}});
  cmd.setPipeline(shaders.hashDbg);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawLightTreeDbg(Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  static bool enable = false;
  if(!enable)
    return;

  auto& scene = wview.sceneGlobals();

  struct Push {
    Vec3 originLwc;
    } push;
  push.originLwc = scene.originLwc;

  cmd.setDebugMarker("LightTree-dbg");
  cmd.setPushData(push);
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, gbufNormal);
  cmd.setBinding(2, zbuffer);
  cmd.setBinding(3, lightsTree.bvh);

  cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}});
  cmd.setPipeline(shaders.lightsTreeDbg);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawHiZ(Encoder<CommandBuffer>& cmd, WorldView& view, bool reuseLeftSeed) {
  cmd.setFramebuffer({});
  cmd.setDebugMarker("HiZ-occluders");
  cmd.setFramebuffer({}, {zbuffer, 1.f, Tempest::Preserve});
  view.drawHiZ(cmd,reuseLeftSeed);
  }

#if defined(__ANDROID__)
// The second eye's HiZ from the first eye's complete depth (still in zbuffer
// at this point): per-tile max (hiz_pot) into hiZLeft, the capped stereo
// window (hiz_stereo) into the HiZ base, the usual mip chain. The occluder seed
// draw that follows clears the depth buffer.
void Renderer::buildHiZStereo(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const Camera& camera) {
  const float tilePx = float(nextPot(uint32_t(zbuffer.w())))/float(std::max(1,hiz.hiZ.w()));
  const auto  viewR  = camera.view();
  const auto  win    = Vr::stereoHiZWindow(vrLeftProj, camera.projective(), vrLeftView, &viewR, vrLeftOrigin, camera.originLwc(),
                                           float(zbuffer.w()), float(zbuffer.h()), tilePx, 50.f);
  if(!vrStereoHiZLogged) {
    int capped = 0;
    for(int i=0; i<=win.windowTiles && i<Vr::StereoHiZWindow::MaxTiles; ++i) if(win.depthClamp[i]<1.f) ++capped;
    Log::i("VR stereo HiZ: active, tile ",tilePx," px, window shift ",win.shiftTiles," tiles ",win.windowTiles," rows +-",win.marginY,
           win.exact ? " exact, capped tiles " : " plain maximum, capped tiles ",capped," (near limit ",win.nearCm," cm)");
    vrStereoHiZLogged = true;
    }
  cmd.setFramebuffer({});
  cmd.setDebugMarker("HiZ-stereo");
  cmd.setBinding(0, zbuffer, Sampler::nearest(ClampMode::ClampToEdge));
  cmd.setBinding(1, hiz.hiZLeft);
  cmd.setPipeline(shaders.hiZPot);
  cmd.dispatch(size_t(hiz.hiZ.w()), size_t(hiz.hiZ.h()));
  // tiles beyond the image hold 0 in hiZLeft (hiz_pot skips them): unknown, not near
  struct Push { int32_t shift, window, marginY, validX, validY; float depthClamp[Vr::StereoHiZWindow::MaxTiles]; } push = {};
  push.shift = win.shiftTiles; push.window = win.windowTiles; push.marginY = win.marginY;
  push.validX = int32_t(std::ceil(float(zbuffer.w())/tilePx)); push.validY = int32_t(std::ceil(float(zbuffer.h())/tilePx));
  for(int i=0; i<Vr::StereoHiZWindow::MaxTiles; ++i) push.depthClamp[i] = win.depthClamp[i];
  static_assert(sizeof(Push)==84, "hiz_stereo.comp push layout");
  cmd.setBinding(0, hiz.hiZLeft);
  cmd.setBinding(1, hiz.hiZ);
  cmd.setPushData(push);
  cmd.setPipeline(shaders.hiZStereo);
  cmd.dispatchThreads(size_t(hiz.hiZ.w()), size_t(hiz.hiZ.h()));
  const uint32_t maxBind = 8, mip = hiz.hiZ.mipCount();
  cmd.setBinding(0, hiz.counter);
  for(uint32_t i=0; i<maxBind; ++i)
    cmd.setBinding(1+i, hiz.hiZ, Sampler::nearest(), std::min(i, mip-1));
  cmd.setPushData(&mip, sizeof(mip));
  cmd.setPipeline(shaders.hiZMip);
  cmd.dispatchThreads(std::max(uint32_t(hiz.hiZ.w())/2u, 1u), std::max(uint32_t(hiz.hiZ.h())/2u, 1u));
  cmd.setFramebuffer({});
  }
#endif

void Renderer::buildHiZ(Tempest::Encoder<Tempest::CommandBuffer>& cmd) {
  assert(hiz.hiZ.w()<=128 && hiz.hiZ.h()<=128); // shader limitation

  cmd.setFramebuffer({});
  cmd.setDebugMarker("HiZ-mip");
  cmd.setBinding(0, zbuffer, Sampler::nearest(ClampMode::ClampToEdge));
  cmd.setBinding(1, hiz.hiZ);
  cmd.setPipeline(shaders.hiZPot);
  cmd.dispatch(size_t(hiz.hiZ.w()), size_t(hiz.hiZ.h()));

  const uint32_t maxBind = 8, mip = hiz.hiZ.mipCount();
  cmd.setBinding(0, hiz.counter);
  for(uint32_t i=0; i<maxBind; ++i)
    cmd.setBinding(1+i, hiz.hiZ, Sampler::nearest(), std::min(i, mip-1));
  cmd.setPushData(&mip, sizeof(mip));
  cmd.setPipeline(shaders.hiZMip);
  cmd.dispatchThreads(std::max(uint32_t(hiz.hiZ.w())/2u, 1u), std::max(uint32_t(hiz.hiZ.h())/2u, 1u));
  }

void Renderer::drawVsm(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  if(!settings.vsmEnabled)
    return;

  static bool omniLights = true;
  const bool directLight  = !settings.rtsmEnabled;
  const bool doVirtualFog = directLight && sky.quality!=VolumetricLQ && sky.quality!=PathTrace;

  auto& scene      = wview.sceneGlobals();
  auto& sceneUbo   = scene.uboGlobal[SceneGlobals::V_Main];
  auto& lightsSsbo = wview.lights().lightsSsbo();

  auto& vsmDbg   = usesImage2d(vsm.vsmDbg,   TextureFormat::R32U, zbuffer.size());
  auto& pageTbl  = usesImage3d(vsm.pageTbl,  TextureFormat::R32U, 32, 32, 16);
  auto& pageHiZ  = usesImage3d(vsm.pageHiZ,  TextureFormat::R32U, 32, 32, 16);
  auto& pageData = usesZBuffer(vsm.pageData, shadowFormat,        8192, 8192);

  auto  pageCount   = uint32_t(vsm.pageData.w()/VSM_PAGE_SIZE) * uint32_t(vsm.pageData.h()/VSM_PAGE_SIZE);
  auto& pageList    = usesSsbo(vsm.pageList,    shaders.vsmClear.sizeofBuffer(0, pageCount));
  auto& pageListTmp = usesSsbo(vsm.pageListTmp, shaders.vsmAllocPages.sizeofBuffer(3, pageCount));

  const uint32_t lightsTotal  = omniLights ? uint32_t(wview.lights().size()) : 0;
  const size_t   numOmniPages = lightsTotal*6;
  auto& pageTblOmni   = usesSsbo(vsm.pageTblOmni,   shaders.vsmClearOmni.sizeofBuffer(0, numOmniPages));
  auto& visibleLights = usesSsbo(vsm.visibleLights, shaders.vsmClearOmni.sizeofBuffer(1, lightsTotal ));

  wview.setVirtualShadowMap(true, pageData, pageTbl, pageHiZ, pageList);

  cmd.setFramebuffer({});
  cmd.setDebugMarker("VSM-pages");
  cmd.setBinding(0, pageList);
  cmd.setBinding(1, pageTbl);
  cmd.setBinding(2, pageHiZ);
  cmd.setPipeline(shaders.vsmClear);
  cmd.dispatchThreads(size_t(pageTbl.w()), size_t(pageTbl.h()), size_t(pageTbl.d()));

  if(omniLights) {
    cmd.setBinding(0, pageTblOmni);
    cmd.setBinding(1, visibleLights);
    cmd.setPipeline(shaders.vsmClearOmni);
    cmd.dispatchThreads(numOmniPages);

    struct Push { float znear; uint32_t lightsTotal; } push = {};
    push.znear       = scene.znear;
    push.lightsTotal = lightsTotal;
    cmd.setBinding(0, sceneUbo);
    cmd.setBinding(1, lightsSsbo);
    cmd.setBinding(2, visibleLights);
    cmd.setBinding(3, *scene.hiZ);
    cmd.setPushData(push);
    cmd.setPipeline(shaders.vsmCullLights);
    cmd.dispatchThreads(wview.lights().size());
    }

  if(directLight) {
    cmd.setBinding(0, sceneUbo);
    cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
    cmd.setBinding(2, gbufNormal,  Sampler::nearest());
    cmd.setBinding(3, zbuffer,     Sampler::nearest());
    cmd.setBinding(4, pageTbl);
    cmd.setBinding(5, pageHiZ);
    cmd.setPushData(settings.vsmMipBias);
    cmd.setPipeline(shaders.vsmMarkPages);
    cmd.dispatchThreads(zbuffer.size());
    }

  if(omniLights) {
    struct Push { Vec3 originLwc; float znear; float vsmMipBias; } push = {};
    push.originLwc  = scene.originLwc;
    push.znear      = scene.znear;
    push.vsmMipBias = settings.vsmMipBias;
    cmd.setBinding(0, sceneUbo);
    cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
    cmd.setBinding(2, gbufNormal,  Sampler::nearest());
    cmd.setBinding(3, zbuffer,     Sampler::nearest());
    cmd.setBinding(4, lightsSsbo);
    cmd.setBinding(5, visibleLights);
    cmd.setBinding(6, pageTblOmni);
    cmd.setBinding(7, vsmDbg);
    cmd.setPushData(&push, sizeof(push));
    cmd.setPipeline(shaders.vsmMarkOmniPages);
    cmd.dispatchThreads(zbuffer.size());

    cmd.setBinding(0, pageTblOmni);
    cmd.setPushData(&lightsTotal, sizeof(lightsTotal));
    cmd.setPipeline(shaders.vsmPostprocessOmni);
    cmd.dispatchThreads(wview.lights().size());
    }

  // sky&fog
  if(doVirtualFog) {
    cmd.setDebugMarker("VSM-pages-epipolar");
    cmd.setBinding(0, epipolar.epTrace);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, epipolar.epipoles);
    cmd.setBinding(3, pageTbl);
    cmd.setBinding(4, pageHiZ);
    cmd.setPipeline(shaders.vsmFogPages);
    cmd.dispatchThreads(epipolar.epTrace.size());
    }

  cmd.setDebugMarker("VSM-pages-alloc");
  if(true) {
    // clump
    cmd.setBinding(0, pageList);
    cmd.setBinding(1, pageTbl);
    cmd.setPipeline(shaders.vsmClumpPages);
    cmd.dispatchThreads(size_t(pageTbl.w()), size_t(pageTbl.h()), size_t(pageTbl.d()));
    }

  cmd.setBinding(0, pageList);
  cmd.setBinding(1, pageTbl);
  cmd.setBinding(2, pageTblOmni);
  cmd.setBinding(3, pageListTmp);
  cmd.setBinding(4, scene.vsmDbg);
  // list
  cmd.setPipeline(shaders.vsmListPages);
  if(omniLights)
    cmd.dispatch(size_t(pageTbl.d() + 1)); else
    cmd.dispatch(size_t(pageTbl.d()));

  cmd.setPipeline(shaders.vsmAllocPages);
  cmd.dispatch(1);

  // hor-merge
  cmd.setPipeline(shaders.vsmMergePages);
  cmd.dispatch(1);

  cmd.setDebugMarker("VSM-visibility");
  wview.visibilityVsm(cmd);

  cmd.setDebugMarker("VSM-rendering");
  cmd.setFramebuffer({}, {pageData, 0.f, Tempest::Preserve});
  wview.drawVsm(cmd);
  }

void Renderer::drawRtsm(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  if(!settings.rtsmEnabled)
    return;

  const int RTSM_BIN_SIZE   = 32;
  const int RTSM_SMALL_TILE = 32;
  const int RTSM_LARGE_TILE = 128;

  const auto& shaders      = Shaders::inst();
  const auto& scene        = wview.sceneGlobals();
  const auto& clusters     = wview.clusters();
  const auto& drawCmd      = wview.drawCommands();
  const auto& buckets      = wview.drawBuckets();
  const auto& instanceSsbo = wview.instanceSsbo();
  const auto& sceneUbo     = scene.uboGlobal[SceneGlobals::V_Vsm];

  const auto largeTiles  = tileCount(scene.zbuffer->size(), RTSM_LARGE_TILE);
  const auto smallTiles  = tileCount(scene.zbuffer->size(), RTSM_SMALL_TILE);
  const auto binTiles    = tileCount(scene.zbuffer->size(), RTSM_BIN_SIZE);
  const auto maxMeshlets = drawCmd.maxMeshlets();

  // alloc resources
  auto& posList     = usesScratch(rtsm.posList,     64*1024*1024); // arbitrary
  auto& outputImage = usesImage2d(rtsm.outputImage, TextureFormat::R8, zbuffer.size());
  auto& pages       = usesImage3d(rtsm.pages,       TextureFormat::R32U, 32, 32, 16);
  auto& meshTiles   = usesImage2d(rtsm.meshTiles,   TextureFormat::RG32U, smallTiles);
  auto& primTiles   = usesImage2d(rtsm.primTiles,   TextureFormat::RG32U, binTiles);
  auto& visList     = usesSsbo   (rtsm.visList,     shaders.rtsmClear.sizeofBuffer(1, maxMeshlets));

  auto& dbg32       = usesImage2d(rtsm.dbg32, TextureFormat::R32U, tileCount(zbuffer.size(), 32));
  auto& dbg16       = usesImage2d(rtsm.dbg16, TextureFormat::R32U, tileCount(zbuffer.size(), 16));

  cmd.setDebugMarker("RTSM-rendering");
  cmd.setFramebuffer({});

  // clear
  {
    cmd.setBinding(0, pages);
    cmd.setBinding(1, visList);
    cmd.setBinding(2, posList);

    cmd.setPipeline(shaders.rtsmClear);
    cmd.dispatchThreads(size_t(pages.w()), size_t(pages.h()), size_t(pages.d()));
  }

  // global cull
  {
    struct Push { uint32_t meshletCount; } push = {};
    push.meshletCount = uint32_t(clusters.size());
    cmd.setPushData(push);

    if(sky.quality==VolumetricHQ) {
      cmd.setBinding(0, epipolar.epTrace);
      cmd.setBinding(1, sceneUbo);
      cmd.setBinding(2, epipolar.epipoles);
      cmd.setBinding(3, pages);
      cmd.setPipeline(shaders.rtsmFogPages);
      cmd.dispatchThreads(epipolar.epTrace.size());
      }

    cmd.setBinding(0, outputImage);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufDiffuse);
    cmd.setBinding(3, gbufNormal);
    cmd.setBinding(4, zbuffer);
    cmd.setBinding(5, clusters.ssbo());
    cmd.setBinding(6, visList);
    cmd.setBinding(7, pages);

    cmd.setPipeline(shaders.rtsmPages);
    cmd.dispatchThreads(scene.zbuffer->size());

    cmd.setBinding(0, pages);
    cmd.setPipeline(shaders.rtsmHiZ);
    cmd.dispatch(1);

    cmd.setBinding(7, posList);
    cmd.setPipeline(shaders.rtsmCulling);
    cmd.dispatchThreads(push.meshletCount);
  }

  // position
  {
    struct Push { Vec3 originLwc; } push = {};
    push.originLwc = scene.originLwc;

    cmd.setPushData(push);
    cmd.setBinding(0, posList);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, visList);

    cmd.setBinding(5,  clusters.ssbo());
    cmd.setBinding(6,  instanceSsbo);
    cmd.setBinding(7,  buckets.ssbo());
    cmd.setBinding(8,  buckets.ibo());
    cmd.setBinding(9,  buckets.vbo());
    cmd.setBinding(10, buckets.morphId());
    cmd.setBinding(11, buckets.morph());

    cmd.setPipeline(shaders.rtsmPosition);
    cmd.dispatchIndirect(visList,0);
  }

  // binning
  {
    cmd.setBinding(0, outputImage);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, meshTiles);
    cmd.setBinding(6, primTiles);
    cmd.setBinding(9, dbg32);

    // meshlets
    cmd.setPipeline(shaders.rtsmMeshletCull);
    cmd.dispatch(largeTiles);

    // primitives
    cmd.setPipeline(shaders.rtsmPrimCull);
    cmd.dispatch(smallTiles);
  }

  // raster
  {
    cmd.setBinding(0, outputImage);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, meshTiles);
    cmd.setBinding(6, primTiles);
    cmd.setBinding(7, buckets.textures());
    cmd.setBinding(8, Sampler::trillinear());
    cmd.setBinding(9, dbg16);

    cmd.setPipeline(shaders.rtsmRaster);
    cmd.dispatchThreads(outputImage.size());
  }
  }

void Renderer::drawRtsmOmni(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  if(!settings.rtsmEnabled)
    return;

  static bool omniLights = true;
  if(!omniLights)
    return;

  //const uint32_t RTSM_SMALL_TILE = 32;
  const uint32_t RTSM_LIGHT_TILE = 64;

  const auto& shaders      = Shaders::inst();
  const auto& scene        = wview.sceneGlobals();
  const auto& sceneUbo     = scene.uboGlobal[SceneGlobals::V_Main];
  const auto& clusters     = wview.clusters();
  const auto& drawCmd      = wview.drawCommands();
  const auto& buckets      = wview.drawBuckets();
  const auto& instanceSsbo = wview.instanceSsbo();
  const auto& lightsSsbo   = wview.lights().lightsSsbo();
  const auto  maxMeshlets  = drawCmd.maxMeshlets();
  const auto  lightsTotal  = uint32_t(wview.lights().size());

  // alloc resources, for omni-lights
  auto& posList        = usesScratch(rtsm.posList,        64*1024*1024); // arbitrary
  auto& outputImageClr = usesImage2d(rtsm.outputImageClr, TextureFormat::R11G11B10UF, zbuffer.size());
  auto& lightTiles     = usesImage2d(rtsm.lightTiles,     TextureFormat::RG32U, tileCount(scene.zbuffer->size(), RTSM_LIGHT_TILE));
  auto& lightBins      = usesImage2d(rtsm.lightBins,      TextureFormat::RG32U, lightsTotal, 1u);
  auto& primTilesOmni  = usesImage2d(rtsm.primTilesOmni,  TextureFormat::R32U, tileCount(zbuffer.size(), RTSM_LIGHT_TILE));
  auto& drawTasks      = usesSsbo   (rtsm.drawTasks,      4*sizeof(uint32_t));
  auto& visList        = usesSsbo   (rtsm.visList,        shaders.rtsmClearOmni.sizeofBuffer(1, maxMeshlets));
  auto& visibleLights  = usesSsbo   (rtsm.visibleLights,  shaders.rtsmClearOmni.sizeofBuffer(2, lightsTotal));

  auto& dbg64          = usesImage2d(rtsm.dbg64, TextureFormat::R32U, tileCount(zbuffer.size(), 64));
  auto& dbg16          = usesImage2d(rtsm.dbg16, TextureFormat::R32U, tileCount(zbuffer.size(), 16));

  cmd.setDebugMarker("RTSM-rendering-omni");
  cmd.setFramebuffer({});
  // clear
  {
    struct Push { uint32_t meshletCount; } push = {};
    push.meshletCount = uint32_t(clusters.size());

    cmd.setPushData(push);
    cmd.setBinding(0, posList);
    cmd.setBinding(1, visList);
    cmd.setBinding(2, visibleLights);
    cmd.setPipeline(shaders.rtsmClearOmni);
    cmd.dispatchThreads(1);
  }

  // cull lights
  {
    struct Push { float znear; uint32_t lightsTotal; } push = {};
    push.znear       = scene.znear;
    push.lightsTotal = lightsTotal;

    cmd.setPushData(push);
    cmd.setBinding(0, sceneUbo);
    cmd.setBinding(1, lightsSsbo);
    cmd.setBinding(2, visibleLights);
    cmd.setBinding(3, visList);
    cmd.setBinding(4, hiz.hiZ);
    cmd.setBinding(5, clusters.ssbo());
    cmd.setBinding(6, posList);

    cmd.setPipeline(shaders.rtsmCullLights);
    cmd.dispatchThreads(lightsTotal);
  }

  // lights
  {
    struct Push { Vec3 originLwc; float znear; } push = {};
    push.originLwc = scene.originLwc;
    push.znear     = scene.znear;

    cmd.setPushData(push);
    cmd.setBinding(0, lightTiles);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, lightsSsbo);
    cmd.setBinding(6, visibleLights);
    //
    cmd.setBinding(9, rtsm.dbg64);

    cmd.setPipeline(shaders.rtsmLightsOmni);
    cmd.dispatch(lightTiles.size());

    cmd.setPipeline(shaders.rtsmBboxesOmni);
    cmd.dispatchThreads(zbuffer.size());

    cmd.setPipeline(shaders.rtsmCompactOmni);
    cmd.dispatch(lightTiles.size());

    cmd.setPipeline(shaders.rtsmCompactLights);
    cmd.dispatchThreads(lightsTotal);
  }

  // meshlet culling
  {
    struct Push { float znear; uint32_t meshletCount; } push = {};
    push.znear        = scene.znear;
    push.meshletCount = uint32_t(clusters.size());

    cmd.setPushData(push);
    cmd.setBinding(0, posList);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, rtsm.visList);
    cmd.setBinding(3, lightsSsbo);
    cmd.setBinding(4, visibleLights);
    cmd.setBinding(5, clusters.ssbo());

    cmd.setPipeline(shaders.rtsmCullingOmni);
    cmd.dispatchThreads(push.meshletCount);
  }

  // position
  {
    struct Push { Vec3 originLwc; } push = {};
    push.originLwc = scene.originLwc;

    cmd.setPushData(push);
    cmd.setBinding(0, posList);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, visList);
    //
    cmd.setBinding(5,  clusters.ssbo());
    cmd.setBinding(6,  instanceSsbo);
    cmd.setBinding(7,  buckets.ssbo());
    cmd.setBinding(8,  buckets.ibo());
    cmd.setBinding(9,  buckets.vbo());
    cmd.setBinding(10, buckets.morphId());
    cmd.setBinding(11, buckets.morph());

    cmd.setPipeline(shaders.rtsmPositionOmni);
    cmd.dispatchIndirect(visList, 0);
  }

  // per-light meshlets, primitives
  {
    struct Push { Vec3 originLwc; float znear; } push = {};
    push.originLwc = scene.originLwc;
    push.znear     = scene.znear;

    cmd.setBinding(0, sceneUbo);
    cmd.setBinding(1, lightsSsbo);
    cmd.setBinding(2, visibleLights);
    cmd.setBinding(3, lightBins);
    cmd.setBinding(4, clusters.ssbo());
    cmd.setBinding(5, posList);

    cmd.setPipeline(shaders.rtsmMeshletOmni);
    cmd.dispatchIndirect(visibleLights, 0);

    cmd.setPipeline(shaders.rtsmBackfaceOmni);
    cmd.dispatchIndirect(visibleLights, 0);
  }

  // in tile primitives
  {
    struct Push { Vec3 originLwc; float znear; } push = {};
    push.originLwc = scene.originLwc;
    push.znear     = scene.znear;
    cmd.setPushData(push);
    cmd.setBinding(0, lightTiles);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, lightsSsbo);
    cmd.setBinding(6, lightBins);
    cmd.setBinding(7, primTilesOmni);
    cmd.setBinding(8, drawTasks);
    cmd.setBinding(9, dbg64);

    cmd.setPipeline(shaders.rtsmTaskOmni);
    cmd.dispatch(1);

    cmd.setPipeline(shaders.rtsmPrimOmni);
    cmd.dispatchIndirect(drawTasks, 0);
  }

  // raster
  {
    struct Push { Vec3 originLwc; } push = {};
    push.originLwc = scene.originLwc;

    cmd.setPushData(push);
    cmd.setBinding(0, outputImageClr);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, lightsSsbo);
    cmd.setBinding(6, primTilesOmni);
    cmd.setBinding(7, buckets.textures());
    cmd.setBinding(8, Sampler::trillinear());
    cmd.setBinding(9, dbg16);

    cmd.setPipeline(shaders.rtsmRasterOmni);
    cmd.dispatchThreads(outputImageClr.size());
  }

  //TODO: swrt ?
  if(0) {
    // raster (ref)
    struct Push { Vec3 originLwc; } push = {};
    push.originLwc  = scene.originLwc;
    cmd.setPushData(push);
    cmd.setBinding(0, outputImageClr);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, lightsSsbo);
    cmd.setBinding(6, visibleLights);
    cmd.setBinding(7, lightBins);
    cmd.setBinding(9, dbg16);

    cmd.setPipeline(shaders.rtsmRenderingOmni);
    cmd.dispatchThreads(outputImageClr.size());
    }
  }

void Renderer::drawSwr(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  if(!settings.swrEnabled)
    return;

  const auto& scene        = wview.sceneGlobals();
  const auto& clusters     = wview.clusters();
  const auto& buckets      = wview.drawBuckets();
  const auto& instanceSsbo = wview.instanceSsbo();

  auto& outputImage = usesImage2d(swr.outputImage, TextureFormat::R32U, zbuffer.size());

  cmd.setFramebuffer({});
  cmd.setDebugMarker("SW-rendering");

  struct Push { uint32_t firstMeshlet; uint32_t meshletCount; float znear; } push = {};
  push.firstMeshlet = 0;
  push.meshletCount = uint32_t(clusters.size());
  push.znear        = scene.znear;

  cmd.setBinding(0, outputImage);
  cmd.setBinding(1, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, *scene.gbufNormals);
  cmd.setBinding(3, *scene.zbuffer);
  cmd.setBinding(4, clusters.ssbo());
  cmd.setBinding(5, buckets.ibo());
  cmd.setBinding(6, buckets.vbo());
  cmd.setBinding(7, buckets.textures());
  cmd.setBinding(8, Sampler::bilinear());

  auto* pso = &Shaders::inst().swRendering;
  switch(Gothic::options().swRenderingPreset) {
    case 1: {
      cmd.setPushData(&push, sizeof(push));
      cmd.setPipeline(*pso);
      //cmd.dispatch(10);
      cmd.dispatch(clusters.size());
      break;
      }
    case 2: {
      IVec2  tileSize = IVec2(128);
      int    tileX    = (outputImage.w()+tileSize.x-1)/tileSize.x;
      int    tileY    = (outputImage.h()+tileSize.y-1)/tileSize.y;
      cmd.setPushData(&push, sizeof(push));
      cmd.setPipeline(*pso);
      cmd.dispatch(size_t(tileX), size_t(tileY)); //outputImage.size());
      break;
      }
    case 3: {
      cmd.setBinding(9,  *scene.lights);
      cmd.setBinding(10, instanceSsbo);
      cmd.setPushData(&push, sizeof(push));
      cmd.setPipeline(*pso);
      cmd.dispatchThreads(outputImage.size());
      break;
      }
    }
  }

void Renderer::drawGBuffer(Encoder<CommandBuffer>& cmd, uint8_t fId, WorldView& view) {
  cmd.setDebugMarker("GBuffer");
  cmd.setFramebuffer({{gbufDiffuse, Tempest::Vec4(), Tempest::Preserve},
                      {gbufNormal,  Tempest::Vec4(), Tempest::Preserve}},
                     {zbuffer, Tempest::Preserve, Tempest::Preserve});
  view.drawGBuffer(cmd,fId);
  }

bool Renderer::drawGWater(Encoder<CommandBuffer>& cmd, WorldView& view, bool merged) {
  static bool water = true;
  if(!water)
    return false;

  cmd.setFramebuffer({});
  cmd.setDebugMarker(merged ? "Water+translucent" : "Water");
  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve},
                      {gbufDiffuse, Vec4(0,0,0,0),     Tempest::Preserve},
                      {gbufNormal,  Vec4(0,0,0,0),     Tempest::Preserve}},
                     {zbuffer, Tempest::Preserve, Tempest::Preserve});
  // cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}},
  //                    {zbuffer, Tempest::Preserve, Tempest::Preserve});
  if(!merged) cmd.setDebugMarker("GWater");
  view.drawWater(cmd);
  return true;
  }

void Renderer::drawReflections(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  auto& pso = settings.zEnvMappingEnabled ? shaders.waterReflectionSSR : shaders.waterReflection;

  cmd.setDebugMarker("Reflections");
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, sceneOpaque, Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(2, gbufDiffuse, Sampler::nearest (ClampMode::ClampToEdge));
  cmd.setBinding(3, gbufNormal,  Sampler::nearest (ClampMode::ClampToEdge));
  cmd.setBinding(4, zbuffer,     Sampler::nearest (ClampMode::ClampToEdge));
  cmd.setBinding(5, sceneDepth,  Sampler::nearest (ClampMode::ClampToEdge));
  cmd.setBinding(6, sky.viewCldLut, sky.sampler);
  cmd.setPipeline(pso);
  if(Gothic::options().doMeshShading) {
    cmd.dispatchMeshThreads(gbufDiffuse.size());
    } else {
    cmd.draw(nullptr, 0, 3);
    }
  }

void Renderer::drawUnderwater(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, zbuffer);

  cmd.setPipeline(shaders.underwaterT);
  cmd.draw(nullptr, 0, 3);
  cmd.setPipeline(shaders.underwaterS);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawShadowMap(Encoder<CommandBuffer>& cmd, uint8_t fId, WorldView& view) {
#if defined(__ANDROID__)
  if(vrShadowActive) {
    drawVrShadow(cmd, view);
    return;
    }
#endif
  // Reverse-depth zero is unoccluded. Clear once when disabled so forward
  // materials and fog never retain stale shadows; subsequent frames do no work.
  if(!vrShadows && !vrShadowClearNeeded) return;
  for(uint8_t i=0; i<Resources::ShadowLayers; ++i) {
    if(shadowMap[i].isEmpty())
      continue;
    cmd.setFramebuffer({});
    cmd.setDebugMarker(string_frm(vrShadows ? "ShadowMap #" : "Shadow clear (disabled) #",i));
    cmd.setFramebuffer({}, {shadowMap[i], 0.f, Tempest::Preserve});
    if(vrShadows && view.mainLight().dir().y > Camera::minShadowY)
      view.drawShadow(cmd,fId,i);
    }
  cmd.setFramebuffer({});
  cmd.setDebugMarker("After shadows");
  vrShadowClearNeeded=false;
  }

#if defined(__ANDROID__)
// VR cached shadow (vr/vrshadowcache.h). Once per stereo frame (first eye):
// swap in a completed static map, build this frame's world-aligned matrices
// (dynamic map around the head; the static map's build matrix is captured on
// the first slice frame and kept for the cycle), and hand them to the world
// view. The second eye reuses the first eye's matrices and maps.
void Renderer::prepareVrShadow(WorldView& wview, const Camera& shadowView, bool secondEye) {
  wview.setVrStaticLighting(vrStaticLighting);
  vrShadowActive = vrShadows && !shadowMap[0].isEmpty() && !shadowMap[1].isEmpty() && !vrShadowBack.isEmpty() &&
                   !settings.vsmEnabled && !settings.rtsmEnabled;
  // 2002 static lighting: the static map is still built (objects only, the
  // bake carries the terrain's own shadows) so trees and buildings cast onto
  // the baked ground; 0.0.37 measured it at about 0.13 ms per frame.
  const bool buildStatic = true;
  if(!vrShadowActive) {
    vrShadowSlicer.reset();
    vrShadowStep        = {};
    vrShadowSwapPending = false;
    vrShadowFrontValid  = false;
    // Shadow tiles: the plain path may draw or clear the maps untracked.
    vrShadowTilesActive      = false;
    vrShadowTileDynamicValid = false;
    vrShadowTileStaticValid  = false;
    vrShadowStaticClear      = false;
    vrShadowDynamicEmpty     = false;
    wview.setVrShadowRange(false, 0, 0);
    wview.setVrShadow(false, Matrix4x4(), Matrix4x4(), Matrix4x4(), false, 0, 1, Vec4(0.f, 0.f, vrBakedShadow ? 1.f : 0.f, 0.f));
    return;
    }
  if(!secondEye) {
    const Vec3     sun    = wview.mainLight().dir();
    const Vec3     center = shadowView.originLwc();
    const uint32_t resS   = uint32_t(std::max(1,shadowMap[1].w()));
    const uint32_t resD   = uint32_t(std::max(1,shadowMap[0].w()));
    if(vrShadowSwapPending) {
      std::swap(shadowMap[1], vrShadowBack);
      vrShadowFront       = vrShadowBuilt;
      vrShadowFrontValid  = true;
      vrShadowSwapPending = false;
      // Shadow tiles: drawVrShadow rebuilds tile 1 from the map swapped in
      // here, before this frame's direct light, so both change together.
      vrShadowTileStaticValid = false;
      vrShadowStaticClear     = false;
      }
    vrShadowSunUp   = sun.y > Camera::minShadowY;
    // Shadow tiles: map 0's tiles are stale until drawVrShadow
    // rebuilds them this frame; a (re)allocated tile image holds nothing yet.
    vrShadowTilesActive      = vrShadowTiles && !shaders.shadowTiles.isEmpty() &&
                               !shaders.directLightShTiles.isEmpty() && !shaders.directAmbientShTiles.isEmpty();
    vrShadowTileDynamicValid = false;
    if(vrShadowTilesActive) {
      for(int i=0; i<Resources::ShadowLayers; ++i) {
        const uint32_t tw = (uint32_t(shadowMap[i].w())+7u)/8u, th = (uint32_t(shadowMap[i].h())+7u)/8u;
        if(vrShadowTile[i].isEmpty() || uint32_t(vrShadowTile[i].w())!=tw || uint32_t(vrShadowTile[i].h())!=th) {
          usesImage2d(vrShadowTile[i], TextureFormat::RG32F, tw, th);
          if(i==1)
            vrShadowTileStaticValid = false;
          }
        }
      }
    const uint64_t generation = wview.drawCommandGeneration();
    vrShadowDynamic = Vr::shadowMatrix(Vr::snapCenter(center,sun,vrShadowDynamicWidth,resD), sun, vrShadowDynamicWidth, vrShadowDepth);
    if(vrShadowSunUp && buildStatic) {
      // Cycle schedule (vr/vrshadowcache.h). On demand a
      // finished map stays until the head moves 8 m, the sun turns 0.25 deg,
      // the command list changes, a static caster moves inside it or 10 s
      // pass; otherwise a cycle starts as soon as one ends. A command change
      // mid-cycle restarts it either way (0.0.36/37: no slice may miss clusters).
      const Vec3 snapped = Vr::snapCenter(center,sun,vrShadowStaticWidth,resS);
      Vr::ShadowSlicer::Inputs in;
      in.center      = snapped;
      in.sun         = sun;
      in.generation  = generation;
      in.nowMs       = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
      in.casterMoves = wview.vrStaticCasterMoves();
      vrShadowStep = vrShadowSlicer.step(Vr::shadowMatrix(snapped, sun, vrShadowStaticWidth, vrShadowDepth), in);
      if(vrShadowStep.begin) {
        // Balanced slices: this map's caster weights give the cycle's cluster
        // ranges (047: raw ranges put the objects into 1-2 of 12 slices).
        vrShadowBounds.clear();
        if(vrShadowBalanced) {
          wview.vrStaticShadowWeights(vrShadowSlicer.build, vrShadowStaticWidth, vrStaticLighting, vrShadowWeights);
          vrShadowBounds.resize(size_t(vrShadowStep.slices)+1);
          Vr::ShadowSlicer::balancedBounds(vrShadowWeights.data(), uint32_t(vrShadowWeights.size()), vrShadowStep.slices, vrShadowBounds.data());
          }
        // Static casters moving inside this map (NPC-held items, movers) request the next rebuild.
        wview.setVrStaticCasterArea(snapped, vrShadowSlicer.movedCasterMs>0 ? 0.75f*vrShadowStaticWidth : 0.f);
        ++vrShadowReasons[vrShadowStep.reason];
        ++vrShadowRebuilds;
        if(vrShadowSlicer.onDemand && (vrShadowRebuilds<=8 || vrShadowRebuilds%32==0)) {
          const uint32_t* r = vrShadowReasons;
          Log::i("VR shadow rebuild #",vrShadowRebuilds," reason=",Vr::ShadowSlicer::reasonName(vrShadowStep.reason),
                 " (first=",r[Vr::ShadowSlicer::R_First]," moved=",r[Vr::ShadowSlicer::R_Moved]," sun=",r[Vr::ShadowSlicer::R_Sun],
                 " commands=",r[Vr::ShadowSlicer::R_Commands]," caster=",r[Vr::ShadowSlicer::R_Caster]," interval=",r[Vr::ShadowSlicer::R_Interval],")");
          }
        }
      } else {
      vrShadowSlicer.reset();
      vrShadowStep       = {};
      vrShadowFrontValid = false;
      }
    vrShadowStaticMat = vrShadowFrontValid ? vrShadowFront : vrShadowSlicer.build;
    if(!vrShadowLogged) {
      Log::i("VR shadow cache: slices=",vrShadowSlicer.slices," staticWidth=",vrShadowStaticWidth," dynamicWidth=",vrShadowDynamicWidth,
             " depth=",vrShadowDepth," resolution=",resS);
      if(vrShadowSlicer.onDemand)
        Log::i("VR shadow on demand: active (rebuild on ",int(vrShadowSlicer.moveCm)," cm head move, ",vrShadowSlicer.sunDeg," deg sun, command change, ",
               vrShadowSlicer.movedCasterMs>0 ? "moved static caster (1 s spacing), " : "",int(vrShadowSlicer.intervalMs/1000)," s safety; balanced slices)");
      else
        Log::i("VR shadow on demand: off (vrShadowOnDemandOff=1), cycling slicer with raw cluster ranges");
      if(vrShadowTilesActive)
        Log::i("VR shadow tiles: active (exact PCF early-out, 8x8-texel min/max tiles + 4-texel margin: dynamic ",vrShadowTile[0].w(),"x",vrShadowTile[0].h(),
               ", static ",vrShadowTile[1].w(),"x",vrShadowTile[1].h(),"; NPC map skipped without animated casters)");
      else
        Log::i("VR shadow tiles: off (",vrShadowTiles ? "shaders missing" : "vrShadowTilesOff=1","), plain PCF");
      Log::i(vrShadowNightSkip ? "VR shadow night skip: active (sun below the horizon: direct light without shadow maps)"
                               : "VR shadow night skip: off (vrShadowNightSkipOff=1)");
      vrShadowLogged = true;
      }
    }
  // Both eyes: updateCamera rewrote shadowMatrix with the desktop cascades, so
  // the cached matrices are restored here for the second eye too (0.0.34/35
  // had shadows in the left eye only).
  shadowMatrix[0] = vrShadowDynamic;
  shadowMatrix[1] = vrShadowStaticMat;
  // Every first-eye frame of a cycle culls its own cluster range into the
  // V_Vsm buffers; idle frames (on demand) cull and draw nothing.
  const bool cull     = !secondEye && vrShadowSunUp && buildStatic && vrShadowStep.build;
  const bool balanced = cull && vrShadowBalanced && vrShadowBounds.size()==size_t(vrShadowStep.slices)+1;
  wview.setVrShadowRange(balanced, balanced ? vrShadowBounds[vrShadowStep.slice] : 0u, balanced ? vrShadowBounds[vrShadowStep.slice+1] : 0u);
  wview.setVrShadow(true, shadowMatrix[0], shadowMatrix[1], vrShadowSlicer.build,
                    cull, vrShadowStep.slice, vrShadowStep.slices, Vec4(1.f, 0.85f, vrBakedShadow ? 1.f : 0.f, 0.f));
  }

void Renderer::drawVrShadow(Encoder<CommandBuffer>& cmd, WorldView& view) {
  if(!vrShadowSunUp) {
    // Light below the horizon: keep both maps unoccluded (clears only).
    for(uint8_t i=0; i<Resources::ShadowLayers; ++i) {
      cmd.setFramebuffer({});
      cmd.setDebugMarker(string_frm("Shadow clear (night) #",i));
      cmd.setFramebuffer({}, {shadowMap[i], 0.f, Tempest::Preserve});
      }
    // Shadow tiles: both maps now hold the clear; the tile variant skips both.
    vrShadowStaticClear     = true;
    vrShadowTileStaticValid = false;
    vrShadowDynamicEmpty    = true;
    } else {
    cmd.setFramebuffer({});
    cmd.setDebugMarker("ShadowMap dynamic");
    cmd.setFramebuffer({}, {shadowMap[0], 0.f, Tempest::Preserve});
    view.drawShadowDynamic(cmd);
    cmd.setFramebuffer({});
    cmd.setDebugMarker("ShadowMap static");
    // On demand: idle frames keep the finished map and draw
    // nothing; the marker stays so the capture row reads ~0 instead of vanishing.
    if(vrShadowStep.build) {
      if(vrShadowStep.begin)
        cmd.setFramebuffer({}, {vrShadowBack, 0.f, Tempest::Preserve}); else
        cmd.setFramebuffer({}, {vrShadowBack, Tempest::Preserve, Tempest::Preserve});
      view.drawShadowStatic(cmd, 0, 1); // the cull already limited this frame to its cluster range
      if(vrShadowStep.end) {
        vrShadowBuilt       = vrShadowSlicer.build;
        vrShadowSwapPending = true;
        }
      }
    }
  // Shadow tiles: map 0 every frame it can hold a caster (no
  // animated sphere reaches its frustum otherwise: skipped exactly by flag);
  // map 1 once per swap, from the very image the direct light samples.
  if(vrShadowSunUp && vrShadowTilesActive) {
    vrShadowDynamicEmpty = !view.vrDynamicShadowCasters(frustrum[SceneGlobals::V_Shadow0]);
    const bool dyn  = !vrShadowDynamicEmpty;
    const bool stat = !vrShadowStaticClear && !vrShadowTileStaticValid;
    if(dyn || stat) {
      cmd.setFramebuffer({});
      cmd.setDebugMarker("Shadow tiles");
      if(dyn) {
        buildVrShadowTiles(cmd, shadowMap[0], vrShadowTile[0]);
        vrShadowTileDynamicValid = true;
        }
      if(stat) {
        buildVrShadowTiles(cmd, shadowMap[1], vrShadowTile[1]);
        vrShadowTileStaticValid = true;
        ++vrShadowTileBuilds;
        if(vrShadowTileBuilds<=8 || vrShadowTileBuilds%32==0)
          Log::i("VR shadow tiles: static tiles #",vrShadowTileBuilds," built from the current static map (rebuilds=",vrShadowRebuilds,")");
        }
      }
    }
  cmd.setFramebuffer({});
  cmd.setDebugMarker("After shadows");
  vrShadowClearNeeded = false;
  }

// Min/max tiles of one VR shadow map (lighting/shadow_tiles.comp), one thread
// per tile, 8x8 tiles per workgroup. The PCF's sampler: the tiles clamp at the
// map edge exactly like its gathers.
void Renderer::buildVrShadowTiles(Encoder<CommandBuffer>& cmd, const ZBuffer& map, StorageImage& tiles) {
  cmd.setBinding(0, map, Resources::shadowSampler());
  cmd.setBinding(1, tiles);
  cmd.setPipeline(shaders.shadowTiles);
  cmd.dispatch((size_t(tiles.w())+7u)/8u, (size_t(tiles.h())+7u)/8u);
  }
#endif

void Renderer::drawShadowResolve(Encoder<CommandBuffer>& cmd, const WorldView& wview, bool combinedAmbient) {
  static bool light = true;
  if(!light)
    return;

  auto& scene = wview.sceneGlobals();
  auto* directPso = vrShadows ? shadow.directLightPso : &shaders.directLight;
  cmd.setDebugMarker(settings.vsmEnabled ? "DirectSunLight-VSM" : "DirectSunLight");

  auto originLwc = scene.originLwc;
  cmd.setPushData(&originLwc, sizeof(originLwc));
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
  cmd.setBinding(2, gbufNormal,  Sampler::nearest());
  cmd.setBinding(3, zbuffer,     Sampler::nearest());
  if(directPso==&shaders.vsmDirectLight) {
    cmd.setBinding(4, vsm.pageTbl);
    cmd.setBinding(5, vsm.pageList);
    cmd.setBinding(6, vsm.pageData);
    cmd.setBinding(8, scene.vsmDbg);
    }
  else if(directPso==&shaders.directLightRq) {
    for(size_t r=0; r<Resources::ShadowLayers; ++r) {
      if(shadowMap[r].isEmpty())
        continue;
      cmd.setBinding(4+r, shadowMap[r], Resources::shadowSampler());
      }
    cmd.setBinding(6, scene.rtScene.tlas);
    cmd.setBinding(7, Sampler::bilinear());
    cmd.setBinding(8, scene.rtScene.tex);
    cmd.setBinding(9, scene.rtScene.vbo);
    cmd.setBinding(10,scene.rtScene.ibo);
    cmd.setBinding(11,scene.rtScene.rtDesc);
    }
  else if(directPso==&shaders.rtsmDirectLight) {
    cmd.setBinding(4, rtsm.outputImage);
    cmd.setBinding(5, rtsm.outputImageClr.isEmpty() ? Resources::fallbackBlack() : textureCast<Texture2d&>(rtsm.outputImageClr));
    }
  else {
    for(size_t r=0; r<Resources::ShadowLayers; ++r) {
      if(shadowMap[r].isEmpty())
        continue;
      cmd.setBinding(4+r, shadowMap[r], Resources::shadowSampler());
      }
    }

#if defined(__ANDROID__)
  // VR shadow variants. Night: the sun is below the horizon
  // and drawVrShadow cleared both maps, so every tap is lit and the variant
  // without SHADOW_MAP gives the same image. Day: the min/max tile early-out
  // once this frame's tiles exist, else the plain PCF (both are exact).
  bool vrTiles = false;
  if(vrShadowActive && directPso==&shaders.directLightSh) {
    if(vrShadowNightSkip && !vrShadowSunUp) {
      directPso = &shaders.directLight;
      }
    else if(vrShadowTilesActive && (vrShadowDynamicEmpty || vrShadowTileDynamicValid) &&
            (vrShadowStaticClear || vrShadowTileStaticValid)) {
      vrTiles = true;
      }
    }
#endif
  const RenderPipeline* pso = directPso;
  if(combinedAmbient) {
    cmd.setBinding(6, sky.irradianceLut);
    pso = directPso==&shaders.directLightSh ? &shaders.directAmbientSh : &shaders.directAmbient;
    }
#if defined(__ANDROID__)
  if(vrTiles) {
    struct Push { Vec3 originLwc; uint32_t flags; } push = {originLwc, (vrShadowDynamicEmpty ? 1u : 0u) | (vrShadowStaticClear ? 2u : 0u)};
    static_assert(sizeof(Push)==16, "direct_light.frag VR_SHADOW_TILES push layout");
    cmd.setPushData(push);
    cmd.setBinding(7, vrShadowTile[0], Sampler::nearest());
    cmd.setBinding(8, vrShadowTile[1], Sampler::nearest());
    pso = combinedAmbient ? &shaders.directAmbientShTiles : &shaders.directLightShTiles;
    }
#endif
  cmd.setPipeline(*pso);
  if(vrShadows && settings.vsmEnabled) {
    cmd.setPushData(settings.vsmMipBias);
    cmd.setPipeline(*directPso);
    }
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::prepareVisibleLights(Encoder<CommandBuffer>& cmd, const WorldView& wview, bool secondEye) {
  vrLightsCompacted=false;
  vrLightTilesReady=false;
  vrSubgroupTilesReady=false;
  vrUniformLightsReady=false;
  vrSlabLightsActive=false;
  vrDrawnLightCount=0;
  if(!vrLighting || !vrLocalLights) return;
  if(settings.rtsmEnabled && !rtsm.outputImageClr.isEmpty()) return;
  vrDrawnLightCount=uint32_t(wview.lights().size());
#if defined(__ANDROID__)
  if(!vrFastLighting || lights.directLightPso!=&shaders.lights || !Resources::device().properties().indirect.indexed)
    return;
  const uint32_t count=uint32_t(wview.lights().visibleLightCount());
  vrLightsCompacted=true;
  vrDrawnLightCount=count;
  // Zero candidates require no compute/draw, and must not reuse old arguments.
  if(count==0) return;
  // UBO descriptors expose the whole allocation. Keep small lists exactly
  // 64 KiB, including after returning from a larger SSBO-only scene.
  if(count<=2048u) { usesSsbo(vrVisibleLights,2048u*32u); usesSsbo(vrSlabList,2048u*32u); }
  else { usesScratch(vrVisibleLights,size_t(count)*32u); usesScratch(vrSlabList,size_t(count)*32u); }
  usesSsbo(vrLightDraw,sizeof(Tempest::DrawIndexedIndirectCommand));
  usesSsbo(vrSlabDraw,12u*sizeof(uint32_t)); // [count, lit tiles, -, -, dispatch xyz, -, draw command]
  const uint32_t tileWidth =(uint32_t(zbuffer.w())+15u)/16u;
  const uint32_t tileHeight=(uint32_t(zbuffer.h())+15u)/16u;
  // Camera-side prior of the slab route for this eye: summed squared coverage
  // of the lights at or above vrSlabCoverage over the same list the GPU
  // compacts. The route itself comes from the VR window's controller, which
  // measures both routes' GPU cost (vr/lightroutecontroller.h): a model
  // cannot tell how much of a camera-enclosing light's screen coverage is
  // actually within its range.
  const uint8_t eye=secondEye ? 1 : 0;
  float gateWeight=0;
  {
    const auto& sg=wview.sceneGlobals();
    for(const auto& l:wview.lights().visibleLights()) {
      bool uncertain=false;
      const float c=LightCoverage::projected(sg.view,sg.proj,l.pos,l.range,sg.znear,uncertain);
      if(c>=vrSlabCoverage) gateWeight+=LightCoverage::gateWeight(c);
      }
    vrSlabView=sg.view;
  }
  vrSlabGateWeights[eye]=gateWeight;
  vrSlabGateEye=eye;
  const bool slabRoute=vrSlabLights && vrSlabRouteRequest[eye];
  // Lights covering at least vrSlabCoverage of the screen go to the slab
  // list; the rest keep the volume route. >1 disables the slab list.
  struct Push { uint32_t lightCount, indexCount; float znear, slabCoverage; uint32_t tileWidth, tileHeight; };
  const Push push{count,uint32_t(Resources::cubeIbo().size()),wview.sceneGlobals().znear,slabRoute ? vrSlabCoverage : 2.f,tileWidth,tileHeight};
  static_assert(sizeof(Push)==24);
  cmd.setFramebuffer({});
  cmd.setDebugMarker("Light visibility");
  cmd.setBinding(4,vrLightDraw);
  cmd.setBinding(5,vrSlabDraw);
  cmd.setPushData(push);
  cmd.setPipeline(shaders.lightVisibilityInit);
  cmd.dispatch(1);
  cmd.setBinding(0,wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1,wview.lights().visibleLightsSsbo());
  cmd.setBinding(2,hiz.hiZ,Sampler::nearest());
  cmd.setBinding(3,vrVisibleLights);
  cmd.setBinding(4,vrLightDraw);
  cmd.setBinding(5,vrSlabList);
  cmd.setBinding(6,vrSlabDraw);
  cmd.setPushData(push);
  cmd.setPipeline(shaders.lightVisibility);
  cmd.dispatchThreads(count);
  if(slabRoute) {
    // 128-bit light mask per 16x16 tile from the tile's view-depth slab and
    // the exact projected sphere bounds. No per-pixel reconstruction; the
    // fragment shader keeps the exact per-pixel radius test and ascending
    // light order, so the HDR sum equals the complete-list route.
    usesScratch(vrLightTiles,size_t(tileWidth)*tileHeight*5u*sizeof(uint32_t)); // [count, 4 mask words] per tile
    vrSlabTileWidth=tileWidth; vrSlabTileHeight=tileHeight; vrSlabOrigin=wview.sceneGlobals().originLwc;
    cmd.setBinding(0,wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(3,zbuffer,Sampler::nearest());
    cmd.setBinding(4,vrSlabList);
    cmd.setBinding(5,vrSlabDraw);
    cmd.setBinding(6,vrLightTiles);
    cmd.setPushData(vrSlabOrigin);
    cmd.setDebugMarker("Light slabs");
    cmd.setPipeline(shaders.lightTilesSlab);
    // Workgroup counts come from the compaction: zero when no light was
    // routed to the slab list, so an empty list costs no builder pass.
    cmd.dispatchIndirect(vrSlabDraw,4u*sizeof(uint32_t));
    vrLightTilesReady=true;
    vrSlabLightsActive=true;
    vrUniformLightsReady=vrUniformLights && count<=2048u && !shaders.lightsSlabUniform.isEmpty();
  }
  else if(vrTiledLights && !vrSlabLights) {
    // "Light slabs: Off" keeps the 0.0.18 tiled comparison route. With slabs
    // on and the gate closed every light uses the volume route.
    const uint32_t width=(uint32_t(zbuffer.w())+15u)/16u;
    const uint32_t height=(uint32_t(zbuffer.h())+15u)/16u;
    usesScratch(vrLightTiles,size_t(width)*height*65u*sizeof(uint32_t));
    cmd.setDebugMarker("Light tiles");
    cmd.setBinding(0,wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(3,zbuffer,Sampler::nearest());
    cmd.setBinding(4,vrVisibleLights);
    cmd.setBinding(5,vrLightDraw);
    cmd.setBinding(6,vrLightTiles);
    const auto origin=wview.sceneGlobals().originLwc;
    cmd.setPushData(origin);
    vrSubgroupTilesReady=vrSubgroupTiles && !shaders.lightTilesSubgroup.isEmpty();
    cmd.setDebugMarker(vrSubgroupTilesReady ? "Light tiles subgroup" : "Light tiles");
    cmd.setPipeline(vrSubgroupTilesReady ? shaders.lightTilesSubgroup : shaders.lightTiles);
    cmd.dispatch(width,height);
    vrLightTilesReady=true;
    vrUniformLightsReady=vrUniformLights && count<=2048u && !shaders.lightsTiledUniform.isEmpty();
  }
#endif
  }

uint32_t Renderer::readVrVisibleLightCount() const {
  // Diagnostic frames only, called after the eye-copy fence completes.
  if(!vrLightsCompacted || vrDrawnLightCount==0) return vrDrawnLightCount;
  Tempest::DrawIndexedIndirectCommand args{};
  Resources::device().readBytes(vrLightDraw,&args,sizeof(args));
  return args.instanceCount;
  }

uint32_t Renderer::readVrSlabLightCount() const {
  // Diagnostic frames only, after the eye fence: lights routed to the slab list.
  if(!vrSlabLightsActive || !vrLightsCompacted || vrDrawnLightCount==0 || vrSlabDraw.byteSize()<4u*sizeof(uint32_t)) return 0;
  uint32_t args[4]={};
  Resources::device().readBytes(vrSlabDraw,args,sizeof(args));
  return args[0];
  }

Renderer::VrLightSlabStats Renderer::readVrLightSlabStats() const {
  // Diagnostic frames only (serial eyes, fence completed). Reads the slab
  // light list and the slab masks written for the last prepared eye.
  VrLightSlabStats stats;
  if(!vrSlabLightsActive || !vrLightsCompacted || vrDrawnLightCount==0) return stats;
  const uint32_t count=std::min(readVrSlabLightCount(),2048u);
  const uint32_t tiles=vrSlabTileWidth*vrSlabTileHeight;
  if(count==0 || tiles==0 || vrLightTiles.byteSize()<size_t(tiles)*20u || vrSlabList.byteSize()<size_t(count)*32u) return stats;
  struct Packed { Tempest::Vec3 pos; float range; Tempest::Vec3 color; float pad; };
  static_assert(sizeof(Packed)==32);
  std::vector<Packed>   lights(count);
  std::vector<uint32_t> words(size_t(tiles)*5u);
  auto& device=Resources::device();
  device.readBytes(vrSlabList,lights.data(),lights.size()*sizeof(Packed));
  device.readBytes(vrLightTiles,words.data(),words.size()*sizeof(uint32_t));
  stats.lights=count; stats.tiles=tiles;
  std::vector<uint32_t> tilesPerLight(std::min<size_t>(count,128u),0);
  uint64_t bitsTotal=0;
  for(uint32_t t=0;t<tiles;++t) {
    uint32_t bits=0;
    for(uint32_t w=0;w<4;++w) {
      uint32_t v=words[size_t(t)*5u+1u+w];
      while(v!=0) {
        const uint32_t i=w*32u+uint32_t(std::countr_zero(v));
        v&=v-1u; ++bits;
        if(i<tilesPerLight.size()) ++tilesPerLight[i];
        }
      }
    if(bits>0) { ++stats.litTiles; bitsTotal+=bits; stats.maxPerTile=std::max(stats.maxPerTile,bits); }
    }
  stats.averagePerLitTile=stats.litTiles>0 ? double(bitsTotal)/double(stats.litTiles) : 0.0;
  // Lit-tile count written by the builder (must agree with the mask scan).
  if(vrSlabDraw.byteSize()>=4u*sizeof(uint32_t)) {
    uint32_t args[4]={};
    device.readBytes(vrSlabDraw,args,sizeof(args));
    stats.drawnTiles=args[1];
    }
  stats.detail.resize(tilesPerLight.size());
  for(size_t i=0;i<tilesPerLight.size();++i) {
    auto& d=stats.detail[i];
    d.range=lights[i].range;
    Tempest::Vec4 viewPos(lights[i].pos.x,lights[i].pos.y,lights[i].pos.z,1.f);
    vrSlabView.project(viewPos);
    d.distance=Tempest::Vec3(viewPos.x,viewPos.y,viewPos.z).length(); // from the eye
    d.coverage=float(tilesPerLight[i])/float(tiles);
    }
  return stats;
  }

void Renderer::drawLights(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  vrLightDepthActive=false;
  static bool light = true;
  if(!light || !vrLighting || !vrLocalLights)
    return;

  if(settings.rtsmEnabled && !rtsm.outputImageClr.isEmpty())
    return;

  auto& scene   = wview.sceneGlobals();
  cmd.setDebugMarker("Point lights");

  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
  cmd.setBinding(2, gbufNormal,  Sampler::nearest());
  cmd.setBinding(3, zbuffer,     Sampler::nearest());
  bool compactLights = false;
#if defined(__ANDROID__)
  compactLights = vrLightsCompacted;
#endif
  const auto lightCount = compactLights ? wview.lights().visibleLightCount() : wview.lights().size();
  if(lightCount==0) return;
  cmd.setBinding(4, compactLights ? vrVisibleLights : wview.lights().lightsSsbo());
#if defined(__ANDROID__)
  if(vrLightTilesReady && compactLights) {
    cmd.setPushData(scene.originLwc);
    if(vrSlabLightsActive) {
      // Large-coverage lights: one masked fullscreen sum. The small lights
      // left in the volume list are drawn by the volume route below.
      cmd.setBinding(4,vrSlabList);
      cmd.setBinding(5,vrSlabDraw);
      cmd.setBinding(6,vrLightTiles);
      cmd.setPipeline(vrUniformLightsReady ? shaders.lightsSlabUniform : shaders.lightsSlab);
      // Vertex count comes from the compaction: 3 with a non-empty slab
      // list, 0 otherwise (no fullscreen pass for an empty list).
      cmd.drawIndirect(vrSlabDraw,8u*sizeof(uint32_t));
      cmd.setBinding(4,vrVisibleLights);
    } else {
      cmd.setBinding(5,vrLightDraw);
      cmd.setBinding(6,vrLightTiles);
      cmd.setPipeline(vrUniformLightsReady ? shaders.lightsTiledUniform : shaders.lightsTiled);
      cmd.draw(nullptr,0,3);
      return;
    }
  }
#endif
  if(lights.directLightPso==&shaders.lightsVsm) {
    cmd.setBinding(5, vsm.pageTblOmni);
    cmd.setBinding(6, vsm.pageData);
    }
  if(lights.directLightPso==&shaders.lightsRq) {
    cmd.setBinding(6, scene.rtScene.tlas);
    cmd.setBinding(7, Sampler::bilinear());
    cmd.setBinding(8, scene.rtScene.tex);
    cmd.setBinding(9, scene.rtScene.vbo);
    cmd.setBinding(10,scene.rtScene.ibo);
    cmd.setBinding(11,scene.rtScene.rtDesc);
    }

  auto  originLwc = scene.originLwc;
  auto& ibo       = Resources::cubeIbo();
  cmd.setPushData(&originLwc, sizeof(originLwc));
#if defined(__ANDROID__)
  const auto* lightPso=lights.directLightPso;
  if(lightPso==&shaders.lights) {
    if(vrFastLighting && vrLightDepth) lightPso=&shaders.lightsFar;
    else if(!vrFastLighting) lightPso=&shaders.lightsReference;
  }
  cmd.setPipeline(*lightPso);
  vrLightDepthActive=lightPso==&shaders.lightsFar;
#else
  cmd.setPipeline(*lights.directLightPso);
#endif
  if(compactLights) cmd.drawIndexedIndirect(ibo,vrLightDraw,0);
  else cmd.draw(nullptr,ibo, 0,ibo.size(), 0,lightCount);
  }

void Renderer::drawSky(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  auto& scene = wview.sceneGlobals();

  cmd.setDebugMarker("Sky");
  if(sky.quality==PathTrace) {
    cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, sky.transLut,     sky.sampler);
    cmd.setBinding(2, sky.multiScatLut, sky.sampler);
    cmd.setBinding(3, sky.cloudsLut,    Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setBinding(4, zbuffer, Sampler::nearest());
    cmd.setBinding(5, shadowMap[1], Resources::shadowSampler());
    cmd.setPipeline(shaders.skyPathTrace);
    cmd.draw(nullptr, 0, 3);
    return;
    }

  auto& skyShader = sky.quality==VolumetricLQ ? shaders.sky : shaders.skySep;
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, sky.transLut,     sky.sampler);
  cmd.setBinding(2, sky.multiScatLut, sky.sampler);
  cmd.setBinding(3, sky.viewLut,      sky.sampler);
  cmd.setBinding(4, sky.fogLut3D,     Sampler::bilinear(ClampMode::ClampToEdge));
  if(sky.quality!=VolumetricLQ)
    cmd.setBinding(5, sky.fogLut3DMs, Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(6, *wview.sky().cloudsDay()  .lay[0], Sampler::trillinear());
  cmd.setBinding(7, *wview.sky().cloudsDay()  .lay[1], Sampler::trillinear());
  cmd.setBinding(8, *wview.sky().cloudsNight().lay[0], Sampler::trillinear());
  cmd.setBinding(9, *wview.sky().cloudsNight().lay[1], Sampler::trillinear());
  cmd.setPipeline(skyShader);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::prepareSSAO(Encoder<CommandBuffer>& cmd, WorldView& wview) {
  if(!settings.zCloudShadowScale)
    return;
  // ssao
  struct PushSsao {
    Matrix4x4 proj;
    Matrix4x4 projInv;
    } push;
  push.proj    = proj;
  push.projInv = proj;
  push.projInv.inverse();

  auto& device = Resources::device();
  const bool halfResolution = settings.ssaoHalfResolution && device.properties().hasStorageFormat(TextureFormat::RG32F);
  const auto aoSize = halfResolution ? Size((zbuffer.w()+1)/2, (zbuffer.h()+1)/2) : zbuffer.size();
  const auto aoFormat = halfResolution ? TextureFormat::RG32F : ssao.aoFormat;
  if(ssao.ssaoBuf.size()!=aoSize || ssao.ssaoBuf.format()!=aoFormat || ssao.ssaoBlur.size()!=zbuffer.size()) {
    Resources::recycle(std::move(ssao.ssaoBuf));
    Resources::recycle(std::move(ssao.ssaoBlur));
    ssao.ssaoBuf  = device.image2d(aoFormat, aoSize);
    ssao.ssaoBlur = device.image2d(ssao.aoFormat, zbuffer.size());
    }

  cmd.setFramebuffer({});
  cmd.setDebugMarker("SSAO");

  cmd.setBinding(0, ssao.ssaoBuf);
  cmd.setBinding(1, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, gbufDiffuse, Sampler::nearest(ClampMode::ClampToEdge));
  cmd.setBinding(3, gbufNormal,  Sampler::nearest(ClampMode::ClampToEdge));
  cmd.setBinding(4, zbuffer,     Sampler::nearest(ClampMode::ClampToEdge));
  cmd.setPushData(&push, sizeof(push));
  cmd.setPipeline(halfResolution ? shaders.ssaoHalf : shaders.ssao);
  cmd.dispatchThreads(ssao.ssaoBuf.size());

  cmd.setDebugMarker(halfResolution ? "SSAO upsample" : "SSAO blur");
  cmd.setBinding(0, ssao.ssaoBlur);
  cmd.setBinding(1, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  if(halfResolution)
    cmd.setBinding(2, ssao.ssaoBuf, Sampler::nearest(ClampMode::ClampToEdge));
  else
    cmd.setBinding(2, ssao.ssaoBuf);
  cmd.setBinding(3, zbuffer, Sampler::nearest(ClampMode::ClampToEdge));
  cmd.setPipeline(halfResolution ? shaders.ssaoUpsample : shaders.ssaoBlur);
  cmd.dispatchThreads(ssao.ssaoBlur.size());
  }

Renderer::FogWorkStats Renderer::readVrFogWorkStats() const {
  FogWorkStats result;
  if(vrFogHistogram.byteSize()==sizeof(result))
    Resources::device().readBytes(vrFogHistogram,&result,sizeof(result));
  return result;
}

void Renderer::prepareFog(Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  auto& scene  = wview.sceneGlobals();
  auto& device = Resources::device();

  if(sky.quality!=PathTrace) {
    cmd.setFramebuffer({});
#if defined(__ANDROID__)
    if(vrDiagnostics) {
      cmd.setDebugMarker("Fog work probe");
      Resources::recycle(std::move(vrFogHistogram));
      uint32_t zero[66]={};
      vrFogHistogram=device.ssbo(zero,sizeof(zero));
      const int32_t dimensions[4]={int32_t(sky.fogLut3D.w()),int32_t(sky.fogLut3D.h()),int32_t(sky.fogLut3D.d()),0};
      cmd.setBinding(0,scene.uboGlobal[SceneGlobals::V_Main]);
      cmd.setBinding(6,hiz.hiZ,Sampler::nearest(ClampMode::ClampToEdge));
      cmd.setBinding(7,vrFogHistogram);
      cmd.setPushData(dimensions,sizeof(dimensions));
      cmd.setPipeline(shaders.vrFogProbe);
      cmd.dispatchThreads(uint32_t(sky.fogLut3D.w()),uint32_t(sky.fogLut3D.h()));
    }
#endif
    cmd.setDebugMarker("Fog volume compute");
    auto& shader = sky.quality==VolumetricLQ ? shaders.fogViewLut3d : shaders.fogViewLutSep;
    cmd.setFramebuffer({});
    cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, sky.transLut,     sky.sampler);
    cmd.setBinding(2, sky.multiScatLut, sky.sampler);
    cmd.setBinding(3, sky.cloudsLut,    Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setBinding(4, sky.fogLut3D,     Sampler::bilinear(ClampMode::ClampToEdge));
    if(sky.quality==VolumetricHQ || sky.quality==Epipolar)
      cmd.setBinding(5, sky.fogLut3DMs);
#if defined(__ANDROID__)
    cmd.setBinding(6, hiz.hiZ, Sampler::nearest(ClampMode::ClampToEdge));
#endif
    cmd.setPipeline(shader);
    cmd.dispatchThreads(uint32_t(sky.fogLut3D.w()), uint32_t(sky.fogLut3D.h()));
    }

  if(settings.vsmEnabled && !settings.pathTraceEnabled && (sky.quality==VolumetricHQ || sky.quality==Epipolar)) {
    cmd.setFramebuffer({});
    cmd.setDebugMarker("VSM-epipolar-fog");
    cmd.setBinding(0, epipolar.epTrace);
    cmd.setBinding(1, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(2, epipolar.epipoles);
    cmd.setBinding(3, vsm.pageTbl);
    cmd.setBinding(4, vsm.pageData);
    cmd.setPipeline(shaders.vsmFogShadow);
    cmd.dispatchThreads(epipolar.epTrace.size());
    }

  switch(sky.quality) {
    case None:
    case VolumetricLQ:
      break;
    case VolumetricHQ: {
      cmd.setDebugMarker("Fog-sunshaft-occlusion");
      auto& occlusionLut = usesImage2d(sky.occlusionLut, TextureFormat::R32U, zbuffer.size());
      if(settings.vsmEnabled && !settings.rtsmEnabled && !settings.pathTraceEnabled) {
        cmd.setFramebuffer({});
        cmd.setBinding(0, occlusionLut);
        cmd.setBinding(1, epipolar.epTrace);
        cmd.setBinding(2, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
        cmd.setBinding(3, epipolar.epipoles);
        cmd.setBinding(4, zbuffer);
        cmd.setPipeline(shaders.fogEpipolarOcclusion);
        cmd.dispatchThreads(zbuffer.size());
        } else {
        cmd.setFramebuffer({});
        cmd.setBinding(2, zbuffer, Sampler::nearest());
        cmd.setBinding(3, scene.uboGlobal[SceneGlobals::V_Main]);
        cmd.setBinding(4, occlusionLut);
        cmd.setBinding(5, shadowMap[1], Resources::shadowSampler());
        cmd.setPipeline(shaders.fogOcclusion);
        cmd.dispatchThreads(occlusionLut.size());
        }
      break;
      }
    case Epipolar:{
      // experimental
      if(vsm.fogDbg.isEmpty())
        vsm.fogDbg = device.image2d(sky.lutRGBFormat, 1024, 2*1024);
      cmd.setFramebuffer({});
      cmd.setDebugMarker("VSM-trace");
      cmd.setBinding(0, vsm.fogDbg);
      cmd.setBinding(1, epipolar.epTrace);
      cmd.setBinding(2, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
      cmd.setBinding(3, epipolar.epipoles);
      cmd.setBinding(4, zbuffer);
      cmd.setBinding(5, sky.transLut,   sky.sampler);
      cmd.setBinding(6, sky.cloudsLut,  Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(7, sky.fogLut3DMs, Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setPipeline(shaders.vsmFogTrace);
      cmd.dispatchThreads(epipolar.epTrace.size());
      break;
      }
    case PathTrace: {
      break;
      }
    }
  }

void Renderer::prepareEpipolar(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  const bool doVolumetricFog = sky.quality!=VolumetricLQ && sky.quality!=PathTrace;
  if(!doVolumetricFog || (!settings.vsmEnabled && !settings.rtsmEnabled))
    return;

  auto& scene  = wview.sceneGlobals();

  auto& epTrace      = usesImage2d(epipolar.epTrace,  TextureFormat::R16,  1024, 2*1024);
  auto& epipoles     = usesSsbo   (epipolar.epipoles, shaders.fogEpipolarVsm.sizeofBuffer(3, size_t(epipolar.epTrace.h())));
  auto& occlusionLut = usesImage2d(sky.occlusionLut, TextureFormat::R32U, zbuffer.size());

  cmd.setDebugMarker("Fog-epipolar");
  cmd.setFramebuffer({});
  cmd.setBinding(0, occlusionLut);
  cmd.setBinding(1, epTrace);
  cmd.setBinding(2, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(3, epipoles);
  cmd.setBinding(4, zbuffer);
  if(settings.vsmEnabled)
    cmd.setPipeline(shaders.fogEpipolarVsm);
  else if(settings.rtsmEnabled)
    cmd.setPipeline(shaders.fogEpipolarVsm);
  cmd.dispatch(uint32_t(epTrace.h()));
  }

void Renderer::prepareLightsBvh(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  if(!Shaders::isLightsTreeSupported())
    return;

  struct Push {
    uint32_t numLightsPot;
    uint32_t numLights;
    } push = {};
  const uint32_t numLights = uint32_t(wview.lights().size());
  push.numLightsPot = nextPot(numLights);
  push.numLights    = numLights;

  auto& lightsSsbo = wview.lights().lightsSsbo();
  auto& bvhMorton  = usesSsbo(lightsTree.bvhMorton,   shaders.lightsBvh.sizeofBuffer(1, push.numLightsPot));
  auto& bvhAlux    = usesSsbo(lightsTree.bvhAlux,     shaders.lightsBvh.sizeofBuffer(2, numLights * 2));
  auto& ctrl       = usesSsboInit(lightsTree.bvhCtrl, shaders.lightsBvh.sizeofBuffer(3, (numLights + 31 )/32));

  const bool ltree = settings.pathTraceEnabled;
  const bool lbvh  = (settings.giMethod==GiMethod::IrrC);

  cmd.setDebugMarker("LightsTree");
  cmd.setFramebuffer({});
  cmd.setPushData(push);
  cmd.setBinding(0, lightsSsbo);
  cmd.setBinding(1, bvhMorton);
  cmd.setBinding(2, bvhAlux);
  cmd.setBinding(3, ctrl);

  cmd.setPipeline(shaders.lightsMorton);
  cmd.dispatch(1); // single pass, for simplicity

  if(ltree) {
    auto& tree = usesSsbo(lightsTree.tree, shaders.lightsTree.sizeofBuffer(4, numLights * 2));
    cmd.setBinding(4, tree);
    cmd.setPipeline(shaders.lightsTopology);
    cmd.dispatchThreads(numLights);

    cmd.setPipeline(shaders.lightsTree);
    cmd.dispatchThreads(numLights);
    }

  if(lbvh) {
    auto& bvh = usesSsbo(lightsTree.bvh, shaders.lightsBvh.sizeofBuffer(4, numLights * 2));
    cmd.setBinding(4, bvh);
    cmd.setPipeline(shaders.lightsTopology);
    cmd.dispatchThreads(numLights);

    cmd.setPipeline(shaders.lightsBvh);
    cmd.dispatchThreads(numLights);
    }
  }

void Renderer::prepareSurfels(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  static bool enable = true;
  if(!enable)
    return;

  if(settings.giMethod!=GiMethod::IrrC || !settings.zCloudShadowScale)
    return;

  const uint32_t maxSurfels = surf.maxSurfels;
  const int32_t  TileSize   = 96;
  const int32_t  LargeTile  = 128;
  const auto     binCount   = tileCount(zbuffer.size(), TileSize);

  auto& scene    = wview.sceneGlobals();
  auto& irrImage = usesImage2d (surf.irrImage, TextureFormat::RGBA16F, zbuffer.size());
  auto& surfels  = usesSsboInit(surf.surfels,  shaders.surfAlloc.sizeofBuffer(4, maxSurfels));

  auto& surfCnts = usesImage2d(surf.surfCnts, TextureFormat::R32U, binCount);
  auto& surfBins = usesImage2d(surf.surfBins, TextureFormat::R32U, binCount);
  auto& surfList = usesSsbo   (surf.surfList, 16*maxSurfels*sizeof(uint32_t));

  struct Push {
    Vec3     originLwc;
    uint32_t tileSize;
    uint32_t pass;
    } push = {};
  push.originLwc = scene.originLwc;
  push.tileSize  = uint32_t(TileSize);
  push.pass      = 0;

  cmd.setDebugMarker("Surfels");

  if(surf.gbuffFree.isEmpty()) {
    auto& gbuffFree = usesSsbo(surf.gbuffFree, shaders.surfFList.sizeofBuffer(0, maxSurfels));
    cmd.setPushData(surf.gbufTilesX);
    cmd.setBinding(0, gbuffFree);
    cmd.setPipeline(shaders.surfFList);
    cmd.dispatchThreads(maxSurfels);
    }

  // prev-frame
  {
    cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, zbuffer);
    cmd.setBinding(2, surfels);

    cmd.setPipeline(shaders.surfUpdate);
    cmd.dispatchThreads(maxSurfels);

    cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, hiz.hiZ);
    cmd.setBinding(2, zbuffer);
    cmd.setBinding(3, surfels);
    cmd.setBinding(4, surf.gbuffFree);
    cmd.setPushData(scene.znear);

    cmd.setPipeline(shaders.surfCulling);
    cmd.dispatchThreads(maxSurfels);

    cmd.setPipeline(shaders.surfCompact);
    cmd.dispatch(1);
  }

  static bool rays = true;
  if(rays) {
    surfelsTrace(cmd, wview, surfels, false);
    }

  static bool apply = true;
  if(apply) {
    surfelsBinning(cmd, wview, TileSize, false);
    surfelsApply(cmd, wview, TileSize, false);
    }

  // current-frame
  static bool alloc = true;
  if(alloc) {
    cmd.setPushData(push);
    cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, irrImage);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, surfels);
    cmd.setBinding(5, surf.gbuffFree);

    const auto tc = tileCount(zbuffer.size(), LargeTile);
    cmd.setPipeline(shaders.surfAlloc);
    cmd.dispatch(tc);
    }

  static bool gc = true;
  if(gc) {
    surfelsBinning(cmd, wview, TileSize, false);

    cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(3, surfels);
    //
    cmd.setBinding(6, surfCnts);
    cmd.setBinding(7, surfBins);
    cmd.setBinding(8, surfList);

    //cmd.setPushData(push);
    //cmd.setPipeline(shaders.surfBinSort); // assist with stable GC
    //cmd.dispatch(surfBins.size());

    cmd.setPushData(push);
    cmd.setPipeline(shaders.surfDecimate);
    cmd.dispatchThreads(maxSurfels);
    }

  if(rays) {
    surfelsTrace(cmd, wview, surfels, true);
    }

  if(apply) {
    surfelsBinning(cmd, wview, TileSize, true);
    surfelsApply(cmd, wview, TileSize, true);
    }
  }

void Renderer::surfelsApply(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview, int32_t tileSize, bool postPass) {
  auto& scene = wview.sceneGlobals();

  auto& irrImage  = surf.irrImage;
  auto& surfels   = surf.surfels;
  auto& surfCnts  = surf.surfCnts;
  auto& surfBins  = surf.surfBins;
  auto& surfList  = surf.surfList;

  // auto& irrImage2 = usesAttachment(surf.irrImage2, TextureFormat::RGBA16F, zbuffer.size());
  // (void)irrImage2;

  struct Push {
    Vec3     originLwc;
    uint32_t tileSize;
    uint32_t pass;
    } push = {};
  push.originLwc = scene.originLwc;
  push.tileSize  = uint32_t(tileSize);
  push.pass      = postPass ? 1 : 0;

  cmd.setPushData(push);
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, irrImage);
  cmd.setBinding(2, gbufNormal);
  cmd.setBinding(3, zbuffer);
  cmd.setBinding(4, surfels);
  //
  cmd.setBinding(6, surfCnts);
  cmd.setBinding(7, surfBins);
  cmd.setBinding(8, surfList);

  cmd.setPipeline(shaders.surfApply);
  cmd.dispatchThreads(zbuffer.size());
  }

void Renderer::surfelsBinning(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview, int32_t tileSize, bool postPass) {
  const  uint32_t maxSurfels = surf.maxSurfels;

  auto& scene       = wview.sceneGlobals();
  auto& surfels     = surf.surfels;
  auto& binCtrl     = usesSsboInit(surf.surfBinsCtrl, sizeof(uint32_t));
  auto& surfCnts    = surf.surfCnts;
  auto& surfBins    = surf.surfBins;
  auto& surfList    = surf.surfList;

  struct Push {
    int32_t tileSize;
    int32_t allocPass;
    int32_t postPass;
    } push = {};
  push.tileSize = tileSize;
  push.postPass = postPass ? 1 : 0;

  cmd.setPushData(push);
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, binCtrl);
  cmd.setBinding(2, surfels);
  cmd.setBinding(3, surfCnts);
  cmd.setBinding(4, surfBins);
  cmd.setBinding(5, surfList);

  cmd.setPipeline(shaders.surfBinClear);
  cmd.dispatchThreads(surfBins.size());

  push.allocPass = 0;
  cmd.setPushData(push);
  cmd.setPipeline(shaders.surfBinPass);
  cmd.dispatchThreads(maxSurfels); //TODO: indirect

  cmd.setPipeline(shaders.surfBinAlloc);
  cmd.dispatchThreads(surfBins.size());

  push.allocPass = 1;
  cmd.setPushData(push);
  cmd.setPipeline(shaders.surfBinPass);
  cmd.dispatchThreads(maxSurfels);
  }

void Renderer::surfelsTrace(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview, StorageBuffer& surfels, bool postPass) {
  static int giMethod = 0;

  auto& scene = wview.sceneGlobals();

  const Tempest::ComputePipeline* pso = &shaders.surfRaycast;
  if(giMethod==1)
    pso = &shaders.surfPathtrace;

  const uint32_t gbufX = surf.gbufTilesX * uint32_t(surf.gbufTile.x);
  const uint32_t gbufY = surf.gbufTilesY * uint32_t(surf.gbufTile.y);

  auto& gbuffDiff = usesImage2d(surf.gbuffDiff, TextureFormat::RGBA8, gbufX, gbufY);
  auto& gbuffNorm = usesImage2d(surf.gbuffNorm, TextureFormat::RGBA8, gbufX, gbufY);
  auto& gbuffHitT = usesImage2d(surf.gbuffHitT, TextureFormat::R16,   gbufX, gbufY);

  const uint32_t pass = postPass ? 1 : 0;
  cmd.setPushData(pass);
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, surfels);
  cmd.setBinding(2, sky.viewCldLut, sky.sampler);
  cmd.setBinding(3, gbuffDiff);
  cmd.setBinding(4, gbuffNorm);
  cmd.setBinding(5, gbuffHitT);
  cmd.setBinding(6, scene.rtScene.tlas);
  cmd.setBinding(7, Sampler::trillinear());
  cmd.setBinding(8, scene.rtScene.tex);
  cmd.setBinding(9, scene.rtScene.vbo);
  cmd.setBinding(10,scene.rtScene.ibo);
  cmd.setBinding(11,scene.rtScene.rtDesc);
  cmd.setBinding(12,shadowMap[1]);
  cmd.setBinding(13,lightsTree.bvh);

  cmd.setPipeline(*pso);
  const uint32_t offset = postPass ? sizeof(uint32_t) : 0;
  if(!(giMethod==0 && postPass==false)) {
    cmd.dispatchIndirect(surfels, offset);
    // cmd.dispatchThreads(maxSurfels);
    }

  if(giMethod==0) {
    cmd.setPipeline(shaders.surLighting);
    cmd.dispatchIndirect(surfels, offset);
    }
  }

void Renderer::prepareIrradiance(Encoder<CommandBuffer>& cmd, WorldView& wview) {
  auto& scene = wview.sceneGlobals();

  cmd.setFramebuffer({});
  cmd.setDebugMarker("Irradiance");
  cmd.setBinding(0, sky.irradianceLut);
  cmd.setBinding(1, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, sky.viewCldLut, sky.sampler);
  cmd.setPipeline(shaders.irradiance);
  cmd.dispatch(1);
  }

void Renderer::prepareGi(Encoder<CommandBuffer>& cmd, WorldView& wview) {
  if(settings.giMethod!=GiMethod::Probes || !settings.zCloudShadowScale) {
    return;
    }

  const bool fisrtFrame = gi.voteTable.isEmpty();

  auto& hashTable          = usesSsbo(gi.hashTable, 2'097'152*sizeof(uint32_t)); // 8MB
  auto& voteTable          = usesSsbo(gi.voteTable, gi.hashTable.byteSize());
  auto& probes             = usesSsbo(gi.probes,    gi.maxProbes*32 + 64);       // probes and header
  auto& freeList           = usesSsbo(gi.freeList,  gi.maxProbes*sizeof(uint32_t) + sizeof(int32_t));
  auto& probesGBuffDiff    = usesImage2d(gi.probesGBuffDiff,    TextureFormat::RGBA8, gi.atlasDim*16, gi.atlasDim*16); // 16x16 tile
  auto& probesGBuffNorm    = usesImage2d(gi.probesGBuffNorm,    TextureFormat::RGBA8, gi.atlasDim*16, gi.atlasDim*16);
  auto& probesGBuffRayT    = usesImage2d(gi.probesGBuffRayT,    TextureFormat::R16,   gi.atlasDim*16, gi.atlasDim*16);
  auto& probesLighting     = usesImage2d(gi.probesLighting,     TextureFormat::R11G11B10UF, gi.atlasDim*3, gi.atlasDim*2);
  auto& probesLightingPrev = usesImage2d(gi.probesLightingPrev, TextureFormat::R11G11B10UF, uint32_t(gi.probesLighting.w()), uint32_t(gi.probesLighting.h()));

  const auto&  scene   = wview.sceneGlobals();
  const size_t maxHash = hashTable.byteSize()/sizeof(uint32_t);

  cmd.setFramebuffer({});
  if(fisrtFrame) {
    cmd.setDebugMarker("GI-Init");
    cmd.setBinding(0, voteTable);
    cmd.setBinding(1, hashTable);
    cmd.setBinding(2, probes);
    cmd.setBinding(3, freeList);
    cmd.setPipeline(shaders.probeInit);
    cmd.dispatch(1);
    cmd.setPipeline(shaders.probeClearHash);
    cmd.dispatchThreads(maxHash);

    cmd.setBinding(0, probesLighting);
    cmd.setBinding(1, Resources::fallbackBlack());
    cmd.setPipeline(shaders.copyImg);
    cmd.dispatchThreads(probesLighting.size());
    }

  static bool alloc = true;
  cmd.setDebugMarker("GI-Alloc");
  cmd.setBinding(0, voteTable);
  cmd.setBinding(1, hashTable);
  cmd.setBinding(2, probes);
  cmd.setBinding(3, freeList);
  cmd.setPipeline(shaders.probeClear);
  cmd.dispatchThreads(maxHash);

  if(alloc) {
    cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
    cmd.setBinding(2, gbufNormal,  Sampler::nearest());
    cmd.setBinding(3, zbuffer,     Sampler::nearest());
    cmd.setBinding(4, voteTable);
    cmd.setBinding(5, hashTable);
    cmd.setBinding(6, probes);
    cmd.setBinding(7, freeList);
    cmd.setPipeline(shaders.probeVote);
    cmd.dispatchThreads(sceneDepth.size());

    cmd.setBinding(0, voteTable);
    cmd.setBinding(1, hashTable);
    cmd.setBinding(2, probes);
    cmd.setBinding(3, freeList);
    cmd.setPipeline(shaders.probePrune);
    cmd.dispatchThreads(gi.maxProbes);

    cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
    cmd.setBinding(2, gbufNormal,  Sampler::nearest());
    cmd.setBinding(3, zbuffer,     Sampler::nearest());
    cmd.setBinding(4, voteTable);
    cmd.setBinding(5, hashTable);
    cmd.setBinding(6, probes);
    cmd.setBinding(7, freeList);
    cmd.setPipeline(shaders.probeAlocation);
    cmd.dispatchThreads(sceneDepth.size());
    }

  cmd.setDebugMarker("GI-Trace");
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, probesGBuffDiff);
  cmd.setBinding(2, probesGBuffNorm);
  cmd.setBinding(3, probesGBuffRayT);
  cmd.setBinding(4, hashTable);
  cmd.setBinding(5, probes);
  cmd.setBinding(6, scene.rtScene.tlas);
  cmd.setBinding(7, Sampler::bilinear());
  cmd.setBinding(8, scene.rtScene.tex);
  cmd.setBinding(9, scene.rtScene.vbo);
  cmd.setBinding(10,scene.rtScene.ibo);
  cmd.setBinding(11,scene.rtScene.rtDesc);
  cmd.setPipeline(shaders.probeTrace);
  cmd.dispatch(1024); // TODO: dispath indirect?

  cmd.setDebugMarker("GI-HashMap");
  cmd.setBinding(0, voteTable);
  cmd.setBinding(1, hashTable);
  cmd.setBinding(2, probes);
  cmd.setBinding(3, freeList);
  cmd.setPipeline(shaders.probeClearHash);
  cmd.dispatchThreads(maxHash);
  cmd.setPipeline(shaders.probeMakeHash);
  cmd.dispatchThreads(gi.maxProbes);

  cmd.setDebugMarker("GI-Lighting");
  cmd.setBinding(0, probesLightingPrev);
  cmd.setBinding(1, probesLighting);
  cmd.setPipeline(shaders.copyImg);
  cmd.dispatchThreads(probesLighting.size());

  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, probesLighting);
  cmd.setBinding(2, probesGBuffDiff, Sampler::nearest());
  cmd.setBinding(3, probesGBuffNorm, Sampler::nearest());
  cmd.setBinding(4, probesGBuffRayT, Sampler::nearest());
  cmd.setBinding(5, sky.viewCldLut,  sky.sampler);
  cmd.setBinding(6, shadowMap[1],    Sampler::bilinear());
  cmd.setBinding(7, probesLightingPrev, Sampler::nearest());
  cmd.setBinding(8, hashTable);
  cmd.setBinding(9, probes);
  cmd.setPipeline(shaders.probeLighting);
  cmd.dispatch(1024);
  }

void Renderer::prepareExposure(Encoder<CommandBuffer>& cmd, WorldView& wview, uint8_t mode) {
  auto& scene = wview.sceneGlobals();
#if defined(__ANDROID__)
  if(mode==2) {
    // Second eye: its Update globals re-uploaded exposure=1
    // and the raw sun/ambient; the first eye's result of this frame is exact
    // for it (lighting/sky_exposure.comp, EXPOSURE_APPLY).
    cmd.setFramebuffer({});
    cmd.setDebugMarker("Exposure");
    cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(5, vrExposureShared);
    cmd.setPipeline(shaders.skyExposureApply);
    cmd.dispatch(1);
    return;
    }
#else
  (void)mode;
#endif

  auto sunDir = wview.sky().sunLight().dir();
  struct Push {
    float baseL        = 0.0;
    float sunOcclusion = 1.0;
    };
  Push push;
  push.sunOcclusion = smoothstep(0.0f, 0.01f, sunDir.y);

  static float override = 0;
  static float add      = 1.5f;
  if(override>0)
    push.baseL = override;
  push.baseL += add;

  cmd.setFramebuffer({});
  cmd.setDebugMarker("Exposure");
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, sky.viewCldLut, sky.sampler);
  cmd.setBinding(2, sky.transLut,   Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(3, sky.cloudsLut,  Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(4, sky.irradianceLut);
  cmd.setPushData(&push, sizeof(push));
#if defined(__ANDROID__)
  if(mode==1) {
    cmd.setBinding(5, usesSsbo(vrExposureShared, 2*sizeof(Vec4)));
    cmd.setPipeline(shaders.skyExposureStore);
    } else
#endif
  cmd.setPipeline(shaders.skyExposure);
  cmd.dispatch(1);
  }

void Renderer::drawPathtrace(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview, uint8_t fId) {
  if(wview.sceneGlobals().rtScene.tlas.isEmpty())
    return;

  if(!Resources::device().properties().hasAttachFormat(Tempest::RGBA16F))
    return;

  static bool enable = true;
  if(!enable)
    return;

  if(!settings.pathTraceEnabled)
    return;

  if(pt.frame.size()!=sceneLinear.size()) {
    Resources::recycle(std::move(pt.frame));
    pt.frame = Resources::device().attachment(Tempest::RGBA16F, sceneLinear.size());
    }

  wview.visibilityPass(cmd, 0);
  wview.visibilityPass(cmd, 1);

  cmd.setFramebuffer({});
  prepareSky(cmd, wview);
  prepareIrradiance(cmd,wview);
  prepareExposure(cmd,wview);

  drawShadowMap(cmd,fId,wview);

  static float eps = 2.f;
  for(int i=0; i<16; ++i) {
    float a = viewProj.data()[i];
    float b = pt.mvpLast.data()[i];
    if(std::abs(a-b)>eps) {
      pt.numFrames = 0;
      break;
      }
    }

  if(pt.numFrames==0)
    pt.mvpLast = viewProj;

  struct Push {
    uint32_t numFrames;
    };
  Push push;
  push.numFrames = pt.numFrames; ++pt.numFrames;

  if(push.numFrames==0)
    cmd.setFramebuffer({{pt.frame, Vec4(0), Tempest::Preserve}}, {zbuffer, 1, Tempest::Preserve}); else
    cmd.setFramebuffer({{pt.frame, Tempest::Preserve, Tempest::Preserve}}, {zbuffer, 1, Tempest::Preserve});
  auto& scene = wview.sceneGlobals();
  cmd.setDebugMarker("Pathtrace");
  cmd.setPushData(push);
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  //cmd.setBinding(1, zbuffer);
  cmd.setBinding(2, sky.irradianceLut);
  cmd.setBinding(3, sky.viewCldLut, sky.sampler);
  //cmd.setBinding(4, shadowMap[1], Sampler::bilinear());
  cmd.setBinding(4, Resources::fallbackBlack(), Sampler::bilinear());
  cmd.setBinding(5, lightsTree.tree);
  cmd.setBinding(6, scene.rtScene.tlas);
  cmd.setBinding(7, Sampler::trillinear());
  cmd.setBinding(8, scene.rtScene.tex);
  cmd.setBinding(9, scene.rtScene.vbo);
  cmd.setBinding(10,scene.rtScene.ibo);
  cmd.setBinding(11,scene.rtScene.rtDesc);

  cmd.setPipeline(shaders.rtPathtrace);
  cmd.draw(nullptr, 0, 3);

  cmd.setFramebuffer({{sceneLinear, Tempest::Discard, Tempest::Preserve}});
  cmd.setDebugMarker("Blit");
  cmd.setBinding(0, pt.frame, Sampler::nearest());
  cmd.setPipeline(shaders.copy);
  cmd.draw(nullptr, 0, 3);

  prepareFog(cmd,wview);

  cmd.setFramebuffer({{sceneLinear, Tempest::Discard, Tempest::Preserve}}, {zbuffer, Tempest::Readonly});
  cmd.setDebugMarker("Sky");
  drawSky(cmd,wview);
  drawSunMoon(cmd,wview);

  cmd.setDebugMarker("Fog");
  drawFog(cmd, wview);
  }

void Renderer::drawRayQueryDbg(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  if(wview.sceneGlobals().rtScene.tlas.isEmpty() || shadowMap[1].isEmpty())
    return;

  static bool enable = false;
  if(!enable)
    return;

  auto& scene = wview.sceneGlobals();
  cmd.setDebugMarker("RQ-dbg");
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, zbuffer);
  cmd.setBinding(2, sky.irradianceLut);
  cmd.setBinding(3, shadowMap[1], Sampler::bilinear());
  //
  cmd.setBinding(6, scene.rtScene.tlas);
  cmd.setBinding(7, Sampler::trillinear());
  cmd.setBinding(8, scene.rtScene.tex);
  cmd.setBinding(9, scene.rtScene.vbo);
  cmd.setBinding(10,scene.rtScene.ibo);
  cmd.setBinding(11,scene.rtScene.rtDesc);

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}}, {zbuffer, Tempest::Readonly});
  cmd.setPipeline(shaders.rtDbg);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawSurfelsDbg(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  if(settings.giMethod!=GiMethod::IrrC)
    return;

  static bool enable = false;
  if(!enable)
    return;

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}}, {zbuffer, Tempest::Preserve, Tempest::Preserve});
  cmd.setDebugMarker("GI-dbg");
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, surf.surfels);
  cmd.setBinding(2, gbufNormal,  Sampler::nearest());
  cmd.setBinding(3, sceneDepth,  Sampler::nearest());
  cmd.setPipeline(shaders.surfDbg);
  cmd.draw(nullptr, 0, 36, 0, surf.maxSurfels);
  }

void Renderer::drawProbesDbg(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  if(settings.giMethod!=GiMethod::Probes)
    return;

  static bool enable = false;
  if(!enable)
    return;

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}}, {zbuffer, Tempest::Preserve, Tempest::Preserve});
  cmd.setDebugMarker("GI-dbg");
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, gi.probesLighting);
  cmd.setBinding(2, gi.probes);
  cmd.setBinding(3, gi.hashTable);
  cmd.setPipeline(shaders.probeDbg);
  cmd.draw(nullptr, 0, 36, 0, gi.maxProbes);
  }

void Renderer::drawProbesHitDbg(Encoder<CommandBuffer>& cmd) {
  if(settings.giMethod!=GiMethod::Probes)
    return;

  static bool enable = false;
  if(!enable)
    return;

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}}, {zbuffer, Tempest::Preserve, Tempest::Preserve});
  cmd.setDebugMarker("GI-dbg");
  cmd.setBinding(1, gi.probesLighting);
  cmd.setBinding(2, gi.probesGBuffDiff);
  cmd.setBinding(3, gi.probesGBuffRayT);
  cmd.setBinding(4, gi.probes);
  cmd.setPipeline(shaders.probeHitDbg);
  cmd.draw(nullptr, 0, 36, 0, gi.maxProbes*256);
  //cmd.draw(nullptr, 0, 36, 0, 1024);
  }

void Renderer::drawAmbient(Encoder<CommandBuffer>& cmd, const WorldView& view) {
  static bool enable = true;
  if(!enable)
    return;

  cmd.setDebugMarker("AmbientLight");
  cmd.setBinding(0, view.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
  cmd.setBinding(2, gbufNormal,  Sampler::nearest());
  if(settings.giMethod==GiMethod::IrrC && settings.zCloudShadowScale) {
    cmd.setBinding(3, surf.irrImage, Sampler::nearest(ClampMode::ClampToEdge));
    cmd.setBinding(4, ssao.ssaoBlur, Sampler::nearest(ClampMode::ClampToEdge)); //TODO: remove once splatting is working
    cmd.setBinding(5, surf.surfels);
    cmd.setPipeline(shaders.ambientLightSurf);
    }
  else if(settings.giMethod==GiMethod::Probes && settings.zCloudShadowScale) {
    cmd.setBinding(3, ssao.ssaoBlur, Sampler::nearest());
    cmd.setBinding(4, zbuffer,       Sampler::nearest());
    cmd.setBinding(5, gi.hashTable);
    cmd.setBinding(6, gi.probes);
    cmd.setBinding(7, gi.probesLighting);
    cmd.setPipeline(shaders.probeAmbient);
    }
  else if(settings.zCloudShadowScale) {
    cmd.setBinding(3, sky.irradianceLut);
    cmd.setBinding(4, ssao.ssaoBlur, Sampler::nearest(ClampMode::ClampToEdge));
    cmd.setPipeline(shaders.ambientLightSsao);
    }
  else {
    cmd.setBinding(3, sky.irradianceLut);
    cmd.setPipeline(shaders.ambientLight);
    }
  cmd.draw(nullptr, 0, 3);
  }

Tempest::Attachment Renderer::screenshoot(uint8_t frameId) {
  auto& device = Resources::device();
  device.waitIdle();

  uint32_t w    = uint32_t(zbuffer.w());
  uint32_t h    = uint32_t(zbuffer.h());
  auto     img  = device.attachment(Tempest::TextureFormat::RGBA8,w,h);

  auto wview  = Gothic::inst().worldView();
  auto camera = Gothic::inst().camera();
  if(wview==nullptr || camera==nullptr)
    return Attachment();

  CommandBuffer cmd;
  {
  auto enc = cmd.startEncoding(device);
  draw(img,enc,frameId,*wview,*camera);
  }

  auto sync = device.submit(cmd);
  sync.wait();
  return img;

  // debug
  auto d16     = device.attachment(TextureFormat::R16,    w, h);
  auto normals = device.attachment(TextureFormat::RGBA16, w, h);

  {
  auto enc = cmd.startEncoding(device);
  enc.setFramebuffer({{normals,Tempest::Discard,Tempest::Preserve}});
  enc.setBinding(0, gbufNormal, Sampler::nearest());
  enc.setPipeline(shaders.copy);
  enc.draw(nullptr, 0, 3);

  enc.setFramebuffer({{d16,Tempest::Discard,Tempest::Preserve}});
  enc.setBinding(0,zbuffer,Sampler::nearest());
  enc.setPipeline(shaders.copy);
  enc.draw(nullptr, 0, 3);
  }
  sync = device.submit(cmd);
  sync.wait();

  auto pm  = device.readPixels(textureCast<const Texture2d&>(normals));
  pm.save("gbufNormal.png");

  pm  = device.readPixels(textureCast<const Texture2d&>(d16));
  pm.save("zbuffer.hdr");

  return img;
  }

Size Renderer::vrEyeRect(Tempest::Size target) const {
  // the internal size from the origin when the eye can be
  // tonemapped into a sub-rectangle (switch on, no CMAA2, render scale < 1),
  // else the whole target. vrwindow hands the same value to QuestXr.
  const auto res = internalResolution(target);
  if(vrScaleRect && !settings.aaEnabled && res!=target && res.w<=target.w && res.h<=target.h)
    return res;
  return target;
  }

float Renderer::internalResolutionScale() const {
  return vrRenderScale>0 ? vrRenderScale : settings.renderScale;
  }

Size Renderer::internalResolution(Tempest::Size src) const {
  return Size(std::max(1,int(float(src.w)*internalResolutionScale())),
              std::max(1,int(float(src.h)*internalResolutionScale())));
  }
