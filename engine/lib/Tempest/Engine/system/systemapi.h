#pragma once

#include <Tempest/Platform>
#include <Tempest/Rect>
#include <Tempest/WidgetState>

#include <memory>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Tempest {

struct GamepadState {
  enum Button : uint32_t {
    A=1u<<0, B=1u<<1, X=1u<<2, Y=1u<<3,
    L1=1u<<4, R1=1u<<5, L3=1u<<6, R3=1u<<7,
    Start=1u<<8, Select=1u<<9,
    Up=1u<<10, Down=1u<<11, Left=1u<<12, Right=1u<<13,
    L2=1u<<14, R2=1u<<15,
    };
  uint32_t buttons = 0;
    struct Sample {
      uint32_t buttons = 0;
      float leftStickX = 0, leftStickY = 0;
      float rightStickX = 0, rightStickY = 0;
      float leftTrigger = 0, rightTrigger = 0;
      };
    // Keep analog triggers in the same ordered stream as buttons for modifier chords.
    std::vector<Sample> changes;
    bool overflow = false;
  float leftStickX   = 0.0f;
  float leftStickY   = 0.0f;
  float rightStickX  = 0.0f;
  float rightStickY  = 0.0f;
  float leftTrigger  = 0.0f;
  float rightTrigger = 0.0f;
  bool  connected    = false;
};

class SizeEvent;
class MouseEvent;
class KeyEvent;
class CloseEvent;
class PaintEvent;
class FocusEvent;

class Widget;
class Window;
class UiOverlay;
class Application;

class SystemApi {
  public:
    struct Window;

    enum ShowMode : uint8_t {
      Minimized,
      Normal,
      Maximized,
      FullScreen,

      Hidden, //internal
      };

    struct TranslateKeyPair final {
      uint16_t src;
      uint16_t result;
      };

    virtual ~SystemApi()=default;
    static Window*  createWindow(Tempest::Window* owner, uint32_t width, uint32_t height);
    static Window*  createWindow(Tempest::Window* owner, ShowMode sm);
    static void     destroyWindow(Window* w);
    static void     exit();

    static Rect     windowClientRect(SystemApi::Window *w);
    /// Client-coordinate area that avoids display cutouts, in rendering pixels.
    static Rect     windowSafeArea(SystemApi::Window *w);

    static bool     setAsFullscreen(SystemApi::Window *w, bool fullScreen);
    static bool     isFullscreen(SystemApi::Window *w);

    static void     setWindowTitle(SystemApi::Window *w, const char* utf8);
    static float    uiScale(SystemApi::Window *w);

    static uint16_t translateKey(uint64_t scancode);
    static void     setupKeyTranslate(const TranslateKeyPair k[], uint16_t funcCount);

    static void     addOverlay (UiOverlay* ui);
    static void     takeOverlay(UiOverlay* ui);

    static GamepadState gamepadState();
    /// Finite vibration on the phone or active controller; zero duration stops both.
    /// Unsupported platforms and devices ignore this request.
    /// Android applications must declare android.permission.VIBRATE in their manifest.
    static void vibrate(uint32_t milliseconds, float strength, bool gamepad);
    static void showSoftInput(std::string_view text);
    static void hideSoftInput();
    /// Writable persistent directory owned by the application.
    /// Returns empty on platforms that do not provide one through the windowing backend.
    static std::string appDataPath();

  protected:
    struct AppCallBack {
      virtual ~AppCallBack()=default;
      virtual uint32_t onTimer()=0;
      };

    SystemApi();
    virtual Window*  implCreateWindow (Tempest::Window *owner,uint32_t width,uint32_t height) = 0;
    virtual Window*  implCreateWindow (Tempest::Window *owner,ShowMode sm) = 0;
    virtual void     implDestroyWindow(Window* w) = 0;
    virtual void     implExit() = 0;

    virtual Rect     implWindowClientRect(SystemApi::Window *w) = 0;
    virtual Rect     implWindowSafeArea(SystemApi::Window *w);

    virtual bool     implSetAsFullscreen(SystemApi::Window *w, bool fullScreen) = 0;
    virtual bool     implIsFullscreen(SystemApi::Window *w) = 0;

    virtual void     implSetCursorPosition(SystemApi::Window *w, int x, int y) = 0;
    virtual void     implShowCursor(SystemApi::Window *w, CursorShape show) = 0;

    virtual float    implUiScale(SystemApi::Window* w);

    virtual GamepadState implGamepadState();
    virtual void         implVibrate(uint32_t milliseconds, float strength, bool gamepad);
    virtual void         implShowSoftInput(std::string_view text);
    virtual void         implHideSoftInput();
    virtual std::string  implAppDataPath();

    virtual bool     implIsRunning() = 0;
    virtual int      implExec(AppCallBack& cb) = 0;
    virtual void     implProcessEvents(AppCallBack& cb) = 0;

    virtual void     implSetWindowTitle(SystemApi::Window *w, const char* utf8) = 0;

    static void      setCursorPosition(SystemApi::Window *w, int x, int y);
    static void      showCursor(SystemApi::Window *w, CursorShape c);

    static void      dispatchOverlayRender(Tempest::Window &w, Tempest::PaintEvent& e);
    static void      dispatchRender    (Tempest::Window& cb);
    static void      dispatchMouseDown (Tempest::Window& cb, MouseEvent& e);
    static void      dispatchMouseUp   (Tempest::Window& cb, MouseEvent& e);
    static void      dispatchMouseMove (Tempest::Window& cb, MouseEvent& e);
    static void      dispatchMouseWheel(Tempest::Window& cb, MouseEvent& e);

    static void      dispatchKeyDown   (Tempest::Window& cb, KeyEvent& e, uint32_t scancode);
    static void      dispatchKeyUp     (Tempest::Window& cb, KeyEvent& e, uint32_t scancode);

    static void      dispatchResize    (Tempest::Window& cb, SizeEvent& e, bool force = false);
    static void      dispatchClose     (Tempest::Window& cb, CloseEvent& e);

    static void      dispatchFocus     (Tempest::Window& cb, FocusEvent& e);

    static SystemApi& inst();

  private:
    static bool       isRunning();
    static int        exec(AppCallBack& cb);
    static void       processEvent(AppCallBack& cb);

    struct Data;
    static Data m;

  friend class Tempest::Window;
  friend class Tempest::Application;
  };

}
