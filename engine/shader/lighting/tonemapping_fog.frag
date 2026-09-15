#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_control_flow_attributes : enable

// VR: the "Reflections+fog composite" pass folded into the tonemapping pass.
// Per pixel, in the order of the original passes: water reflections added
// (water/water_reflection.frag, blend One/One on water pixels), then the
// non-volumetric fog composite (sky/fog.frag, blend One/OneMinusSrcAlpha with
// (lum, 1-tr): color = lum + color*tr, horizon haze included), then the
// production tonemapping. Saves one load and one store of the eye image and
// a pass switch per eye; the intermediate R11G11B10 rounding of the old
// composite is gone (tools/test-fog-tone.ps1 measures the difference).
// Native resolution only (no upscale), camera above water only.

#include "scene.glsl"
#include "common.glsl"
#include "sky/sky_common.glsl"
#include "lighting/tonemapping.glsl"
#include "lighting/purkinje_shift.glsl"

layout(push_constant, std140) uniform PushConstant {
  VideoSettings settings; // hdr.y = hazeStart, hdr.z = hazeEnd (cm), hdr.x = hdr peak as before
  } push;

layout(binding  = 0, std140) uniform UboScene {
  SceneDesc scene;
  };
layout(binding  = 1) uniform sampler2D  textureD;     // scene colour after lights, sky, water, translucent
layout(binding  = 2) uniform sampler2D  depth;        // zbuffer
layout(binding  = 3) uniform sampler3D  fogLut;
layout(binding  = 4) uniform sampler2D  skyLUT;       // sky view LUT (haze)
layout(binding  = 5) uniform sampler2D  sceneColor;   // stash (SSR input; the SSR path is a stub today)
layout(binding  = 6) uniform sampler2D  gbufDiffuse;
layout(binding  = 7) uniform usampler2D gbufNormal;
layout(binding  = 8) uniform sampler2D  sceneDepth;   // stash depth
layout(binding  = 9) uniform sampler2D  skyCldLUT;    // sky+clouds view LUT (reflections)

layout(location = 0) in  vec2 uv;
layout(location = 0) out vec4 outColor;

const uvec3 TONEMAP_DITHER_TARGET_BITS = uvec3(8, 8, 8);

// ---- water reflections (water/water_reflection.frag, non-SSR result; the SSR variant returns the sky too)
float intersectPlane(const vec3 pos, const vec3 dir, const vec4 plane) {
  float dist = dot(vec4(pos,1.0), plane);
  float step = -dot(plane.xyz,dir);
  if(abs(step)<=0.0)
    return 0;
  return dist/step;
  }

vec3 sunBloom(vec3 refl) {
  const float sunSolidAngle  = 0.53*M_PI/180.0;
  const float minSunCosTheta = cos(sunSolidAngle);

  float cosTheta = dot(refl, scene.sunDir);
  if(cosTheta >= minSunCosTheta)
    return vec3(1.0);
  float offset = minSunCosTheta - cosTheta;
  float gaussianBloom = exp(-offset*50000.0)*0.5;
  float invBloom = 1.0/(0.02 + offset*300.0)*0.01;
  return vec3(gaussianBloom+invBloom);
  }

// Returns the additive reflection term of a water pixel (vec3(0) when none).
vec3 waterReflection(const vec4 diff) {
  if(!isGBufWater(diff.a))
    return vec3(0);
  const vec2  fragCoord = (gl_FragCoord.xy*scene.screenResInv)*2.0-vec2(1.0);
  const bool  underWater = (scene.underWater!=0);
  const float ior        = (underWater ? IorAir : IorWater);

  const float d       = texelFetch (depth,      ivec2(gl_FragCoord.xy), 0).r;
  const vec3  normal  = normalFetch(gbufNormal, ivec2(gl_FragCoord.xy));

  const vec4  start4  = scene.viewProjectInv*vec4(fragCoord.x, fragCoord.y, d, 1.0);
  const vec3  start   = start4.xyz/start4.w;
  const vec3  camPos  = scene.camPos;

  const vec3  view    = normalize(start - camPos);
        vec3  refl    = reflect(view, normal);
  if(refl.y<0 && !underWater) {
    refl.y = 0;
    refl   = normalize(refl);
    }

  const float f = fresnel(refl,normal,ior);
  if(f<=0.0001)
    return vec3(0);

  vec3 sky = vec3(0);
  if(!underWater) {
    vec3 sun = sunBloom(refl) * scene.sunColor;
    sky += textureSkyLUT(skyCldLUT, vec3(0,RPlanet,0), refl, scene.sunDir) * scene.GSunIntensity;
    sky *= scene.exposure;
    }
  return sky * WaterAlbedo * f;
  }

// ---- fog composite (sky/fog.frag, non-volumetric path)
vec3 project(mat4 m, vec3 pos) {
  vec4 p = m*vec4(pos,1);
  return p.xyz/p.w;
  }

vec4 fogSample(vec2 fuv, float z) {
  // Geometry samples the whole LQ volume (fogVolumeFar); the sky keeps the old
  // volume end dFogMax, which sky.frag subtracts, so sky and clouds are unchanged.
  const float dZ = linearDepth(z>=1.0 ? dFogMax : z, scene.clipInfo);
  const float d  = fogVolumeSlice(dZ, scene.clipInfo);
  return textureLod(fogLut, vec3(fuv,d), 0);
  }

vec3 fogComposite(vec3 color) {
  const ivec2 size  = textureSize(depth,0);
  const vec2  inPos = (vec2(gl_FragCoord.xy)/vec2(size))*2.0 - vec2(1.0);
  const vec2  fuv   = vec2(gl_FragCoord.xy)/vec2(size);
  const vec3  sunDir = scene.sunDir;

  const float dMax   = texelFetch(depth,ivec2(gl_FragCoord.xy),0).r;
  const bool  isSky  = (dMax==1.0);
  const vec4  val    = fogSample(fuv,dMax);

  vec3  lum = val.rgb;
  float tr  = isSky ? 1 : val.a;

  const float hazeStart = push.settings.hdr.y, hazeEnd = push.settings.hdr.z;
  if(!isSky && hazeEnd>hazeStart) {
    const float dist = linearDepth(dMax, scene.clipInfo);
    const float f    = smoothstep(hazeStart, hazeEnd, dist);
    if(f>0.0) {
      const vec3 dir    = normalize(project(scene.viewProjectLwcInv, vec3(inPos,1.0)));
      const vec3 skyLum = textureSkyLUT(skyLUT, vec3(0.0, RPlanet + scene.plPosY, 0.0), dir, sunDir);
      lum = mix(lum, skyLum, f);
      tr  = tr*(1.0-f);
      }
    }

  lum *= scene.GSunIntensity;
  lum *= scene.exposure;
  // blend One / OneMinusSrcAlpha with src (lum, 1-tr)
  return lum + color*tr;
  }

void main() {
  float exposure = scene.exposure;
  vec3  color    = textureLod(textureD, uv, 0).rgb;

  const vec4 diff = texelFetch(gbufDiffuse, ivec2(gl_FragCoord.xy), 0);
  color += waterReflection(diff);
  color  = fogComposite(color);

  VideoSettings settings = push.settings;
  settings.hdr.y = 0.0;
  settings.hdr.z = 0.0;
  color = gameTonemap(color, settings);
  if(settings.hdr.x<=0.0)
    color += dither(gl_FragCoord.xy, TONEMAP_DITHER_TARGET_BITS);
  outColor = vec4(color, 1.0);
  }
