#version 450
layout(binding=0) uniform sampler2D albedo;
layout(binding=1) uniform sampler2D sceneDepth;
layout(push_constant) uniform Push {mat4 mvp;vec4 light;vec4 viewport;vec4 tint;} push;
layout(location=0) in vec2 texcoord;
layout(location=0) out vec4 result;
// Distances are Gothic world centimetres. A narrow soft band avoids coplanar
// depth quantization flicker without exposing items behind scene geometry.
float pickupVisibility(float itemDepth,float sceneDepth,float pixelSlope) {
  float tolerance=clamp(pixelSlope,0.5,2.0);
  return 1.0-smoothstep(tolerance,tolerance*2.0,itemDepth-sceneDepth);
}
void main() {
  float depth=texture(sceneDepth,gl_FragCoord.xy*push.viewport.xy).r;
  float itemDistance=push.viewport.w/(gl_FragCoord.z-push.viewport.z);
  float sceneDistance=push.viewport.w/(depth-push.viewport.z);
  float visibility=pickupVisibility(itemDistance,sceneDistance,fwidth(itemDistance));
  float alpha=texture(albedo,texcoord).a;
  if(alpha<0.1)discard;
  result=vec4(push.tint.rgb,alpha*push.tint.a*0.6*visibility);
}
