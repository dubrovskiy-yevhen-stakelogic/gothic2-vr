#pragma once
#include "utils/radialinput.h"
#include "diagnostics/baselinesmoke.h"
#include "vr/xrmath.h"
#include "vr/vrcontrols.h"
#include "vr/vrrunning.h"
#include "vr/vrwelcome.h"
#include "vr/vrprofiler.h"
#include "vr/lightroutecontroller.h"
#include "vr/vrgameplay.h"
#include "vr/vrhandrenderer.h"

#include "camera.h"
#include "resources.h"

#include <Tempest/Window>
#include <Tempest/CommandBuffer>
#include <Tempest/Fence>
#include <Tempest/VulkanApi>
#include <Tempest/Device>
#include <Tempest/VertexBuffer>
#include <Tempest/UniformBuffer>
#include <Tempest/VectorImage>
#include <Tempest/Event>
#include <Tempest/Pixmap>
#include <Tempest/Sprite>
#include <Tempest/Font>
#include <Tempest/TextureAtlas>
#include <Tempest/Timer>
#include <Tempest/Swapchain>

#include <vector>
#include <thread>
#include <fstream>
#if defined(__ANDROID__)
#include <Tempest/FramePacer>
#endif

#include "world/world.h"
#include "world/focus.h"
#include "game/playercontrol.h"
#include "graphics/renderer.h"
#include "ui/dialogmenu.h"
#include "ui/inventorymenu.h"
#include "ui/chapterscreen.h"
#include "ui/documentmenu.h"
#include "ui/videowidget.h"
#include "ui/menuroot.h"
#include "ui/consolewidget.h"
#if defined(__MOBILE_PLATFORM__)
#include "ui/touchinput.h"
#endif

#include "utils/keycodec.h"
#include "utils/gamepadbindings.h"
#include "resources.h"

// A desktop swapchain to present into. Tempest::Swapchain has no default
// constructor, so the member may only exist where one can actually be created:
// every non-VR build, and the Windows VR build, whose mirror window shows eye 0.
// The Quest has no desktop window at all and leaves the member out entirely.
#if !defined(GOTHIC2VR_OPENXR) || defined(_WIN32)
#define GOTHIC2VR_MIRROR 1
#endif

// The controller bridge (gamepad.cpp) — button/axis state, the binding table, the
// VR settings menu tick, snap turning and VR locomotion. It was written for the
// Quest and guarded on __ANDROID__, which leaves the Windows VR build with no
// input at all: MainWindow::tickGamepad is the only caller of tickVrMenu and the
// only producer of movement from QuestXr::gamepad(). VR needs it on every
// platform; a flat desktop build keeps mouse and keyboard and stays out.
#if defined(__ANDROID__) || defined(GOTHIC2VR_OPENXR)
#define GOTHIC2VR_CONTROLLER 1
#endif

class MenuRoot;
class GameSession;
class Interactive;

class MainWindow : public Tempest::Window {
  public:
    explicit MainWindow(Tempest::Device& device);
    ~MainWindow() override;

    float uiScale() const;
    int baselineExitCode() const { return baseline.exitCode(); }

  private:
    void paintEvent     (Tempest::PaintEvent& event) override;
    void resizeEvent    (Tempest::SizeEvent & event) override;

    void mouseDownEvent (Tempest::MouseEvent& event) override;
    void mouseUpEvent   (Tempest::MouseEvent& event) override;
    void mouseDragEvent (Tempest::MouseEvent& event) override;
    void mouseMoveEvent (Tempest::MouseEvent& event) override;
    void mouseWheelEvent(Tempest::MouseEvent& event) override;

    void keyDownEvent   (Tempest::KeyEvent&   event) override;
    void keyRepeatEvent (Tempest::KeyEvent&   event) override;
    void keyUpEvent     (Tempest::KeyEvent&   event) override;

    void focusEvent     (Tempest::FocusEvent&  event) override;

    void paintFocus     (Tempest::Painter& p, const Focus& fc, const Tempest::Matrix4x4& vp);
    void paintFocus     (Tempest::Painter& p, Tempest::Rect rect);

    Tempest::Size playerBarSize() const;
    void drawBar(Tempest::Painter& p, const Tempest::Texture2d *bar, int x, int y, float v, Tempest::AlignFlag flg);
    void drawMsg(Tempest::Painter& p);
    void drawProgress(Tempest::Painter& p, int x, int y, int w, int h, float v);
    void drawLoading (Tempest::Painter& p,int x,int y,int w,int h);
    void drawSaving  (Tempest::Painter& p);
    void drawSaving  (Tempest::Painter& p, const Tempest::Texture2d& back, int w, int h, float scale);

    void startGame(std::string_view slot);
    void loadGame (std::string_view slot);
    void saveGame (std::string_view slot, std::string_view name);

    void onVideo(std::string_view fname);
    void onStartLoading();
    void onWorldLoaded();
    void onSessionExit();
    void onBenchmarkFinished();
    void setGameImpl(std::unique_ptr<GameSession>&& w);
    void clearInput();
    void setFullscreen(bool fs);

    void processMouse(Tempest::MouseEvent& event, bool enable);
    void tickMouse(uint64_t dt);
    void tickGamepad();
    void applySystemWheelSelection(size_t selected, bool touch=false);
    void updateControllerOverlay();
    void paintControllerOverlay(Tempest::PaintEvent& event, int top);
    GamepadBindings::Context controllerContext() const;
    void controllerAction(const GamepadBindings::Event& event);
    void controllerQuickSlot(size_t slot, bool toggleDraw=true);
    void controllerUiKey(Tempest::Event::KeyType key, bool repeat, bool touchNavigation=false);
#if defined(__MOBILE_PLATFORM__)
    void onTouchCommand(TouchInput::Command command, bool pressed);
#endif
    void onSettings();

    void setupUi();

    void render() override;
#if defined(GOTHIC2VR_OPENXR)
    void renderVr();
    bool tickVrMenu(const Tempest::GamepadState& pad,uint64_t now);
    void paintVrOverlay(Tempest::PaintEvent& event,bool world);
    void moveRoomScale(Camera& camera,bool allowed);
#if defined(GOTHIC2VR_MIRROR)
    // Desktop mirror: drawVrMirror() records eye 0 into the swapchain image
    // while vrOutput still holds it, presentVrMirror() presents at frame end.
    bool vrMirrorEnabled();
    void drawVrMirror();
    void presentVrMirror();
#endif
#endif

    uint64_t tick();
    void     updateAnimation(uint64_t dt);
    void     tickCamera(uint64_t dt);
    void     isDialogClosed(bool& ret);

    template<Tempest::KeyEvent::KeyType k>
    void     onMarvinKey();

    Camera::Mode solveCameraMode() const;

    enum RuntimeMode : uint8_t {
      R_Normal,
      R_Suspended,
      R_Step,
      };

    Tempest::Device&      device;
#if defined(GOTHIC2VR_MIRROR)
    Tempest::Swapchain    swapchain;
#endif
#if defined(GOTHIC2VR_OPENXR)
    Tempest::Attachment   vrOutput;
    Tempest::CommandBuffer vrHudCommand;
    Tempest::Fence         vrHudFence;
    Vr::Menu              vrMenu;
    Vr::Welcome           vrWelcome;
    Vr::Gameplay          vrGameplay;
    Vr::HandRenderer      vrHands;
    Vr::Turning           vrTurning;
    Vr::Profiler          vrProfiler;
    uint64_t              vrLightingFrames=0;
    // Measured-cost local-light route (slabs vs volumes) per eye, fed with the
    // previous pipelined frame's GPU timings before the next eyes are recorded.
    LightRouteController  vrLightRoute;
    struct VrLightRoutePending { uint64_t frameId=0; std::array<uint8_t,2> slots{}; std::array<bool,2> route{},fed{}; bool valid=false; } vrLightRoutePending;
    float                 vrLightRouteWeight[2]={0,0};
    // Early left eye: the previous frame used the pinned eye slots (left 0,
    // right 1) and completed; settings of the previous frame (drain policy).
    bool                  vrPinnedSlots=false;
    std::string           vrLastSettingsKey;
    Tempest::VectorImage::Mesh vrOverlayMesh[Resources::MaxFramesInFlight];
    Npc*                  vrPlayer=nullptr;
    Camera*               vrCamera=nullptr;
    Tempest::Vec3         vrLastPlayerPosition;
    float                 vrEyeHeight=170;
    float                 vrCrouchOffset=0;
    Vr::Running           vrRunButton;
    bool                  vrRunning=false;
    int                   vrRunningLogged=-1;
    uint32_t              vrProfileGeneration=0;
    bool                  vrSaveFailed=false;
#if defined(GOTHIC2VR_MIRROR)
    Tempest::CommandBuffer vrMirrorCommand;
    Tempest::Fence         vrMirrorFence;
    uint64_t               vrMirrorPresented=0; // tickCount of the last mirror present
    bool                   vrMirrorDrawn=false; // this frame recorded a mirror image
#endif
#endif
    bool                 hdrRequested = false;
    Tempest::TextureAtlas atlas;
    Shaders               shaders;
    Renderer              renderer;

    Tempest::VectorImage  uiLayer, numOverlay;
    Tempest::VectorImage::Mesh uiMesh [Resources::MaxFramesInFlight];
    Tempest::VectorImage::Mesh numMesh[Resources::MaxFramesInFlight];

    Tempest::Fence         fence   [Resources::MaxFramesInFlight];
    Tempest::CommandBuffer commands[Resources::MaxFramesInFlight];
    // Split left eye: the preparation half of the left eye (uploads, visibility,
    // sky LUT, HiZ, shadow maps) is submitted before the main half is recorded.
    Tempest::Fence         prepFence   [Resources::MaxFramesInFlight];
    Tempest::CommandBuffer prepCommands[Resources::MaxFramesInFlight];
    bool                   prepRecorded[Resources::MaxFramesInFlight]={}; // slot holds a preparation buffer of its current frame
    uint8_t                cmdId = 0;
    BaselineSmoke          baseline;

    Tempest::Texture2d        background;
    const Tempest::Texture2d* loadBox=nullptr;
    const Tempest::Texture2d* loadVal=nullptr;

    const Tempest::Texture2d* barBack=nullptr;
    const Tempest::Texture2d* barHp  =nullptr;
    const Tempest::Texture2d* barMisc=nullptr;
    const Tempest::Texture2d* barMana=nullptr;

    const Tempest::Texture2d* focusImg=nullptr;

    const Tempest::Texture2d* saveback=nullptr;

    bool                      mouseP[Tempest::MouseEvent::ButtonBack]={};

    KeyCodec                  keycodec;

    MenuRoot                  rootMenu;
    VideoWidget               video;
    InventoryMenu             inventory;
    DialogMenu                dialogs;
    DocumentMenu              document;
    ChapterScreen             chapter;
    ConsoleWidget             console;
    RuntimeMode               runtimeMode = R_Normal;

    Tempest::Widget*          uiKeyUp=nullptr;
    Tempest::Point            dMouse;
    PlayerControl             player;
#if defined(GOTHIC2VR_CONTROLLER)
    Tempest::Timer            controllerTimer;
    GamepadBindings           controllerBindings;
    bool                      controllerConnected=false;
    bool                      controllerExploration=false;
    bool                      controllerFocused=true;
    bool                      controllerAxesBlocked=true;
    uint32_t                  controllerButtons=0;
    uint32_t                  controllerTriggers=0;
    uint32_t                  wheelHeldMask=0;
    size_t                    wheelQuickSlot=size_t(-1);
    RadialInput::StickSelector wheelStickInput;
    uint64_t                  controllerLastPoll=0;
    uint64_t                  controllerLookIdle=0;
    uint64_t                  controllerLastSwitch=0;
    bool                      controllerSwitchReady=true;
    bool                      controllerWasPresent=false;
    bool                      controllerDisconnectPending=false;
    int                       controllerOverlayContext=-1;
    std::string               controllerOverlayTitle;
    struct ControllerGuideGroup {
      std::string title;
      std::vector<std::string> lines;
      };
    std::array<ControllerGuideGroup,5> controllerOverlayGroups;
#endif
#if defined(__MOBILE_PLATFORM__)
    TouchInput                mobileUi;
    bool                      touchWheelOwned=false;
    bool onTouchWheel(TouchInput::Command command, TouchInput::WheelPhase phase, Tempest::Point pos);
    uint64_t                  touchLookIdle=0;
    bool                      touchMovementBlocked=false;
    std::optional<KeyCodec::ActionMapping> touchHeldAction;
    std::optional<KeyCodec::ActionMapping> touchTapRelease;
#endif
    uint64_t                  lastTick=0;

    Tempest::Shortcut         funcKey[11];
    Tempest::Shortcut         displayPos;

    struct BenchmarkData {
      std::vector<uint64_t> low1procent;
      size_t                numFrames = 0;
      double                fpsSum = 0;
      void                  push(uint64_t t);
      void                  clear();
      };
    struct Fps {
      uint64_t dt[10]={};
      double   get() const;
      void     push(uint64_t t);
      };
    Fps           fps;
    bool          profileGpu = false;
    uint32_t      gpuProfileFrames = 0;
    uint32_t      gpuProfileAttempts = 0;
    std::ofstream gpuProfileLog;
#if defined(__ANDROID__)
    uint64_t      fpsOverlayUpdated = 0;
    bool          showFps = false;
    uint32_t      maxFps = 60;
    Tempest::FramePacer framePacer;
#endif
    BenchmarkData benchmark;
    uint64_t      maxFpsInv = 0;
  };
