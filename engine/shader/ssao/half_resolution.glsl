#ifndef SSAO_HALF_RESOLUTION_GLSL
#define SSAO_HALF_RESOLUTION_GLSL

ivec2 ssaoNearestDepthPixel(sampler2D depth, ivec2 halfPixel) {
  const ivec2 last = textureSize(depth, 0)-1;
  const ivec2 base = min(halfPixel*2, last);
  ivec2 nearest = base;
  float nearestDepth = texelFetch(depth, base, 0).r;
  for(int y=0; y<2; ++y)
    for(int x=0; x<2; ++x) {
      const ivec2 pixel = min(base+ivec2(x,y), last);
      const float z = texelFetch(depth, pixel, 0).r;
      if(z<nearestDepth) {
        nearest = pixel;
        nearestDepth = z;
        }
      }
  return nearest;
  }

float ssaoResolveHalf(sampler2D aoDepth, ivec2 pixel, float depth) {
  const ivec2 last = textureSize(aoDepth, 0)-1;
  const ivec2 base = pixel/2;
  const vec2 center = (vec2(pixel)+0.5)*0.5-0.5;
  // Reject unrelated surfaces using linear depth, not nonlinear hardware depth.
  const float depthTolerance = max(1.0, abs(depth)*0.02);
  float sum = 0.0;
  float weights = 0.0;
  for(int y=-1; y<=1; ++y)
    for(int x=-1; x<=1; ++x) {
      const ivec2 at = clamp(base+ivec2(x,y), ivec2(0), last);
      const vec2 sampleValue = texelFetch(aoDepth, at, 0).rg;
      const vec2 delta = vec2(at)-center;
      const float dz = (sampleValue.g-depth)/depthTolerance;
      const float weight = exp2(-0.5*dot(delta,delta)-dz*dz);
      sum += sampleValue.r*weight;
      weights += weight;
      }
  // A background surface absent from the half-resolution guide must not inherit foreground shadows.
  return weights>0.0001 ? clamp(sum/weights, 0.0, 1.0) : 1.0;
  }

#endif
