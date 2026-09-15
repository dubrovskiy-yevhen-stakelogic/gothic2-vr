#ifndef AMBIENT_COMMON_GLSL
#define AMBIENT_COMMON_GLSL

// Shared by the separate ambient pass and the mobile sun+ambient pass.
vec3 skyIrradiance(in texture2D irradiance, vec3 n) {
  ivec3 d;
  d.x = n.x>=0 ? 1 : 0;
  d.y = n.y>=0 ? 1 : 0;
  d.z = n.z>=0 ? 1 : 0;
  n = n*n;
  vec3 ret = vec3(0);
  ret += texelFetch(irradiance, ivec2(0,d.x), 0).rgb * n.x;
  ret += texelFetch(irradiance, ivec2(1,d.y), 0).rgb * n.y;
  ret += texelFetch(irradiance, ivec2(2,d.z), 0).rgb * n.z;
  return ret;
  }

vec3 ambientLuminance(in texture2D irradiance, vec3 ambient, vec3 norm) {
  vec3 ret = vec3(0);
  ret += ambient;
  ret += skyIrradiance(irradiance, norm)*0.8;
  ret += (norm.y*0.25+0.75) * NightAmbient * Fd_Lambert;
  return ret;
  }

#endif
