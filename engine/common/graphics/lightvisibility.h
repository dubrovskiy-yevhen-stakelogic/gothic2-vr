#pragma once

#include <cmath>
#include <limits>

namespace LightVisibility {

// Conservative version of lighting/light.vert's sphere/plane test. Retain
// uncertain inputs and a rounding band so CPU/GPU arithmetic cannot make a
// contributing light disappear at a frustum edge.
inline bool contributes(const float planes[6][4],float x,float y,float z,float range,
                        float red,float green,float blue) {
  if(range==0.f || (red==0.f && green==0.f && blue==0.f))
    return false;
  if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
     !std::isfinite(range) || range<0.f)
    return true;
  for(unsigned i=0;i<6;++i) {
    const float ax=planes[i][0]*x,by=planes[i][1]*y,cz=planes[i][2]*z,d=planes[i][3];
    const float distance=((ax+by)+cz)+d;
    const float error=8.f*std::numeric_limits<float>::epsilon()*
                      (std::abs(ax)+std::abs(by)+std::abs(cz)+std::abs(d)+range+1.f);
    if(std::isfinite(distance) && std::isfinite(error) && distance < -range-error)
      return false;
    }
  return true;
  }

}
