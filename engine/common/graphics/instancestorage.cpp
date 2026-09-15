#include "instancestorage.h"
#include "shaders.h"
#include "utils/workers.h"

#include <Tempest/Log>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <cassert>

using namespace Tempest;

static uint32_t nextPot(uint32_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
  }

static uint32_t alignAs(uint32_t sz, uint32_t alignment) {
  return ((sz+alignment-1)/alignment)*alignment;
  }

static void atomicOr(uint32_t& bits, uint32_t mask) {
#if defined(__GNUC__) || defined(__clang__)
  // NDK 27's libc++ does not provide atomic_ref. Compiler atomics operate on
  // the existing uint32_t object without pretending it is a std::atomic.
  __atomic_fetch_or(&bits, mask, __ATOMIC_RELAXED);
#else
  std::atomic_ref<uint32_t>(bits).fetch_or(mask, std::memory_order_relaxed);
#endif
  }

static void bitSet(std::vector<uint32_t>& b, size_t id) {
  auto& bits = b[id/32];
  id %= 32;
  atomicOr(bits, 1u << id);
  }

static void bitSetRange(std::vector<uint32_t>& b, size_t first, size_t count) {
  while(count>0) {
    const size_t shift = first%32;
    const size_t take = std::min(count, 32-shift);
    const uint32_t mask = (UINT32_MAX >> (32-take)) << shift;
    // Different animated objects can share a dirty word. Combine this object's
    // contiguous blocks without losing updates from the other worker threads.
    atomicOr(b[first/32], mask);
    first += take;
    count -= take;
    }
  }

static bool bitAt(std::vector<uint32_t>& b, size_t id) {
  auto bits = b[id/32];
  id %= 32;
  return bits & (1u << id);
  }

using namespace Tempest;

InstanceStorage::Id::Id(Id&& other) noexcept
  :owner(other.owner), rgn(other.rgn) {
  other.owner = nullptr;
  }

InstanceStorage::Id& InstanceStorage::Id::operator = (Id&& other) noexcept {
  std::swap(owner, other.owner);
  std::swap(rgn,   other.rgn);
  return *this;
  }

InstanceStorage::Id::~Id() {
  if(owner!=nullptr)
    owner->free(rgn);
  }

void InstanceStorage::Id::set(const Tempest::Matrix4x4* mat) {
  if(owner==nullptr)
    return;

  owner->invalidateCpu();
  auto data = reinterpret_cast<Matrix4x4*>(owner->dataCpu.data() + rgn.begin);
  std::memcpy(data, mat, rgn.asize);

  bitSetRange(owner->durty, rgn.begin/blockSz, (rgn.asize+blockSz-1)/blockSz);
  }

void InstanceStorage::Id::set(const Tempest::Matrix4x4& obj, size_t offset) {
  if(owner==nullptr)
    return;

  auto data = reinterpret_cast<Matrix4x4*>(owner->dataCpu.data() + rgn.begin);
  if(data[offset] == obj)
    return;
  owner->invalidateCpu();
  data[offset] = obj;
  bitSet(owner->durty, (rgn.begin+offset*sizeof(Matrix4x4))/blockSz);
  }

void InstanceStorage::Id::set(const void* data, size_t offset, size_t size) {
  if(owner==nullptr)
    return;

  auto src = reinterpret_cast<const uint8_t*>(data);
  auto dst = (owner->dataCpu.data() + rgn.begin + offset);

  if(std::memcmp(src, dst, size)==0)
    return;

  owner->invalidateCpu();
  std::memcpy(dst, src, size);
  const size_t first = (rgn.begin+offset)/blockSz;
  const size_t last  = (rgn.begin+offset+size-1)/blockSz;
  bitSetRange(owner->durty, first, last-first+1);
  }


void InstanceStorage::Id::copyCpu(void* data,size_t offset,size_t size) const {
  assert(owner!=nullptr && offset<=rgn.asize && size<=rgn.asize-offset);
  std::memcpy(data,owner->dataCpu.data()+rgn.begin+offset,size);
  }

InstanceStorage::InstanceStorage() {
  dataCpu.reserve(131072);
  dataCpu.resize(sizeof(Matrix4x4)); // also avoid null-ssbo
  reinterpret_cast<Matrix4x4*>(dataCpu.data())->identity();

  patchCpu.reserve(4*1024*1024);
  patchBlock.reserve(16*1024);

  uploadTh = std::thread([this](){ uploadMain(); });
  }

InstanceStorage::~InstanceStorage() {
  {
    std::unique_lock<std::mutex> lck(sync);
    uploadFId = Resources::MaxFramesInFlight;
  }
  uploadCnd.notify_one();
  uploadTh.join();
  }

bool InstanceStorage::commit(Encoder<CommandBuffer>& cmd, uint8_t fId) {
  auto& device = Resources::device();

  std::atomic_thread_fence(std::memory_order_acquire);
  join();
  commitPackMs=0;
  commitPrepared=false;

  const size_t dataSize = (dataCpu.size() + 0xFFF) & ~size_t(0xFFF);
  if(dataGpu.byteSize()!=dataSize) {
    invalidateCpu();
    Resources::recycle(std::move(dataGpu));
    dataGpu = device.ssbo(BufferHeap::Device,Tempest::Uninitialized,dataSize);
    dataGpu.update(dataCpu);
    std::memset(durty.data(), 0, durty.size()*sizeof(uint32_t));
    return true;
    }

  commitPrepared=cpuPrepared();
  if(!commitPrepared) {
    const auto start=std::chrono::steady_clock::now();
    prepareCpu();
    commitPackMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
  invalidateCpu();
  if(patchBlock.empty())
    return false;

  auto& path = patchGpu[fId];
  if(path.byteSize() < patchCpu.size()) {
    path = device.ssbo(BufferHeap::Upload, Uninitialized, patchCpu.size());
    }

  {
    std::unique_lock<std::mutex> lck(sync);
    uploadFId = fId;
  }
  uploadCnd.notify_one();

  cmd.setFramebuffer({});
  cmd.setBinding(0, dataGpu);
  cmd.setBinding(1, path);
  cmd.setPipeline(Shaders::inst().patch);
  cmd.dispatch(patchBlock.size());
  std::memset(durty.data(), 0, durty.size()*sizeof(durty[0]));
  return false;
  }

void InstanceStorage::prepareCpu() {
  // The prior command was submitted only after this worker completed. Still
  // join here for non-VR callers and retry paths before reusing its CPU vector.
  join();
  invalidateCpu();
  dirtySnapshot=durty;
  patchBlock.clear();
  size_t payloadSize = 0;
  for(size_t i = 0; i<blockCnt; ++i) {
    if(i%32==0 && dirtySnapshot[i/32]==0) {
      i+=31;
      continue;
      }
    if(!bitAt(dirtySnapshot,i))
      continue;
    auto begin = i; ++i;
    while(i<blockCnt) {
      if(!bitAt(dirtySnapshot,i))
        break;
      ++i;
      }

    uint32_t size    = uint32_t((i-begin)*blockSz);
    uint32_t chunkSz = 256;

    Path p = {};
    p.dst  = uint32_t(begin*blockSz);
    p.src  = uint32_t(payloadSize);
    while(size>0) {
      p.size       = std::min<uint32_t>(size, chunkSz);
      size        -= p.size;
      patchBlock.push_back(p);

      payloadSize += p.size;
      p.dst       += p.size;
      p.src       += p.size;
      }
    }
  if(patchBlock.empty()) {
    patchCpu.clear();
    prepared.store(true,std::memory_order_relaxed);
    return;
    }

  const size_t headerSize = patchBlock.size()*sizeof(Path);
  patchCpu.resize(headerSize + payloadSize);
  for(auto& i:patchBlock) {
    i.src += uint32_t(headerSize);
    std::memcpy(patchCpu.data()+i.src, dataCpu.data() + i.dst, i.size);

    // uint's in shader
    i.src  /= 4;
    i.dst  /= 4;
    i.size /= 4;
    }
  std::memcpy(patchCpu.data(), patchBlock.data(), headerSize);
  // Preserve live dirty bits until commit. Repeated/abandoned snapshots and
  // allocation failures cannot consume updates that were never submitted.
  prepared.store(true,std::memory_order_relaxed);
  }

void InstanceStorage::join() {
  std::unique_lock<std::mutex> lck(sync);
  uploadDone.wait(lck, [this](){ return uploadFId<0; });
  }

InstanceStorage::Id InstanceStorage::alloc(const size_t size) {
  if(size==0)
    return Id(*this,Range());

  invalidateCpu();
  const auto nsize = alignAs(nextPot(uint32_t(size)), alignment);
  for(size_t i=0; i<rgn.size(); ++i) {
    if(rgn[i].size==nsize) {
      auto ret = rgn[i];
      ret.asize = size;
      rgn.erase(rgn.begin()+intptr_t(i));
      return Id(*this,ret);
      }
    }
  size_t retId = size_t(-1);
  for(size_t i=0; i<rgn.size(); ++i) {
    if(rgn[i].size>nsize && (retId==size_t(-1) || rgn[i].size<rgn[retId].size)) {
      retId = i;
      }
    }
  if(retId!=size_t(-1)) {
    Range ret = rgn[retId];
    ret.size  = nsize;
    ret.asize = size;
    rgn[retId].begin += nsize;
    rgn[retId].size  -= nsize;
    return Id(*this,ret);
    }
  Range r;
  r.begin = dataCpu.size();
  r.size  = nsize;
  r.asize = size;

  dataCpu.resize(dataCpu.size() + nsize);

  blockCnt = (dataCpu.size()+blockSz-1)/blockSz;
  durty.resize((blockCnt+32-1)/32, 0);
  return Id(*this,r);
  }

bool InstanceStorage::realloc(Id& id, const size_t size) {
  invalidateCpu();
  if(size==0) {
    if(id.isEmpty())
      return false;
    id = Id(*this,Range());
    return true;
    }

  if(size<=id.rgn.size) {
    id.rgn.asize = size;
    return false;
    }

  auto next = alloc(size);
  if(id.isEmpty()) {
    id = std::move(next);
    return true;
    }

  auto data = dataCpu.data();
  std::memcpy(data+next.rgn.begin, data+id.rgn.begin, id.rgn.asize);
  for(size_t i=0; i<id.rgn.asize; ++i) {
    bitSet(durty, (next.rgn.begin + i)/blockSz);
    }
  id = std::move(next);
  return true;
  }

const Tempest::StorageBuffer& InstanceStorage::ssbo() const {
  return dataGpu;
  }

void InstanceStorage::free(const Range& r) {
  invalidateCpu();
  for(auto& i:rgn) {
    if(i.begin+i.size==r.begin) {
      i.size  += r.size;
      return;
      }
    if(r.begin+r.size==i.begin) {
      i.begin -= r.size;
      i.size  += r.size;
      return;
      }
    }
  auto at = std::lower_bound(rgn.begin(),rgn.end(),r,[](const Range& l, const Range& r){
    return l.begin<r.begin;
    });
  rgn.insert(at,r);
  }

void InstanceStorage::uploadMain() {
  Workers::setThreadName("InstanceStorage upload");
  while(true) {
    std::unique_lock<std::mutex> lck(sync);
    // A job (or shutdown) may have been posted before the worker began waiting.
    uploadCnd.wait(lck, [this](){ return uploadFId>=0; });
    if(uploadFId==Resources::MaxFramesInFlight)
      break;
    if(uploadFId<0)
      continue;

    patchGpu[uploadFId].update(patchCpu);
    uploadFId = -1;
    lck.unlock();
    uploadDone.notify_all();
    }
  }
