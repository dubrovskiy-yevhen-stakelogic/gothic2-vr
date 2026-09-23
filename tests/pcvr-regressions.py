"""Compile the resize dispatcher, Widget resize and PCVR callbacks without a GPU."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
def extract(path,signature):
    s=(root/path).read_text();start=s.index(signature);end=s.index('{',start)+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
code=r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include "vr/vrhudrect.h"
struct SizeEvent {uint32_t w,h;SizeEvent(uint32_t w,uint32_t h):w(w),h(h){}};
struct Widget {
  struct Rect {int w=1280,h=1370;} wrect;
  struct Layout {void applyLayout(){}} layout;Layout* lay=&layout;
  int w()const{return wrect.w;}int h()const{return wrect.h;}
  virtual void resizeEvent(SizeEvent&){} void resize(int,int);
};
struct EventDispatcher {void dispatchResize(Widget&,SizeEvent&,bool force=false);};
struct QuestXr {
  static QuestXr& inst(){static QuestXr x;return x;}
  uint32_t width(){return 1280;}uint32_t height(){return 1370;}
  int colorFormat=0,colorView=0;const char* colorFormatName="";
  void selectSwapchainFormat(const std::vector<int64_t>&);
};
enum {VK_FORMAT_R8G8B8A8_SRGB=43,VK_FORMAT_R8G8B8A8_UNORM=37,VK_FORMAT_B8G8R8A8_SRGB=50,VK_FORMAT_B8G8R8A8_UNORM=44};
namespace Tempest {struct Log {template<class... T>static void i(T...){} };}
struct Camera {uint32_t w=0,h=0;void setViewport(uint32_t x,uint32_t y){w=x;h=y;}};
struct Gothic {Camera cam;static Gothic& inst(){static Gothic g;return g;}Camera* camera(){return &cam;}};
struct MainWindow:Widget {
  struct Device {int waits=0;void waitIdle(){++waits;}} device;
  struct Swapchain {int resets=0;void reset(){++resets;}} swapchain;
  void resizeEvent(SizeEvent&) override;
};
int checks=0;
void check(bool ok,const char* label){++checks;if(!ok){std::printf("FAIL %s\n",label);std::exit(1);}}
'''
for path,sig in [
    ('engine/lib/Tempest/Engine/ui/widget.cpp','void Widget::resize(int w, int h)'),
    ('engine/lib/Tempest/Engine/system/eventdispatcher.cpp','void EventDispatcher::dispatchResize('),
    ('engine/common/mainwindow.cpp','void MainWindow::resizeEvent('),
    ('engine/common/vr/questxr.cpp','void QuestXr::selectSwapchainFormat(')]:
    code+='\n'+extract(path,sig)+'\n'
code+=r'''
int main(){
  MainWindow window;EventDispatcher dispatcher;int events=0;
  for(auto [width,height]:std::vector<std::pair<int,int>>{{1920,1080},{3840,2160},{640,480},{0,0},{1280,1370},{900,1800}}){
    SizeEvent e(width,height);dispatcher.dispatchResize(window,e,true);++events;
    check(window.w()==1280 && window.h()==1370,"mirror resize retains headset UI coordinate space");
    check(Gothic::inst().cam.w==1280 && Gothic::inst().cam.h==1370,"camera viewport retains XR dimensions");
    check(window.swapchain.resets==events && window.device.waits==events,"one mirror reset per event without recursion loop");
    Vr::HudRect hud;Vr::hudRectAdd(hud,-1,-1,1,1,0,0,window.w(),window.h());
    check(hud.w==1280 && hud.h==1370,"full UI cannot exceed headset image after resize");
  }
  const auto rect=Vr::hudCopyRegion({0,0,3840,2160},1280,1370);
  check(rect.w==1280 && rect.h==1370,"composition bounds clamp to XR image");
  const auto empty=Vr::hudCopyRegion({},1280,1370);
  check(empty.w==1280 && empty.h==1370,"empty HUD preserves full-image fallback");
  auto& xr=QuestXr::inst();
  for(auto formats:std::vector<std::vector<int64_t>>{{},{50},{37},{44},{50,37,44}}){
    bool rejected=false;try{xr.selectSwapchainFormat(formats);}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"formats requiring unimplemented colour conversion fail explicitly");
  }
  xr.selectSwapchainFormat({50,37,43});
  check(xr.colorFormat==43 && xr.colorView==37,"sRGB RGBA format with UNORM rendering view selected");
  xr.selectSwapchainFormat({43});check(xr.colorFormat==43,"supported format works alone");
  std::printf("PCVR resize and formats: %d production checks passed\n",checks);
}
'''
(out/'pcvr-regressions.cpp').write_text(code,encoding='utf-8')
