#pragma once

#include <cmath>
#include <algorithm>
#include <numbers>
#include <limits>
#include <utility>

namespace RadialInput {

class StickSelector final {
  public:
    void reset() { *this=StickSelector(); }
    std::pair<float,float> update(float lx,float ly,float rx,float ry) {
      const float magnitude[]={lx*lx+ly*ly,rx*rx+ry*ry};
      const bool held[]={magnitude[0]>=0.25f,magnitude[1]>=0.25f};
      if(!initialized) {
        for(int i=0;i<2;++i) { armed[i]=!held[i]; previous[i]=held[i]; }
        initialized=true;
        return {};
        }
      int next=-1;
      for(int i=0;i<2;++i) {
        if(!held[i]) armed[i]=true;
        if(armed[i] && held[i] && !previous[i] && (next<0 || magnitude[i]>magnitude[next])) next=i;
        previous[i]=held[i];
        }
      if(next>=0) active=next;
      if(active<0 || !held[active]) return {};
      return active==0 ? std::pair(lx,ly) : std::pair(rx,ry);
      }
  private:
    bool initialized=false;
    bool armed[2]={false,false};
    bool previous[2]={false,false};
    int active=-1;
  };

inline int sector(float x, float y, float inner, float outer, int count=8) {
  const float distance=std::hypot(x,y);
  if(count<=0 || distance<inner || distance>outer) return -1;
  float angle=std::atan2(x,-y);
  if(angle<0) angle+=2.f*std::numbers::pi_v<float>;
  return int(std::floor(angle/(2.f*std::numbers::pi_v<float>/float(count))+0.5f))%count;
  }

struct Layout {
  float x=0, y=0;
  float radius=0, outer=0;
  int cell=0, footer=0;
  };

inline float touchDeadZone(int width, int height) {
  return std::max(16.f,float(std::min(width,height))/40.f);
  }

inline int touchSector(float x, float y, float originX, float originY, int width, int height, int count) {
  // The wheel stays centered on screen, but selection follows the held finger's displacement.
  return sector(x-originX,y-originY,touchDeadZone(width,height),std::numeric_limits<float>::max(),count);
  }

inline Layout layout(int width, int height, float scale, int choices, int lineHeight) {
  if(width<=0 || height<=0) return {};
  const float side=float(std::min(width,height));
  const float margin=std::min(std::max(8.f,side*0.025f),side*0.1f);
  const int footer=int(std::min(float(3*lineHeight)+margin,side*0.25f));
  float cell=std::min(64.f*std::max(0.1f,scale),side*0.115f);
  float radius=choices<=2 ? cell*1.05f : std::max(cell*1.6f,side*0.17f);
  float outer=radius+cell*0.6f;
  // Keep the wheel centered while leaving room for its title above and item labels below.
  const float available=std::min(float(width)*0.5f-margin,float(height)*0.5f-float(footer)-2*margin);
  const float fit=std::min(1.f,available/outer);
  cell*=fit; radius*=fit; outer*=fit;
  return {float(width)*0.5f,float(height)*0.5f,
          radius,outer,std::max(1,int(cell)),footer};
  }

}
