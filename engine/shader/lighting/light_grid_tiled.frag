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
layout(binding=8,std430) readonly buffer Visible { LightSource visibleLights[]; };
layout(binding=9,std430) readonly buffer Tiles { uint tiles[]; };
layout(binding=10,std430) readonly buffer Arguments { uint args[]; };
layout(push_constant,std430) uniform Push {
  vec3 origin;
  uint tableMask;
  uint fallbackCount;
  float cellSize;
  uint globalLightCount;
  uint visibleCount;
} push;

void main() {
  const ivec2 pixel=ivec2(gl_FragCoord.xy);
  const ivec2 size=textureSize(depth,0);
  const float z=texelFetch(depth,pixel,0).x;
  if(z==1.0) { outColor=vec4(0); return; }
  const uint tileWidth=uint((size.x+15)/16);
  const uint base=(uint(pixel.y/16)*tileWidth+uint(pixel.x/16))*65u;
  const uint tileCount=tiles[base];
  const bool overflow=tileCount>64u;
  const uint count=overflow?args[1]:tileCount;
  if(count==0u) { outColor=vec4(0); return; }
  const vec2 scr=(gl_FragCoord.xy/vec2(size))*2.0-1.0;
  const vec4 p=scene.viewProjectLwcInv*vec4(scr,z,1.0);
  const vec3 pos=p.xyz/p.w+push.origin;
  const vec3 normal=normalFetch(gbufNormal,pixel);
  const vec3 linear=textureAlbedo(texelFetch(gbufDiffuse,pixel,0).xyz);
  vec3 sum=vec3(0);
  for(uint i=0u;i<count;++i) {
    const uint id=overflow?(0x80000000u|i):tiles[base+1u+i];
    LightSource source;
    if((id&0x80000000u)!=0u) source=visibleLights[id&0x7fffffffu];
    else source=lights[id];
    if(!(source.range>0.0)) continue;
    const vec3 ldir=pos-source.pos;
    const float factor=dot(ldir,ldir)/(source.range*source.range);
    if(factor>1.0) continue;
    const float smoothFactor=max(1.0-factor*factor,0.0);
    const float lambert=max(0.0,-dot(normalize(ldir),normal));
    const float light=(lambert/max(factor,0.005))*(smoothFactor*smoothFactor);
    if(light<=0.0) continue;
    // The same FP32 light equation and single HDR blend as light_tiled.frag.
    sum+=linear*(source.color*light)*max(1.0,scene.exposure)*Fd_Lambert*0.25;
  }
  outColor=vec4(sum,0.0);
}
