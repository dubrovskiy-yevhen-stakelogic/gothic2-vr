"""Hold an object upload while exercising the production early-submit boundary."""
from pathlib import Path
import sys

root=Path(__file__).resolve().parents[1]
def block(path,signature):
    s=(root/path).read_text();a=s.index(signature);b=s.index('{',a)+1;depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]

renderer=(root/'engine/common/graphics/renderer.cpp').read_text()
start=renderer.index('if(prep!=nullptr && onPrepared)')
if renderer[start+len('if(prep!=nullptr && onPrepared)'):].lstrip().startswith('{'):
    boundary=block('engine/common/graphics/renderer.cpp','if(prep!=nullptr && onPrepared)')
else:
    boundary=renderer[start:renderer.index(';',start)+1]
join=block('engine/common/graphics/instancestorage.cpp','void InstanceStorage::join()')
# Signal entry for the test harness without changing the production wait body.
join=join.replace('{','{ signalFirstEvent();',1)
visual=block('engine/common/graphics/visualobjects.cpp','void VisualObjects::postFrameupdate()')
world=block('engine/common/graphics/worldview.cpp','void WorldView::postFrameupdate()')
source=r'''
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
struct InstanceStorage {
  std::mutex sync;std::condition_variable uploadDone;int uploadFId=0;
  std::promise<void> event;std::atomic_bool signaled=false,uploaded=false;
  void signalFirstEvent(){if(!signaled.exchange(true))event.set_value();}
  void join();
};
struct VisualObjects {InstanceStorage instanceMem;void postFrameupdate();};
struct WorldView {VisualObjects visuals;void postFrameupdate();};
'''+join+'\n'+visual+'\n'+world+r'''
void record(WorldView& wview,void* prep,const std::function<void()>& onPrepared) {
'''+boundary+r'''
  wview.postFrameupdate(); // production end-of-world wait, before the main submit
}
int main() {
  int failures=0,checks=0;
  for(int mode=0;mode<3;++mode) {
    WorldView wview;auto& upload=wview.visuals.instanceMem;
    std::promise<void> release;auto gate=release.get_future();auto event=upload.event.get_future();
    std::atomic_int earlySubmits=0;std::atomic_bool staleRead=false,finished=false;
    std::thread worker([&]{gate.wait();{std::lock_guard<std::mutex> lock(upload.sync);
      upload.uploaded=true;upload.uploadFId=-1;}upload.uploadDone.notify_all();});
    std::function<void()> submit=[&]{++earlySubmits;staleRead=!upload.uploaded.load();upload.signalFirstEvent();};
    if(mode==2)submit={};
    std::thread render([&]{record(wview,mode==1?nullptr:&wview,submit);finished=true;});
    const bool signaled=event.wait_for(std::chrono::seconds(5))==std::future_status::ready;
    const bool waited=signaled && earlySubmits==0 && !finished;
    release.set_value();worker.join();render.join();
    ++checks;if(!waited){++failures;std::printf("FAIL mode=%d submitted before held upload completed\n",mode);}
    ++checks;if(staleRead){++failures;std::printf("FAIL mode=%d GPU callback observed stale upload data\n",mode);}
    ++checks;if(earlySubmits!=(mode==0?1:0) || !finished){++failures;std::printf("FAIL submit count/completion mode=%d\n",mode);}
  }
  std::printf("Split upload: %d checks, %d failures (split, unsplit, no callback)\n",checks,failures);
  return failures?1:0;
}
'''
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
(out/'split-upload.cpp').write_text(source,encoding='utf-8')
