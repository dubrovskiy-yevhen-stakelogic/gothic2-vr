#version 450
#extension GL_GOOGLE_include_directive : enable
#include "lighting/tonemapping.glsl"
layout(binding=1) uniform sampler2D gbufDiffuse;
layout(location=0) out vec4 outColor;
void main() {
  // Diagnostic base colour, already in the pre-exposed scene's useful range.
  // No lights, normals, irradiance or shadow textures are evaluated here.
  outColor=vec4(textureAlbedo(texelFetch(gbufDiffuse,ivec2(gl_FragCoord.xy),0).rgb)*0.25,1.0);
}
