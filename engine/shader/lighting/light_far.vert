#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive    : enable

#include "scene.glsl"

out gl_PerVertex {
  vec4 gl_Position;
  };

layout(push_constant, std140) uniform Pbo {
  vec3  origin; //lwc
  } push;

layout(binding = 0, std140) uniform UboScene {
  SceneDesc scene;
  };

layout(binding = 4, std430) readonly buffer SsboLighting {
  LightSource lights[];
  };

layout(location = 0) out vec4 cenPosition;
layout(location = 1) out vec3 color;
layout(location = 2) out flat uint lightId;

vec3 v[] = {
  {-1,-1,-1},
  { 1,-1,-1},
  { 1, 1,-1},
  {-1, 1,-1},

  {-1,-1, 1},
  { 1,-1, 1},
  { 1, 1, 1},
  {-1, 1, 1},
  };

bool testFrustrum(in vec3 at, in float R){
  for(int i=0; i<6; i++) {
    if(dot(scene.frustrum[i],vec4(at,1.0))<-R)
      return false;
    }
  return true;
  }

// A clipped convex cube projects inside the bounds of its surviving vertices
// and its edge intersections with the near plane (clip-space z == 0).
bool includeLightBound(vec4 p, inout vec4 bounds) {
  if(any(isnan(p)) || any(isinf(p)) || p.w<=0.0)
    return false;
  const vec2 ndc = p.xy/p.w;
  if(any(isnan(ndc)) || any(isinf(ndc)))
    return false;
  bounds.xy = min(bounds.xy, ndc);
  bounds.zw = max(bounds.zw, ndc);
  return true;
  }

vec4 clippedLightBounds(vec4 corner[8]) {
  const vec4 fullViewport = vec4(-1.0, -1.0, 1.0, 1.0);
  // These are the twelve edges of the existing v[]/Resources::cubeIbo cube.
  const uvec2 edge[12] = uvec2[12](
    uvec2(0,1), uvec2(1,2), uvec2(2,3), uvec2(3,0),
    uvec2(4,5), uvec2(5,6), uvec2(6,7), uvec2(7,4),
    uvec2(0,4), uvec2(1,5), uvec2(2,6), uvec2(3,7));
  vec4 bounds = vec4(1.0, 1.0, -1.0, -1.0);
  for(int i=0; i<8; ++i) {
    if(any(isnan(corner[i])) || any(isinf(corner[i])))
      return fullViewport;
    if(corner[i].z>=0.0 && !includeLightBound(corner[i], bounds))
      return fullViewport;
    }
  for(int i=0; i<12; ++i) {
    const vec4 a = corner[edge[i].x];
    const vec4 b = corner[edge[i].y];
    if((a.z<0.0)==(b.z<0.0))
      continue;
    const float t = -a.z/(b.z-a.z);
    if(!includeLightBound(mix(a,b,t), bounds))
      return fullViewport;
    }
  // Expand by one raster pixel before viewport clipping. This also protects
  // the bound at pixel edges against ordinary floating-point rounding.
  const vec2 pad = 2.0*scene.screenResInv;
  return clamp(bounds+vec4(-pad,pad), vec4(-1.0), vec4(1.0));
  }

void main(void) {
  LightSource light = lights[gl_InstanceIndex];

  if(!testFrustrum(light.pos,light.range)) {
    // skip invisible lights, make sure that they don't turn into FQS
    gl_Position = vec4(0.0,0.0,-1.0,1.0);
    cenPosition = vec4(0.0);
    color       = vec3(0.0);
    return;
    }

  int neg = 0;
  bool farClipped=false;
  vec4 clipCorner[8];
  for(int i=0;i<8;++i) {
    vec3 at  = light.pos + v[i]*light.range;
    clipCorner[i] = scene.viewProject*vec4(at,1.0);
    farClipped = farClipped || clipCorner[i].z>=clipCorner[i].w;
    if(clipCorner[i].z<0.0)
      neg++;
    }

  const vec3 inPos = v[gl_VertexIndex];
  vec4 pos = vec4(0);
  if((neg>0 && neg<8) || (farClipped && neg<8)) {
    // Keep the existing surviving quad face and winding, with tight bounds.
    if(gl_VertexIndex>=4) {
      pos = vec4(uintBitsToFloat(0x7fc00000));
      } else {
      #if defined(REFERENCE_LIGHT_BOUNDS)
      const vec4 bounds = vec4(-1.0,-1.0,1.0,1.0);
#else
      const vec4 bounds = clippedLightBounds(clipCorner);
#endif
      const vec2 quad = mix(bounds.xy, bounds.zw, vec2(inPos.x,inPos.y)*0.5+0.5);
      // Backface quad retains every surface inside the sphere. Use the
      // farthest positive-depth corner; uncertainty keeps the whole depth range.
      float farDepth=0.0;
      bool valid=true;
      for(int c=0;c<8;++c) {
        vec4 p=clipCorner[c];
        if(any(isnan(p)) || any(isinf(p))) {valid=false;break;}
        if(p.z>=0.0) {
          if(p.w<=0.0) {valid=false;break;}
          farDepth=max(farDepth,p.z/p.w);
        }
      }
      if(!valid || isnan(farDepth) || isinf(farDepth)) farDepth=1.0;
      farDepth=clamp(farDepth+0.00001,0.0,1.0);
      pos = vec4(quad,farDepth,1.0);
      }
    } else {
    pos = clipCorner[gl_VertexIndex];
    }

  //const vec3 origin = vec3(38983.9336, 4080.52637, -1888.59839);
  gl_Position = pos;
  cenPosition = vec4(light.pos,light.range);
  color       = light.color;
  lightId     = gl_InstanceIndex;
  }
