#pragma once

#include <Tempest/DescriptorArray>
#include <Tempest/StorageBuffer>
#include <Tempest/IndexBuffer>
#include <unordered_map>

#include "material.h"

class StaticMesh;
class AnimMesh;

class DrawBuckets {
  public:
    DrawBuckets();

    struct Bucket {
      const StaticMesh* staticMesh = nullptr;
      const AnimMesh*   animMesh   = nullptr;
      Material          mat;
      };

    class Id {
      public:
        Id() = default;
        Id(DrawBuckets& owner):owner(&owner){}
        Id(Id&& other) noexcept;
        Id& operator = (Id&& other) noexcept;
        ~Id();

        uint16_t toInt() const { return id; }

        Bucket& operator *  () { return  owner->bucketsCpu[id]; }
        Bucket* operator -> () { return &owner->bucketsCpu[id]; }

      private:
        Id(DrawBuckets* owner, size_t id);

        DrawBuckets* owner = nullptr;
        uint16_t     id    = 0;
      friend class DrawBuckets;
      };

    Id   alloc(const Material& mat, const StaticMesh& mesh);
    Id   alloc(const Material& mat, const AnimMesh&   mesh);

    const Bucket& operator[](size_t i) { return bucketsCpu[i]; }

    auto ssbo() const -> const Tempest::StorageBuffer& { return bucketsGpu; }
    auto buckets() -> const std::vector<Bucket>&;

    auto& textures() const { return desc.tex;     }
    auto& vbo()      const { return desc.vbo;     }
    auto& ibo()      const { return desc.ibo;     }
    auto& morphId()  const { return desc.morphId; }
    auto& morph()    const { return desc.morph;   }

    bool commit(Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t fId);
    bool enableIndexed(uint32_t bucketId);
    auto& indexedPool() const { return indexedPoolGpu; }
    auto& indexedRanges() const { return indexedRangesGpu; }
    size_t indexedMeshCount() const { return indexedMeshes.size(); }

  private:
    void updateBindlessArrays();

    enum BucketFlg : uint32_t {
      BK_SOLID = 0x1,
      BK_SKIN  = 0x2,
      BK_MORPH = 0x4,
      BK_WATER = 0x8,
      BK_CAMERA_FADE = 0x10,
      BK_FADE_MULTIPLY = 0x20,
      };

    struct BucketGpu final {
      Tempest::Vec4  bbox[2];
      Tempest::Point texAniMapDirPeriod;
      float          bboxRadius       = 0;
      float          waveMaxAmplitude = 0;
      float          alphaWeight      = 1;
      float          envMapping       = 0;
      uint32_t       flags            = 0;
      uint32_t       padd[1]          = {};
      };

    struct {
      Tempest::DescriptorArray tex;
      Tempest::DescriptorArray vbo;
      Tempest::DescriptorArray ibo;
      Tempest::DescriptorArray morphId;
      Tempest::DescriptorArray morph;
      } desc;

    std::vector<Bucket>      bucketsCpu;
    Tempest::StorageBuffer   bucketsGpu;
    bool                     bucketsDurtyBit = false;
    struct IndexedRange { uint32_t first=0, count=0; };
    std::unordered_map<const Tempest::IndexBuffer<uint32_t>*,IndexedRange> indexedMeshes;
    std::vector<IndexedRange> indexedRangesCpu;
    std::vector<uint32_t> indexedPoolCpu;
    Tempest::IndexBuffer<uint32_t> indexedPoolGpu;
    Tempest::StorageBuffer indexedRangesGpu;
    bool indexedPoolDirty=false, indexedRangesDirty=false;
  };
