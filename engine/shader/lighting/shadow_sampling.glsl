#ifndef SHADOWSAMPLING_GLSL
#define SHADOWSAMPLING_GLSL

#include "scene.glsl"

vec4 shadowSample(in sampler2D shadowMap, vec2 shPos) {
  shPos.xy = shPos.xy*vec2(0.5,0.5)+vec2(0.5);
  return textureGather(shadowMap,shPos);
  }

vec4 shadowSample(in sampler2D shadowMap, vec2 shPos, out vec2 m) {
  shPos.xy = shPos.xy*vec2(0.5,0.5)+vec2(0.5);
  m        = fract(shPos.xy * textureSize(shadowMap, 0) - vec2(0.5));
  return textureGather(shadowMap,shPos);
  }

float shadowResolve(in vec4 sh, float z) {
  z  = max(0,z);
  sh = step(sh,vec4(z));
  return 0.25*(sh.x+sh.y+sh.z+sh.w);
  }

float shadowResolve(in vec4 sh, float z, vec2 m) {
  const float bias = 0.0002;
  z  = max(0, z);
  sh = step(sh, vec4(z));

  vec2 xx = mix(sh.wz, sh.xy, m.y);
  return    mix(xx.x,  xx.y,  m.x);
  }

float calcShadowMs4(in sampler2D shadowMap0, vec3 shPos0) {
  vec2  bias = vec2(1)/vec2(textureSize(shadowMap0, 0));
  vec4  lay0 = vec4(0);
  float ret  = 0;

  vec2  m0;
  lay0 = shadowSample(shadowMap0,shPos0.xy + bias*vec2(-1.5,  0.5), m0);
  ret += shadowResolve(lay0,shPos0.z,m0);
  lay0 = shadowSample(shadowMap0,shPos0.xy + bias*vec2( 0.5,  0.5), m0);
  ret += shadowResolve(lay0,shPos0.z,m0);
  lay0 = shadowSample(shadowMap0,shPos0.xy + bias*vec2(-1.5, -1.5), m0);
  ret += shadowResolve(lay0,shPos0.z,m0);
  lay0 = shadowSample(shadowMap0,shPos0.xy + bias*vec2( 0.5, -1.5), m0);
  ret += shadowResolve(lay0,shPos0.z,m0);

  return ret * 0.25;
  }

float calcShadow(in SceneDesc scene,
                 in sampler2D shadowMap0, vec3 shPos0,
                 in sampler2D shadowMap1, vec3 shPos1) {
  if(scene.vrShadowParams.x>0.5) {
    // VR cached shadow: map 0 holds the animated casters of this frame, map 1
    // the cached static world; both are world-aligned and independent, so a
    // point is lit only where both agree. The static map fades out towards
    // its edge instead of ending in a hard line.
    float ret = 1.0;
    if(abs(shPos0.x)<0.99 && abs(shPos0.y)<0.99)
      ret = calcShadowMs4(shadowMap0, shPos0);
    const float edge = max(abs(shPos1.x), abs(shPos1.y));
    if(edge<1.0) {
      const float fade = clamp((1.0-edge)/max(1.0-scene.vrShadowParams.y, 0.001), 0.0, 1.0);
      ret = min(ret, mix(1.0, calcShadowMs4(shadowMap1, shPos1), fade));
      }
    return ret;
    }
  if(abs(shPos0.x)<0.99 && abs(shPos0.y)<0.99) {
    // This lookup only selects the close cascade; outside its bounds the
    // result is unused. Keep the existing PCF samples and arithmetic intact.
    vec2 minMax = scene.closeupShadowSlice;
    vec2 m1;
    vec4 lay1   = shadowSample(shadowMap1,shPos1.xy, m1);
    lay1.x = max(max(lay1.x,lay1.y), max(lay1.z,lay1.w));
    if(lay1.x<minMax[1])
      return calcShadowMs4(shadowMap0,shPos0);
    }

  if(abs(shPos1.x)<1.0 && abs(shPos1.y)<1.0) {
    return calcShadowMs4(shadowMap1,shPos1);
    }
  return 1.0;
  }

float calcShadow(in vec3 pos, const vec3 normal, in SceneDesc scene, in sampler2D shadowMap0, in sampler2D shadowMap1) {
  vec4 shadowPos[2];
#if defined(LWC)
  shadowPos[0] = scene.viewShadowLwc[0]*vec4(pos + normal*5,  1.0);
  shadowPos[1] = scene.viewShadowLwc[1]*vec4(pos + normal*25, 1.0);
#else
  shadowPos[0] = scene.viewShadow   [0]*vec4(pos + normal*5, 1.0);
  shadowPos[1] = scene.viewShadow   [1]*vec4(pos + normal*25, 1.0);
#endif

  vec3 shPos0  = (shadowPos[0].xyz)/shadowPos[0].w;
  vec3 shPos1  = (shadowPos[1].xyz)/shadowPos[1].w;
  return calcShadow(scene, shadowMap0,shPos0, shadowMap1,shPos1);
  }

float calcShadow(in vec4 pos4, in float bias, in SceneDesc scene, in sampler2D shadowMap0, in sampler2D shadowMap1) {
  vec4 shadowPos[2];
  vec3 offset = bias*scene.sunDir*pos4.w;
#if defined(LWC)
  shadowPos[0] = scene.viewShadowLwc[0]*vec4(pos4.xyz + offset*1.0, pos4.w);
  shadowPos[1] = scene.viewShadowLwc[1]*vec4(pos4.xyz + offset*3.0, pos4.w);
#else
  shadowPos[0] = scene.viewShadow   [0]*vec4(pos4.xyz + offset*1.0, pos4.w);
  shadowPos[1] = scene.viewShadow   [1]*vec4(pos4.xyz + offset*3.0, pos4.w);
#endif

  vec3 shPos0  = (shadowPos[0].xyz)/shadowPos[0].w;
  vec3 shPos1  = (shadowPos[1].xyz)/shadowPos[1].w;
  return calcShadow(scene, shadowMap0,shPos0, shadowMap1,shPos1);
  }

#if defined(VR_SHADOW_TILES)
// VR shadow tiles (lighting/shadow_tiles.comp): tiles.xy = the
// min/max depth over the receiver's 8x8-texel tile plus a 4-texel margin, which
// holds every texel the four PCF gathers can read (edge-clamped like the
// ClampToEdge sampler). With z >= max every step() is 1, with z < min every
// step() is 0, so the early-out is the PCF's own value; mixed tiles run the
// unchanged calcShadowMs4.
const uint VR_SHADOW_SKIP_DYNAMIC = 1u; // map 0 has no caster this frame (cleared): every tap lit
const uint VR_SHADOW_SKIP_STATIC  = 2u; // map 1 holds the night clear: every tap lit

float calcShadowMs4Tiled(in sampler2D shadowMap, in sampler2D tiles, vec3 shPos) {
  const vec2  u  = (shPos.xy*vec2(0.5,0.5)+vec2(0.5)) * vec2(textureSize(shadowMap, 0));
  const ivec2 t  = clamp(ivec2(floor(u*0.125)), ivec2(0), textureSize(tiles, 0)-ivec2(1));
  const vec2  mm = texelFetch(tiles, t, 0).xy;
  const float z  = max(0, shPos.z);
  if(z>=mm.y)
    return 1.0;
  if(z<mm.x)
    return 0.0;
  return calcShadowMs4(shadowMap, shPos);
  }

// The VR branch of calcShadow with the tile early-out. A dynamic result of 0 is
// final: min(0, mix(1, s, fade)) is 0 for any s and fade in [0,1].
float calcShadowTiled(in SceneDesc scene,
                      in sampler2D shadowMap0, in sampler2D tiles0, vec3 shPos0,
                      in sampler2D shadowMap1, in sampler2D tiles1, vec3 shPos1, uint flags) {
  if(scene.vrShadowParams.x>0.5) {
    float ret = 1.0;
    if((flags & VR_SHADOW_SKIP_DYNAMIC)==0u && abs(shPos0.x)<0.99 && abs(shPos0.y)<0.99)
      ret = calcShadowMs4Tiled(shadowMap0, tiles0, shPos0);
    if(ret==0.0 || (flags & VR_SHADOW_SKIP_STATIC)!=0u)
      return ret;
    const float edge = max(abs(shPos1.x), abs(shPos1.y));
    if(edge<1.0) {
      const float fade = clamp((1.0-edge)/max(1.0-scene.vrShadowParams.y, 0.001), 0.0, 1.0);
      ret = min(ret, mix(1.0, calcShadowMs4Tiled(shadowMap1, tiles1, shPos1), fade));
      }
    return ret;
    }
  return calcShadow(scene, shadowMap0,shPos0, shadowMap1,shPos1);
  }

float calcShadowTiled(in vec3 pos, const vec3 normal, in SceneDesc scene,
                      in sampler2D shadowMap0, in sampler2D tiles0,
                      in sampler2D shadowMap1, in sampler2D tiles1, uint flags) {
  vec4 shadowPos[2];
#if defined(LWC)
  shadowPos[0] = scene.viewShadowLwc[0]*vec4(pos + normal*5,  1.0);
  shadowPos[1] = scene.viewShadowLwc[1]*vec4(pos + normal*25, 1.0);
#else
  shadowPos[0] = scene.viewShadow   [0]*vec4(pos + normal*5, 1.0);
  shadowPos[1] = scene.viewShadow   [1]*vec4(pos + normal*25, 1.0);
#endif

  vec3 shPos0  = (shadowPos[0].xyz)/shadowPos[0].w;
  vec3 shPos1  = (shadowPos[1].xyz)/shadowPos[1].w;
  return calcShadowTiled(scene, shadowMap0,tiles0,shPos0, shadowMap1,tiles1,shPos1, flags);
  }
#endif

#endif
