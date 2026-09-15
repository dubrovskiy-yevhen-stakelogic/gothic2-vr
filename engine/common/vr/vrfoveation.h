#pragma once
// Fixed foveation profiles for VK_EXT_fragment_density_map on the Quest 3.
// Measured on the Adreno 740 with regular (non-subsampled) attachments
// (tests/fdm-bench.cpp): the driver applies the density per tiler bin, a
// full-width horizontal strip whose height depends on the pass's attachment
// bytes (about 576 px for one R11G11B10 target, less with more attachments),
// and a bin is rendered at the finest density found inside it. A radial map
// therefore only ever reduced the bottom strip. The production profiles are
// horizontal bands in 196 px steps from the top and bottom edges: along the
// centre column this matches the radial FFR ladder, the horizontal periphery
// stays at full rate. Levels 4+ are diagnostic maps for the fixture.
#include <Tempest/Pixmap>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Vr {

// Shading-rate fraction (area) for the pixel row y of an image of `height` rows.
inline float foveationRowDensity(int level, uint32_t y, uint32_t height) {
  const uint32_t top    = y;
  const uint32_t bottom = (y<height) ? height-1-y : 0;
  const uint32_t edge   = std::min(top,bottom); // rows to the nearer edge
  switch(level) {
    case 1:  return edge<196u ? 0.5f   : 1.f;                                              // Low: outer 196 px at 1/2
    case 2:  return edge<196u ? 0.25f  : (edge<392u ? 0.5f : 1.f);                         // Medium: 1/4, then 1/2
    case 3:  return edge<196u ? 0.125f : (edge<392u ? 0.25f : (edge<588u ? 0.5f : 1.f));  // High: 1/8, 1/4, 1/2
    default: return 1.f;
    }
  }

// Radial fraction by the normalised distance r from the image centre
// (r = 1 on the inscribed ellipse); diagnostic profiles only.
inline float foveationDensity(int level, float r) {
  switch(level) {
    case 4:  return r<0.35f ? 1.f : 0.0625f;                     // diagnostic: 1/16 outside the centre
    case 5:  return r<0.35f ? 1.f : 0.25f;                       // diagnostic: 1/4 outside the centre
    case 16: return r<0.45f ? 1.f : (r<0.70f ? 0.5f : 0.25f);    // diagnostic: the former radial Medium
    default: return 1.f;
    }
  }

inline uint32_t foveationMapSize(uint32_t pixels, uint32_t texel) {
  return (pixels + texel - 1)/texel;
  }

// RG8 density map for an eye image of width x height with `texel` pixels per
// map texel (within the device's min/max fragment density texel size).
inline Tempest::Pixmap foveationMap(int level, uint32_t width, uint32_t height, uint32_t texel) {
  const uint32_t mw = foveationMapSize(width,texel), mh = foveationMapSize(height,texel);
  Tempest::Pixmap pm(mw,mh,Tempest::TextureFormat::RG8);
  auto* data = static_cast<uint8_t*>(pm.data());
  for(uint32_t y=0; y<mh; ++y)
    for(uint32_t x=0; x<mw; ++x) {
      const float px = (float(x)+0.5f)*float(texel), py = (float(y)+0.5f)*float(texel);
      const float dx = (px - float(width)*0.5f)/(float(width)*0.5f);
      const float dy = (py - float(height)*0.5f)/(float(height)*0.5f);
      const float r  = std::sqrt(dx*dx + dy*dy);
      float d = 1.f;
      if(level>=1 && level<=3) {
        // a texel takes the finest density of the rows it covers, so a band
        // edge inside a texel never coarsens the rows above it
        const uint32_t y0 = y*texel, y1 = std::min(height, y0+texel);
        d = 0.f;
        for(uint32_t row=y0; row<y1; ++row)
          d = std::max(d, foveationRowDensity(level,row,height));
        } else {
        d = std::clamp(foveationDensity(level,r),0.f,1.f);
        }
      if(level==6)  d = (x<mw/2) ? 0.25f : 1.f;                                        // diagnostic: left half
      if(level==7)  d = (y<mh/2) ? 0.25f : 1.f;                                        // diagnostic: top half
      if(level==8)  d = 0.25f;                                                          // diagnostic: whole image
      if(level==9)  d = (y>=mh/4 && y<mh/4+3) ? 0.25f : 1.f;                            // diagnostic: one band of 3 texel rows
      if(level==10) d = (x>=mw/8 && x<mw/8+5 && y>=mh*3/4 && y<mh*3/4+5) ? 0.25f : 1.f; // diagnostic: small square
      if(level==11) d = (y<6) ? 0.25f : 1.f;                                            // diagnostic: top 6 texel rows
      if(level==12) d = (y<12) ? 0.25f : 1.f;                                           // diagnostic: top 12 texel rows
      if(level==13) d = (y+6>=mh) ? 0.25f : 1.f;                                        // diagnostic: bottom 6 texel rows
      if(level==14) d = (y<6 || y+6>=mh) ? 0.25f : 1.f;                                 // diagnostic: top and bottom 6 rows
      const auto v = uint8_t(std::lround(std::clamp(d,0.f,1.f)*255.f));
      data[(size_t(y)*mw + x)*2 + 0] = v;
      data[(size_t(y)*mw + x)*2 + 1] = v;
      }
  return pm;
  }

}
