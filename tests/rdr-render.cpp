#include "vr/vrratemap.h"
#include "vr/vrcontrols.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
void check(bool b,const char* m) {if(!b){std::puts(m);std::exit(1);}}
int main() {
  using namespace Vr::Rdr;
  FragmentRateMapSpec s; s.width=1832;s.height=1920;s.texelWidth=s.texelHeight=16;s.layers=2;
  s.centers={FragmentRateCenter{.46f,.51f},FragmentRateCenter{.54f,.51f}};
  FragmentRateMap m;
  check(buildFragmentRateMap(s,m),"off map");
  check(m.tileCounts[0]==m.pixels.size() && fragmentRatePermille(m.tileCounts)==1000,"off is full rate");
  unsigned previous=1001;
  for(unsigned l=1;l<fragmentRateLevelCount;++l) {
    s.profile=FragmentRateProfile::Conservative;
    s.inner=fragmentRateLevels[l].inner/1000.;s.outer=fragmentRateLevels[l].outer/1000.;
    s.verticalScale=fragmentRateLevels[l].verticalScale/1000.;
    check(buildFragmentRateMap(s,m),"RDR level builds");
    auto work=fragmentRatePermille(m.tileCounts);check(work<previous,"levels reduce estimated shading monotonically");previous=work;
    check(m.width==115 && m.height==120 && m.layers==2,"odd eye extent uses ceiling tiles");
    for(auto pixel:m.pixels)check(pixel==0 || pixel==5 || pixel==10,"only supported Vulkan rates encoded");
    for(unsigned eye=0;eye<2;++eye) {
      auto x=unsigned(s.centers[eye].x*s.width)/s.texelWidth;
      auto y=unsigned(s.centers[eye].y*s.height)/s.texelHeight;
      check(m.pixels[(eye*m.height+y)*m.width+x]==0,"each optical centre remains full rate");
    }
  }
  s.texelWidth=3;check(!buildFragmentRateMap(s,m),"invalid texel rejected");
  Vr::Settings st;check(st.cutsceneMode==1 && !st.taa && st.vrs==1,"release defaults");
  std::istringstream old("FoveationBands=3\nCutsceneMode=0\n");st.read(old);
  check(st.foveation==0 && st.cutsceneMode==0,"broken FDM disabled; explicit player mode retained");
  std::istringstream off("VRS=0\n");st.read(off);check(st.vrs==0,"explicit VRS Off retained");
  st.taa=true;st.vrs=4;std::ostringstream out;st.write(out);
  Vr::Settings back;std::istringstream in(out.str());back.read(in);
  check(back.taa && back.vrs==4 && back.cutsceneMode==0,"RDR settings roundtrip");
  std::puts("PASS RDR profiles, stereo optical centres, rate encoding and settings migration");
}
