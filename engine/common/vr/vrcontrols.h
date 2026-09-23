#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <span>
#include <vector>
#include <filesystem>
#include "vrinteractionmath.h"

namespace Vr {
// Where Settings::save() writes and MainWindow reads. The working-directory names
// are what the Quest has always used and what the host suites compile against;
// MainWindow points these at Vr::Platform::settingsFile() during startup, because
// on a desktop the working directory is whatever launched the game rather than the
// install. Deliberately an indirection instead of including vrassets.h: that header
// reaches <windows.h> and undefines macros (VOID, CONST, ...) that collide with
// ZenKit, and mainwindow.h pulls this one in ahead of the Windows headers the
// engine still needs intact.
inline std::filesystem::path settingsFile{"VR.ini"};
inline std::filesystem::path settingsTempFile{"VR.ini.tmp"};
enum class TurnMode { Snap, Smooth, Physical };
struct Settings {
  HolsterSettings interaction;
  // A, B, X, Y, L3, R3. The shipped default assumes Touch ergonomics: four face
  // buttons and two stick clicks. Index has all six; WMR loses Y; a Vive wand and
  // the khr/simple floor have neither face buttons nor a stick, and reach only
  // A (the right menu button) and the two trackpad clicks, so they fall back to
  // the three rows that a wand can actually produce.
  static constexpr std::array<int,6> touchMapping={1,2,3,4,9,5};
  static constexpr std::array<int,6> wandMapping ={2,0,0,0,9,3};
  std::array<int,6> mapping=touchMapping;
  // True only while this session swapped in wandMapping itself. Never read from or
  // written to VR.ini: it exists so the fallback can be undone when a full
  // controller comes back, and so it is not persisted over the player's profile.
  bool reducedMapping=false;
  // Follow the controller the runtime reports. Only a map that is still exactly the
  // shipped default is replaced, and only a fallback this session applied is taken
  // back, so an edited row and a deliberate Quest-side choice are both left alone.
  void adoptControllerDefaults(bool reduced) {
    if(reduced) { if(mapping==touchMapping) { mapping=wandMapping; reducedMapping=true; } }
    else if(reducedMapping) { if(mapping==wandMapping) mapping=touchMapping; reducedMapping=false; }
  }
  static const char* mappingName(int id) {static const char* names[]={"None","Jump","Interact","Inventory","Journal","Crouch","Lock target","Walk","Draw / stow","Run"};return names[std::clamp(id,0,9)];}
  TurnMode turn=TurnMode::Snap;
  int snapAngle=30, smoothSpeed=90;
  bool welcomeSeen=false;
  float runSpeed=1;
  bool runHold=false;
  float worldScale=1, renderScale=1, hudDistance=2, hudX=0, hudY=0;
  int fogMode=0, gpuSampling=1; // Mobile atmosphere / sampled GPU queries
  bool fastLighting=true, indexedObjects=true, batchedObjects=true, frameOverlap=true;
  bool shadows=true, lighting=true, localLights=true;
  bool tiledLights=true, subgroupTiles=true, uniformLights=true, stereoSeed=true, framePipeline=true;
  bool cpuStaging=true, lightDepth=true, mergedTransparency=true;
  bool directOutput=true;
  bool slabLights=true; // depth-slab light masks; overrides TiledLights when On
  int objectDistance=3; // 0 unlimited, 1 = 300 m, 2 = 200 m, 3 = 120 m (original zone far plane; default)
  bool horizonHaze=true;  // distant geometry fades into the sky colour before the far plane
  int horizon=1;          // VR far plane: 0 = 300 m, 1 = 450 m, 2 = 600 m, 3 = 1 km; the haze band is 55-95 % of it
  // XR_EXT_performance_settings level per domain: 0 = runtime default (no request),
  // 1 = power savings, 2 = sustained low, 3 = sustained high, 4 = boost.
  int cpuLevel=4,gpuLevel=4;
  int terrainLod=2;       // VR terrain detail: 0 = full, 1 = far (clustered levels from 200 m), 2 = near (120 m), 3 = aggressive (80 m)
  int foveation=0;        // fixed foveation (fragment density map): 0 off, 1 low, 2 medium, 3 high; off until proven in the headset
  bool staticLighting=true; // Performance -> Lighting: 2002 static (Spacer bake lights the landscape, static lights and the static shadow map off) / Dynamic (experimental)
  bool earlyLeftEye=false; // record the next left eye while the previous right eye and HUD execute; experimental
  bool roomScale=true, headMovement=true, profiler=true, hidePlayer=true;
  // Live profiler display while Profiler is on: 0 = panel, 1 = one compact line
  // at the bottom, 2 = CSV only. The panel keeps the HUD layer and its clear/copy
  // large; captures should use Compact or CSV only.
  int profilerView=0;
  void sanitize() {
    runSpeed=std::isfinite(runSpeed)?std::clamp(runSpeed,.75f,2.f):1.f;
    profilerView=std::clamp(profilerView,0,2);
    interaction.sanitize();for(auto& m:mapping)m=std::clamp(m,0,9);
    fogMode=std::clamp(fogMode,0,1); gpuSampling=std::clamp(gpuSampling,0,2); objectDistance=std::clamp(objectDistance,0,3); horizon=std::clamp(horizon,0,3); cpuLevel=std::clamp(cpuLevel,0,4); gpuLevel=std::clamp(gpuLevel,0,4); terrainLod=std::clamp(terrainLod,0,3); foveation=std::clamp(foveation,0,3);
    hudX=std::isfinite(hudX)?std::clamp(hudX,-0.3f,0.3f):0.f;
    hudY=std::isfinite(hudY)?std::clamp(hudY,-0.3f,0.3f):0.f;
    if(int(turn)<0 || int(turn)>2) turn=TurnMode::Snap;
    snapAngle=std::clamp(snapAngle,15,90); smoothSpeed=std::clamp(smoothSpeed,30,180);
    worldScale=std::isfinite(worldScale)?std::clamp(worldScale,0.5f,2.f):1.f;
    renderScale=std::isfinite(renderScale)?std::clamp(renderScale,0.6f,1.f):1.f;
    hudDistance=std::isfinite(hudDistance)?std::clamp(hudDistance,1.f,3.f):2.f;
  }
  void read(std::istream& file) {
    std::string line;bool holsterLayoutVersion=false,mirrorVersion=false,buttonVersion=false;
    while(std::getline(file,line)) {
      const auto eq=line.find('='); if(eq==std::string::npos) continue;
      const auto key=line.substr(0,eq); std::istringstream value(line.substr(eq+1)); double n=0;
      bool stringSetting=false;
      for(int i=0;i<4;++i) if(key=="HolsterItem"+std::to_string(i)) {
        auto name=line.substr(eq+1);if(!name.empty() && name.back()=='\r') name.pop_back();
        if(name.size()<128 && name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos) interaction.items[size_t(i)]=name;
        stringSetting=true;
      }
      if(key=="DefaultMeleeGrip") {
        ItemCalibration c;if(value>>c.offset.x>>c.offset.y>>c.offset.z>>c.rotation.x>>c.rotation.y>>c.rotation.z){value>>c.grip;if(value)value>>c.scale;c.sanitize();interaction.meleeDefault=c;}continue;
      }
      if(key.starts_with("ItemCal_")) {
        const auto id=key.substr(8);ItemCalibration cal;
        if(id.size()<140 && id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos &&
           value>>cal.offset.x>>cal.offset.y>>cal.offset.z>>cal.rotation.x>>cal.rotation.y>>cal.rotation.z) {value>>cal.grip;if(value)value>>cal.scale;if(value)value>>cal.stringHeight;if(value)value>>cal.stringCenter;if(value)value>>cal.stringSide;if(value)value>>cal.stringDepth;cal.sanitize();interaction.calibration[id]=cal;}
        continue;
      }
      if(stringSetting) continue;
      if(!(value>>n) || !std::isfinite(n) || std::abs(n)>100000) continue;
      for(int i=0;i<4;++i) {
        if(key=="HolsterX"+std::to_string(i)) interaction.offsets[size_t(i)].x=float(n);
        if(key=="HolsterY"+std::to_string(i)) interaction.offsets[size_t(i)].y=float(n);
        if(key=="HolsterZ"+std::to_string(i)) interaction.offsets[size_t(i)].z=float(n);
      }
      for(size_t i=0;i<mapping.size();++i) if(key=="ButtonMap"+std::to_string(i))mapping[i]=int(n);
      if(key=="CalibrationMirrorVersion")mirrorVersion=n>=1;
      else if(key=="HolsterLayoutVersion") holsterLayoutVersion=n>=1;
      else if(key=="Holsters") interaction.enabled=n!=0;
      else if(key=="VRHands") interaction.showHands=n!=0;
      else if(key=="HolsterModels") interaction.showHolsters=n!=0;
      else if(key=="ButtonLayoutVersion") buttonVersion=n>=2;
      else if(key=="PickupHighlight") interaction.pickupHighlight=n!=0;
      else if(key=="PickupHighlightRange") interaction.pickupHighlightRange=float(n);
      else if(key=="IgnoreWeaponRequirements") interaction.ignoreWeaponRequirements=n!=0;
      else if(key=="BowSight") interaction.bowSight=n!=0;
      else if(key=="GripLock") interaction.gripLock=n!=0;
      else if(key=="PhysicalCombat") interaction.physicalCombat=n!=0;
      else if(key=="HolsterRadius") interaction.radius=float(n);
      else if(key=="PickupRadius") interaction.pickupRadius=float(n);
      else if(key=="SwingSpeed") interaction.swingSpeed=float(n);
      else if(key=="TurnMode") turn=TurnMode(int(n));
      else if(key=="SnapAngle") snapAngle=int(n);
      else if(key=="SmoothSpeed") smoothSpeed=int(n);
      else if(key=="WelcomeSeen") welcomeSeen=n!=0;
      else if(key=="RunSpeed") runSpeed=float(n);
      else if(key=="RunHold") runHold=n!=0;
      else if(key=="WorldScale") worldScale=float(n);
      else if(key=="RenderScale") renderScale=float(n);
      else if(key=="HudDistance") hudDistance=float(n);
      else if(key=="HudX") hudX=float(n);
      else if(key=="HudY") hudY=float(n);
      else if(key=="FogMode") fogMode=int(n);
      else if(key=="FrameOverlap") frameOverlap=n!=0;
      else if(key=="IndexedObjects") indexedObjects=n!=0;
      else if(key=="BatchedObjects") batchedObjects=n!=0;
      else if(key=="FastLighting") fastLighting=n!=0;
      else if(key=="Shadows") shadows=n!=0;
      else if(key=="Lighting") lighting=n!=0;
      else if(key=="LocalLights") localLights=n!=0;
      else if(key=="TiledLights") tiledLights=n!=0;
      else if(key=="SubgroupTiles") subgroupTiles=n!=0;
      else if(key=="UniformLights") uniformLights=n!=0;
      else if(key=="StereoSeed") stereoSeed=n!=0;
      else if(key=="FramePipeline") framePipeline=n!=0;
      else if(key=="CpuStaging") cpuStaging=n!=0;
      else if(key=="LightDepth") lightDepth=n!=0;
      else if(key=="MergedTransparency") mergedTransparency=n!=0;
      else if(key=="DirectOutput") directOutput=n!=0;
      else if(key=="SlabLights") slabLights=n!=0;
      else if(key=="ObjectDistance") objectDistance=int(n);
      else if(key=="HorizonHaze") horizonHaze=n!=0;
      else if(key=="Horizon") horizon=int(n);
      else if(key=="CpuLevel") cpuLevel=int(n);
      else if(key=="GpuLevel") gpuLevel=int(n);
      else if(key=="TerrainLod") terrainLod=int(n);
      else if(key=="FoveationBands") foveation=int(n); // 0.0.28 wrote "Foveation"; that key is ignored so the 0.0.28 default does not persist
      else if(key=="PowerMode") cpuLevel=gpuLevel=(int(n)==2?4:int(n)==1?3:0); // 0.0.26 single Power row
      else if(key=="EarlyLeftEye") earlyLeftEye=n!=0;
      else if(key=="StaticLighting") staticLighting=n!=0;
      else if(key=="GpuSampling") gpuSampling=int(n);
      else if(key=="RoomScale") roomScale=n!=0;
      else if(key=="HeadMovement") headMovement=n!=0;
      else if(key=="Profiler") profiler=n!=0;
      else if(key=="ProfilerView") profilerView=int(n);
      else if(key=="HidePlayer") hidePlayer=n!=0;
    }
    if(!buttonVersion && mapping[4]==5 && mapping[5]==6){mapping[4]=9;mapping[5]=5;}
    // Correct the exact copied pair reported on the headset; preserve custom left edits.
    if(!mirrorVersion) {
      auto r=interaction.calibration.find("ITMW_FRANCISDAGGER_MIS_R"),l=interaction.calibration.find("ITMW_FRANCISDAGGER_MIS_L");
      if(r!=interaction.calibration.end() && l!=interaction.calibration.end() && r->second.offset==l->second.offset && r->second.rotation==l->second.rotation && r->second.grip==l->second.grip)l->second=mirrorCalibration(r->second);
    }
    // Upgrade only the untouched old belt; retain every custom position.
    if(!holsterLayoutVersion && (interaction.offsets[0]-Vec3(.23f,-.70f,0)).length()<.0001f)
      interaction.offsets[0]=HolsterSettings{}.offsets[0];
    if(!interaction.calibration.contains("DEFAULT_2H_SUP_R")) {
      auto axe=interaction.calibration.find("ITMW_2H_ORCAXE_01_SUP_R");
      if(axe!=interaction.calibration.end())interaction.calibration.emplace("DEFAULT_2H_SUP_R",axe->second);
      else if(auto left=interaction.calibration.find("ITMW_2H_ORCAXE_01_SUP_L");left!=interaction.calibration.end())
        interaction.calibration.emplace("DEFAULT_2H_SUP_R",mirrorCalibration(left->second));
    }
    // Older profiles used the support palm as the string anchor. Preserve that anchor once.
    std::vector<std::pair<std::string,ItemCalibration>> anchors;
    for(const auto& [key,cal]:interaction.calibration) {
      if(!key.ends_with("_SUP_L") && !key.ends_with("_SUP_R"))continue;
      auto anchor=key;anchor.replace(anchor.size()-5,3,"STR");
      if(!interaction.calibration.contains(anchor))anchors.emplace_back(anchor,cal);
    }
    for(const auto& entry:anchors)interaction.calibration.insert(entry);
    sanitize();
  }
  void write(std::ostream& f) const {
    const auto& d=interaction.meleeDefault;f<<"CalibrationMirrorVersion=1\nDefaultMeleeGrip="<<d.offset.x<<' '<<d.offset.y<<' '<<d.offset.z<<' '<<d.rotation.x<<' '<<d.rotation.y<<' '<<d.rotation.z<<' '<<d.grip<<' '<<d.scale<<'\n';
    f<<"ButtonLayoutVersion=2\n";
    // An untouched wand fallback is a property of the controller, not of the
    // profile: persist the map it replaced, so a wand session does not rewrite the
    // button layout a Touch session will read back. An edited row is still saved.
    const auto& persisted=(reducedMapping && mapping==wandMapping)?touchMapping:mapping;
    for(size_t i=0;i<persisted.size();++i)f<<"ButtonMap"<<i<<'='<<persisted[i]<<'\n';
    for(const auto& [id,c]:interaction.calibration) f<<"ItemCal_"<<id<<'='<<c.offset.x<<' '<<c.offset.y<<' '<<c.offset.z<<' '<<c.rotation.x<<' '<<c.rotation.y<<' '<<c.rotation.z<<' '<<c.grip<<' '<<c.scale<<' '<<c.stringHeight<<' '<<c.stringCenter<<' '<<c.stringSide<<' '<<c.stringDepth<<'\n';
    f<<"[Interaction]\nHolsterLayoutVersion=1\nHolsters="<<interaction.enabled<<"\nVRHands="<<interaction.showHands<<"\nHolsterModels="<<interaction.showHolsters
     <<"\nIgnoreWeaponRequirements="<<interaction.ignoreWeaponRequirements<<"\nBowSight="<<interaction.bowSight<<"\nPickupHighlightRange="<<interaction.pickupHighlightRange<<"\nPickupHighlight="<<interaction.pickupHighlight<<"\nGripLock="<<interaction.gripLock<<"\nPhysicalCombat="<<interaction.physicalCombat<<"\nHolsterRadius="<<interaction.radius<<"\nPickupRadius="<<interaction.pickupRadius<<"\nSwingSpeed="<<interaction.swingSpeed<<'\n';
    for(int i=0;i<4;++i) f<<"HolsterItem"<<i<<'='<<interaction.items[size_t(i)]<<"\nHolsterX"<<i<<'='<<interaction.offsets[size_t(i)].x<<"\nHolsterY"<<i<<'='<<interaction.offsets[size_t(i)].y<<"\nHolsterZ"<<i<<'='<<interaction.offsets[size_t(i)].z<<'\n';

    f<<"[VR]\nTurnMode="<<int(turn)<<"\nSnapAngle="<<snapAngle<<"\nSmoothSpeed="<<smoothSpeed
     <<"\nWelcomeSeen="<<welcomeSeen<<"\nRunSpeed="<<runSpeed<<"\nRunHold="<<runHold<<"\nWorldScale="<<worldScale<<"\nRenderScale="<<renderScale<<"\nHudDistance="<<hudDistance
     <<"\nHudX="<<hudX<<"\nHudY="<<hudY<<"\nFogMode="<<fogMode<<"\nGpuSampling="<<gpuSampling
     <<"\nFastLighting="<<fastLighting<<"\nIndexedObjects="<<indexedObjects<<"\nBatchedObjects="<<batchedObjects<<"\nFrameOverlap="<<frameOverlap
     <<"\nShadows="<<shadows<<"\nLighting="<<lighting<<"\nLocalLights="<<localLights
     <<"\nTiledLights="<<tiledLights<<"\nSubgroupTiles="<<subgroupTiles<<"\nUniformLights="<<uniformLights<<"\nStereoSeed="<<stereoSeed<<"\nFramePipeline="<<framePipeline
     <<"\nCpuStaging="<<cpuStaging
     <<"\nLightDepth="<<lightDepth
     <<"\nMergedTransparency="<<mergedTransparency
     <<"\nDirectOutput="<<directOutput
     <<"\nSlabLights="<<slabLights
     <<"\nObjectDistance="<<objectDistance
     <<"\nHorizonHaze="<<horizonHaze<<"\nEarlyLeftEye="<<earlyLeftEye<<"\nHorizon="<<horizon<<"\nCpuLevel="<<cpuLevel<<"\nGpuLevel="<<gpuLevel<<"\nTerrainLod="<<terrainLod<<"\nFoveationBands="<<foveation<<"\nStaticLighting="<<staticLighting
     <<"\nRoomScale="<<roomScale<<"\nHeadMovement="<<headMovement<<"\nProfiler="<<profiler<<"\nProfilerView="<<profilerView<<"\nHidePlayer="<<hidePlayer<<'\n';
  }
  bool save() const {
    { std::ofstream f(settingsTempFile,std::ios::trunc); write(f); f.flush(); if(!f) return false; }
#if defined(_WIN32)
    // Not std::rename: the Windows CRT refuses to rename onto a file that already
    // exists, so every save after the first would fail and report the settings as
    // unsaved while quietly leaving the previous VR.ini in place.
    std::error_code error; std::filesystem::rename(settingsTempFile,settingsFile,error); return !error;
#else
    return std::rename(settingsTempFile.c_str(),settingsFile.c_str())==0;
#endif
  }
};
struct Input {
  bool focused=false,leftGrip=false,rightGrip=false,menu=false,a=false,b=false;
  bool yButton=false,leftClick=false,rightClick=false;
  float x=0,y=0,trigger=0,secondaryTrigger=0;
  float rightX=0,rightY=0;
};
class Menu {
  public:
    enum Row { Turn,SnapAngle,SmoothSpeed,WorldScale,RoomScale,HeadMovement,HidePlayer,RenderScale,HudDistance,Profiler,Recenter,Close,
               Locomotion,Hud,Performance,HudX,HudY,HudReset,FogMode,GpuSampling,Back,FastLighting,IndexedObjects,RenderTests,Shadows,Lighting,LocalLights,FrameOverlap,TiledLights,SubgroupTiles,UniformLights,StereoSeed,FramePipeline,BatchedObjects,CpuStaging,LightDepth,MergedTransparency,DirectOutput,SlabLights,ObjectDistance,Holsters,Cheats,CheatPlayer,CheatEnemies,CheatItems,CheatWorld,Hands,HolsterEnabled,HolsterModels,HolsterPoint,HolsterItem,HolsterX,HolsterY,HolsterZ,HolsterRadius,PickupRadius,PhysicalCombat,SwingSpeed,HolsterReset,GodMode,Heal,EnemyCategory,EnemySelect,EnemySpawn,ItemCategory,ItemSelect,ItemQuantity,ItemGive,TimeHour,TimeApply,WeatherMode,WeatherApply,Controls,ItemCalibrationMenu,MapA,MapB,MapX,MapY,MapL3,MapR3,MapReset,CalHand,CalItem,CalX,CalY,CalZ,CalPitch,CalYaw,CalRoll,CalReset,ItemSwords,ItemTwoHanded,ItemBows,ItemCrossbows,ItemPotions,ItemAmmo,ItemArmor,ItemMagic,ItemOther,ItemAll,EnemyAnimals,EnemyGoblins,EnemyOrcs,EnemyUndead,EnemyGolems,EnemyMonsters,EnemyPeople,EnemyAll,HorizonHaze,EarlyLeftEye,Horizon,Power,CpuLevel,GpuLevel,TerrainLod,Foveation,HolsterAtLeft,HolsterAtRight,CalModel,CalAim,CalSupport,CalHolstered,CalStep,CalGrip,CalFlip,CalCopyHand,CalAimX,CalAimY,CalAimZ,CalAimPitch,CalAimYaw,CalAimRoll,CalAimReset,CalSupX,CalSupY,CalSupZ,CalSupPitch,CalSupYaw,CalSupRoll,CalSupReset,CalHolX,CalHolY,CalHolZ,CalHolPitch,CalHolYaw,CalHolRoll,CalHolReset,CalUseDefault,GripLock,PickupHighlight,CalBow,CalBowString,CalArrow,StaticLighting,CalScale,CalStringHeight,CalStringCenter,PickupHighlightRange,BowSight,IgnoreWeaponRequirements,HolsterSlot0,HolsterSlot1,HolsterSlot2,HolsterSlot3,HolsterMoveTarget,HolsterMove,HolsterClear,HolsterDrop,OpenGameInterface,CalStringSide,CalStringDepth,CalUseBowDefault,CalUseCrossbowDefault,RunSpeed,OpenCharacterStats,OpenGameMenu,RunMode };
    enum class Page { Main,Locomotion,Hud,Performance,RenderTests,Holsters,Cheats,CheatPlayer,CheatEnemies,CheatItems,CheatWorld,ItemList,Controls,Calibration,EnemyList,Power,CalibrationHome,Aim,Support,Holstered,HolsterSlot };
    static const char* performanceLevelName(int level) {
      switch(level) { case 1: return "Power savings"; case 2: return "Sustained low"; case 3: return "Sustained high"; case 4: return "Boost"; default: return "Runtime default"; }
      }
    Settings settings;
    int action=-1,actionDirection=0,holsterPoint=0,holsterMoveTarget=1,calibrationHand=1;
    Page page=Page::Main;
    int calibrationStep=1; // fine / normal / coarse
    bool calibrationPage() const {return page==Page::CalibrationHome || page==Page::Calibration || page==Page::Aim || page==Page::Support || page==Page::Holstered;}
    bool visible=false,changed=false,recenter=false;
    int selected=0;
    bool calibrationValueRow() const {
      const auto r=row();return (r>=CalX && r<=CalRoll) || (r>=CalAimX && r<=CalAimRoll) ||
        (r>=CalSupX && r<=CalSupRoll) || (r>=CalHolX && r<=CalHolRoll) || r==CalGrip || r==CalScale || r==CalStringHeight || r==CalStringCenter || r==CalStringSide || r==CalStringDepth;
    }
    bool toggleRow() const {
      switch(row()) {
        case RunMode:case RoomScale:case HeadMovement:case HidePlayer:case FogMode:case FastLighting:case IndexedObjects:
        case BatchedObjects:case HorizonHaze:case EarlyLeftEye:case StaticLighting:case Shadows:case Lighting:
        case LocalLights:case FrameOverlap:case TiledLights:case SubgroupTiles:case UniformLights:case StereoSeed:
        case FramePipeline:case CpuStaging:case LightDepth:case MergedTransparency:case DirectOutput:case SlabLights:
        case Hands:case HolsterEnabled:case HolsterModels:case PickupHighlight:case BowSight:case GripLock:
        case PhysicalCombat:case IgnoreWeaponRequirements:case GodMode:return true;
        default:return false;
      }
    }
    bool valueRow() const {
      if(calibrationValueRow())return true;
      switch(row()) {
        case CalItem:case CalHand:case CalStep:case HolsterPoint:case HolsterItem:case HolsterMoveTarget:
        case HolsterX:case HolsterY:case HolsterZ:case HolsterRadius:case PickupRadius:case SwingSpeed:
        case MapA:case MapB:case MapX:case MapY:case MapL3:case MapR3:
        case EnemyCategory:case EnemySelect:case ItemCategory:case ItemSelect:case ItemQuantity:case TimeHour:case WeatherMode:
        case RunMode:case RunSpeed:case Turn:case SnapAngle:case SmoothSpeed:case WorldScale:case RoomScale:case HeadMovement:
        case HidePlayer:case RenderScale:case HudDistance:case HudX:case HudY:case Profiler:case FogMode:case GpuSampling:
        case FastLighting:case IndexedObjects:case BatchedObjects:case ObjectDistance:case HorizonHaze:case EarlyLeftEye:
        case Horizon:case CpuLevel:case GpuLevel:case TerrainLod:case Foveation:case StaticLighting:
        case Shadows:case Lighting:case LocalLights:case FrameOverlap:case TiledLights:case SubgroupTiles:case UniformLights:
        case StereoSeed:case FramePipeline:case CpuStaging:case LightDepth:case MergedTransparency:case DirectOutput:case SlabLights:
        case Hands:case HolsterEnabled:case HolsterModels:case PickupHighlight:case PickupHighlightRange:case BowSight:
        case GripLock:case PhysicalCombat:case IgnoreWeaponRequirements:case GodMode:return true;
        default:return false;
      }
    }
    bool interactionPreview(bool focused) const { return visible && focused; }
    std::span<const Row> rows() const {
      static constexpr Row main[]={Locomotion,Hud,Performance,Recenter,Holsters,Cheats,Controls,ItemCalibrationMenu,OpenGameInterface,OpenCharacterStats,Close};
      static constexpr Row locomotion[]={Turn,SnapAngle,SmoothSpeed,WorldScale,RoomScale,HeadMovement,RunSpeed,RunMode,Back};
      static constexpr Row hud[]={PickupHighlight,PickupHighlightRange,BowSight,HudDistance,HudX,HudY,HudReset,HidePlayer,Back};
      static constexpr Row performance[]={RenderScale,FogMode,Profiler,GpuSampling,FastLighting,IndexedObjects,BatchedObjects,ObjectDistance,RenderTests,DirectOutput,HorizonHaze,EarlyLeftEye,Horizon,Power,TerrainLod,Foveation,StaticLighting,Back};
      static constexpr Row power[]={CpuLevel,GpuLevel,Back};
      static constexpr Row renderTests[]={Shadows,Lighting,LocalLights,FrameOverlap,TiledLights,SubgroupTiles,UniformLights,StereoSeed,FramePipeline,CpuStaging,LightDepth,MergedTransparency,SlabLights,Back};
      static constexpr Row holsters[]={HolsterSlot0,HolsterSlot1,HolsterSlot2,HolsterSlot3,GripLock,Hands,HolsterEnabled,HolsterModels,HolsterPoint,HolsterItem,HolsterX,HolsterY,HolsterZ,HolsterRadius,PickupRadius,PhysicalCombat,SwingSpeed,HolsterReset,HolsterAtLeft,HolsterAtRight,CalHolstered,Back};
      static constexpr Row holsterSlot[]={HolsterItem,HolsterMoveTarget,HolsterMove,HolsterClear,HolsterDrop,HolsterX,HolsterY,HolsterZ,HolsterAtLeft,HolsterAtRight,CalHolstered,Back};
      static constexpr Row cheats[]={CheatPlayer,CheatEnemies,CheatItems,CheatWorld,Back};
      static constexpr Row cheatPlayer[]={GodMode,Heal,IgnoreWeaponRequirements,Back};
      static constexpr Row cheatEnemies[]={EnemyAnimals,EnemyGoblins,EnemyOrcs,EnemyUndead,EnemyGolems,EnemyMonsters,EnemyPeople,EnemyAll,Back};
      static constexpr Row enemyList[]={EnemyCategory,EnemySelect,EnemySpawn,Back};
      static constexpr Row cheatItems[]={ItemSwords,ItemTwoHanded,ItemBows,ItemCrossbows,ItemPotions,ItemAmmo,ItemArmor,ItemMagic,ItemOther,ItemAll,Back};
      static constexpr Row itemList[]={ItemCategory,ItemSelect,ItemQuantity,ItemGive,Back};
      static constexpr Row controls[]={MapA,MapB,MapX,MapY,MapL3,MapR3,MapReset,Back};
      static constexpr Row calibrationHome[]={CalItem,CalHand,CalModel,CalAim,CalSupport,CalHolstered,CalStep,CalCopyHand,CalUseDefault,CalUseBowDefault,CalUseCrossbowDefault,CalBow,CalBowString,CalArrow,Back};
      static constexpr Row calibration[]={CalItem,CalHand,CalStep,CalX,CalY,CalZ,CalPitch,CalYaw,CalRoll,CalGrip,CalScale,CalFlip,CalReset,Back};
      static constexpr Row aim[]={CalItem,CalStep,CalAimX,CalAimY,CalAimZ,CalAimPitch,CalAimYaw,CalAimRoll,CalAimReset,Back};
      static constexpr Row support[]={CalItem,CalStep,CalSupX,CalSupY,CalSupZ,CalSupPitch,CalSupYaw,CalSupRoll,CalStringHeight,CalStringCenter,CalStringSide,CalStringDepth,CalSupReset,Back};
      static constexpr Row holstered[]={CalItem,HolsterPoint,CalStep,CalHolX,CalHolY,CalHolZ,CalHolPitch,CalHolYaw,CalHolRoll,CalHolReset,Back};
      static constexpr Row cheatWorld[]={TimeHour,TimeApply,WeatherMode,Back};
      switch(page) {
        case Page::CalibrationHome:return calibrationHome;
        case Page::Aim:return aim;
        case Page::Support:return support;
        case Page::Holstered:return holstered;
        case Page::Controls:return controls;
        case Page::Calibration:return calibration;
        case Page::ItemList:return itemList;
        case Page::EnemyList:return enemyList;
        case Page::Holsters: return holsters;
        case Page::HolsterSlot:return holsterSlot;
        case Page::Cheats: return cheats;
        case Page::CheatPlayer: return cheatPlayer;
        case Page::CheatEnemies: return cheatEnemies;
        case Page::CheatItems: return cheatItems;
        case Page::CheatWorld: return cheatWorld;
        case Page::Locomotion: return locomotion;
        case Page::Hud: return hud;
        case Page::Performance: return performance;
        case Page::RenderTests: return renderTests;
        case Page::Power: return power;
        default: return main;
      }
    }
    Row row() const { return rows()[size_t(selected)]; }
    const char* title() const {
      switch(page) {
        case Page::Controls:return "VR - BUTTON MAPPING";
        case Page::CalibrationHome:return "VR - WEAPON CALIBRATION";
        case Page::Calibration:return "CALIBRATION - MODEL / GRIP";
        case Page::Aim:return "CALIBRATION - AIM RAY";
        case Page::Support:return "CALIBRATION - SUPPORT HAND";
        case Page::Holstered:return "CALIBRATION - HOLSTER MODEL";
        case Page::ItemList:return "VR - GIVE ITEM";
        case Page::EnemyList:return "VR - SPAWN ENEMY";
        case Page::Holsters: return "GOTHIC II VR - HOLSTERS";
        case Page::HolsterSlot:return "HOLSTER - ASSIGNED ITEM";
        case Page::Cheats: return "GOTHIC II VR - CHEATS";
        case Page::CheatPlayer: return "CHEATS - PLAYER";
        case Page::CheatEnemies: return "CHEATS - ENEMIES";
        case Page::CheatItems: return "CHEATS - ITEMS";
        case Page::CheatWorld: return "CHEATS - WORLD";
        case Page::Locomotion: return "GOTHIC II VR - LOCOMOTION";
        case Page::Hud: return "GOTHIC II VR - HUD";
        case Page::Performance: return "GOTHIC II VR - PERFORMANCE";
        case Page::RenderTests: return "GOTHIC II VR - RENDER TESTS";
        case Page::Power: return "PERFORMANCE - POWER";
        default: return "GOTHIC II VR - SETTINGS";
      }
    }
    bool update(const Input& in,uint64_t now) {
      changed=false; recenter=false; action=-1; actionDirection=0;
      if(!in.focused) { gate=true; chordDown=true; gameChordDown=true; neutral=false; return true; }
      const bool chord=(in.leftClick && in.rightClick) || (in.leftGrip && in.rightGrip && in.menu);
      const bool gameChord=in.leftGrip && in.rightGrip && in.yButton;
      if(!chord) chordDown=false;
      if(!gameChord) gameChordDown=false;
      if(gate) {
        if(!in.menu && !in.a && !in.b && !in.yButton && !in.leftClick && !in.rightClick &&
           in.trigger<0.2f && in.secondaryTrigger<0.2f && std::abs(in.x)<0.25f && std::abs(in.y)<0.25f &&
           std::abs(in.rightX)<0.25f && std::abs(in.rightY)<0.25f) {
          gate=false; lastA=lastB=lastTrigger=false; axis=0; neutral=true;
        }
        return true;
      }
      if(chord && !chordDown) {
        chordDown=true; visible=!visible; page=Page::Main; selected=0; gate=true; neutral=false;
        return true;
      }
      if(gameChord && !gameChordDown) {
        gameChordDown=true; visible=false; action=OpenGameMenu; gate=true; neutral=false;
        return true;
      }
      const bool triggerEdit=visible && valueRow();
      const bool accept=(in.a&&!lastA) || (!triggerEdit && in.trigger>0.7f&&!lastTrigger);
      const bool back=in.b&&!lastB;
      lastA=in.a; lastB=in.b; lastTrigger=in.trigger>0.3f;
      if(!visible) return false;
      if(back) { goBack(); return true; }
      const bool triggerHeld=in.trigger>.3f || in.secondaryTrigger>.3f;
      const int triggerDirection=int(in.trigger>.55f)-int(in.secondaryTrigger>.55f);
      // Freeze selection while a trigger is held, including its release hysteresis.
      const int next=triggerHeld ? (triggerEdit?2*triggerDirection:0) :
        std::abs(in.y)>0.6f ? (in.y>0?1:-1) : 0;
      if(next==0) { axis=0; neutral=true; }
      if(neutral && next!=0 && (axis!=next || (now>=repeatAt && !(triggerHeld && toggleRow())))) {
        const bool first=axis!=next; axis=next; repeatAt=now+(first?400u:160u);
        if(std::abs(next)==1) selected=(selected+int(rows().size())+(next>0?1:-1))%int(rows().size());
        else adjust(next>0?1:-1);
      }
      if(accept && !gate) adjust(1,true);
      return true;
    }
  private:
    bool chordDown=false,gameChordDown=false,gate=true,neutral=false,lastA=false,lastB=false,lastTrigger=false;
    int axis=0; uint64_t repeatAt=0;
    void open(Page next) { page=next; selected=0; gate=true; }
    void goBack() {
      if(calibrationPage() && page!=Page::CalibrationHome){page=Page::CalibrationHome;selected=0;gate=true;return;}
      if(page==Page::Main) visible=false;
      else if(page==Page::HolsterSlot){page=Page::Holsters;selected=holsterPoint;}
      else if(page==Page::EnemyList) {page=Page::CheatEnemies;selected=0;}
      else if(page==Page::ItemList) {page=Page::CheatItems;selected=0;}
      else if(page==Page::RenderTests) { page=Page::Performance; selected=8; }
      else if(page==Page::Power) { page=Page::Performance; selected=13; }
      else if(page==Page::CheatPlayer || page==Page::CheatEnemies || page==Page::CheatItems || page==Page::CheatWorld) { selected=int(page)-int(Page::CheatPlayer);page=Page::Cheats; }
      else { selected=page==Page::Locomotion?0:page==Page::Hud?1:page==Page::Holsters?4:page==Page::Cheats?5:page==Page::Controls?6:page==Page::CalibrationHome?7:2; page=Page::Main; }
      gate=true;
    }
    void adjust(int direction,bool activated=false) {
      const Row selectedRow=row();
      if(!activated && (selectedRow==CalUseBowDefault || selectedRow==CalUseCrossbowDefault || selectedRow==OpenGameInterface || selectedRow==OpenCharacterStats || selectedRow==HolsterMove || selectedRow==HolsterClear || selectedRow==HolsterDrop || selectedRow==HolsterAtLeft || selectedRow==HolsterAtRight || selectedRow==EnemySpawn || selectedRow==ItemGive || selectedRow==Heal || selectedRow==TimeApply || selectedRow==WeatherApply)) return;
      switch(selectedRow) {
        case OpenGameMenu:action=OpenGameMenu;visible=false;gate=true;return;
        case OpenGameInterface:action=OpenGameInterface;visible=false;gate=true;return;
        case OpenCharacterStats:action=OpenCharacterStats;visible=false;gate=true;return;
        case HolsterSlot0:case HolsterSlot1:case HolsterSlot2:case HolsterSlot3:
          holsterPoint=int(selectedRow)-int(HolsterSlot0);holsterMoveTarget=(holsterPoint+1)%4;open(Page::HolsterSlot);return;
        case HolsterMoveTarget:holsterMoveTarget=(holsterMoveTarget+4+direction)%4;return;
        case HolsterMove:case HolsterClear:case HolsterDrop:action=int(selectedRow);actionDirection=direction;return;
        case IgnoreWeaponRequirements:settings.interaction.ignoreWeaponRequirements=!settings.interaction.ignoreWeaponRequirements;break;
        case Controls:open(Page::Controls);return;
        case ItemCalibrationMenu:open(Page::CalibrationHome);return;
        case CalModel:open(Page::Calibration);return;
        case CalAim:open(Page::Aim);return;
        case CalSupport:open(Page::Support);return;
        case CalHolstered:action=CalHolstered;actionDirection=direction;open(Page::Holstered);return;
        case CalStep:calibrationStep=(calibrationStep+3+direction)%3;return;
        case MapA:case MapB:case MapX:case MapY:case MapL3:case MapR3: {
          auto& v=settings.mapping[size_t(selectedRow-MapA)];v=(v+10+direction)%10;break;
        }
        case MapReset:settings.mapping={1,2,3,4,9,5};break;
        case CalHand:calibrationHand=1-calibrationHand;return;
        case CalBow:case CalBowString:case CalArrow:
        case CalScale:case CalStringHeight:case CalStringCenter:case CalStringSide:case CalStringDepth:
        case CalItem:case CalGrip:case CalFlip:case CalCopyHand:case CalUseDefault:case CalUseBowDefault:case CalUseCrossbowDefault:
        case CalAimX:case CalAimY:case CalAimZ:case CalAimPitch:case CalAimYaw:case CalAimRoll:case CalAimReset:
        case CalSupX:case CalSupY:case CalSupZ:case CalSupPitch:case CalSupYaw:case CalSupRoll:case CalSupReset:
        case CalHolX:case CalHolY:case CalHolZ:case CalHolPitch:case CalHolYaw:case CalHolRoll:case CalHolReset:
          action=int(selectedRow);actionDirection=direction;return;
        case CalX:case CalY:case CalZ:case CalPitch:case CalYaw:case CalRoll:case CalReset:
          action=int(selectedRow);actionDirection=direction;return;
        case EnemyAnimals:case EnemyGoblins:case EnemyOrcs:case EnemyUndead:case EnemyGolems:case EnemyMonsters:case EnemyPeople:case EnemyAll:
          action=int(selectedRow);actionDirection=direction;open(Page::EnemyList);return;
        case ItemSwords:case ItemTwoHanded:case ItemBows:case ItemCrossbows:case ItemPotions:case ItemAmmo:case ItemArmor:case ItemMagic:case ItemOther:case ItemAll:
          action=int(selectedRow);actionDirection=direction;open(Page::ItemList);return;
        case Holsters: open(Page::Holsters); return;
        case Cheats: open(Page::Cheats); return;
        case CheatPlayer: open(Page::CheatPlayer); return;
        case CheatEnemies: open(Page::CheatEnemies); return;
        case CheatItems: open(Page::CheatItems); return;
        case CheatWorld: open(Page::CheatWorld); return;
        case Hands: settings.interaction.showHands=!settings.interaction.showHands;break;
        case HolsterEnabled: settings.interaction.enabled=!settings.interaction.enabled;break;
        case HolsterModels: settings.interaction.showHolsters=!settings.interaction.showHolsters;break;
        case PickupHighlight: settings.interaction.pickupHighlight=!settings.interaction.pickupHighlight;break;
        case PickupHighlightRange:settings.interaction.pickupHighlightRange+=float(direction);break;
        case BowSight:settings.interaction.bowSight=!settings.interaction.bowSight;break;
        case GripLock: settings.interaction.gripLock=!settings.interaction.gripLock;break;
        case PhysicalCombat: settings.interaction.physicalCombat=!settings.interaction.physicalCombat;break;
        case HolsterPoint: holsterPoint=(holsterPoint+4+direction)%4;return;
        case HolsterX: settings.interaction.offsets[size_t(holsterPoint)].x+=float(direction)*.025f;break;
        case HolsterY: settings.interaction.offsets[size_t(holsterPoint)].y+=float(direction)*.025f;break;
        case HolsterZ: settings.interaction.offsets[size_t(holsterPoint)].z+=float(direction)*.025f;break;
        case HolsterRadius: settings.interaction.radius+=float(direction)*.025f;break;
        case PickupRadius: settings.interaction.pickupRadius+=float(direction)*.05f;break;
        case SwingSpeed: settings.interaction.swingSpeed+=float(direction)*.2f;break;
        case HolsterReset: {
          auto previous=std::move(settings.interaction);
          settings.interaction=HolsterSettings{};
          settings.interaction.calibration=std::move(previous.calibration);
          settings.interaction.meleeDefault=previous.meleeDefault;
          settings.interaction.pickupHighlight=previous.pickupHighlight;
          settings.interaction.pickupHighlightRange=previous.pickupHighlightRange;
          settings.interaction.bowSight=previous.bowSight;
          settings.interaction.ignoreWeaponRequirements=previous.ignoreWeaponRequirements;break;
        }
        case HolsterAtLeft: case HolsterAtRight: case HolsterItem: case GodMode: case Heal: case EnemyCategory: case EnemySelect: case EnemySpawn:
        case ItemCategory: case ItemSelect: case ItemQuantity: case ItemGive: case TimeHour: case TimeApply: case WeatherMode: case WeatherApply:
          action=int(selectedRow);actionDirection=direction;return;
        case Locomotion: open(Page::Locomotion); return;
        case Hud: open(Page::Hud); return;
        case Performance: open(Page::Performance); return;
        case RenderTests: open(Page::RenderTests); return;
        case Power: open(Page::Power); return;
        case Back: goBack(); return;
        case Turn: settings.turn=TurnMode((int(settings.turn)+3+direction)%3); break;
        case SnapAngle: settings.snapAngle+=direction*15; break;
        case SmoothSpeed: settings.smoothSpeed+=direction*15; break;
        case RunMode: settings.runHold=!settings.runHold; break;
        case RunSpeed: settings.runSpeed+=float(direction)*.05f; break;
        case WorldScale: settings.worldScale+=float(direction)*0.05f; recenter=true; break;
        case RoomScale: settings.roomScale=!settings.roomScale; recenter=true; break;
        case HeadMovement: settings.headMovement=!settings.headMovement; break;
        case HidePlayer: settings.hidePlayer=!settings.hidePlayer; break;
        case RenderScale: settings.renderScale+=float(direction)*0.05f; break;
        case HudDistance: settings.hudDistance+=float(direction)*0.1f; break;
        case HudX: settings.hudX+=float(direction)*0.025f; break;
        case HudY: settings.hudY+=float(direction)*0.025f; break;
        case HudReset: settings.hudX=settings.hudY=0; settings.hudDistance=2; break;
        case FogMode: settings.fogMode=1-settings.fogMode; break;
        case FrameOverlap: settings.frameOverlap=!settings.frameOverlap; break;
        case IndexedObjects: settings.indexedObjects=!settings.indexedObjects; break;
        case BatchedObjects: settings.batchedObjects=!settings.batchedObjects; break;
        case ObjectDistance: settings.objectDistance=(settings.objectDistance+4+direction)%4; break;
        case HorizonHaze: settings.horizonHaze=!settings.horizonHaze; break;
        case Horizon: settings.horizon=(settings.horizon+4+direction)%4; break;
        case CpuLevel: settings.cpuLevel=(settings.cpuLevel+5+direction)%5; break;
        case GpuLevel: settings.gpuLevel=(settings.gpuLevel+5+direction)%5; break;
        case TerrainLod: settings.terrainLod=(settings.terrainLod+4+direction)%4; break;
        case Foveation: settings.foveation=(settings.foveation+4+direction)%4; break;
        case StaticLighting: settings.staticLighting=!settings.staticLighting; break;
        case EarlyLeftEye: settings.earlyLeftEye=!settings.earlyLeftEye; break;
        case FastLighting: settings.fastLighting=!settings.fastLighting; break;
        case Shadows: settings.shadows=!settings.shadows; break;
        case Lighting: settings.lighting=!settings.lighting; break;
        case LocalLights: settings.localLights=!settings.localLights; break;
        case TiledLights: settings.tiledLights=!settings.tiledLights; break;
        case SubgroupTiles: settings.subgroupTiles=!settings.subgroupTiles; break;
        case UniformLights: settings.uniformLights=!settings.uniformLights; break;
        case StereoSeed: settings.stereoSeed=!settings.stereoSeed; break;
        case FramePipeline: settings.framePipeline=!settings.framePipeline; break;
        case CpuStaging: settings.cpuStaging=!settings.cpuStaging; break;
        case LightDepth: settings.lightDepth=!settings.lightDepth; break;
        case MergedTransparency: settings.mergedTransparency=!settings.mergedTransparency; break;
        case DirectOutput: settings.directOutput=!settings.directOutput; break;
        case SlabLights: settings.slabLights=!settings.slabLights; break;
        case GpuSampling: settings.gpuSampling=(settings.gpuSampling+3+direction)%3; break;
        case Profiler: {
          // Off -> Panel -> Compact -> CSV only -> Off (one row: menu indices unchanged).
          int state=settings.profiler?1+settings.profilerView:0;
          state=(state+4+direction)%4;
          settings.profiler=state!=0;
          if(state!=0) settings.profilerView=state-1;
          break;
        }
        case Recenter: recenter=true; return;
        case Close: visible=false; gate=true; return;
      }
      settings.sanitize(); changed=true;
    }
};
class Turning {
  public:
    float update(float x,bool enabled,const Settings& s,float seconds) {
      if(!enabled) { ready=false; return 0; }
      if(std::abs(x)<0.25f) ready=true;
      if(!ready || s.turn==TurnMode::Physical) return 0;
      if(s.turn==TurnMode::Smooth) {
        if(std::abs(x)<0.25f) return 0;
        return -std::copysign((std::min(1.f,std::abs(x))-0.25f)/0.75f,x)*float(s.smoothSpeed)*std::clamp(seconds,0.f,0.05f);
      }
      if(std::abs(x)>0.7f) { ready=false; return x>0?-float(s.snapAngle):float(s.snapAngle); }
      return 0;
    }
  private: bool ready=false;
};
}
