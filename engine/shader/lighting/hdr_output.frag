#version 450
#extension GL_GOOGLE_include_directive : enable

#include "hdr_output.glsl"

layout(binding = 0) uniform sampler2D composite;
layout(push_constant) uniform Push { float paperWhiteNits; float peakNits; } settings;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
  // UI and video retain the engine's existing gamma-encoded compositing convention.
  vec3 linear = pow(max(textureLod(composite, uv, 0).rgb, vec3(0.0)), vec3(2.2));
  linear = min(linear, vec3(settings.peakNits/settings.paperWhiteNits));
  vec3 pq = hdr10Encode(linear, settings.paperWhiteNits);
  // Quantize only at the final 10-bit output, not in the floating-point intermediate.
  float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) - 0.5;
  outColor = vec4(clamp(pq + noise/1023.0, vec3(0.0), vec3(1.0)), 1.0);
  }
