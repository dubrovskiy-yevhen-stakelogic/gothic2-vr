"""Run production fragments against the reported 033 gameplay regressions."""
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[1]

def source(path):
    return (root / 'engine/common' / path).read_text(encoding='utf-8')

def method(path, signature):
    s = source(path)
    start = s.index(signature)
    end = s.index('{', start) + 1
    depth = 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[start:end]

fixture = r'''
#include <Tempest/Matrix4x4>
#include <Tempest/Vec>
#include <Tempest/SystemApi>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
using namespace Tempest;
int checks=0;
void test(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
namespace weatherTest {
struct Color {float r,g,b,a;Color(float r=0,float g=0,float b=0,float a=0):r(r),g(g),b(b),a(a){}};
struct Texture {int id;};
struct Resources {
  static inline Texture black{1},grey{2};
  static const Texture* loadTexture(Color c){return c.a==0?nullptr:&grey;}
  static const Texture& fallbackBlack(){return black;}
};
struct Sky {int weather=0;struct {const Texture* lay[2]={};} weatherClouds;void setVrWeather(int);};
'''
fixture += method('graphics/sky/sky.cpp', 'void Sky::setVrWeather(int mode)')
fixture += r'''
void run(){Sky sky;
  for(int mode=0;mode<4;++mode){sky.setVrWeather(mode);test(sky.weatherClouds.lay[0] && sky.weatherClouds.lay[1],"every weather supplies valid cloud texture bindings");}
  sky.setVrWeather(1);test(sky.weatherClouds.lay[0]==&Resources::black,"clear uses real fallback instead of transparent null sentinel");
  sky.setVrWeather(99);test(sky.weather==3,"weather clamps invalid mode");
}
}
namespace musicTest {
struct Gothic {static inline int enabled=1,index=0;static inline float volume=.7f;
 static int settingsGetI(const char*,const char* key){return std::string(key)=="musicEnabled"?enabled:index;}
 static float settingsGetF(const char*,const char*){return volume;}};
struct Log{template<class... T>static void i(T...){}};
struct MusicProvider {bool enabled=true;MusicProvider(int,int){} virtual ~MusicProvider()=default;
 void setEnabled(bool b){enabled=b;}bool isEnabled()const{return enabled;}void playTheme(int,int){}};
struct OpenGothicMusicProvider:MusicProvider{using MusicProvider::MusicProvider;};
struct GothicKitMusicProvider:MusicProvider{using MusicProvider::MusicProvider;};
struct SoundEffect {float volume=1,volumeAtPlay=-1;void setVolume(float v){volume=v;}void play(){volumeAtPlay=volume;}};
struct Device {std::unique_ptr<MusicProvider> owned;SoundEffect load(std::unique_ptr<MusicProvider> p){owned=std::move(p);return {};}};
constexpr int SAMPLE_RATE=44100,PROVIDER_OPENGOTHIC=0;
struct GameMusic {Device device;MusicProvider* impl=nullptr;int provider=-1;bool videoMuted=false;SoundEffect sound;
 struct {int theme=0,tags=0;} currentMusic;
 bool isEnabled()const{return impl && impl->isEnabled();}void setEnabled(bool e){impl->setEnabled(e);}
 void setupSettings();void setVideoMuted(bool);
};
'''
fixture += method('gamemusic.cpp', 'void GameMusic::setVideoMuted(bool muted)')
fixture += method('gamemusic.cpp', 'void GameMusic::setupSettings()')
fixture += r'''
void run(){GameMusic music;music.setupSettings();test(music.sound.volume==.7f,"normal music volume");
 music.setVideoMuted(true);test(music.sound.volume==0 && music.isEnabled(),"cinematic suppresses sound without overwriting enabled setting");
 Gothic::volume=.4f;music.setupSettings();test(music.sound.volume==0,"loading settings refresh cannot unmute video");
 Gothic::index=1;music.setupSettings();test(music.sound.volumeAtPlay==0,"replacement provider starts muted before audio playback");
 music.setVideoMuted(false);test(music.sound.volume==.4f,"video end restores latest volume");
 music.setVideoMuted(true);Gothic::enabled=0;music.setupSettings();music.setVideoMuted(false);
 test(!music.isEnabled(),"video end preserves user-disabled music");
}
}
namespace saveTest {
namespace zenkit {enum class MenuItemType{INPUT,TEXT};}
namespace KeyCodec {enum Action{None};}
struct Handle {zenkit::MenuItemType type=zenkit::MenuItemType::INPUT;int on_sel_action=0,on_event_action=0;
 std::vector<std::string> on_sel_action_s={"SAVEGAME_SAVE"};std::array<std::string,1> text;};
struct Item {std::shared_ptr<Handle> handle=std::make_shared<Handle>();size_t slot=0;};
struct GameMenu {bool closeFlag=false,legacyInput=false;int saves=0;std::string savedName;
 size_t saveSlotId(const Item& i){return i.slot;}void execSaveGame(Item& i){++saves;savedName=i.handle->text[0];}
 void execSingle(Item&,int,KeyCodec::Action);
};
'''
# Exercise the actual early save path; legacy modal dialog is an observable trap.
s = source('ui/gamemenu.cpp')
start = s.index('void GameMenu::execSingle(')
end = s.index('#endif', start) + len('#endif')
fixture += s[start:end] + '\n legacyInput=true;\n}\n'
fixture += r'''
void run(){GameMenu menu;Item slot;menu.execSingle(slot,0,KeyCodec::None);
 test(menu.saves==1 && menu.savedName=="Save 1" && menu.closeFlag && !menu.legacyInput,"empty save slot bypasses keyboard and closes menu");
 slot.slot=9;slot.handle->text[0]="old name";menu.execSingle(slot,0,KeyCodec::None);
 test(menu.saves==2 && menu.savedName=="Save 10","overwrite uses deterministic native slot name");
 menu.execSingle(slot,1,KeyCodec::None);test(menu.saves==2,"sliding selection cannot save");
 slot.handle->on_sel_action_s={"SAVEGAME_LOAD"};menu.execSingle(slot,0,KeyCodec::None);test(menu.saves==2,"load menu cannot invoke save");
 slot.handle->on_sel_action_s={"SAVEGAME_SAVE"};slot.slot=size_t(-1);menu.execSingle(slot,0,KeyCodec::None);test(menu.saves==2,"invalid save slot rejected");
}
}
namespace particlesTest {
struct Axes {Vec3 pfxLeft,pfxTop,pfxDepth;};
Axes axes(const Matrix4x4& v,const Matrix4x4& vp){Axes uboGlobalCpu;
'''
fixture += '\n'.join(line for line in source('graphics/sceneglobals.cpp').splitlines() if 'uboGlobalCpu.pfx' in line and '= Tempest::Vec3::normalize' in line)
fixture += r'''
 return uboGlobalCpu;
}
void run(){auto view=Matrix4x4::mkIdentity();view.rotateOY(28);auto left=Matrix4x4::mkIdentity(),right=left;
 left[2][0]=.22f;right[2][0]=-.22f;left[2][1]=right[2][1]=.08f;
 const auto a=axes(view,left*view),b=axes(view,right*view);
 test((a.pfxLeft-b.pfxLeft).length()<.00001f && (a.pfxTop-b.pfxTop).length()<.00001f && (a.pfxDepth-b.pfxDepth).length()<.00001f,"asymmetric stereo projection cannot tilt flame billboards differently");
 const auto dot=[](Vec3 x,Vec3 y){return x.x*y.x+x.y*y.y+x.z*y.z;};
 test(std::abs(dot(a.pfxLeft,a.pfxDepth))<.00001f && std::abs(dot(a.pfxTop,a.pfxDepth))<.00001f,"particle billboard basis remains orthogonal");
 test(std::abs(a.pfxLeft.length()-1)<.00001f,"particle basis retains world size");
}
}
namespace runTest {
struct {struct {std::array<int,6> mapping={0,0,0,0,9,0};}settings;}vrMenu;
bool held(uint32_t buttons){GamepadState gp;gp.buttons=buttons;bool vrRunning=true;
'''
s = source('gamepad.cpp')
start = s.index('  const uint32_t runButtons[]')
end = s.index('  struct InputGuard', start)
fixture += s[start:end]
fixture += r'''
 return runDown;
}
void run(){test(!held(0),"no run button when released");test(held(GamepadState::L3),"L3 held runs");
 test(!held(0),"released raw button is false");test(!held(GamepadState::R3),"crouch does not run");
 vrMenu.settings.mapping[4]=0;vrMenu.settings.mapping[5]=9;
 test(held(GamepadState::R3) && !held(GamepadState::L3),"custom run mapping reads its assigned button");}
}
namespace returnTest {
struct Item {bool live=true;};struct Focus {Item* item=nullptr;};
struct World {Focus validateFocus(Focus f){if(f.item && !f.item->live)f.item=nullptr;return f;}};
struct Npc {bool canTake=true;int inventory=0;bool takeItemVr(Item& item){if(!canTake)return false;item.live=false;++inventory;return true;}};
struct Gameplay {struct ReturnItem{Item* item;uint64_t time;};std::vector<ReturnItem> returning;
 void message(const char*,uint64_t){}void returnItems(World&,Npc&,uint64_t);
};
'''
fixture += method('vr/vrgameplay.cpp', 'void Gameplay::returnItems(World& world,Npc& player,uint64_t now)')
fixture += r'''
void run(){World world;Npc player;Gameplay game;Item item;game.returning.push_back({&item,1800});
 game.returnItems(world,player,1799);test(item.live && player.inventory==0,"throw remains catchable until recall time");
 game.returnItems(world,player,1800);test(!item.live && player.inventory==1 && game.returning.empty(),"expired throw transfers world item back to holster inventory");
 game.returnItems(world,player,9999);test(player.inventory==1,"recall cannot duplicate inventory");
 item.live=false;game.returning.push_back({&item,1800});game.returnItems(world,player,1000);
 test(game.returning.empty() && player.inventory==1,"already caught or NPC-picked weapon cancels recall");
 item.live=true;player.canTake=false;game.returning.push_back({&item,1800});game.returnItems(world,player,2000);
 test(item.live && game.returning.size()==1,"failed transfer retains ownership and recall request");
 player.canTake=true;game.returnItems(world,player,2016);test(!item.live && player.inventory==2,"recall retries after blocked transfer");
}
}
'''
fixture += r'''
namespace speedTest {
struct Npc {float rotationYRad()const{return 0;}float rotationRad()const{return 0;}};
struct MoveAlgo {Npc npc;float mulSpeed=1,vrLocomotionSpeed=1;bool diving=false;
 bool isDive()const{return diving;}
 void applyRotation(Vec3&,const Vec3&)const;void applyRotation(Vec3&,const Vec3&,float)const;
};
'''
fixture += method('game/movealgo.cpp','void MoveAlgo::applyRotation(Tempest::Vec3& out, const Tempest::Vec3& dpos) const')
fixture += method('game/movealgo.cpp','void MoveAlgo::applyRotation(Tempest::Vec3& out, const Tempest::Vec3& dpos, float rot) const')
fixture += r'''
void run(){MoveAlgo movement;Vec3 out;
 auto speed=[&]{movement.applyRotation(out,{0,2,10});return std::sqrt(out.x*out.x+out.z*out.z);};
 test(std::abs(speed()-10)<.001f,"ordinary NPC retains native speed");
 movement.vrLocomotionSpeed=.72f;test(std::abs(speed()-7.2f)<.001f,"VR walk uses brisk baseline");
 test(out.y==2,"VR horizontal pace does not alter vertical animation displacement");
 movement.mulSpeed=2;test(std::abs(speed()-14.4f)<.001f,"speed potion multiplies baseline instead of being overwritten");
 movement.vrLocomotionSpeed=1;test(std::abs(speed()-20)<.001f && movement.mulSpeed==2,"hold sprint retains native potion bonus");
}
}
int main(){weatherTest::run();musicTest::run();saveTest::run();particlesTest::run();runTest::run();returnTest::run();speedTest::run();
 std::printf("VR 033 regressions: %d production checks passed\n",checks);}
'''
out=Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
(out / 'regressions.cpp').write_text(fixture, encoding='utf-8')
print('Extracted weather, audio, save, stereo particle, run and weapon recall production code')
