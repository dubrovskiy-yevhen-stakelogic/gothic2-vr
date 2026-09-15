#pragma once

#include <Tempest/Device>
#include <Tempest/Matrix4x4>
#include <Tempest/UniformBuffer>

#include "graphics/visualobjects.h"
#include "graphics/mesh/submesh/staticmesh.h"

class PackedMesh;

class Landscape final {
  public:
    Landscape(VisualObjects& visual, const PackedMesh& wmesh);

    const Tempest::StorageBuffer& bvh()   const { return bvhNodes; }
    // Number of clustered terrain LOD levels packed after the full detail (0 or 2).
    uint8_t lodLevels() const { return levels; }
    // Luminance of a fully lit vertex of the Spacer bake (0 = no bake), vr/vrbakedlight.h.
    float   bakedLit() const { return lit; }

  private:
    using Item = VisualObjects::Item;

    struct Block {
      Item mesh;
      };

    std::vector<Block>     blocks;
    StaticMesh             mesh;
    uint8_t                levels = 0;
    float                  lit = 0;
    Tempest::StorageBuffer meshletDesc;

    Tempest::StorageBuffer bvhNodes;
  };
