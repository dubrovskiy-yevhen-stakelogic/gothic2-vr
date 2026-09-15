#pragma once

#include <Tempest/Device>
#include <Tempest/Matrix4x4>
#include <Tempest/UniformBuffer>

#include <condition_variable>
#include <atomic>
#include <vector>
#include <thread>

#include "resources.h"

class InstanceStorage {
  private:
    struct Range {
      size_t begin = 0;
      size_t size  = 0;
      size_t asize = 0;
      };

    static constexpr size_t blockSz   = 64;
    static constexpr size_t alignment = 64;

  public:
    class Id {
      public:
        Id() = default;
        Id(InstanceStorage& owner, Range rgn):owner(&owner), rgn(rgn){}
        Id(Id&& other) noexcept;
        Id& operator = (Id&& other) noexcept;
        ~Id();

        const size_t   size() const { return rgn.asize; }
        void           set(const Tempest::Matrix4x4* anim);
        void           set(const Tempest::Matrix4x4& obj, size_t offset);
        void           set(const void* data, size_t offset, size_t size);
        void           copyCpu(void* data,size_t offset,size_t size) const;

        template<class T>
        const uint32_t offsetId() const { return uint32_t(rgn.begin/sizeof(T)); }

        bool           isEmpty() const { return rgn.asize==0; }

      private:
        InstanceStorage* owner = nullptr;
        Range            rgn;
      friend class InstanceStorage;
      };

    InstanceStorage();
    ~InstanceStorage();

    Id   alloc(const size_t size);
    bool realloc(Id& id, const size_t size);
    auto ssbo () const -> const Tempest::StorageBuffer&;
    bool commit(Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t fId);
    void join();
    // CPU-only snapshot. Call after animation workers have completed. The
    // validity token does not allow setters to race with packing or commit.
    void prepareCpu();
    void discardCpu() { invalidateCpu(); }
    bool cpuPrepared() const { return prepared.load(std::memory_order_relaxed); }
    double lastPackMs() const { return commitPackMs; }
    bool usedPreparedCpu() const { return commitPrepared; }

  private:
    void free(const Range& r);
    void uploadMain();
    void invalidateCpu() { if(prepared.load(std::memory_order_relaxed)) prepared.store(false,std::memory_order_relaxed); }

    struct Path {
      uint32_t dst;
      uint32_t src;
      uint32_t size;
      };

    std::vector<Range>      rgn;
    std::vector<uint32_t>   durty;
    std::vector<uint32_t>   dirtySnapshot;
    std::atomic_bool        prepared{false};
    double                 commitPackMs=0;
    bool                   commitPrepared=false;
    size_t                  blockCnt = 0;

    Tempest::StorageBuffer  patchGpu[Resources::MaxFramesInFlight];
    std::vector<uint8_t>    patchCpu;
    std::vector<Path>       patchBlock;

    Tempest::StorageBuffer  dataGpu;
    std::vector<uint8_t>    dataCpu;

    std::thread             uploadTh;
    std::mutex              sync;
    std::condition_variable uploadCnd;
    std::condition_variable uploadDone;
    int32_t                 uploadFId = -1;
  };
