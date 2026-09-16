#include <optional>
#include "mainwindow.h"
#if defined(GOTHIC2VR_OPENXR)
#include "questxr.h"
#include "vr/vrhudrect.h"
#include "gothic.h"
#include "world/objects/npc.h"
#include "world/world.h"
#include "graphics/sceneglobals.h"
#include "utils/gthfont.h"
#include <Tempest/Application>
#include <Tempest/Painter>
#include <Tempest/Brush>
#include <Tempest/Log>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <string_view>
#include <sstream>
#include <fstream>
using namespace Tempest;

// Adreno clock of a sampled frame (profiler CSV gpu_clock_mhz, ):
// one pread of the kgsl node kept open (sysfs re-reads at offset 0); -1 = unreadable.
static double readGpuClockMhz() {
  static const int fd=::open("/sys/class/kgsl/kgsl-3d0/gpuclk",O_RDONLY|O_CLOEXEC);
  if(fd<0) return -1;
  char buf[32]={};
  const ssize_t n=::pread(fd,buf,sizeof(buf)-1,0);
  if(n<=0) return -1;
  return std::strtod(buf,nullptr)/1e6;
}

bool MainWindow::tickVrMenu(const GamepadState& pad,uint64_t now) {
  auto& game=Gothic::inst();
  const bool welcomeEligible=game.player() && game.checkLoading()==Gothic::LoadState::Idle &&
      !rootMenu.isActive() && !inventory.isActive() && !video.isActive() && !chapter.isActive() &&
      !document.isActive() && !console.isActive() && !vrMenu.visible;
  const bool moving=std::hypot(pad.leftStickX,pad.leftStickY)>.35f;
  // Stick deflection is never a dismissal. Triggers and grip buttons are deliberate presses.
  const uint32_t buttons=pad.buttons|(pad.leftTrigger>.7f?1u<<30:0u)|(pad.rightTrigger>.7f?1u<<31:0u);
  vrWelcome.shown=vrWelcome.shown || vrMenu.settings.welcomeSeen;
  const bool wasWelcome=vrWelcome.visible;
  if(vrWelcome.update(welcomeEligible,pad.connected && QuestXr::inst().focused(),moving,buttons)) {
    if(wasWelcome && !vrWelcome.visible) {
      vrMenu.settings.welcomeSeen=true;
      vrSaveFailed=!vrMenu.settings.save();
      if(vrSaveFailed)Log::e("VR welcome acknowledgement could not be saved");
    }
    vrGameplay.suspend();
    Vr::Input blocked;vrMenu.update(blocked,now);
    return true;
  }
  Vr::Input in;
  in.focused=QuestXr::inst().focused();
  in.leftGrip=(pad.buttons&GamepadState::L1)!=0; in.rightGrip=(pad.buttons&GamepadState::R1)!=0;
  in.menu=(pad.buttons&GamepadState::Start)!=0; in.a=(pad.buttons&GamepadState::A)!=0; in.b=(pad.buttons&GamepadState::B)!=0;
  in.x=pad.leftStickX; in.y=pad.leftStickY; in.trigger=pad.rightTrigger; in.secondaryTrigger=pad.leftTrigger;
  const bool before=vrMenu.visible;
  const bool consumed=vrMenu.update(in,now);
  if(vrMenu.action==Vr::Menu::OpenGameInterface) {
    vrGameplay.suspend();
    if(auto player=Gothic::inst().player(); player && !dialogs.isActive()) {
      rootMenu.closeAll();
      if(inventory.isWheelOpen())inventory.close();
      if(!inventory.isActive())inventory.open(*player);
    }
    clearInput();
  }
  else if(vrMenu.action==Vr::Menu::OpenCharacterStats) {
    vrGameplay.suspend();
    if(auto player=Gothic::inst().player(); player && !player->isDown() && !dialogs.isActive() &&
       Gothic::inst().checkLoading()==Gothic::LoadState::Idle) {
      if(inventory.isActive() || inventory.isWheelOpen())inventory.close();
      rootMenu.setMenu("MENU_STATUS",KeyCodec::Status);
      rootMenu.setPlayer(*player);
    }
    clearInput();
  }
  else if(vrMenu.action>=0) vrGameplay.queue(vrMenu.action,vrMenu.actionDirection,vrMenu.action==Vr::Menu::CalHolstered?vrMenu.holsterPoint:(vrMenu.calibrationPage()?vrMenu.calibrationHand:vrMenu.holsterPoint));
  if(vrMenu.changed) {
    vrSaveFailed=!vrMenu.settings.save();
    if(vrSaveFailed) Log::e("VR settings save failed; previous VR.ini preserved");
    else Log::i("VR settings saved: turn=",int(vrMenu.settings.turn)," scale=",vrMenu.settings.worldScale," render=",vrMenu.settings.renderScale);
  }
  if(vrMenu.recenter) {
    if(auto camera=Gothic::inst().camera()) camera->onRotateMouse(PointF(0,QuestXr::inst().headYawDegrees()));
    QuestXr::inst().recenter();
  }
  if(before!=vrMenu.visible) { Log::i("VR settings menu ",vrMenu.visible?"opened":"closed"); update(); }
  return consumed;
}

void MainWindow::moveRoomScale(Camera& camera,bool allowed) {
  auto& xr=QuestXr::inst();
  auto pl=Gothic::inst().player();
  if(!pl) { xr.roomDelta(false); return; }
  const auto position=pl->position();
  if(vrPlayer!=pl || vrCamera!=&camera || (position-vrLastPlayerPosition).length()>300.f) {
    xr.roomDelta(false);
    vrPlayer=pl; vrCamera=&camera;
    const float bone=pl->cameraBone(true).y-position.y;
    vrEyeHeight=std::isfinite(bone)&&bone>100.f&&bone<220.f?bone:170.f;
  }
  auto delta=xr.roomDelta(allowed && vrMenu.settings.roomScale);
  if(delta.length()>0.0001f) {
    const auto basis=camera.vrBaseView(Vec3(),camera.spin().y);
    auto inverse=basis; inverse.inverse();
    const float units=100.f/vrMenu.settings.worldScale;
    Vec3 step(delta.x*units,0,-delta.z*units); inverse.project(step);
    const auto start=pl->position();
    // Native movement sweeps the same collision capsule used by the game.
    // Consume only accepted displacement; blocked movement remains pending
    // until the physical head retreats, so contact cannot re-anchor through a wall.
    pl->tryMove(step);
    auto accepted=pl->position()-start; accepted.y=0;
    basis.project(accepted);
    xr.consumeRoom(Vec3(accepted.x/units,0,-accepted.z/units));
  }
  vrLastPlayerPosition=pl->position();
}

void MainWindow::paintVrOverlay(PaintEvent& event,bool gameplay) {
  Painter p(event);
  const bool panel=vrMenu.visible || vrWelcome.visible;
  const float density=std::max(1.f,float(h())/(panel?1000.f:650.f));
  const auto& font=Resources::font(density);
  const int line=std::max(24,font.pixelSize()+10);
  const int width=panel?w()*6/10:w()*8/10, x=(w()-width)/2;
  auto text=[&](int y,const std::string& s) {
    p.setBrush(Color(1,1,1,1));
    auto rowFont=font;
    const int textWidth=font.textSize(s).w;
    if(panel && textWidth>width-36)
      rowFont.setScale(density*float(width-36)/float(textWidth));
    rowFont.drawText(p,x+18,y,s);
  };
  if(vrWelcome.visible) {
    const int top=std::max(0,(h()-line*12)/2);
    p.setBrush(Color(.025f,.035f,.055f,.97f));p.drawRect(x,top,width,line*12);
    int y=top+line;
    for(const char* row:{"Welcome to Gothic II VR 0.1.1 Alpha", "",
        "This is a very early version of the VR mod.",
        "The game cannot yet be completed in VR.",
        "Many features are unfinished or do not work yet.",
        "Expect bugs. Save often and keep backup saves.", "",
        "Feedback and bug reports: Discord",
        "https://discord.com/channels/747967102895390741/1543691482861408276", "",
        "Press any button to continue. Moving the stick will not close this."}) {
      text(y,row);y+=line;
    }
    return;
  }
  if(gameplay && !vrMenu.visible)if(auto world=Gothic::inst().world()) {
    if(auto target=vrGameplay.healthTarget(*world,player.focus(),Application::tickCount())) {
      const auto maximum=target->attribute(ATR_HITPOINTSMAX);
      const float hp=maximum>0?std::clamp(float(target->attribute(ATR_HITPOINTS))/float(maximum),0.f,1.f):0.f;
      const int top=std::max(10,h()/12);
      drawBar(p,barHp,w()/2,top,hp,AlignHCenter|AlignTop);
      const auto size=font.textSize(target->displayName());
      p.setBrush(Color(1,1,1,1));
      font.drawText(p,(w()-size.w)/2,top+playerBarSize().h+line,target->displayName());
    }
  }
  const float blocked=QuestXr::inst().roomBlocked();
  if(blocked>4.f) {
    p.setBrush(Color(0,0,0,std::clamp((blocked-4.f)/15.f,0.f,0.9f))); p.drawRect(0,0,w(),h());
    text(h()/2,"Wall contact - step back");
  }
  int y=line;
  if(vrMenu.visible) {
    const int panelHeight=line*10;
    const int top=std::max(0,int(float(h())*.61f)-panelHeight/2);
    p.setBrush(Color(0.025f,0.035f,0.055f,vrMenu.calibrationPage() || vrMenu.page==Vr::Menu::Page::Holsters?.50f:.94f)); p.drawRect(x,top,width,panelHeight);
    y=top+line; text(y,vrMenu.title()); y+=line;
    char value[160];
    const auto& s=vrMenu.settings;
    const int visibleRows=6;
    const size_t first=size_t(std::max(0,vrMenu.selected-visibleRows+1));
    for(size_t index=first;index<vrMenu.rows().size() && index<first+size_t(visibleRows);++index) {
      const auto row=vrMenu.rows()[index];
      switch(row) {
        case Vr::Menu::Locomotion: std::snprintf(value,sizeof(value),"Locomotion  >"); break;
        case Vr::Menu::Hud: std::snprintf(value,sizeof(value),"HUD  >"); break;
        case Vr::Menu::Performance: std::snprintf(value,sizeof(value),"Performance  >"); break;
        case Vr::Menu::RenderTests: std::snprintf(value,sizeof(value),"Render tests  >"); break;
        case Vr::Menu::Shadows: std::snprintf(value,sizeof(value),"Shadows: < %s >",s.shadows?"On":"Off"); break;
        case Vr::Menu::Lighting: std::snprintf(value,sizeof(value),"Surface lighting: < %s >",s.lighting?"Normal":"Unlit"); break;
        case Vr::Menu::FrameOverlap: std::snprintf(value,sizeof(value),"CPU/GPU overlap: < %s >",s.frameOverlap?"On":"Off (comparison)"); break;
        case Vr::Menu::TiledLights: std::snprintf(value,sizeof(value),"Screen light tiles: < %s >",s.tiledLights?"On":"Off"); break;
        case Vr::Menu::SubgroupTiles: std::snprintf(value,sizeof(value),"Subgroup light tiles: < %s >",s.subgroupTiles?"On":"Off (comparison)"); break;
        case Vr::Menu::UniformLights: std::snprintf(value,sizeof(value),"Fast light access: < %s >",s.uniformLights?"On":"Off (comparison)"); break;
        case Vr::Menu::FramePipeline: std::snprintf(value,sizeof(value),"Frame pipeline: < %s >",s.framePipeline?"On":"Off (comparison)"); break;
        case Vr::Menu::StereoSeed: std::snprintf(value,sizeof(value),"Shared depth seed: < %s >",s.stereoSeed?"On":"Off (comparison)"); break;
        case Vr::Menu::LocalLights: std::snprintf(value,sizeof(value),"Local lights: < %s >",s.localLights?"On":"Off"); break;
        case Vr::Menu::Back: std::snprintf(value,sizeof(value),"< Back"); break;
        case Vr::Menu::HudX: std::snprintf(value,sizeof(value),"HUD horizontal: < %+.1f%% >",double(s.hudX*100)); break;
        case Vr::Menu::HudY: std::snprintf(value,sizeof(value),"HUD vertical: < %+.1f%% >",double(s.hudY*100)); break;
        case Vr::Menu::HudReset: std::snprintf(value,sizeof(value),"Reset HUD position / distance"); break;
        case Vr::Menu::FogMode: std::snprintf(value,sizeof(value),"Fog: < %s >",s.fogMode==0?"Mobile atmosphere":"Volumetric rays"); break;
        case Vr::Menu::IndexedObjects: std::snprintf(value,sizeof(value),"Geometry optimization: < %s >",s.indexedObjects?"On":"Off (comparison)"); break;
        case Vr::Menu::BatchedObjects: std::snprintf(value,sizeof(value),"Object batching: < %s >",s.batchedObjects?"On":"Off (comparison)"); break;
        case Vr::Menu::ObjectDistance: std::snprintf(value,sizeof(value),"Object distance: < %s >",s.objectDistance==0?"Unlimited":s.objectDistance==1?"300 m":s.objectDistance==2?"200 m":"120 m (original)"); break;
        case Vr::Menu::HorizonHaze: std::snprintf(value,sizeof(value),"Horizon haze: < %s >",s.horizonHaze?"On":"Off (comparison)"); break;
        case Vr::Menu::Horizon: std::snprintf(value,sizeof(value),"Horizon: < %s >",s.horizon==0?"300 m":s.horizon==1?"450 m":s.horizon==2?"600 m":"1 km"); break;
        case Vr::Menu::Power: std::snprintf(value,sizeof(value),"Power  >"); break;
        case Vr::Menu::CpuLevel: std::snprintf(value,sizeof(value),"CPU level: < %s >",Vr::Menu::performanceLevelName(s.cpuLevel)); break;
        case Vr::Menu::GpuLevel: std::snprintf(value,sizeof(value),"GPU level: < %s >",Vr::Menu::performanceLevelName(s.gpuLevel)); break;
        case Vr::Menu::TerrainLod: std::snprintf(value,sizeof(value),"Terrain detail: < %s >",s.terrainLod==0?"Full":s.terrainLod==1?"Far (200 m)":s.terrainLod==2?"Near (120 m)":"Aggressive (80 m)"); break;
        case Vr::Menu::Foveation: std::snprintf(value,sizeof(value),"Foveation: < %s >",s.foveation==0?"Off":s.foveation==1?"Low":s.foveation==2?"Medium":"High"); break;
        case Vr::Menu::StaticLighting: std::snprintf(value,sizeof(value),"Lighting: < %s >",s.staticLighting?"2002 static":"Dynamic (experimental)"); break;
        case Vr::Menu::EarlyLeftEye: std::snprintf(value,sizeof(value),"Early left eye: < %s >",s.earlyLeftEye?"On":"Off (comparison)"); break;
        case Vr::Menu::CpuStaging: std::snprintf(value,sizeof(value),"CPU staging: < %s >",s.cpuStaging?"On":"Off (comparison)"); break;
        case Vr::Menu::LightDepth: std::snprintf(value,sizeof(value),"Light depth: < %s >",s.lightDepth?"On":"Off (comparison)"); break;
        case Vr::Menu::MergedTransparency: std::snprintf(value,sizeof(value),"Merge transparent: < %s >",s.mergedTransparency?"On":"Off (comparison)"); break;
        case Vr::Menu::DirectOutput: std::snprintf(value,sizeof(value),"Direct eye output: < %s >",s.directOutput?"On":"Off (comparison)"); break;
        case Vr::Menu::SlabLights: std::snprintf(value,sizeof(value),"Light slabs: < %s >",s.slabLights?"On":"Off (comparison)"); break;
        case Vr::Menu::FastLighting: std::snprintf(value,sizeof(value),"Lighting optimization: < %s >",s.fastLighting?"On":"Off (comparison)"); break;
        case Vr::Menu::GpuSampling: std::snprintf(value,sizeof(value),"GPU detail: < %s >",s.gpuSampling==0?"Off":s.gpuSampling==1?"Sampled":"Every frame"); break;
        case Vr::Menu::Turn: std::snprintf(value,sizeof(value),"Turning: < %s >",s.turn==Vr::TurnMode::Snap?"Snap":s.turn==Vr::TurnMode::Smooth?"Smooth":"Physical only"); break;
        case Vr::Menu::SnapAngle: std::snprintf(value,sizeof(value),"Snap angle: < %d degrees >",s.snapAngle); break;
        case Vr::Menu::SmoothSpeed: std::snprintf(value,sizeof(value),"Smooth speed: < %d deg/s >",s.smoothSpeed); break;
        case Vr::Menu::RunSpeed: std::snprintf(value,sizeof(value),"Running speed: < %.2fx >",double(s.runSpeed)); break;
        case Vr::Menu::WorldScale: std::snprintf(value,sizeof(value),"World size: < %.2fx >",double(s.worldScale)); break;
        case Vr::Menu::RoomScale: std::snprintf(value,sizeof(value),"Room scale + collision: < %s >",s.roomScale?"On":"Off"); break;
        case Vr::Menu::HeadMovement: std::snprintf(value,sizeof(value),"Movement direction: < %s >",s.headMovement?"Head":"Body forward"); break;
        case Vr::Menu::HidePlayer: std::snprintf(value,sizeof(value),"Hide player model: < %s >",s.hidePlayer?"On":"Off"); break;
        case Vr::Menu::RenderScale: std::snprintf(value,sizeof(value),"World render resolution: < %.0f%% >",double(s.renderScale*100)); break;
        case Vr::Menu::HudDistance: std::snprintf(value,sizeof(value),"HUD distance: < %.1f m >",double(s.hudDistance)); break;
        case Vr::Menu::Profiler: std::snprintf(value,sizeof(value),"Live profiler + CSV: < %s >",!s.profiler?"Off":s.profilerView==1?"Compact":s.profilerView==2?"CSV only":"Panel"); break;
        case Vr::Menu::Recenter: std::snprintf(value,sizeof(value),"Recenter tracking"); break;
        default: {auto label=vrGameplay.label(row,vrMenu);std::snprintf(value,sizeof(value),"%s",label.empty()?"Close VR menu":label.c_str());break;}
      }
      if(int(index)==vrMenu.selected) { p.setBrush(Color(0.12f,0.24f,0.4f,1)); p.drawRect(x+8,y-line+7,width-16,line); }
      text(y,value); y+=line;
    }
    text(y,vrSaveFailed?"Save failed":"Stick: scroll | LT/RT: -/+ | A: select | B: back"); y+=line;
    if(Application::tickCount()<vrGameplay.noticeUntil)text(y,vrGameplay.notice);
  }
  if(!vrMenu.visible && Application::tickCount()<vrGameplay.noticeUntil) {text(h()*2/3,vrGameplay.notice);}
  if(vrMenu.settings.profiler && vrMenu.settings.profilerView!=2 && !vrMenu.visible && !QuestXr::inst().cinema()) {
    const auto& a=vrProfiler.average;
    char s[200];
    if(vrMenu.settings.profilerView==1) {
      // Compact: one line at the bottom, beside the status bars, so the HUD
      // layer and its clear/copy stay a bottom strip; the
      // details go to the CSV.
      if(a.frame<=0) std::snprintf(s,sizeof(s),"VR profiler: warming up...");
      else std::snprintf(s,sizeof(s),"%.1f FPS | %.1f ms p95 %.1f | GPU L %.2f R %.2f",1000/a.frame,a.frame,vrProfiler.p95,
                         a.gpuValid[0]?a.gpu[0]:-1.0,a.gpuValid[1]?a.gpu[1]:-1.0);
      const int textW=font.textSize(s).w, left=std::max(0,(w()-textW)/2), base=h()-line*2;
      p.setBrush(Color(0.02f,0.03f,0.05f,0.8f)); p.drawRect(left-10,base-line+7,textW+20,line);
      p.setBrush(Color(1,1,1,1)); font.drawText(p,left,base,s);
      return;
    }
    if(!vrMenu.visible) { const int top=h()/4; y=top+line; p.setBrush(Color(0.02f,0.03f,0.05f,0.8f)); p.drawRect(x,top,width,line*10+8); }
    if(a.frame<=0) { text(y,"VR profiler: warming up..."); return; }
    std::snprintf(s,sizeof(s),"App %.1f FPS / %.0f Hz | Frame %.1f ms | p95 %.1f",1000/a.frame,double(vrProfiler.refresh),a.frame,vrProfiler.p95); text(y,s); y+=line;
    std::snprintf(s,sizeof(s),"Simulation %.2f | UI %.2f | Wait XR %.2f | End XR %.2f",a.simulation,a.ui,a.wait,a.end); text(y,s); y+=line;
    for(int i=0;i<2;++i) {
      if(a.gpuValid[i]) std::snprintf(s,sizeof(s),"%s eye: CPU %.2f | GPU %.2f | Wait %.2f | Output CPU %.2f",i==0?"Left":"Right",a.record[i],a.gpu[i],a.gpuWait[i],a.copy[i]);
      else std::snprintf(s,sizeof(s),"%s eye: CPU %.2f | GPU n/a | Wait %.2f | Output CPU %.2f",i==0?"Left":"Right",a.record[i],a.gpuWait[i],a.copy[i]);
      text(y,s); y+=line;
    }
    text(y,a.gpuValid[0]?"Heavy L: "+vrProfiler.worstPass[0]:"GPU detail: waiting for sample / disabled"); y+=line;
    text(y,a.gpuValid[1]?"Heavy R: "+vrProfiler.worstPass[1]:"GPU passes are logged on sampled frames"); y+=line;
    std::snprintf(s,sizeof(s),"Meshlets (geometry pieces) L %llu/%llu | R %llu/%llu",
                  (unsigned long long)vrProfiler.cullVisible[0],(unsigned long long)vrProfiler.cullFrustum[0],
                  (unsigned long long)vrProfiler.cullVisible[1],(unsigned long long)vrProfiler.cullFrustum[1]); text(y,s); y+=line;
    std::snprintf(s,sizeof(s),"Terrain pieces L/R %llu/%llu | models %llu/%llu | Fog steps %.1f/%.1f",
                  (unsigned long long)vrProfiler.cullTerrain[0],(unsigned long long)vrProfiler.cullTerrain[1],
                  (unsigned long long)vrProfiler.cullModels[0],(unsigned long long)vrProfiler.cullModels[1],
                  double(vrProfiler.fogSteps[0]),double(vrProfiler.fogSteps[1])); text(y,s); y+=line;
    std::snprintf(s,sizeof(s),"Pipeline %s | Previous GPU wait %.2f ms | Output %s/%s",vrProfiler.current.pipelineActive?"Active":"Off",vrProfiler.average.priorGpuWait,vrProfiler.current.directOutputActive[0]?"Direct":"Copy",vrProfiler.current.directOutputActive[1]?"Direct":"Copy"); text(y,s); y+=line;
    const auto* worldView=Gothic::inst().worldView();
    const bool sharedSeed=worldView && worldView->drawCommands().stereoSeedReused();
    const char* tileMode=renderer.vrUsesSlabLights() ? (renderer.vrUsesSubgroupTiles()?"Slab SG":"Slab") : (renderer.vrUsesSubgroupTiles()?"SG":(renderer.vrUsesTiledLights()?"Std":"Off"));
  std::snprintf(s,sizeof(s),"Budget %.2f | R %.0f%% | Tiles %s | Lights %s | Seed %s",vrProfiler.refresh>0?1000.0/double(vrProfiler.refresh):0.0,double(vrMenu.settings.renderScale*100),tileMode,renderer.vrUsesUniformLights()?"UBO":"SSBO",sharedSeed?"Shared":"Own"); text(y,s);
  }
}

void MainWindow::renderVr() {
  auto& xr=QuestXr::inst();
  vrProfiler.begin(vrMenu.settings.profiler,vrMenu.settings.gpuSampling,vrMenu.settings.fogMode,vrMenu.settings.renderScale,vrMenu.visible,vrMenu.settings.fastLighting,vrMenu.settings.indexedObjects,vrMenu.settings.shadows,vrMenu.settings.lighting,vrMenu.settings.localLights,vrMenu.settings.frameOverlap,vrMenu.settings.batchedObjects);
  vrProfiler.current.objectDistance=vrMenu.settings.objectDistance;
  vrProfiler.current.horizonHaze=vrMenu.settings.horizonHaze;
  vrProfiler.current.horizon=vrMenu.settings.horizon;
  vrProfiler.current.terrainLod=vrMenu.settings.terrainLod;
  vrProfiler.current.foveation=vrMenu.settings.foveation;
  vrProfiler.current.earlyLeftEye=vrMenu.settings.earlyLeftEye;
  vrProfiler.current.tiledLights=vrMenu.settings.tiledLights;
  vrProfiler.current.subgroupTiles=vrMenu.settings.subgroupTiles;
  vrProfiler.current.uniformLights=vrMenu.settings.uniformLights;
  vrProfiler.current.stereoSeed=vrMenu.settings.stereoSeed;
  vrProfiler.current.framePipeline=vrMenu.settings.framePipeline;
  vrProfiler.current.cpuStaging=vrMenu.settings.cpuStaging;
  vrProfiler.current.lightDepth=vrMenu.settings.lightDepth;
  vrProfiler.current.mergedTransparency=vrMenu.settings.mergedTransparency;
  vrProfiler.current.directOutput=vrMenu.settings.directOutput;
  vrProfiler.current.slabLights=vrMenu.settings.slabLights;
  //  (Gothic.ini [ENGINE]): vrHudRectCopyOff=1 restores the full
  // HUD clear and copy; vrProfilerExtrasOff=1 drops the HUD/copy GPU timestamps
  // and the GPU clock column.
  static const bool hudRectCopy=Gothic::settingsGetI("ENGINE","vrHudRectCopyOff")==0;
  static const bool profilerExtras=Gothic::settingsGetI("ENGINE","vrProfilerExtrasOff")==0;
  static bool r3Logged=false;
  if(!r3Logged) {
    Log::i("VR HUD rect clear/copy: ",hudRectCopy?"active":"off"," | VR profiler extras (HUD/copy GPU ms, GPU clock): ",profilerExtras?"active":"off");
    r3Logged=true;
  }
  // Copy timestamps only while the profiler runs: the cached copy commands
  // otherwise carry two timestamps and a query read every frame for nobody.
  xr.setHudRectCopy(hudRectCopy); xr.setCopyTimestamps(profilerExtras && vrMenu.settings.profiler);
  vrProfiler.current.profilerView=vrMenu.settings.profilerView;
  if(vrProfiler.sampleGpu && profilerExtras) vrProfiler.current.gpuClockMhz=readGpuClockMhz();
  struct FrameScope {
    QuestXr& xr; Vr::Profiler& profile; Tempest::Fence* renderFences; Tempest::Fence& hudFence; Tempest::CommandBuffer* commands; Tempest::Fence* prepFences; Tempest::CommandBuffer* prepCommands; const bool* prepRecorded; Tempest::CommandBuffer* hudCommands; bool world=false,complete=false,overlay=false; float distance=2;
    ~FrameScope() {
      if(!complete) {
        // A render may have been submitted before XR acquisition/copy failed.
        // Drain its fence as well as XR's pending copy fences during cleanup.
        auto drain=[](Tempest::Fence& f) {
          try { f.wait(); } catch(...) { Tempest::Log::e("VR fence wait failed during frame cleanup"); }
        };
        for(uint8_t i=0;i<Resources::MaxFramesInFlight;++i) { drain(renderFences[i]); drain(prepFences[i]); }
        drain(hudFence);
      }
      const double start=Vr::milliseconds(); xr.endFrame(world,complete,overlay,distance);
      profile.current.end=Vr::milliseconds()-start;
      {
        // log per-phase timings of frames longer than 250 ms
        static double lastFrameEnd=0;
        const double now=Vr::milliseconds(),gap=lastFrameEnd>0?now-lastFrameEnd:0;
        lastFrameEnd=complete && xr.focused() ? now : 0;
        const auto& c=profile.current;
        if(gap>250) Tempest::Log::e("VR stall frame=",int(gap)," ms wait=",int(c.wait)," priorGpuWait=",int(c.priorGpuWait),
          " tick=",int(c.tick)," simulation=",int(c.simulation)," cpuStage=",int(c.cpuStage)," recordL=",int(c.record[0])," recordR=",int(c.record[1]),
          " ui=",int(c.ui)," end=",int(c.end)," complete=",int(complete)," focused=",int(xr.focused()));
      }
      if(complete) profile.finish();
      else {
        try { if(profile.pendingHud()) profile.collectPendingHud(hudCommands->gpuTimings(),xr.copyGpuMs(2)); profile.collectPendingGpu([&](uint8_t slot) { auto m=commands[slot].gpuTimings(); if(!prepRecorded[slot]) return m; auto t=prepCommands[slot].gpuTimings(); t.insert(t.end(),m.begin(),m.end()); return t; }); }
        catch(...) { profile.finalizePending(); }
      }
    }
  } frame{xr,vrProfiler,fence,vrHudFence,commands,prepFence,prepCommands,prepRecorded,&vrHudCommand};
  const double waitStart=Vr::milliseconds();
  const bool render=xr.beginFrame();
  vrProfiler.current.wait=Vr::milliseconds()-waitStart; vrProfiler.refresh=xr.refreshRate();
  if(!render) {
    vrProfiler.suspend();
    if(xr.shouldExit()) { SystemApi::exit(); return; }
    vrGameplay.suspend();clearInput(); Application::sleep(10); return;
  }
  static bool once=true;
  if(once) { Gothic::inst().emitGlobalSoundWav("GAMESTART.WAV"); once=false; }
  auto& gothic=Gothic::inst();
  bool previousDrained=false;
  // Local-light GPU cost of one eye (the lighting pass, the compaction and the
  // slab builder) from a completed command buffer's timestamps.
  auto feedLightRoute=[&](uint32_t eye,uint64_t frameId,bool slabRoute,const auto& timings) {
    double ms=0; bool any=false;
    for(const auto& t:timings) {
      const std::string_view name=t.name;
      if(name=="Lighting+Sky" || name=="Light visibility" || name=="Light slabs" || name=="Light tiles" || name=="Light tiles subgroup") { ms+=t.milliseconds; any=true; }
    }
    if(any) vrLightRoute.report(uint8_t(eye),frameId,slabRoute?LightRouteController::Slabs:LightRouteController::Volumes,float(ms));
  };
  auto drainPrevious=[&] {
    if(previousDrained) return;
    const double start=Vr::milliseconds();
    xr.drainCopies();
    for(auto& submitted:fence) submitted.wait();
    for(auto& submitted:prepFence) submitted.wait();
    vrHudFence.wait();
    vrProfiler.current.priorGpuWait+=Vr::milliseconds()-start;
    if(vrLightRoutePending.valid) {
      // The previous pipelined frame's fences are complete: read its timestamps
      // before startEncoding resets the query pools.
      for(uint32_t eye=0;eye<2;++eye)
        if(!vrLightRoutePending.fed[eye])
          feedLightRoute(eye,vrLightRoutePending.frameId,vrLightRoutePending.route[eye],commands[vrLightRoutePending.slots[eye]].gpuTimings());
      vrLightRoutePending.valid=false;
    }
    // The previous sampled frame's HUD and HUD copy are complete: read them
    // before the HUD command buffer is re-encoded.
    if(vrProfiler.pendingHud()) vrProfiler.collectPendingHud(vrHudCommand.gpuTimings(),xr.copyGpuMs(2));
    vrProfiler.collectPendingGpu([&](uint8_t slot) { auto m=commands[slot].gpuTimings(); if(!prepRecorded[slot]) return m; auto t=prepCommands[slot].gpuTimings(); t.insert(t.end(),m.begin(),m.end()); return t; });
    previousDrained=true;
  };
  // Early left eye, first half of the drain: only the previous left eye's
  // pinned slot 0 and its XR image. Its timestamps are read here, before the
  // next left eye re-encodes the slot.
  bool leftDrained=false;
  auto drainLeft=[&] {
    if(previousDrained || leftDrained) return;
    const double start=Vr::milliseconds();
    xr.drainCopy(0);
    fence[0].wait();
    prepFence[0].wait();
    vrProfiler.current.priorGpuWait+=Vr::milliseconds()-start;
    if(vrLightRoutePending.valid && !vrLightRoutePending.fed[0]) {
      feedLightRoute(0,vrLightRoutePending.frameId,vrLightRoutePending.route[0],commands[vrLightRoutePending.slots[0]].gpuTimings());
      vrLightRoutePending.fed[0]=true;
    }
    vrProfiler.collectPendingEye(0,[&](uint8_t slot) { auto m=commands[slot].gpuTimings(); if(!prepRecorded[slot]) return m; auto t=prepCommands[slot].gpuTimings(); t.insert(t.end(),m.begin(),m.end()); return t; });
    leftDrained=true;
  };
  // Second half, after the next left eye is submitted: the previous right eye
  // (slot 1), the HUD and their XR copies. Never waits for the new left eye.
  auto drainRight=[&] {
    if(previousDrained) return;
    const double start=Vr::milliseconds();
    xr.drainCopy(1);
    xr.drainCopy(2);
    fence[1].wait();
    vrHudFence.wait();
    const double waited=Vr::milliseconds()-start;
    vrProfiler.current.priorGpuWait+=waited;
    vrProfiler.current.rightDrain+=waited;
    if(vrLightRoutePending.valid) {
      if(!vrLightRoutePending.fed[1])
        feedLightRoute(1,vrLightRoutePending.frameId,vrLightRoutePending.route[1],commands[vrLightRoutePending.slots[1]].gpuTimings());
      vrLightRoutePending.valid=false;
    }
    if(vrProfiler.pendingHud()) vrProfiler.collectPendingHud(vrHudCommand.gpuTimings(),xr.copyGpuMs(2));
    vrProfiler.collectPendingGpu([&](uint8_t slot) { auto m=commands[slot].gpuTimings(); if(!prepRecorded[slot]) return m; auto t=prepCommands[slot].gpuTimings(); t.insert(t.end(),m.begin(),m.end()); return t; });
    previousDrained=true;
  };
  // Loading completion destroys preview images; video EOF destroys its sampled
  // frame texture. Those transitions must finish prior drawing before tick().
  if(gothic.checkLoading()!=Gothic::LoadState::Idle || video.isActive()) drainPrevious();
  auto camera=gothic.camera();
  xr.setWorldScale(vrMenu.settings.worldScale);
  {
    // Horizon (Performance -> Horizon): the VR far plane. The fog volume
    // spans 95 % of it and the horizon haze fades geometry into the sky over
    // its last 55-95 %, so nothing beyond the plane is ever visible (the
    // original engine's far clip plus fog). Terrain and objects beyond it are
    // frustum-culled; both the OpenXR eye projections and the camera far
    // plane follow the setting.
    static const float horizonCm[4]={30000.f,45000.f,60000.f,100000.f};
    const float farCm=horizonCm[std::clamp(vrMenu.settings.horizon,0,3)];
    xr.setFarPlane(farCm); Camera::setVrFarPlane(farCm);
    // OpenXR CPU/GPU performance level (Performance -> Power); re-applied only when it changes.
    xr.setPerformanceLevels(vrMenu.settings.cpuLevel,vrMenu.settings.gpuLevel);
  }
  if(camera) {
    camera->clearVrView(); camera->setFirstPerson(true);
    if(!camera->isCutscene() && !dialogs.isActive()) camera->setSpin(PointF(0,camera->spin().y));
    if(auto pl=gothic.player(); pl && !camera->isCutscene())
      camera->setVrView(xr.headView(camera->vrBaseView(pl->position()+Vec3(0,vrEyeHeight-vrCrouchOffset,0),camera->spin().y)),xr.headProjection());
  }
  if(auto world=gothic.world();world && gothic.checkLoading()==Gothic::LoadState::Idle) {
    const bool gaze=camera && xr.focused() && !vrMenu.visible && !camera->isCutscene() && !rootMenu.isActive() && !dialogs.isActive();
    if(gaze){auto head=camera->view();head.inverse();world->setVrGaze(true,Vr::origin(head),Vr::axis(head,2));}
    else world->setVrGaze(false);
  }
  const double simulationStart=Vr::milliseconds();
  uint64_t dt=0;
  if(xr.focused() && !vrMenu.visible && !vrWelcome.visible) dt=tick();
  else { clearInput(); lastTick=Application::tickCount(); }
  vrProfiler.current.tick=Vr::milliseconds()-simulationStart;
  camera=gothic.camera();
  auto pl=gothic.player();
  const bool world=camera && gothic.worldView() && gothic.isInGame() && gothic.checkLoading()==Gothic::LoadState::Idle &&
      !camera->isCutscene() && !rootMenu.isActive() && !dialogs.isActive() && !inventory.isActive() &&
      !video.isActive() && !chapter.isActive() && !document.isActive() && !console.isActive();
  double simPhase=Vr::milliseconds();
  if(camera) moveRoomScale(*camera,world && xr.focused() && !vrMenu.visible && !vrWelcome.visible && pl && !pl->isDown() && !pl->isSwim() && !pl->isDive() && pl->interactive()==nullptr);
  vrProfiler.current.room=Vr::milliseconds()-simPhase;
  simPhase=Vr::milliseconds(); updateAnimation(dt);
  vrProfiler.current.animation=Vr::milliseconds()-simPhase;
  simPhase=Vr::milliseconds(); tickCamera(dt);
  vrProfiler.current.camera=Vr::milliseconds()-simPhase;
  vrProfiler.current.simulation=Vr::milliseconds()-simulationStart;
  if(pl) pl->setRenderVisible(!world || !vrMenu.settings.hidePlayer);
  Matrix4x4 base=Matrix4x4::mkIdentity();
  if(world && pl) {
    const float crouch=(pl->walkMode()&WalkBit::WM_Sneak)!=WalkBit::WM_Run?50.f:0.f;
    vrCrouchOffset+=(crouch-vrCrouchOffset)*std::min(1.f,float(dt)/100.f);
    base=camera->vrBaseView(pl->position()+Vec3(0,vrEyeHeight-vrCrouchOffset,0),camera->spin().y);
    camera->setVrView(xr.headView(base),xr.headProjection());
  } else if(camera) camera->clearVrView();
  frame.world=world; vrProfiler.current.world=world;
  xr.setCinema(!world);
  frame.distance=vrMenu.settings.hudDistance;
  // CPU vectors only: all animation writers have joined, and the previous GPU
  // frame reads its own uploaded buffers. Mapped uploads remain after drain.
  if(world && !previousDrained && vrMenu.settings.cpuStaging && vrMenu.settings.frameOverlap && vrMenu.settings.framePipeline) {
    const double start=Vr::milliseconds();
    const auto staged=renderer.stageVrCpu(*gothic.worldView(),gothic.world()->tickCount());
    vrProfiler.current.cpuStage=Vr::milliseconds()-start;
    vrProfiler.current.stageParts={staged.visual,staged.instances,staged.clusters};
    vrProfiler.current.cpuStageActive=true;
  } else if(auto view=gothic.worldView()) view->discardCpuStage();
  // Diagnostics and the overlap policy are decided before the drain: the
  // early left eye depends on them.
  const bool lightingProbe=world && ++vrLightingFrames==30;
  const bool visibilityProbe=world && (lightingProbe || (vrMenu.settings.profiler && vrLightingFrames%300==0));
  vrProfiler.current.diagnostic=visibilityProbe;
  if(visibilityProbe && vrProfiler.active && vrMenu.settings.gpuSampling!=0) {
    vrProfiler.sampleGpu=true; vrProfiler.current.sampled=true;
  }
  const bool overlap=world && vrMenu.settings.frameOverlap && !visibilityProbe;
  vrProfiler.current.overlap=overlap;
  // Sampled queries are collected after next frame's normal drain, before
  // command reuse. Explicit visibility/readback diagnostics remain serial.
  const bool pipeline=overlap && vrMenu.settings.framePipeline;
  vrProfiler.current.pipelineActive=pipeline;
  // Early left eye (Performance -> Early left eye). Overlapped frames pin the
  // eye command slots (left 0, right 1), so the next left eye needs only the
  // previous LEFT eye's slot and XR image: it is recorded and submitted while
  // the previous right eye and HUD still execute, which removes the GPU idle
  // gap of the left-eye recording. The right slot, HUD, XR copies, UI and
  // resource recycling are drained after its submission. A settings or size
  // change, a previous frame without pinned slots, or a copied (non-direct)
  // eye output falls back to the full drain.
  const bool pinnedPrevious=vrPinnedSlots; vrPinnedSlots=false;
  std::string settingsKey;
  { std::ostringstream key; vrMenu.settings.write(key); settingsKey=key.str(); }
  const bool earlyLeft=pipeline && pinnedPrevious && vrMenu.settings.earlyLeftEye && xr.directOutputReady() &&
                       settingsKey==vrLastSettingsKey && w()==int(xr.width()) && h()==int(xr.height()) &&
                       !vrGameplay.hasQueuedActions();
  vrLastSettingsKey=settingsKey;
  vrProfiler.current.earlyLeftActive=earlyLeft;
  // The preceding stereo frame may still execute while CPU simulation updates
  // staging data. Drain it before any mapped upload, resource recycling,
  // framebuffer resize, or command/fence reuse. The early left eye drains only
  // its own slot here; drainRight() follows the left eye's submission.
  if(earlyLeft) drainLeft(); else drainPrevious();
  // Gameplay may create or remove meshes. Queued menu actions disable the early
  // left eye, so they still run after the whole previous frame completed. Hand
  // and holster interactions are the same kind of world change tick() makes
  // while the previous frame executes; the hand vertices are double-buffered
  // by frame (vrHands, slot cmdId), so the previous right eye keeps its set.
  if(vrGameplay.update(gothic.checkLoading()==Gothic::LoadState::Idle?gothic.world():nullptr,xr,vrMenu,base,Application::tickCount(),world && !vrMenu.visible && !vrWelcome.visible))
    vrSaveFailed=!vrMenu.settings.save();
  if(world) vrHands.prepare(device,vrGameplay,Application::tickCount(),cmdId);
  if(w()!=int(xr.width()) || h()!=int(xr.height())) Widget::resize(int(xr.width()),int(xr.height()));
  renderer.setVrRenderScale(vrMenu.settings.renderScale);
  renderer.setVrFogMode(vrMenu.settings.fogMode);
  renderer.setVrFastLighting(vrMenu.settings.fastLighting);
  renderer.setVrIndexedObjects(vrMenu.settings.indexedObjects);
  renderer.setVrBatchedObjects(vrMenu.settings.batchedObjects);
  renderer.setVrObjectDistance(vrMenu.settings.objectDistance);
  renderer.setVrTerrainLod(vrMenu.settings.terrainLod);
  renderer.setVrFoveation(vrMenu.settings.foveation);
  renderer.setVrStaticLighting(vrMenu.settings.staticLighting);
  renderer.setVrHorizonHaze(vrMenu.settings.horizonHaze);
  renderer.setVrShadows(vrMenu.settings.shadows);
  renderer.setVrLighting(vrMenu.settings.lighting);
  renderer.setVrLocalLights(vrMenu.settings.localLights);
  renderer.setVrLightDepth(vrMenu.settings.lightDepth);
  renderer.setVrMergedTransparency(vrMenu.settings.mergedTransparency);
  xr.setDirectOutput(vrMenu.settings.directOutput);
  renderer.setVrTiledLights(vrMenu.settings.tiledLights);
  renderer.setVrSlabLights(vrMenu.settings.slabLights);
  renderer.setVrSubgroupTiles(vrMenu.settings.subgroupTiles);
  renderer.setVrUniformLights(vrMenu.settings.uniformLights);
  renderer.setVrStereoSeed(vrMenu.settings.stereoSeed);
  renderer.setVrDiagnostics(visibilityProbe);
  // UI meshes, the glyph atlas and the recycle ring may be read by the
  // previous HUD: with the early left eye this runs after drainRight().
  auto prepareUi=[&] {
  Resources::resetRecycled(cmdId);
  const double uiStart=Vr::milliseconds();
  if(video.isActive()) {
    video.paint(device,cmdId); uiLayer.clear(); PaintEvent p(uiLayer,atlas,w(),h()); video.paintEvent(p);
  } else {
    dispatchPaintEvent(uiLayer,atlas); numOverlay.clear(); PaintEvent p(numOverlay,atlas,w(),h()); inventory.paintNumOverlay(p);
  }
  uiMesh[cmdId].update(device,uiLayer); numMesh[cmdId].update(device,numOverlay);
  vrProfiler.current.ui=Vr::milliseconds()-uiStart;
  };
  if(!earlyLeft) prepareUi();
  // Reuse one head-centred shadow map in both eyes. LWC transforms remain per-eye.
  Camera shadowCamera; if(camera) shadowCamera=*camera;
  const auto headView=camera?camera->view():Matrix4x4::mkIdentity();
  const auto headProjection=camera?camera->projective():Matrix4x4::mkIdentity();
  struct CameraScope {
    Camera* camera; Matrix4x4 view,projection; bool world;
    ~CameraScope() { if(camera) { if(world) camera->setVrView(view,projection); else camera->clearVrView(); } }
  } cameraScope{camera,headView,headProjection,world};
  static_assert(Resources::MaxFramesInFlight>=2);
  // Overlapped frames pin the eye slots (left 0, right 1): the next left eye
  // reuses the previous left eye's slot, never the right eye's.
  auto eyeFrame=[&](uint32_t eye) { return overlap ? uint8_t(eye) : cmdId; };
  auto recordCopy=[&](uint32_t eye) {
    const auto& t=vrProfiler.current.transport[eye];
    vrProfiler.current.gpuWait[eye]=t.fence;
    vrProfiler.current.copy[eye]=t.acquire+t.wait+t.record+t.submit+t.release;
  };
  float eyeExposure[2]={};
  // Measured-cost route per eye (vr/lightroutecontroller.h): needs GPU
  // timestamps every world frame; diagnostic frames split the lighting pass
  // and are excluded.
  const bool lightRouteActive=world && vrMenu.settings.slabLights && vrMenu.settings.lighting && vrMenu.settings.localLights && !visibilityProbe;
  std::array<bool,2> eyeRouteUsed{};
  for(uint32_t eye=0;eye<(world?2u:1u);++eye) {
    if(eye==1 && earlyLeft) {
      // The next left eye is submitted: wait for the previous right eye, HUD
      // and XR copies; only then may UI and recycling reuse what they read.
      drainRight();
      prepareUi();
    }
    if(world) camera->setVrView(xr.eyeView(eye,base),xr.projection(eye));
    const uint8_t fId=eyeFrame(eye);
    auto& cmd=commands[fId];
    if(lightRouteActive) {
      const auto route=vrLightRoute.decide(uint8_t(eye),vrProfiler.frameId,vrLightRouteWeight[eye]);
      renderer.setVrSlabRoute(eye==1,route==LightRouteController::Slabs);
    } else {
      renderer.setVrSlabRoute(eye==1,vrMenu.settings.slabLights && vrLightRoute.route(uint8_t(eye))==LightRouteController::Slabs);
    }
    auto& transport=vrProfiler.current.transport[eye];
    auto* directTarget=world ? xr.acquireRenderTarget(eye,&transport) : nullptr;
    vrProfiler.current.directOutputActive[eye]=directTarget!=nullptr;
    // Menu/cinema frames: the panel shows the whole image.
    if(!world) xr.setEyeRect(eye,0,0);
    double start=Vr::milliseconds();
    {
      // Split left eye (Gothic.ini [ENGINE] vrSplitLeftOff=1 disables): the
      // renderer records its preparation into prepCommands, onPrepared submits
      // it, and the GPU starts while the GBuffer and lighting are still recorded.
      static const bool splitLeft=Gothic::settingsGetI("ENGINE","vrSplitLeftOff")==0;
      static bool splitLogged=false; if(!splitLogged) { Log::i("VR split left eye: ",splitLeft?"active":"off"); splitLogged=true; }
      std::optional<Tempest::Encoder<Tempest::CommandBuffer>> prep;
      if(world && eye==0 && splitLeft) prep.emplace(prepCommands[fId].startEncoding(device,vrProfiler.sampleGpu || lightRouteActive));
      // Timings of a slot include its preparation buffer only when this
      // frame recorded one there (0.0.42 counted a stale left-eye preparation
      // into the right eye: +1.9 ms per pair on paper).
      prepRecorded[fId]=prep.has_value();
      auto enc=cmd.startEncoding(device,vrProfiler.sampleGpu || lightRouteActive);
      if(world) {
        auto& target=directTarget?*directTarget:vrOutput;
        // Render scale < 1: the renderer tonemaps into this
        // top-left rectangle of the target; the projection view's imageRect,
        // the eye copy and the hands' scene-depth lookup use the same one.
        const auto eyeRect=renderer.vrEyeRect(target.size());
        xr.setEyeRect(eye,eyeRect.w,eyeRect.h);
        // Hands and held items draw inside the renderer's tonemapping pass
        // (own depth attachment, no extra load/store of the eye image).
        const auto handsVp=camera->projective()*camera->view();
        const auto pickupVp=camera->projective()*camera->viewLwc();
        const auto pickupOrigin=camera->originLwc();
        const Vec2 clipPlanes(camera->zNear(),camera->zFar());
        renderer.setVrOverlay({vrHands.hasContent()?vrHands.depthBuffer(uint32_t(target.w()),uint32_t(target.h())):nullptr,
                               [&,handsVp,pickupVp,pickupOrigin,clipPlanes,eyeRect](Tempest::Encoder<Tempest::CommandBuffer>& overlay){ vrHands.drawInPass(renderer.vrDepthBuffer(),overlay,handsVp,pickupVp,pickupOrigin,clipPlanes,cmdId,eyeRect); }});
        renderer.setVrEyeTarget(true);
        renderer.draw(target,enc,fId,*gothic.worldView(),*camera,&shadowCamera,eye==1,prep?&*prep:nullptr,
                      [&]{ prep.reset(); prepFence[fId]=device.submit(prepCommands[fId]); vrProfiler.current.prepSubmit[eye]=Vr::milliseconds()-start; });
        renderer.setVrOverlay({});
      }
      else renderer.draw(vrOutput,enc,fId,uiMesh[cmdId],numMesh[cmdId],inventory,video);
    }
    if(world) {
      const auto cpu=renderer.vrCpuStats();
      vrProfiler.current.prepare[eye]=cpu.update; vrProfiler.current.upload[eye]=cpu.uploads; vrProfiler.current.encode[eye]=cpu.encode;
      const auto& detail=gothic.worldView()->uploadCpuStats();
      const double phases[]={detail.scene,detail.lights,detail.instances,detail.clusters,detail.commands,detail.buckets};
      std::copy(std::begin(phases),std::end(phases),vrProfiler.current.uploadParts[eye]);
      vrProfiler.current.latePack[eye]={detail.instancePack,detail.clusterPack};
      vrProfiler.current.stageUsed[eye]={detail.instancesStaged,detail.clustersStaged};
      vrProfiler.current.lightDepthActive[eye]=renderer.vrUsesLightDepth();
      vrProfiler.current.mergedTransparencyActive[eye]=renderer.vrUsesMergedTransparency();
      vrProfiler.current.slabLightsActive[eye]=renderer.vrUsesSlabLights();
      vrProfiler.current.slabGateWeight[eye]=renderer.vrSlabGateWeight();
      vrProfiler.current.slabProbe[eye]=lightRouteActive && vrLightRoute.probing(uint8_t(eye));
      vrProfiler.current.slabCost[eye]={vrLightRoute.cost(uint8_t(eye),LightRouteController::Volumes),vrLightRoute.cost(uint8_t(eye),LightRouteController::Slabs)};
      vrLightRouteWeight[eye]=renderer.vrSlabGateWeight();
      eyeRouteUsed[eye]=renderer.vrUsesSlabLights();
    }
    fence[fId]=device.submit(cmd); vrProfiler.current.record[eye]=Vr::milliseconds()-start;
    // Each eye is rendered in queue order; fallback copies follow that eye.
    // Distinct upload slots and commands let CPU recording overlap GPU work.
    if(directTarget) {
      xr.submitRenderTarget(eye,fence[fId].share(),&transport);
      if(!overlap) xr.finishEyeCopy(eye,&transport);
    }
    else if(overlap) xr.queueEyeCopy(eye,textureCast<const Texture2d&>(vrOutput),&transport);
    else xr.copyEye(eye,textureCast<const Texture2d&>(vrOutput),&transport);
    recordCopy(eye);
    if(vrProfiler.sampleGpu && !overlap) { auto m=cmd.gpuTimings(); if(prepRecorded[fId]) { auto t=prepCommands[fId].gpuTimings(); t.insert(t.end(),m.begin(),m.end()); m=std::move(t); } vrProfiler.eyeGpu(eye,m); }
    if(lightRouteActive && !overlap) feedLightRoute(eye,vrProfiler.frameId,eyeRouteUsed[eye],cmd.gpuTimings());
    if(visibilityProbe) {
      const auto stats=gothic.worldView()->drawCommands().readVisibilityStats();
      vrProfiler.cullVisible[eye]=stats.visible; vrProfiler.cullFrustum[eye]=stats.frustum;
      vrProfiler.cullTerrain[eye]=stats.terrain; vrProfiler.cullObjectParts[eye]=stats.objectParts; vrProfiler.cullModels[eye]=stats.models;
      Log::i("VR culling frame=",vrProfiler.frameId," eye=",eye," meshlets active=",stats.active,
             " frustum=",stats.frustum," visible=",stats.visible," terrain=",stats.terrain,
             " objectParts=",stats.objectParts," models=",stats.models," indexedMeshlets=",stats.indexedMainMeshlets,
             " batchedMeshlets=",stats.batchedMainMeshlets," batchedCommands=",stats.batchedMainCommands," (diagnostic readback frame)");
      Log::i("VR depth seed frame=",vrProfiler.frameId," eye=",eye," meshlets=",stats.seedVisible,
             " terrain=",stats.seedTerrain," objectParts=",stats.seedObjectParts," shared=",stats.seedReused);
      Log::i("VR material visibility frame=",vrProfiler.frameId," eye=",eye,
             " solid=",stats.mainMaterialMeshlets[Material::Solid]," masked=",stats.mainMaterialMeshlets[Material::AlphaTest],
             " water=",stats.mainMaterialMeshlets[Material::Water]," ghost=",stats.mainMaterialMeshlets[Material::Ghost],
             " waterCommands=",stats.mainMaterialCommands[Material::Water]," ghostCommands=",stats.mainMaterialCommands[Material::Ghost],
             " gbufferTerrain=",stats.gbufferTerrain," static=",stats.gbufferStatic," movable=",stats.gbufferMovable,
             " animated=",stats.gbufferAnimated," morph=",stats.gbufferMorph," drawCommandPfx=",stats.gbufferPfx,
             " separateParticles=unmeasured");
      Log::i("VR local light draws frame=",vrProfiler.frameId," eye=",eye,
             " count=",renderer.readVrVisibleLightCount()," slabCount=",renderer.readVrSlabLightCount(),
             " slabGateWeight=",renderer.vrSlabGateWeight()," slabRoute=",renderer.vrSlabRouteRequested(),
             " slabCostVolumes=",vrLightRoute.cost(uint8_t(eye),LightRouteController::Volumes)," slabCostSlabs=",vrLightRoute.cost(uint8_t(eye),LightRouteController::Slabs),
             " slabSwitches=",vrLightRoute.switches(uint8_t(eye))," surfaceLighting=",vrMenu.settings.lighting,
             " localLights=",vrMenu.settings.localLights," tiled=",renderer.vrUsesTiledLights()," slab=",renderer.vrUsesSlabLights()," subgroupTiles=",renderer.vrUsesSubgroupTiles()," uniformLights=",renderer.vrUsesUniformLights());
      {
        // Actual per-light screen coverage from the slab masks: which sources
        // dominate the local-light cost at this view.
        const auto slab=renderer.readVrLightSlabStats();
        if(slab.lights>0) {
          Log::i("VR light slabs frame=",vrProfiler.frameId," eye=",eye," lights=",slab.lights," tiles=",slab.tiles,
                 " litTiles=",slab.litTiles," drawnTiles=",slab.drawnTiles," avgPerLitTile=",slab.averagePerLitTile," maxPerTile=",slab.maxPerTile);
          std::string detail;
          for(size_t i=0;i<slab.detail.size() && i<32;++i) {
            char buf[96];
            std::snprintf(buf,sizeof(buf),"%s%zu:r=%.0f d=%.0f c=%.1f%%",i?" ":"",i,double(slab.detail[i].range),double(slab.detail[i].distance),double(slab.detail[i].coverage*100));
            detail+=buf;
          }
          Log::i("VR light slab detail frame=",vrProfiler.frameId," eye=",eye," [",detail,"]");
        }
      }
      if(eye==0) {
        for(uint32_t cascade=0; cascade<Resources::ShadowLayers; ++cascade)
          Log::i("VR shadow casters frame=",vrProfiler.frameId," cascade=",cascade,
                 " enabled=",vrMenu.settings.shadows,
                 " meshlets=",vrMenu.settings.shadows?stats.shadowVisible[cascade]:0," terrain=",vrMenu.settings.shadows?stats.shadowTerrain[cascade]:0,
                 " objectParts=",vrMenu.settings.shadows?stats.shadowObjectParts[cascade]:0," indexedMeshlets=",vrMenu.settings.shadows?stats.indexedShadowMeshlets[cascade]:0);
        Log::i("VR lighting mode frame=",vrProfiler.frameId," optimized=",vrMenu.settings.fastLighting,
               " allocatedLightSlots=",gothic.worldView()->lights().size(),
               " cpuCandidateLightSlots=",gothic.worldView()->lights().visibleLightCount(),
               " indexedObjects=",vrMenu.settings.indexedObjects,
               " batchedObjects=",vrMenu.settings.batchedObjects," objectDistance=",vrMenu.settings.objectDistance," terrainLod=",vrMenu.settings.terrainLod," foveation=",vrMenu.settings.foveation,
               " shadows=",vrMenu.settings.shadows," lighting=",vrMenu.settings.lighting," localLights=",vrMenu.settings.localLights,
               " indexedSupported=",device.properties().indirect.indexed);
      }
      const auto fog=renderer.readVrFogWorkStats();
      uint32_t columns=0,steps=0,groups=0,groupSteps=0;
      for(uint32_t i=0;i<=32;++i) {
        columns+=fog.columns[i]; steps+=i*fog.columns[i];
        groups+=fog.groups[i]; groupSteps+=i*fog.groups[i];
      }
      if(eye==0) {
        // Adreno clock and thermal state of this diagnostic frame: pass costs
        // of long sessions are only comparable at the same clock.
        std::string clock="?",temperature="?";
        { std::ifstream f("/sys/class/kgsl/kgsl-3d0/gpuclk"); if(f) std::getline(f,clock); }
        { std::ifstream f("/sys/class/kgsl/kgsl-3d0/temp");   if(f) std::getline(f,temperature); }
        // devfreq bounds show the runtime's cap/floor (GPU level 5 via
        // com.oculus.trade_cpu_for_gpu_amount: is the cap 690 MHz in play?)
        std::string capHz, floorHz;
        { std::ifstream f("/sys/class/kgsl/kgsl-3d0/devfreq/max_freq"); if(f) std::getline(f,capHz); }
        { std::ifstream f("/sys/class/kgsl/kgsl-3d0/devfreq/min_freq"); if(f) std::getline(f,floorHz); }
        Log::i("VR gpu clock frame=",vrProfiler.frameId," gpuclk=",clock.c_str()," temp=",temperature.c_str()," cap=",capHz.c_str()," floor=",floorHz.c_str());
        // CPU clusters (cpufreq policies) of the same frame; the main thread's core shows which cluster runs the game.
        std::string cpu;
        for(int policy=0;policy<8;++policy) {
          std::ifstream f("/sys/devices/system/cpu/cpufreq/policy"+std::to_string(policy)+"/scaling_cur_freq");
          std::string khz; if(!f || !std::getline(f,khz)) continue;
          cpu+=" policy"+std::to_string(policy)+"="+khz;
          }
        Log::i("VR cpu clock frame=",vrProfiler.frameId," core=",sched_getcpu(),cpu.c_str());
      }
      Log::i("VR fog work frame=",vrProfiler.frameId," eye=",eye," columns=",columns," steps=",steps,
             " fullColumns=",fog.columns[32]," groups=",groups," groupMaxSteps=",groupSteps);
      vrProfiler.fogSteps[eye]=columns>0?float(steps)/float(columns):0;
    }
    if(lightingProbe) {
      SceneGlobals::UboGlobal lighting{};
      device.readBytes(gothic.worldView()->sceneGlobals().uboGlobal[SceneGlobals::V_Main],&lighting,sizeof(lighting));
      eyeExposure[eye]=lighting.exposure;
    }
  }
  if(lightingProbe) {
    const bool valid=std::isfinite(eyeExposure[0]) && std::isfinite(eyeExposure[1]) && eyeExposure[0]>0 && eyeExposure[1]>0;
    Log::i("VR lighting probe L=",eyeExposure[0]," R=",eyeExposure[1]," valid=",valid,
           " ratio=",valid?eyeExposure[1]/eyeExposure[0]:0.f," (diagnostic readback frame)");
  }
  // HUD layer (vr/vrhudrect.h): the quad covers only what was painted this
  // frame (status bars, dialogue, menu); nothing painted, no layer.
  const bool hudWanted=world || vrMenu.visible || vrWelcome.visible || vrMenu.settings.profiler;
  const double hudStart=Vr::milliseconds();
  frame.overlay=false;
  VectorImage overlay;
  Vr::HudRect rect;
  if(hudWanted) {
    PaintEvent paint(overlay,atlas,w(),h()); paintVrOverlay(paint,world);
    const int hudShiftX=world?int(vrMenu.settings.hudX*float(w())):0, hudShiftY=world?int(-vrMenu.settings.hudY*float(h())):0;
    if(world) {
      for(const auto* img:{&uiLayer,&numOverlay}) { const auto b=img->bounds(); Vr::hudRectAdd(rect,b.x0,b.y0,b.x1,b.y1,hudShiftX,hudShiftY,int(w()),int(h())); }
    }
    { const auto b=overlay.bounds(); Vr::hudRectAdd(rect,b.x0,b.y0,b.x1,b.y1,0,0,int(w()),int(h())); }
    vrProfiler.current.hudRect[0]=rect.w; vrProfiler.current.hudRect[1]=rect.h;
    frame.overlay=!rect.empty();
    xr.setHudRect(rect.x,rect.y,rect.w,rect.h);
  }
  // HUD GPU time of a sampled frame: timestamps on the HUD
  // command buffer and on QuestXr's HUD copy. Pipelined frames read them in
  // the next drain; the others once the copy has completed in this frame.
  const bool sampleHud=frame.overlay && vrProfiler.sampleGpu && profilerExtras;
  auto readHudGpu=[&] { vrProfiler.current.hudGpu=Vr::Profiler::gpuSum(vrHudCommand.gpuTimings()); vrProfiler.current.hudCopyGpu=xr.copyGpuMs(2); };
  if(frame.overlay) {
    const double start=hudStart;
    vrOverlayMesh[cmdId].update(device,overlay);
    auto& cmd=vrHudCommand;
    {
      auto enc=cmd.startEncoding(device,sampleHud);
      // the clear, the UI draws and the store cover only
      // the painted rectangle, the part the compositor samples; QuestXr copies
      // the same rectangle (vr/vrhudrect.h hudCopyRegion). Every painted point
      // lies inside it (bounds + 8 px padding, same HUD shift as the viewport).
      if(hudRectCopy) {
        const auto area=Vr::hudCopyRegion(rect,uint32_t(w()),uint32_t(h()));
        enc.setRenderArea(Tempest::Rect(area.x,area.y,area.w,area.h));
      }
      enc.setFramebuffer({{vrOutput,Vec4(),Tempest::Preserve}});
      if(world) {
        // Shift gameplay HUD only. VR settings and profiler keep a stable centre.
        enc.setViewport(int(vrMenu.settings.hudX*float(w())),int(-vrMenu.settings.hudY*float(h())),w(),h());
        uiMesh[cmdId].draw(enc); numMesh[cmdId].draw(enc);
        enc.setViewport(0,0,w(),h());
      }
      vrOverlayMesh[cmdId].draw(enc);
    }
    vrHudFence=device.submit(cmd);
    if(overlap) xr.queueEyeCopy(2,textureCast<const Texture2d&>(vrOutput),&vrProfiler.current.transport[2]);
    else xr.copyEye(2,textureCast<const Texture2d&>(vrOutput),&vrProfiler.current.transport[2]);
    if(sampleHud && !overlap) readHudGpu();
    vrProfiler.current.hud=Vr::milliseconds()-start;
  }
  if(overlap) {
    for(uint32_t eye=0;eye<2;++eye) {
      if(pipeline) xr.releaseEyeCopy(eye,&vrProfiler.current.transport[eye]);
      else xr.finishEyeCopy(eye,&vrProfiler.current.transport[eye]);
      recordCopy(eye);
      if(vrProfiler.sampleGpu) {
        if(pipeline) vrProfiler.deferEyeGpu(eye,eyeFrame(eye));
        else vrProfiler.eyeGpu(eye,commands[eyeFrame(eye)].gpuTimings());
      }
      if(lightRouteActive) {
        if(pipeline) { vrLightRoutePending.frameId=vrProfiler.frameId; vrLightRoutePending.slots[eye]=eyeFrame(eye); vrLightRoutePending.route[eye]=eyeRouteUsed[eye]; vrLightRoutePending.fed[eye]=false; vrLightRoutePending.valid=true; }
        else feedLightRoute(eye,vrProfiler.frameId,eyeRouteUsed[eye],commands[eyeFrame(eye)].gpuTimings());
      }
    }
    if(frame.overlay) {
      const double start=Vr::milliseconds();
      if(pipeline) xr.releaseEyeCopy(2,&vrProfiler.current.transport[2]);
      else xr.finishEyeCopy(2,&vrProfiler.current.transport[2]);
      if(sampleHud) { if(pipeline) vrProfiler.deferHudGpu(); else readHudGpu(); }
      vrProfiler.current.hud+=Vr::milliseconds()-start;
    }
  }
  vrPinnedSlots=pipeline;
  frame.complete=true;
  cmdId=(cmdId+1u)%Resources::MaxFramesInFlight;
}
#endif
