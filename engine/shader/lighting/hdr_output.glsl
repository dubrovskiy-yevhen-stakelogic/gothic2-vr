#ifndef HDR_OUTPUT_GLSL
#define HDR_OUTPUT_GLSL

// Linear Rec.709 to Rec.2020, followed by SMPTE ST 2084 absolute luminance encoding.
vec3 hdr10Encode(vec3 linear709, float paperWhiteNits) {
  const mat3 to2020 = mat3(0.627404, 0.069097, 0.0163916,
                          0.329282, 0.919540, 0.0880132,
                          0.0433136, 0.0113612, 0.895595);
  vec3 normalized = clamp(to2020 * linear709 * (paperWhiteNits/10000.0), vec3(0.0), vec3(1.0));
  const float m1 = 2610.0/16384.0;
  const float m2 = 2523.0/32.0;
  const float c1 = 3424.0/4096.0;
  const float c2 = 2413.0/128.0;
  const float c3 = 2392.0/128.0;
  vec3 p = pow(normalized, vec3(m1));
  return pow((vec3(c1)+c2*p)/(vec3(1.0)+c3*p), vec3(m2));
  }

#endif
