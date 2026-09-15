#version 450
layout(binding=0) uniform sampler2D albedo;
layout(binding=1) uniform sampler2D sceneDepth;
layout(push_constant) uniform Push { mat4 mvp; vec4 light; vec4 viewport; vec4 tint; } push;
layout(location=0) in vec2 texcoord;
layout(location=1) in vec4 shade;
layout(location=0) out vec4 result;
void main() {
  float depth=texture(sceneDepth,gl_FragCoord.xy*push.viewport.xy).r;
  if(gl_FragCoord.z>depth+0.000002) discard;
  result=texture(albedo,texcoord)*shade;
  if(result.a<push.light.w) discard;
}
