#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform sampler2D src;
layout(binding = 1) uniform sampler2D zs;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outDepth;

// VR: the stash may be smaller than the scene (half resolution); scale maps a
// stash texel to the scene texel it copies.
layout(push_constant, std430) uniform Push {
  int scale;
  } push;

void main() {
  const ivec2 at = ivec2(gl_FragCoord.xy)*max(push.scale,1);
  outColor = texelFetch(src, at, 0);
  outDepth = texelFetch(zs,  at, 0);
  }
