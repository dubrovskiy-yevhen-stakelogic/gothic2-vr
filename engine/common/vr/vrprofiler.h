#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace Vr {
inline double milliseconds() { return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct CopyTimings { bool reused=false; double acquire=0,wait=0,record=0,submit=0,fence=0,release=0; };
struct FrameStats {
      double prepSubmit[2]={};   // split left eye: ms from the record start to the preparation submit
      int    hudRect[2]={};      // HUD layer rectangle (pixels), 0 = no layer
  //  instrumentation (sampled frames; -1 = unknown or not sampled):
  double hudGpu=-1,hudCopyGpu=-1; // GPU ms of the HUD command buffer and of the QuestXr HUD copy
  double gpuClockMhz=-1;          // kgsl gpuclk, read once per sampled frame
  int profilerView=0;             // live profiler display: 0 panel, 1 compact, 2 CSV only
  double frame=0,wait=0,simulation=0,ui=0,end=0;
  double record[2]={},gpuWait[2]={},gpu[2]={},copy[2]={};
  double hud=0,priorGpuWait=0;
  double cpuStage=0;
  std::array<double,3> stageParts{};
  std::array<std::array<double,2>,2> latePack{};
  std::array<std::array<bool,2>,2> stageUsed{};
  bool cpuStaging=true,cpuStageActive=false;
  bool directOutput=true;
  std::array<bool,2> directOutputActive{};
  bool lightDepth=true;
  std::array<bool,2> lightDepthActive{};
  bool mergedTransparency=true;
  std::array<bool,2> mergedTransparencyActive{};
  bool slabLights=true;
  std::array<bool,2> slabLightsActive{};
  std::array<float,2> slabGateWeight{}; // camera-side slab prior per eye (summed squared coverage)
  std::array<bool,2> slabProbe{};        // this eye rendered a controller probe with the other route
  std::array<std::array<float,2>,2> slabCost{}; // controller cost estimates per eye: [volumes, slabs] ms (-1 unknown)
  bool horizonHaze=true,earlyLeftEye=true; // settings
  int horizon=1; // VR far plane setting
  int terrainLod=2; // VR terrain detail setting
  int foveation=2; // fixed foveation level
  bool earlyLeftActive=false; // this frame recorded its left eye before draining the previous right eye/HUD
  double rightDrain=0;        // ms waited for the previous right eye/HUD after the early left eye
  double tick=0,animation=0,camera=0,room=0;
  double prepare[2]={},upload[2]={},encode[2]={};
  double uploadParts[2][6]={};
  CopyTimings transport[3];
  bool menu=false,sampled=false,diagnostic=false,fastLighting=true,indexedObjects=true,batchedObjects=true;
  bool shadows=true,lighting=true,localLights=true,overlap=true,tiledLights=true,subgroupTiles=true,uniformLights=true,stereoSeed=true,framePipeline=true,pipelineActive=false;
  int fogMode=0,gpuSampling=1,objectDistance=0;
  float renderScale=1;
  bool world=false,gpuValid[2]={};
  bool sameConfiguration(const FrameStats& other) const {
    return slabLights==other.slabLights && directOutput==other.directOutput && mergedTransparency==other.mergedTransparency && lightDepth==other.lightDepth && cpuStaging==other.cpuStaging && fastLighting==other.fastLighting && indexedObjects==other.indexedObjects && batchedObjects==other.batchedObjects &&
           shadows==other.shadows && lighting==other.lighting && localLights==other.localLights && overlap==other.overlap && tiledLights==other.tiledLights && subgroupTiles==other.subgroupTiles && uniformLights==other.uniformLights && stereoSeed==other.stereoSeed && framePipeline==other.framePipeline &&
           world==other.world && menu==other.menu && fogMode==other.fogMode && gpuSampling==other.gpuSampling && objectDistance==other.objectDistance && horizonHaze==other.horizonHaze && earlyLeftEye==other.earlyLeftEye && horizon==other.horizon && terrainLod==other.terrainLod && foveation==other.foveation &&
           std::abs(renderScale-other.renderScale)<=0.001f;
  }
};
class Profiler {
  public:
    ~Profiler() { finalizePending(); }
    FrameStats current,average;
    double p95=0; float refresh=0;
    std::string worstPass[2];
    bool active=false,sampleGpu=false;
    uint64_t frameId=0, cullVisible[2]={}, cullFrustum[2]={}, cullTerrain[2]={}, cullObjectParts[2]={}, cullModels[2]={};
    float fogSteps[2]={};
    void begin(bool enabled,int sampling,int fog,float scale,bool menu,bool fastLighting=true,bool indexedObjects=true,bool shadows=true,bool lighting=true,bool localLights=true,bool overlap=true,bool batchedObjects=true) {
      ++frameId; sampleGpu=enabled && (sampling==2 || (sampling==1 && frameId%16==0));
      if(enabled && !active) {
        samples=0; index=0; previous=0; lastReport=milliseconds();
        const auto suffix=std::to_string(uint64_t(std::chrono::system_clock::now().time_since_epoch().count()));
        log=std::make_shared<LogSession>();
        auto& csv=log->csv; auto& passes=log->passes;
        csv.open("vr-profile-"+suffix+".csv");
        passes.open("vr-gpu-passes-"+suffix+".csv");
        passes<<"frame_id,world,menu,render_scale,fog_mode,gpu_sampling,diagnostic,eye,pass,gpu_ms,fast_lighting,indexed_objects,shadows,lighting,local_lights,frame_overlap,tiled_lights,subgroup_tiles,uniform_lights,stereo_seed,frame_pipeline,pipeline_active,prior_gpu_wait_ms,batched_objects,cpu_staging,cpu_stage_active,cpu_stage_ms,stage_visual_ms,stage_instances_ms,stage_clusters_ms,left_instance_pack_ms,left_cluster_pack_ms,right_instance_pack_ms,right_cluster_pack_ms,left_instances_staged,left_clusters_staged,right_instances_staged,right_clusters_staged,light_depth,left_light_depth_active,right_light_depth_active,merged_transparency,left_merged_transparency_active,right_merged_transparency_active,direct_output,left_direct_output_active,right_direct_output_active,slab_lights,left_slab_lights_active,right_slab_lights_active,left_slab_gate_weight,right_slab_gate_weight,left_slab_probe,right_slab_probe,left_slab_cost_volumes,left_slab_cost_slabs,right_slab_cost_volumes,right_slab_cost_slabs,object_distance,horizon_haze,early_left_eye,early_left_active,right_drain_ms,horizon,terrain_lod,foveation\n";
        csv<<"world,refresh_hz,frame_ms,wait_xr_ms,simulation_ms,ui_ms,left_record_ms,left_wait_ms,left_gpu_ms,left_copy_ms,right_record_ms,right_wait_ms,right_gpu_ms,right_copy_ms,hud_ms,end_xr_ms,frame_id,time_ms,menu,render_scale,fog_mode,gpu_sampling,gpu_sampled,left_acquire_ms,left_swap_wait_ms,left_copy_record_ms,left_submit_ms,left_release_ms,right_acquire_ms,right_swap_wait_ms,right_copy_record_ms,right_submit_ms,right_release_ms,hud_acquire_ms,hud_swap_wait_ms,hud_copy_record_ms,hud_submit_ms,hud_release_ms,diagnostic,left_copy_cached,right_copy_cached,hud_copy_cached,fast_lighting,indexed_objects,shadows,lighting,local_lights,frame_overlap,tick_ms,animation_ms,camera_ms,room_ms,left_prepare_ms,left_upload_ms,left_encode_ms,right_prepare_ms,right_upload_ms,right_encode_ms,tiled_lights,left_scene_upload_ms,left_lights_upload_ms,left_instances_upload_ms,left_clusters_upload_ms,left_commands_upload_ms,left_buckets_upload_ms,right_scene_upload_ms,right_lights_upload_ms,right_instances_upload_ms,right_clusters_upload_ms,right_commands_upload_ms,right_buckets_upload_ms,subgroup_tiles,uniform_lights,stereo_seed,frame_pipeline,pipeline_active,prior_gpu_wait_ms,batched_objects,cpu_staging,cpu_stage_active,cpu_stage_ms,stage_visual_ms,stage_instances_ms,stage_clusters_ms,left_instance_pack_ms,left_cluster_pack_ms,right_instance_pack_ms,right_cluster_pack_ms,left_instances_staged,left_clusters_staged,right_instances_staged,right_clusters_staged,light_depth,left_light_depth_active,right_light_depth_active,merged_transparency,left_merged_transparency_active,right_merged_transparency_active,direct_output,left_direct_output_active,right_direct_output_active,slab_lights,left_slab_lights_active,right_slab_lights_active,left_slab_gate_weight,right_slab_gate_weight,left_slab_probe,right_slab_probe,left_slab_cost_volumes,left_slab_cost_slabs,right_slab_cost_volumes,right_slab_cost_slabs,object_distance,horizon_haze,early_left_eye,early_left_active,right_drain_ms,horizon,terrain_lod,foveation,hud_rect_w,hud_rect_h,prep_submit_ms,hud_gpu_ms,hud_copy_gpu_ms,gpu_clock_mhz,profiler_view\n";
      } else if(!enabled && active) { log.reset(); }
      // A previous sampled frame owns its log session until its GPU queries
      // are collected, even when this frame disables or restarts profiling.
      for(auto& timings:currentPasses) timings.clear();
      deferredEyes=0; deferredHud=false;
      active=enabled; current={}; current.menu=menu; current.fastLighting=fastLighting; current.indexedObjects=indexedObjects; current.shadows=shadows; current.lighting=lighting; current.localLights=localLights; current.overlap=overlap; current.fogMode=fog; current.gpuSampling=sampling; current.renderScale=scale; current.sampled=sampleGpu;
      current.batchedObjects=batchedObjects;
    }
    template<class Timings> void eyeGpu(uint32_t eye,const Timings& timings) {
      recordGpu(current,currentPasses,eye,timings);
    }
    void deferEyeGpu(uint32_t eye,uint8_t commandSlot) {
      if(!active || eye>=2) return;
      deferredEyes|=uint8_t(1u<<eye); deferredSlots[eye]=commandSlot;
    }
    bool hasPendingGpu() const { return pendingValid; }
    // Sum of a command buffer's timestamp intervals; -1 when none are available.
    template<class Timings> static double gpuSum(const Timings& timings) {
      if(timings.empty()) return -1;
      double ms=0; for(const auto& t:timings) ms+=t.milliseconds;
      return ms;
    }
    // A pipelined sampled frame's HUD render and HUD copy complete with the
    // next frame's drain: that drain reads them into the pending row before
    // collectPendingGpu() publishes it (and before the HUD is re-encoded).
    void deferHudGpu() { if(active) deferredHud=true; }
    bool pendingHud() const { return pendingValid && pending.deferredHud && !pending.hudCollected; }
    template<class Timings> void collectPendingHud(const Timings& hud,double copyMs) {
      if(!pendingHud()) return;
      pending.stats.hudGpu=gpuSum(hud); pending.stats.hudCopyGpu=copyMs; pending.hudCollected=true;
      collectedHudGpu=pending.stats.hudGpu;
    }
    double collectedHudGpu=-1; // last HUD value read into a pending row (fixtures check it)
    template<class ReadTimings> void collectPendingGpu(ReadTimings&& read) {
      if(!pendingValid) return;
      // The caller has completed all previous render/copy fences. Read these
      // exact slots before startEncoding resets their timestamp query pools.
      for(uint32_t eye=0;eye<2;++eye) if((pending.deferredEyes&(1u<<eye))!=0 && (pending.collectedEyes&(1u<<eye))==0)
        recordGpu(pending.stats,pending.passes,eye,read(pending.slots[eye]));
      finalizePending();
    }
    // Early left eye: read one eye's completed slot before the next frame
    // re-encodes it; collectPendingGpu() adds the other eye and publishes.
    template<class ReadTimings> void collectPendingEye(uint32_t eye,ReadTimings&& read) {
      if(!pendingValid || eye>=2 || (pending.deferredEyes&(1u<<eye))==0 || (pending.collectedEyes&(1u<<eye))!=0) return;
      recordGpu(pending.stats,pending.passes,eye,read(pending.slots[eye]));
      pending.collectedEyes|=uint8_t(1u<<eye);
    }
    uint8_t pendingCollectedEyes() const { return pendingValid ? pending.collectedEyes : 0; }
    void finalizePending() {
      if(!pendingValid) return;
      pendingValid=false;
      // Shutdown or unavailable queries preserve a sampled CPU row with -1
      // for unknown GPU values; another frame's timings are never substituted.
      publish(pending);
      pending={};
    }
    void suspend() { previous=0; }
    void finish(double now=milliseconds()) {
      if(!active) return;
      current.frame=previous>0?now-previous:0; previous=now;
      FrameRecord frame;
      frame.stats=current; frame.id=frameId; frame.time=now; frame.refresh=refresh;
      frame.log=log; frame.passes=std::move(currentPasses);
      frame.slots=deferredSlots; frame.deferredEyes=deferredEyes; frame.deferredHud=deferredHud;
      if(pendingValid) finalizePending(); // Preserve an unresolved row on abnormal caller sequencing.
      if(deferredEyes!=0 || deferredHud) { pending=std::move(frame); pendingValid=true; }
      else publish(frame);
    }
  private:
    struct PassTiming { std::string name; double milliseconds=0; };
    using EyePasses=std::array<std::vector<PassTiming>,2>;
    struct LogSession { std::ofstream csv,passes; };
    struct FrameRecord {
      FrameStats stats;
      uint64_t id=0;
      double time=0;
      float refresh=0;
      std::shared_ptr<LogSession> log;
      EyePasses passes;
      std::array<uint8_t,2> slots{};
      uint8_t deferredEyes=0;
      uint8_t collectedEyes=0; // eyes already read by collectPendingEye()
      bool deferredHud=false,hudCollected=false;
    };
    template<class Timings> static void recordGpu(FrameStats& frame,EyePasses& passes,uint32_t eye,const Timings& timings) {
      if(eye>=2) return;
      frame.gpuValid[eye]=!timings.empty(); frame.gpu[eye]=0;
      passes[eye].clear();
      for(const auto& t:timings) {
        passes[eye].push_back({std::string(t.name),t.milliseconds});
        frame.gpu[eye]+=t.milliseconds;
      }
    }
    void publish(const FrameRecord& frame) {
      if(!frame.log) return;
      const auto& current=frame.stats;
      const auto frameId=frame.id; const double now=frame.time; const float refresh=frame.refresh;
      auto& csv=frame.log->csv; auto& passes=frame.log->passes;
      for(uint32_t eye=0;eye<2;++eye) {
        double worst=0;
        for(const auto& t:frame.passes[eye]) {
          // Renderer labels are fixed internal names, without commas or quotes.
          passes<<frameId<<','<<current.world<<','<<current.menu<<','<<current.renderScale<<','<<current.fogMode<<','<<current.gpuSampling<<','<<current.diagnostic<<','<<eye<<','<<t.name<<','<<t.milliseconds<<','<<current.fastLighting<<','<<current.indexedObjects<<','<<current.shadows<<','<<current.lighting<<','<<current.localLights<<','<<current.overlap<<','<<current.tiledLights<<','<<current.subgroupTiles<<','<<current.uniformLights<<','<<current.stereoSeed<<','<<current.framePipeline<<','<<current.pipelineActive<<','<<current.priorGpuWait<<','<<current.batchedObjects<<','<<current.cpuStaging<<','<<current.cpuStageActive<<','<<current.cpuStage; for(double part:current.stageParts) passes<<','<<part; for(const auto& eye:current.latePack) for(double part:eye) passes<<','<<part; for(const auto& eye:current.stageUsed) for(bool used:eye) passes<<','<<used; passes<<','<<current.lightDepth; for(bool used:current.lightDepthActive) passes<<','<<used; passes<<','<<current.mergedTransparency; for(bool used:current.mergedTransparencyActive) passes<<','<<used; passes<<','<<current.directOutput; for(bool used:current.directOutputActive) passes<<','<<used; passes<<','<<current.slabLights; for(bool used:current.slabLightsActive) passes<<','<<used; for(float weight:current.slabGateWeight) passes<<','<<weight; for(bool probe:current.slabProbe) passes<<','<<probe; for(const auto& c:current.slabCost) passes<<','<<c[0]<<','<<c[1]; passes<<','<<current.objectDistance<<','<<current.horizonHaze<<','<<current.earlyLeftEye<<','<<current.earlyLeftActive<<','<<current.rightDrain<<','<<current.horizon<<','<<current.terrainLod<<','<<current.foveation<<'\n';
          if(frame.log==log && current.sameConfiguration(this->current) && t.milliseconds>worst) {
            worst=t.milliseconds; worstPass[eye]=t.name;
          }
        }
      }
      if(current.frame<=0) return;
      csv<<current.world<<','<<refresh<<','<<current.frame<<','<<current.wait<<','<<current.simulation<<','<<current.ui;
      for(int i=0;i<2;++i) csv<<','<<current.record[i]<<','<<current.gpuWait[i]<<','<<(current.gpuValid[i]?current.gpu[i]:-1)<<','<<current.copy[i];
      csv<<','<<current.hud<<','<<current.end<<','<<frameId<<','<<now<<','<<current.menu<<','<<current.renderScale<<','<<current.fogMode<<','<<current.gpuSampling<<','<<current.sampled;
      for(const auto& c:current.transport) csv<<','<<c.acquire<<','<<c.wait<<','<<c.record<<','<<c.submit<<','<<c.release;
      csv<<','<<current.diagnostic; for(const auto& c:current.transport) csv<<','<<c.reused; csv<<','<<current.fastLighting<<','<<current.indexedObjects<<','<<current.shadows<<','<<current.lighting<<','<<current.localLights<<','<<current.overlap<<','<<current.tick<<','<<current.animation<<','<<current.camera<<','<<current.room; for(int i=0;i<2;++i) csv<<','<<current.prepare[i]<<','<<current.upload[i]<<','<<current.encode[i]; csv<<','<<current.tiledLights; for(const auto& eye:current.uploadParts) for(double phase:eye) csv<<','<<phase; csv<<','<<current.subgroupTiles<<','<<current.uniformLights<<','<<current.stereoSeed<<','<<current.framePipeline<<','<<current.pipelineActive<<','<<current.priorGpuWait<<','<<current.batchedObjects<<','<<current.cpuStaging<<','<<current.cpuStageActive<<','<<current.cpuStage; for(double part:current.stageParts) csv<<','<<part; for(const auto& eye:current.latePack) for(double part:eye) csv<<','<<part; for(const auto& eye:current.stageUsed) for(bool used:eye) csv<<','<<used; csv<<','<<current.lightDepth; for(bool used:current.lightDepthActive) csv<<','<<used; csv<<','<<current.mergedTransparency; for(bool used:current.mergedTransparencyActive) csv<<','<<used; csv<<','<<current.directOutput; for(bool used:current.directOutputActive) csv<<','<<used; csv<<','<<current.slabLights; for(bool used:current.slabLightsActive) csv<<','<<used; for(float weight:current.slabGateWeight) csv<<','<<weight; for(bool probe:current.slabProbe) csv<<','<<probe; for(const auto& c:current.slabCost) csv<<','<<c[0]<<','<<c[1]; csv<<','<<current.objectDistance<<','<<current.horizonHaze<<','<<current.earlyLeftEye<<','<<current.earlyLeftActive<<','<<current.rightDrain<<','<<current.horizon<<','<<current.terrainLod<<','<<current.foveation<<','<<current.hudRect[0]<<','<<current.hudRect[1]<<','<<current.prepSubmit[0]<<','<<current.hudGpu<<','<<current.hudCopyGpu<<','<<current.gpuClockMhz<<','<<current.profilerView<<'\n';
      if(frame.log!=log) return; // A disabled/restarted session cannot contaminate the new average.
      ring[index]=current; index=(index+1)%ring.size(); samples=std::min(samples+1,ring.size());
      if(now-lastReport<500) return;
      lastReport=now; csv.flush(); passes.flush(); average={}; std::vector<double> frames;
      // Do not mix menu and world performance in the displayed average.
      size_t count=0,gpuCount[2]={};
      for(size_t i=0;i<samples;++i) {
        const auto& f=ring[i]; if(f.diagnostic || !f.sameConfiguration(this->current)) continue;
        ++count; frames.push_back(f.frame);
        average.frame+=f.frame; average.wait+=f.wait; average.simulation+=f.simulation; average.ui+=f.ui; average.hud+=f.hud; average.end+=f.end; average.priorGpuWait+=f.priorGpuWait; average.cpuStage+=f.cpuStage;
        for(int eye=0;eye<2;++eye) {
          average.record[eye]+=f.record[eye]; average.gpuWait[eye]+=f.gpuWait[eye]; average.copy[eye]+=f.copy[eye];
          if(f.gpuValid[eye]) { average.gpu[eye]+=f.gpu[eye]; ++gpuCount[eye]; }
        }
      }
      const double n=double(std::max(size_t(1),count));
      average.frame/=n; average.wait/=n; average.simulation/=n; average.ui/=n; average.hud/=n; average.end/=n; average.priorGpuWait/=n; average.cpuStage/=n;
      for(int eye=0;eye<2;++eye) {
        average.record[eye]/=n; average.gpuWait[eye]/=n; average.copy[eye]/=n;
        average.gpu[eye]/=double(std::max(size_t(1),gpuCount[eye])); average.gpuValid[eye]=gpuCount[eye]>0;
      }
      std::sort(frames.begin(),frames.end()); if(!frames.empty()) p95=frames[std::min(frames.size()-1,size_t(double(frames.size())*0.95))];
    }
    std::array<FrameStats,120> ring{};
    size_t samples=0,index=0;
    double previous=0,lastReport=0;
    std::shared_ptr<LogSession> log;
    EyePasses currentPasses;
    std::array<uint8_t,2> deferredSlots{};
    uint8_t deferredEyes=0;
    bool deferredHud=false;
    FrameRecord pending;
    bool pendingValid=false;
};
}
