#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_samplerless_texture_functions : enable
#include "scene.glsl"
#include "common.glsl"
#include "lighting/tonemapping.glsl"

layout(early_fragment_tests) in;
layout(location=0) out vec4 outColor;
layout(binding=0,std140) uniform UboScene { SceneDesc scene; };
layout(binding=1) uniform texture2D gbufDiffuse;
layout(binding=2) uniform utexture2D gbufNormal;
layout(binding=3) uniform texture2D depth;
layout(binding=4,std430) readonly buffer Lights { LightSource lights[]; };
struct Cell { ivec3 key; uint offset; uint count; uint pad0; uint pad1; uint pad2; };
layout(binding=5,std430) readonly buffer Grid { Cell cells[]; };
layout(binding=6,std430) readonly buffer Indices { uint ids[]; };
layout(binding=7,std430) readonly buffer Fallback { uint fallbackIds[]; };
layout(push_constant,std430) uniform Push {
  vec3 origin;
  uint tableMask;
  uint fallbackCount;
  float cellSize;
  uint lightCount;
} push;

void main() {
  const ivec2 pixel=ivec2(gl_FragCoord.xy);
  const float z=texelFetch(depth,pixel,0).x;
  if(z==1.0) { outColor=vec4(0); return; }
  const vec2 scr=(gl_FragCoord.xy/vec2(textureSize(depth,0)))*2.0-1.0;
  const vec4 p=scene.viewProjectLwcInv*vec4(scr,z,1.0);
  const vec3 pos=p.xyz/p.w+push.origin;
  const vec3 cellPosition=floor(pos/push.cellSize);
  const bool allLights=any(isnan(cellPosition)) || any(isinf(cellPosition)) ||
                       any(greaterThanEqual(abs(cellPosition),vec3(2147483000.0)));
  uint offset=0u, count=0u;
  if(!allLights) {
    const ivec3 key=ivec3(cellPosition);
    uint slot=(uint(key.x)*73856093u ^ uint(key.y)*19349663u ^ uint(key.z)*83492791u)&push.tableMask;
    // The world table is shared by both eyes; only this surface position is
    // eye-dependent. Empty cells terminate probing without a GPU prepass.
    for(uint probe=0u;probe<=push.tableMask;++probe) {
      const Cell cell=cells[slot];
      if(cell.count==0u) break;
      if(all(equal(cell.key,key))) { offset=cell.offset; count=cell.count; break; }
      slot=(slot+1u)&push.tableMask;
    }
  }
  const uint total=allLights?push.lightCount:count+push.fallbackCount;
  if(total==0u) { outColor=vec4(0); return; }
  const vec3 normal=normalFetch(gbufNormal,pixel);
  const vec3 linear=textureAlbedo(texelFetch(gbufDiffuse,pixel,0).xyz);
  vec3 sum=vec3(0);
  for(uint i=0u;i<total;++i) {
    const uint id=allLights?i:(i<count?ids[offset+i]:fallbackIds[i-count]);
    const LightSource source=lights[id];
    if(!(source.range>0.0)) continue;
    const vec3 ldir=pos-source.pos;
    const float factor=dot(ldir,ldir)/(source.range*source.range);
    if(factor>1.0) continue;
    const float smoothFactor=max(1.0-factor*factor,0.0);
    const float lambert=max(0.0,-dot(normalize(ldir),normal));
    const float light=(lambert/max(factor,0.005))*(smoothFactor*smoothFactor);
    if(light<=0.0) continue;
    sum+=linear*(source.color*light)*max(1.0,scene.exposure)*Fd_Lambert*0.25;
  }
  outColor=vec4(sum,0.0);
}
