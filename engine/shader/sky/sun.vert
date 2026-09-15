#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive    : enable

out gl_PerVertex {
  vec4 gl_Position;
  };

layout(push_constant, std430) uniform UboPush {
  vec2  sunPos;
  vec2  sunSz;
  vec3  sunDir;
  float GSunIntensity;
  mat4  viewProjectInv;
  uint  isSun;
  } push;

layout(location = 0) out vec2 uv;
layout(location = 1) noperspective out vec2 outPos;

const vec2 vert[] = {
   {-1,-1},{ 1,1},{1,-1},
   {-1,-1},{-1,1},{1, 1}
};

void main() {
  const bool isSun = (push.isSun&1u)!=0;

  vec2 v = vert[gl_VertexIndex];
  uv = v*0.5 + vec2(0.5);

  if((push.isSun&2u)!=0) {
    // A fixed tangent quad at infinity keeps its angular size in both eyes.
    // A fixed pixel-sized quad changes apparent size under headset projection.
    vec3 center = normalize(push.sunDir);
    vec3 pole = abs(center.y)<0.99 ? vec3(0,1,0) : vec3(0,0,1);
    vec3 right = normalize(cross(pole,center));
    vec3 down = cross(right,center);
    vec3 direction = center + right*(v.x*push.sunSz.x) + down*(v.y*push.sunSz.y);
    vec4 clip = inverse(push.viewProjectInv)*vec4(direction,0.0);
    outPos = clip.xy / (abs(clip.w)>1e-6 ? clip.w : 1e-6);
    gl_Position = vec4(clip.xy,clip.w,clip.w);
    return;
  }

  float scale = 1.0 - (isSun ? length(push.sunPos)*0.25 : 0);
  vec2  pos   = v*(push.sunSz*scale) + push.sunPos;

  outPos      = pos;
  gl_Position = vec4(pos, 1.0, 1.0);
  }
