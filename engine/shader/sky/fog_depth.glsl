#ifndef FOG_DEPTH_GLSL
#define FOG_DEPTH_GLSL

int visibleStepCount(ivec2 column, ivec3 volumeSize) {
#if defined(DEPTH_LIMIT)
  // A fog texel contributes to neighbouring pixels through bilinear filtering.
  // Expand its complete support, then read every overlapping conservative HiZ
  // tile. Sky/window pixels therefore retain the entire atmosphere column.
  const vec2 scale = vec2(scene.screenRes)/vec2(volumeSize.xy);
  ivec2 lo = ivec2(floor((vec2(column)-1.0)*scale));
  ivec2 hi = ivec2(ceil ((vec2(column)+2.0)*scale));
  lo = clamp(lo, ivec2(0), scene.screenRes-1)/scene.hiZTileSize;
  hi = clamp(hi, ivec2(0), scene.screenRes-1)/scene.hiZTileSize;
  float packedDepth = 0.0;
  for(int y=lo.y; y<=hi.y; ++y)
    for(int x=lo.x; x<=hi.x; ++x)
      packedDepth = max(packedDepth, texelFetch(currentHiZ, ivec2(x,y), 0).r);

  // Inverse of packHiZ. R16's saturation below .95 only makes this bound
  // farther away (conservative); hiz_pot already rounds depth toward far.
  const float depth = min(0.95 + packedDepth*0.05, 1.0);
#if defined(FOG_VOLUME_FAR)
  const float d = fogVolumeSlice(linearDepth(depth, scene.clipInfo), scene.clipInfo);
#else
  const float d0 = linearDepth(dFogMin, scene.clipInfo);
  const float d1 = linearDepth(dFogMax, scene.clipInfo);
  const float d = clamp((linearDepth(depth, scene.clipInfo)-d0)/(d1-d0), 0.0, 1.0);
#endif
  // Trilinear Z interpolation needs the next texel; keep one extra step for
  // numerical rounding. Never change the original integration step spacing.
  return clamp(int(ceil(d*float(volumeSize.z)+0.5))+1, 1, volumeSize.z);
#else
  return volumeSize.z;
#endif
  }

#endif
