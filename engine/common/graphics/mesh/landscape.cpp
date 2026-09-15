#include "landscape.h"

#include <Tempest/Log>
#include <cstddef>

#include "graphics/mesh/submesh/packedmesh.h"
#include "gothic.h"

using namespace Tempest;

Landscape::Landscape(VisualObjects& visual, const PackedMesh &packed)
  :mesh(packed,false), lit(packed.bakedLit) {
  auto& device = Resources::device();

  meshletDesc = Resources::ssbo(packed.meshletBounds.data(), packed.meshletBounds.size()*sizeof(packed.meshletBounds[0]));
  bvhNodes    = Resources::ssbo(packed.bvhNodes.data(),  packed.bvhNodes.size()*sizeof(packed.bvhNodes[0]));
  //bvhNodes    = Resources::ssbo(packed.bvh8Nodes.data(), packed.bvh8Nodes.size()*sizeof(packed.bvh8Nodes[0]));

  uint32_t lodMeshlets[4] = {};
  blocks.reserve(packed.subMeshes.size());
  for(size_t i=0; i<packed.subMeshes.size(); ++i) {
    auto& sub      = packed.subMeshes[i];
    if(sub.lod<4)
      lodMeshlets[sub.lod] += uint32_t(sub.iboLength/PackedMesh::MaxInd);
    if(sub.lod>=1 && sub.lod<=2)
      levels = std::max<uint8_t>(levels, sub.lod);
    auto  id       = uint32_t(sub.iboOffset/PackedMesh::MaxInd);
    auto  material = Resources::loadMaterial(sub.material,true);

    if(material.alpha==Material::AdditiveLight || sub.iboLength==0) {
      continue;
      }

    if(Gothic::options().doRayQuery) {
      mesh.sub[i].blas = device.blas(mesh.vbo,mesh.ibo,sub.iboOffset,sub.iboLength);
      }

    Block b;
    b.mesh = visual.get(mesh,material,sub.iboOffset,sub.iboLength,&packed.meshletBounds[id],DrawCommands::Landscape,sub.lod);
    b.mesh.setObjMatrix(Matrix4x4::mkIdentity());
    blocks.emplace_back(std::move(b));
    }
  if(levels>0)
    Log::i("VR terrain LOD meshlets: full=",lodMeshlets[0]," boundary=",lodMeshlets[PackedMesh::LodBoundary],
           " level1=",lodMeshlets[1]," level2=",lodMeshlets[2]," tile=",int(PackedMesh::LodTile),"cm");
  }
