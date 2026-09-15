#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_samplerless_texture_functions : enable
#include "scene.glsl"
#include "common.glsl"
#include "lighting/tonemapping.glsl"

// Fullscreen local lighting over the 128-bit per-tile masks written by
// light_tiles_slab.comp. Same light equation, statement order and single HDR
// blend as light_tiled.frag; only the per-tile list encoding differs. Lights
// are visited in ascending compact index, so the FP32 sum order is
// deterministic and equals the complete-list route for every contributor.
// Pixels of tiles without lights exit after one depth fetch and one mask read;
// on Adreno that is cheaper than rasterising lit tiles as separate quads.
layout(early_fragment_tests) in;
layout(location=0) out vec4 outColor;
layout(binding=0,std140) uniform UboScene { SceneDesc scene; };
layout(binding=1) uniform texture2D gbufDiffuse;
layout(binding=2) uniform utexture2D gbufNormal;
layout(binding=3) uniform texture2D depth;
#if defined(UNIFORM_LIGHTS)
layout(binding=4,std140) uniform Lights { LightSource lights[2048]; };
#else
layout(binding=4,std430) readonly buffer Lights { LightSource lights[]; };
#endif
layout(binding=5,std430) readonly buffer SlabArguments { uint slabArgs[]; }; // [0] slab light count
layout(binding=6,std430) readonly buffer Tiles { uint tiles[]; };
layout(push_constant,std430) uniform Push { vec3 origin; } push;

const uint MaskWords=4u;
const uint MaskBits=MaskWords*32u;
const uint TileStride=MaskWords+1u;

// Identical statements to light_tiled.frag's loop body, kept as a macro so
// both routes compile the same expression tree.
#define ACCUMULATE_LIGHT(source) \
  { \
    const vec3 ldir=pos-source.pos; \
    const float factor=dot(ldir,ldir)/(source.range*source.range); \
    if(factor>1.0) continue; \
    const float smoothFactor=max(1.0-factor*factor,0.0); \
    const float lambert=max(0.0,-dot(normalize(ldir),normal)); \
    const float light=(lambert/max(factor,0.005))*(smoothFactor*smoothFactor); \
    if(light<=0.0) continue; \
    sum+=linear*(source.color*light)*max(1.0,scene.exposure)*Fd_Lambert*0.25; \
  }

void main() {
  const ivec2 pixel=ivec2(gl_FragCoord.xy);
  const ivec2 size=textureSize(depth,0);
  const float z=texelFetch(depth,pixel,0).x;
  if(z==1.0) { outColor=vec4(0); return; }
  const uint tileWidth=uint((size.x+15)/16);
  const uint base=(uint(pixel.y/16)*tileWidth+uint(pixel.x/16))*TileStride;
  // One dependent load decides the early exit: the builder writes a zero count
  // for tiles without lights (and for zero lights), 0xffffffff on the
  // complete-list route, so the argument buffer is read only for lit pixels.
  uint set=tiles[base];
  if(set==0u) { outColor=vec4(0); return; }
  const uint total=slabArgs[0];
  if(total==0u) { outColor=vec4(0); return; }
  const bool overflow=total>MaskBits;
  uint words[MaskWords];
  if(overflow) set=total;
  else for(uint w=0u;w<MaskWords;++w) words[w]=tiles[base+1u+w];
  const vec2 scr=(gl_FragCoord.xy/vec2(size))*2.0-1.0;
  const vec4 p=scene.viewProjectLwcInv*vec4(scr,z,1.0);
  const vec3 pos=p.xyz/p.w+push.origin;
  const vec3 normal=normalFetch(gbufNormal,pixel);
  const vec3 linear=textureAlbedo(texelFetch(gbufDiffuse,pixel,0).xyz);
  vec3 sum=vec3(0);
  if(overflow) {
    for(uint i=0u;i<total;++i) {
      const LightSource source=lights[i];
      ACCUMULATE_LIGHT(source)
      }
    } else {
    if(set*2u>=total) {
      // Dense mask: a uniform sequential loop with one bit test per light beats
      // the data-dependent bit walk. Same ascending order, same sum.
      for(uint w=0u;w<MaskWords;++w) {
        const uint bits=words[w];
        if(bits==0u) continue;
        for(uint b=0u;b<32u;++b) {
          const uint i=w*32u+b;
          if(i>=total) break;
          if((bits&(1u<<b))==0u) continue;
          const LightSource source=lights[i];
          ACCUMULATE_LIGHT(source)
          }
        }
      } else {
      for(uint w=0u;w<MaskWords;++w) {
        uint bits=words[w];
        while(bits!=0u) {
          const uint i=w*32u+uint(findLSB(bits));
          bits&=bits-1u;
          const LightSource source=lights[i];
          ACCUMULATE_LIGHT(source)
          }
        }
      }
    }
  outColor=vec4(sum,0.0);
  }
