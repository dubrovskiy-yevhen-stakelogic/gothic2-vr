#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

// View-independent candidates only. Shading still reads each stable light slot
// and evaluates its current radius/color; a grid entry never caches radiance.
namespace LightGrid {

constexpr float CellSize = 800.f;
constexpr uint32_t MaxCells = 65536;
constexpr uint32_t MaxIds = 1048576;
constexpr uint32_t MaxCellsPerLight = 4096;
constexpr int32_t MaxCoordinate = 1048576;

struct Entry {
  int32_t x = 0, y = 0, z = 0;
  uint32_t offset = 0;
  uint32_t count = 0;
  uint32_t padding[3] = {};
  };
static_assert(sizeof(Entry)==32 && offsetof(Entry,count)==16);

struct Cell {
  int32_t x, y, z;
  bool operator == (const Cell& b) const { return x==b.x && y==b.y && z==b.z; }
  };

inline uint32_t hash(const Cell& c) {
  return uint32_t(c.x)*73856093u ^ uint32_t(c.y)*19349663u ^ uint32_t(c.z)*83492791u;
  }

struct CellHash {
  size_t operator () (const Cell& c) const { return hash(c); }
  };

inline float envelope(float range) {
  // Non-finite inputs must take the fallback path, including +infinity which
  // std::min would otherwise turn into an apparently finite envelope.
  return std::isfinite(range) ? std::min(range,2000.f) : range;
  }

// A slot is static until it changes AFTER inclusion in a built snapshot. Its
// initial placement during loading is not motion. Promotion lasts until free.
struct Slot {
  bool active = false;
  bool built = false;
  bool fallback = false;
  float builtEnvelope = 0;

  bool moved() {
    if(!built || fallback)
      return false;
    fallback = true;
    return true;
    }

  bool rangeChanged(float range) {
    if(!built || fallback)
      return false;
    if(std::isfinite(range) && std::isfinite(builtEnvelope) && range<=builtEnvelope)
      return false;
    fallback = true;
    return true;
    }
  };

struct Sphere {
  float x, y, z, radius;
  uint32_t id;
  bool fallback = false;
  };

struct Data {
  std::vector<Entry> table;
  std::vector<uint32_t> ids;
  std::vector<uint32_t> fallback;
  uint32_t cells = 0;

  uint32_t mask() const { return uint32_t(table.size()-1); }

  const Entry* find(const Cell& c) const {
    if(table.empty())
      return nullptr;
    uint32_t slot = hash(c)&mask();
    for(size_t i=0; i<table.size(); ++i) {
      const auto& e = table[slot];
      if(e.count==0)
        return nullptr;
      if(e.x==c.x && e.y==c.y && e.z==c.z)
        return &e;
      slot = (slot+1)&mask();
      }
    return nullptr;
    }
  };

inline bool sphereCells(const Sphere& sphere, std::vector<Cell>& cells) {
  cells.clear();
  if(!std::isfinite(sphere.x) || !std::isfinite(sphere.y) || !std::isfinite(sphere.z) ||
     !std::isfinite(sphere.radius))
    return false;
  if(sphere.radius<=0)
    return true;

  const double center[3] = {sphere.x,sphere.y,sphere.z};
  const double magnitude = std::max({std::abs(center[0]),std::abs(center[1]),std::abs(center[2]),
                                     double(sphere.radius),double(CellSize)});
  // Include tangencies and float rounding at cell boundaries. Inflation only
  // adds candidates; the fragment shader retains the exact attenuation test.
  const double radius = double(sphere.radius)+std::max(0.01,4*double(std::numeric_limits<float>::epsilon())*magnitude);
  int32_t lo[3], hi[3];
  uint64_t checks = 1;
  for(size_t i=0; i<3; ++i) {
    const double low = std::floor((center[i]-radius)/CellSize);
    const double high = std::floor((center[i]+radius)/CellSize);
    if(low < -MaxCoordinate || high > MaxCoordinate)
      return false;
    lo[i] = int32_t(low);
    hi[i] = int32_t(high);
    checks *= uint64_t(hi[i]-lo[i]+1);
    if(checks>MaxCellsPerLight)
      return false;
    }

  const double radius2 = radius*radius;
  for(int32_t z=lo[2]; z<=hi[2]; ++z)
    for(int32_t y=lo[1]; y<=hi[1]; ++y)
      for(int32_t x=lo[0]; x<=hi[0]; ++x) {
        const Cell cell{x,y,z};
        const int32_t coord[3] = {x,y,z};
        double distance2 = 0;
        for(size_t i=0; i<3; ++i) {
          const double low = double(coord[i])*CellSize, high = low+CellSize;
          const double delta = center[i]<low ? low-center[i] : (center[i]>high ? center[i]-high : 0);
          distance2 += delta*delta;
          }
        if(distance2<=radius2)
          cells.push_back(cell);
        }
  return true;
  }

inline Data build(const std::vector<Sphere>& lights, uint32_t maxCells=MaxCells, uint32_t maxIds=MaxIds) {
  Data result;
  maxCells = std::min(maxCells,MaxCells);
  maxIds = std::min(maxIds,MaxIds);
  std::unordered_map<Cell,std::vector<uint32_t>,CellHash> cells;
  std::vector<Cell> candidate;
  size_t idCount = 0;
  for(const auto& light:lights) {
    if(light.fallback || !sphereCells(light,candidate)) {
      result.fallback.push_back(light.id);
      continue;
      }
    size_t newCells = 0;
    for(const auto& cell:candidate)
      newCells += cells.find(cell)==cells.end() ? 1u : 0u;
    if(cells.size()+newCells>maxCells || idCount+candidate.size()>maxIds) {
      // Decide before inserting ANY cell: each ID belongs to exactly one
      // candidate source, so fallback never doubles its light energy.
      result.fallback.push_back(light.id);
      continue;
      }
    for(const auto& cell:candidate)
      cells[cell].push_back(light.id);
    idCount += candidate.size();
    }

  result.cells = uint32_t(cells.size());
  size_t tableSize = 2;
  while(tableSize<cells.size()*2)
    tableSize *= 2;
  result.table.resize(tableSize);
  result.ids.reserve(idCount);
  for(const auto& [cell,ids]:cells) {
    uint32_t slot = hash(cell)&result.mask();
    while(result.table[slot].count!=0)
      slot = (slot+1)&result.mask();
    auto& entry = result.table[slot];
    entry.x = cell.x;
    entry.y = cell.y;
    entry.z = cell.z;
    entry.offset = uint32_t(result.ids.size());
    entry.count = uint32_t(ids.size());
    result.ids.insert(result.ids.end(),ids.begin(),ids.end());
    }
  return result;
  }

}
