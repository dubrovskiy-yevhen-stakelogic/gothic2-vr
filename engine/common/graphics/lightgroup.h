#pragma once

#include <Tempest/CommandBuffer>
#include <unordered_set>
#include <zenkit/vobs/Light.hh>

#include "lightsource.h"
#include "resources.h"

class DbgPainter;
class SceneGlobals;
class World;

class LightGroup final {
  public:
    LightGroup(const SceneGlobals& scene);

    class Light final {
      public:
        Light() = default;
        Light(Light&& other);
        Light& operator = (Light&& other);
        ~Light();

        void     setPosition(float x, float y, float z);
        void     setPosition(const Tempest::Vec3& p);

        void     setEnabled(bool e);

        void     setRange (float r);
        void     setColor (const Tempest::Vec3& c);
        void     setColor (const std::vector<Tempest::Vec3>& c, float fps, bool smooth);
        void     setTimeOffset(uint64_t t);

        uint64_t effectPrefferedTime() const;

      private:
        Light(LightGroup& l, size_t id):owner(&l), id(id) {}
        LightGroup* owner = nullptr;
        size_t      id    = 0;

      friend class LightGroup;
      };

    Light  add(const zenkit::LightPreset& vob);
    Light  add(const zenkit::VLight& vob);
    Light  add(std::string_view preset);
    size_t size() const { return lightSourceData.size(); }

    void   tick(uint64_t time);
    bool   updateLights();
    auto&  lightsSsbo() const { return lightSourceSsbo; }
    // Compact data is for the ordinary unshadowed light shader only. Other
    // paths index shadow tables by the original stable light slot.
    struct LightSsbo {
      Tempest::Vec3 pos;
      float         range  = 0;
      Tempest::Vec3 color;
      float         pading = 0;
      };
    size_t visibleLightCount() const { return visibleLightData.size(); }
    auto&  visibleLightsSsbo() const { return visibleLightSsbo[visibleFrameId]; }
    // CPU copy of the compacted-list input (same order and coordinates the
    // GPU compaction reads), for the camera-side slab-route gate.
    auto&  visibleLights() const { return visibleLightData; }

    void   prepareGlobals(Tempest::Encoder<Tempest::CommandBuffer> &cmd, uint8_t fId);
    // VR 2002 static lighting: lights the Spacer compiled into the world mesh
    // (zCVobLight lightStatic) leave the per-eye visible list.
    void   setVrStaticLighting(bool on) { vrStaticLighting = on; }
    size_t staticLightCount() const { size_t n=0; for(auto f:staticVob) n+=f; return n; }

    void   dbgLights(DbgPainter& p) const;

  private:
    using Vertex = Resources::VertexL;

    struct Path {
      uint32_t dst;
      uint32_t src;
      uint32_t size;
      };

    struct VsmSsbo {
      uint32_t mask[6];
      };

    size_t                     alloc(bool dynamic);
    void                       free(size_t id);

    void                       markAsDurty(size_t id);
    void                       markAsDurtyNoSync(size_t id);
    void                       resetDurty();
    void                       prepareVisibleLights(uint8_t fId);

    const zenkit::LightPreset& findPreset(std::string_view preset) const;

    const SceneGlobals&             scene;
    std::vector<zenkit::LightPreset> presets;

    std::mutex                       sync;
    std::vector<size_t>              freeList;
    std::vector<LightSource>         lightSourceDesc;
    std::vector<LightSsbo>           lightSourceData;
    std::unordered_set<size_t>       animatedLights;
    std::vector<uint8_t>             staticVob;      // zenkit LightPreset::is_static per light id
    bool                             vrStaticLighting = false;
    std::vector<uint32_t>            duryBit;

    Tempest::StorageBuffer           lightSourceSsbo;
    Tempest::StorageBuffer           patchSsbo[Resources::MaxFramesInFlight];
    std::vector<LightSsbo>           visibleLightData;
    Tempest::StorageBuffer           visibleLightSsbo[Resources::MaxFramesInFlight];
    uint8_t                         visibleFrameId = 0;
  };

