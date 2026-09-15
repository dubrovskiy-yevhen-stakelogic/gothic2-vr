#include "androidapi.h"

#include <Tempest/Platform>
#include <Tempest/Log>

#ifdef __ANDROID__

#include <android_native_app_glue.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/log.h>
#include <android/keycodes.h>
#include <jni.h>

#include <Tempest/Window>
#include <Tempest/Event>
#include <Tempest/TextCodec>
#if defined(TEMPEST_BUILD_AUDIO)
#include <Tempest/SoundDevice>
#endif

#include <string>
#include <vector>
#include <cmath>
#include <exception>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <algorithm>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "Tempest", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "Tempest", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "Tempest", __VA_ARGS__))

using namespace Tempest;

// Forward declarations
static void onAppCmd(struct android_app* app, int32_t cmd);
static int32_t onInputEvent(struct android_app* app, AInputEvent* event);

struct AndroidWindow {
  ANativeWindow*       nativeWindow = nullptr;
  Tempest::Window*     owner        = nullptr;
  int32_t              width        = 0;
  int32_t              height       = 0;
  float                density      = 1.0f;
  std::atomic_bool     hasPendingFrame{false};
  bool                 isFullscreen = true;

  struct TouchState {
    struct Touch {
      int32_t id;
      float   x;
      float   y;
      bool    active;
    };
    std::vector<Touch> touches;

    TouchState() {
      touches.reserve(10);
    }

    int add(int32_t id, float x, float y) {
      for (auto& t : touches) {
        if (!t.active) {
          t.id = id;
          t.x = x;
          t.y = y;
          t.active = true;
          return static_cast<int>(&t - touches.data());
        }
      }
      touches.push_back({id, x, y, true});
      return static_cast<int>(touches.size() - 1);
    }

    int find(int32_t id) const {
      for (size_t i = 0; i < touches.size(); ++i) {
        if (touches[i].active && touches[i].id == id)
          return static_cast<int>(i);
      }
      return -1;
    }

    int update(int32_t id, float x, float y) {
      int idx = find(id);
      if (idx >= 0) {
        touches[idx].x = x;
        touches[idx].y = y;
      }
      return idx;
    }

    int remove(int32_t id) {
      int idx = find(id);
      if (idx >= 0) {
        touches[idx].active = false;
      }
      return idx;
    }
  };
  TouchState touch;
};

// Global state
static struct android_app*  g_app          = nullptr;
static AndroidWindow*       g_mainWindow   = nullptr;
static std::atomic_bool     g_isRunning{false};
static std::atomic_bool     g_isActive{false};
static std::atomic_bool     g_hasWindow{false};
static bool                 g_isResumed = false;
static bool                 g_hasFocus  = false;
static std::mutex           g_softInputMutex;
static std::u32string       g_softInputText;

android_app* AndroidApi::nativeApp() { return g_app; }
static bool g_vrRenderLoop=false;
void AndroidApi::setVrRenderLoop(bool enabled) { g_vrRenderLoop=enabled; }

static std::mutex g_cutoutMutex;
static struct {
  int left = 0, top = 0, right = 0, bottom = 0;
  int width = 0, height = 0;
  } g_cutout;

extern "C" JNIEXPORT void JNICALL
Java_org_tempest_TempestNativeActivity_nativeCutoutChanged(JNIEnv*, jobject,
    jint left, jint top, jint right, jint bottom, jint width, jint height) {
  std::lock_guard<std::mutex> lock(g_cutoutMutex);
  g_cutout = {left,top,right,bottom,width,height};
  }

Rect AndroidApi::implWindowSafeArea(SystemApi::Window* w) {
  const auto client = implWindowClientRect(w);
  std::lock_guard<std::mutex> lock(g_cutoutMutex);
  if(g_cutout.width<=0 || g_cutout.height<=0)
    return Rect(0,0,client.w,client.h);
  // Insets arrive in Android view pixels; the rendering surface can have a different size.
  const float sx = float(client.w)/float(g_cutout.width);
  const float sy = float(client.h)/float(g_cutout.height);
  const int left = std::clamp(int(std::ceil(g_cutout.left*sx)),0,client.w);
  const int top = std::clamp(int(std::ceil(g_cutout.top*sy)),0,client.h);
  const int right = std::clamp(int(std::ceil(g_cutout.right*sx)),0,client.w-left);
  const int bottom = std::clamp(int(std::ceil(g_cutout.bottom*sy)),0,client.h-top);
  return Rect(left,top,client.w-left-right,client.h-top-bottom);
  }

// Gamepad state tracking (uses struct from SystemApi)
static GamepadState g_gamepad;
static std::mutex g_gamepadMutex;

GamepadState AndroidApi::implGamepadState() {
  std::lock_guard<std::mutex> guard(g_gamepadMutex);
  auto state = g_gamepad;
  g_gamepad.changes.clear();
  g_gamepad.overflow = false;
  return state;
}

extern "C" JNIEXPORT void JNICALL
Java_org_tempest_TempestNativeActivity_nativeGamepad(JNIEnv*, jobject, jboolean connected,
                                                   jint buttons, jfloat lx, jfloat ly,
                                                   jfloat rx, jfloat ry, jfloat lt, jfloat rt) {
  std::lock_guard<std::mutex> guard(g_gamepadMutex);
  const auto next = uint32_t(buttons);
  if(next!=g_gamepad.buttons || lt!=g_gamepad.leftTrigger || rt!=g_gamepad.rightTrigger ||
     lx!=g_gamepad.leftStickX || ly!=g_gamepad.leftStickY || rx!=g_gamepad.rightStickX || ry!=g_gamepad.rightStickY) {
    if(g_gamepad.changes.size()>=256) {
      g_gamepad.changes.clear();
      g_gamepad.overflow = true;
      }
    g_gamepad.changes.push_back({next,lx,ly,rx,ry,lt,rt});
    }
  g_gamepad.buttons = next;
  g_gamepad.connected = connected;
  g_gamepad.leftStickX = lx;
  g_gamepad.leftStickY = ly;
  g_gamepad.rightStickX = rx;
  g_gamepad.rightStickY = ry;
  g_gamepad.leftTrigger = lt;
  g_gamepad.rightTrigger = rt;
  }

std::string AndroidApi::implAppDataPath() {
  if(g_app==nullptr || g_app->activity==nullptr)
    return {};
  const char* path = g_app->activity->externalDataPath;
  if(path==nullptr || path[0]=='\0')
    path = g_app->activity->internalDataPath;
  return path==nullptr ? std::string{} : std::string(path);
}

void AndroidApi::implVibrate(uint32_t milliseconds, float strength, bool gamepad) {
  if(g_app==nullptr || g_app->activity==nullptr)
    return;
  auto& activity = *g_app->activity;
  JNIEnv* env = nullptr;
  const auto status = activity.vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6);
  const bool attach = status==JNI_EDETACHED;
  if(attach && activity.vm->AttachCurrentThread(&env,nullptr)!=JNI_OK)
    return;
  if(env==nullptr)
    return;
  jclass cls = env->GetObjectClass(activity.clazz);
  jmethodID method = cls==nullptr ? nullptr : env->GetMethodID(cls,"vibrate","(IFZ)V");
  if(method!=nullptr)
    env->CallVoidMethod(activity.clazz,method,jint(std::min(milliseconds,1000u)),jfloat(strength),jboolean(gamepad));
  if(env->ExceptionCheck())
    env->ExceptionClear();
  if(cls!=nullptr)
    env->DeleteLocalRef(cls);
  if(attach)
    activity.vm->DetachCurrentThread();
  }

void AndroidApi::implShowSoftInput(std::string_view text) {
  if(g_app==nullptr || g_app->activity==nullptr)
    return;

  auto utf16 = TextCodec::toUtf16(text);
  {
    std::lock_guard<std::mutex> lock(g_softInputMutex);
    g_softInputText.clear();
    for(size_t i=0; i<utf16.size(); ++i) {
      char32_t code = utf16[i];
      if(0xD800<=code && code<=0xDBFF && i+1<utf16.size()) {
        const char32_t low = utf16[i+1];
        if(0xDC00<=low && low<=0xDFFF) {
          code = 0x10000+((code-0xD800)<<10)+(low-0xDC00);
          ++i;
          }
        }
      g_softInputText.push_back(code);
      }
  }

  JNIEnv* env = nullptr;
  g_app->activity->vm->AttachCurrentThread(&env,nullptr);
  if(env==nullptr)
    return;
  jclass activityClass = env->GetObjectClass(g_app->activity->clazz);
  jmethodID show = activityClass==nullptr ? nullptr :
      env->GetMethodID(activityClass,"showSoftInput","(Ljava/lang/String;)V");
  if(show!=nullptr) {
    jstring value = env->NewString(reinterpret_cast<const jchar*>(utf16.data()),jsize(utf16.size()));
    env->CallVoidMethod(g_app->activity->clazz,show,value);
    env->DeleteLocalRef(value);
    }
  g_app->activity->vm->DetachCurrentThread();
  }

void AndroidApi::implHideSoftInput() {
  if(g_app==nullptr || g_app->activity==nullptr)
    return;

  JNIEnv* env = nullptr;
  g_app->activity->vm->AttachCurrentThread(&env,nullptr);
  if(env==nullptr)
    return;
  jclass activityClass = env->GetObjectClass(g_app->activity->clazz);
  jmethodID hide = activityClass==nullptr ? nullptr :
      env->GetMethodID(activityClass,"hideSoftInput","()V");
  if(hide!=nullptr)
    env->CallVoidMethod(g_app->activity->clazz,hide);
  g_app->activity->vm->DetachCurrentThread();
  }

// Event queue for cross-thread communication
struct AppEvent {
  enum Type {
    None,
    Resize,
    DisplayChanged,
    TouchDown,
    TouchMove,
    TouchUp,
    PointerScroll,
    KeyDown,
    KeyUp,
    Focus,
    Close,
    GamepadAxis
  };
  Type type = None;

  union {
    struct { int32_t w, h; } resize;
    struct { int x, y, pointerId; } touch;
    struct { int x, y, delta; } scroll;
    struct { uint32_t keyCode, code; } key;
    struct { bool gained; } focus;
    struct { float lx, ly, rx, ry, lt, rt; } gamepad;
  } data{};
};

static std::mutex              g_eventMutex;
static std::queue<AppEvent>    g_eventQueue;

// Enable immersive fullscreen mode (hide navigation bar)
static void enableImmersiveMode() {
  if (g_app == nullptr || g_app->activity == nullptr)
    return;

  JNIEnv* env = nullptr;
  g_app->activity->vm->AttachCurrentThread(&env, nullptr);
  if (env == nullptr)
    return;

  jclass activityClass = env->GetObjectClass(g_app->activity->clazz);
  if (activityClass == nullptr) {
    g_app->activity->vm->DetachCurrentThread();
    return;
  }

  // Get the window
  jmethodID getWindow = env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");
  if (getWindow == nullptr) {
    g_app->activity->vm->DetachCurrentThread();
    return;
  }
  jobject window = env->CallObjectMethod(g_app->activity->clazz, getWindow);
  if (window == nullptr) {
    g_app->activity->vm->DetachCurrentThread();
    return;
  }

  // Get the decor view
  jclass windowClass = env->GetObjectClass(window);
  jmethodID getDecorView = env->GetMethodID(windowClass, "getDecorView", "()Landroid/view/View;");
  if (getDecorView == nullptr) {
    g_app->activity->vm->DetachCurrentThread();
    return;
  }
  jobject decorView = env->CallObjectMethod(window, getDecorView);
  if (decorView == nullptr) {
    g_app->activity->vm->DetachCurrentThread();
    return;
  }

  // Set system UI visibility flags for immersive mode
  jclass viewClass = env->GetObjectClass(decorView);
  jmethodID setSystemUiVisibility = env->GetMethodID(viewClass, "setSystemUiVisibility", "(I)V");
  if (setSystemUiVisibility != nullptr) {
    // SYSTEM_UI_FLAG_FULLSCREEN = 0x00000004
    // SYSTEM_UI_FLAG_HIDE_NAVIGATION = 0x00000002
    // SYSTEM_UI_FLAG_IMMERSIVE_STICKY = 0x00001000
    // SYSTEM_UI_FLAG_LAYOUT_STABLE = 0x00000100
    // SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION = 0x00000200
    // SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN = 0x00000400
    const int flags = 0x00000004 | 0x00000002 | 0x00001000 | 0x00000100 | 0x00000200 | 0x00000400;
    env->CallVoidMethod(decorView, setSystemUiVisibility, flags);
    LOGI("Immersive mode enabled");
  }

  g_app->activity->vm->DetachCurrentThread();
}

static void pushEvent(const AppEvent& evt) {
  std::lock_guard<std::mutex> lock(g_eventMutex);
  g_eventQueue.push(evt);
}

static bool popEvent(AppEvent& evt) {
  std::lock_guard<std::mutex> lock(g_eventMutex);
  if (g_eventQueue.empty())
    return false;
  evt = g_eventQueue.front();
  g_eventQueue.pop();
  return true;
}

extern "C" JNIEXPORT void JNICALL
Java_org_tempest_TempestNativeActivity_nativeDisplayChanged(JNIEnv*, jobject) {
  AppEvent evt;
  evt.type = AppEvent::DisplayChanged;
  pushEvent(evt);
  }

extern "C" float tempest_android_hdr_peak_luminance() {
  if(g_app==nullptr || g_app->activity==nullptr)
    return 0.f;
  auto* vm = g_app->activity->vm;
  JNIEnv* env = nullptr;
  const bool attach = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6)==JNI_EDETACHED;
  if(attach && vm->AttachCurrentThread(&env,nullptr)!=JNI_OK)
    return 0.f;
  if(env==nullptr)
    return 0.f;
  jclass cls = env->GetObjectClass(g_app->activity->clazz);
  jmethodID method = cls==nullptr ? nullptr : env->GetMethodID(cls,"getHdrPeakLuminance","()F");
  float peak = method==nullptr ? 0.f : env->CallFloatMethod(g_app->activity->clazz,method);
  if(env->ExceptionCheck()) {
    env->ExceptionClear();
    peak = 0.f;
    }
  if(cls!=nullptr)
    env->DeleteLocalRef(cls);
  if(attach)
    vm->DetachCurrentThread();
  return peak;
  }

static void pushKey(uint32_t keyCode, uint32_t code) {
  AppEvent evt;
  evt.data.key.keyCode = keyCode;
  evt.data.key.code    = code;
  evt.type = AppEvent::KeyDown;
  pushEvent(evt);
  evt.type = AppEvent::KeyUp;
  pushEvent(evt);
  }

static std::u32string fromJavaString(JNIEnv* env, jstring text) {
  std::u32string ret;
  if(text==nullptr)
    return ret;

  const jsize size = env->GetStringLength(text);
  const jchar* src = env->GetStringChars(text,nullptr);
  if(src==nullptr)
    return ret;
  ret.reserve(size_t(size));
  for(jsize i=0; i<size; ++i) {
    char32_t code = src[i];
    if(0xD800<=code && code<=0xDBFF && i+1<size) {
      const char32_t low = src[i+1];
      if(0xDC00<=low && low<=0xDFFF) {
        code = 0x10000+((code-0xD800)<<10)+(low-0xDC00);
        ++i;
        }
      }
    ret.push_back(code);
    }
  env->ReleaseStringChars(text,src);
  return ret;
  }

extern "C" JNIEXPORT void JNICALL
Java_org_tempest_TempestNativeActivity_nativeSetText(JNIEnv* env, jobject, jstring text) {
  auto next = fromJavaString(env,text);
  std::lock_guard<std::mutex> lock(g_softInputMutex);
  for(size_t i=0; i<g_softInputText.size(); ++i)
    pushKey(AKEYCODE_DEL,0);
  for(char32_t code:next)
    pushKey(AKEYCODE_UNKNOWN,uint32_t(code));
  g_softInputText = std::move(next);
  }

extern "C" JNIEXPORT void JNICALL
Java_org_tempest_TempestNativeActivity_nativeEditorAction(JNIEnv*, jobject) {
  pushKey(AKEYCODE_ENTER,'\n');
  }

static void useMusicVolumeControls() {
  if(g_app==nullptr || g_app->activity==nullptr)
    return;

  JNIEnv* env = nullptr;
  g_app->activity->vm->AttachCurrentThread(&env,nullptr);
  if(env==nullptr)
    return;

  jclass activityClass = env->GetObjectClass(g_app->activity->clazz);
  if(activityClass!=nullptr) {
    jmethodID setVolumeControlStream = env->GetMethodID(activityClass,"setVolumeControlStream","(I)V");
    if(setVolumeControlStream!=nullptr)
      env->CallVoidMethod(g_app->activity->clazz,setVolumeControlStream,3);
    }
  g_app->activity->vm->DetachCurrentThread();
  }

// Handle application lifecycle commands
static void onAppCmd(struct android_app* app, int32_t cmd) {
  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      if (app->window != nullptr) {
        g_hasWindow.store(true);
        enableImmersiveMode();  // Enable immersive fullscreen mode
        if (g_mainWindow != nullptr) {
          g_mainWindow->nativeWindow = app->window;
          g_mainWindow->width  = ANativeWindow_getWidth(app->window);
          g_mainWindow->height = ANativeWindow_getHeight(app->window);

          AppEvent evt;
          evt.type = AppEvent::Resize;
          evt.data.resize.w = g_mainWindow->width;
          evt.data.resize.h = g_mainWindow->height;
          pushEvent(evt);
        }
        LOGI("Window initialized: %dx%d", ANativeWindow_getWidth(app->window), ANativeWindow_getHeight(app->window));
      }
      break;

    case APP_CMD_TERM_WINDOW:
      g_hasWindow.store(false);
      if (g_mainWindow != nullptr) {
        g_mainWindow->nativeWindow = nullptr;
      }
      LOGI("Window terminated");
      break;

    case APP_CMD_GAINED_FOCUS:
      g_hasFocus = true;
      g_isActive.store(g_isResumed && g_hasFocus);
      enableImmersiveMode();  // Re-enable immersive mode when focus is gained
      {
        AppEvent evt;
        evt.type = AppEvent::Focus;
        evt.data.focus.gained = true;
        pushEvent(evt);
      }
      LOGI("Focus gained");
      break;

    case APP_CMD_LOST_FOCUS:
      g_hasFocus = false;
      g_isActive.store(false);
      {
        AppEvent evt;
        evt.type = AppEvent::Focus;
        evt.data.focus.gained = false;
        pushEvent(evt);
      }
      LOGI("Focus lost");
      break;

    case APP_CMD_START:
      LOGI("App started");
      break;

    case APP_CMD_RESUME:
      g_isResumed = true;
      g_isActive.store(g_isResumed && g_hasFocus);
      ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
#if defined(TEMPEST_BUILD_AUDIO)
      SoundDevice::resumeAll();
#endif
      enableImmersiveMode();  // Re-enable immersive mode on resume
      LOGI("App resumed");
      break;

    case APP_CMD_PAUSE:
      g_isResumed = false;
      g_isActive.store(false);
#if defined(TEMPEST_BUILD_AUDIO)
      SoundDevice::pauseAll();
#endif
      ANativeActivity_setWindowFlags(app->activity, 0, AWINDOW_FLAG_KEEP_SCREEN_ON);
      LOGI("App paused");
      break;

    case APP_CMD_STOP:
      LOGI("App stopped");
      break;

    case APP_CMD_DESTROY:
      g_isRunning.store(false);
      {
        AppEvent evt;
        evt.type = AppEvent::Close;
        pushEvent(evt);
      }
      LOGI("App destroyed");
      break;

    case APP_CMD_CONFIG_CHANGED:
      LOGI("Config changed");
      // Window resize is handled in APP_CMD_WINDOW_RESIZED or when we get a new window
      break;

    case APP_CMD_WINDOW_RESIZED:
      if (app->window != nullptr && g_mainWindow != nullptr) {
        int32_t newW = ANativeWindow_getWidth(app->window);
        int32_t newH = ANativeWindow_getHeight(app->window);
        LOGI("Window resized: %dx%d -> %dx%d", g_mainWindow->width, g_mainWindow->height, newW, newH);
        if (newW != g_mainWindow->width || newH != g_mainWindow->height) {
          g_mainWindow->width  = newW;
          g_mainWindow->height = newH;

          AppEvent evt;
          evt.type = AppEvent::Resize;
          evt.data.resize.w = newW;
          evt.data.resize.h = newH;
          pushEvent(evt);
        }
      }
      break;

    default:
      break;
  }
}

// Handle input events
static int32_t onInputEvent(struct android_app* app, AInputEvent* event) {
  if (g_mainWindow == nullptr || g_mainWindow->owner == nullptr)
    return 0;

  int32_t eventType = AInputEvent_getType(event);
  int32_t source = AInputEvent_getSource(event);

  // The activity reports controller inputs without translating them into keyboard keys.
  const bool pointer=(source & AINPUT_SOURCE_CLASS_POINTER)!=0;
  if(!pointer && ((source & AINPUT_SOURCE_GAMEPAD)==AINPUT_SOURCE_GAMEPAD ||
     (source & AINPUT_SOURCE_JOYSTICK)==AINPUT_SOURCE_JOYSTICK))
    return 0;

  if (eventType == AINPUT_EVENT_TYPE_MOTION) {

    // Touch input
    int32_t action = AMotionEvent_getAction(event);
    int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
    int32_t pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    float x = AMotionEvent_getX(event, pointerIndex);
    float y = AMotionEvent_getY(event, pointerIndex);
    int32_t pointerId = AMotionEvent_getPointerId(event, pointerIndex);

    static unsigned pointerLogCount=0;
    if(pointerLogCount<16 && (actionMasked==AMOTION_EVENT_ACTION_DOWN || actionMasked==AMOTION_EVENT_ACTION_UP ||
                             actionMasked==AMOTION_EVENT_ACTION_SCROLL)) {
      ++pointerLogCount;
      LOGI("Panel pointer action=%d source=0x%x x=%.1f y=%.1f",actionMasked,source,x,y);
      }

    AppEvent evt;
    evt.data.touch.x = static_cast<int>(x);
    evt.data.touch.y = static_cast<int>(y);

    switch (actionMasked) {
      case AMOTION_EVENT_ACTION_SCROLL: {
        const float vertical=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_VSCROLL,0);
        if(vertical==0) return 0;
        evt.type=AppEvent::PointerScroll;
        evt.data.scroll={int(x),int(y),vertical>0 ? 120 : -120};
        pushEvent(evt);
        return 1;
        }
      case AMOTION_EVENT_ACTION_DOWN:
      case AMOTION_EVENT_ACTION_POINTER_DOWN:
        evt.type = AppEvent::TouchDown;
        evt.data.touch.pointerId = g_mainWindow->touch.add(pointerId, x, y);
        pushEvent(evt);
        return 1;

      case AMOTION_EVENT_ACTION_UP:
      case AMOTION_EVENT_ACTION_POINTER_UP:
        evt.type = AppEvent::TouchUp;
        evt.data.touch.pointerId = g_mainWindow->touch.remove(pointerId);
        pushEvent(evt);
        return 1;

      case AMOTION_EVENT_ACTION_MOVE:
        // Handle all pointers that moved
        for (size_t i = 0; i < AMotionEvent_getPointerCount(event); ++i) {
          int32_t pId = AMotionEvent_getPointerId(event, i);
          float px = AMotionEvent_getX(event, i);
          float py = AMotionEvent_getY(event, i);
          int idx = g_mainWindow->touch.update(pId, px, py);
          if (idx >= 0) {
            evt.type = AppEvent::TouchMove;
            evt.data.touch.x = static_cast<int>(px);
            evt.data.touch.y = static_cast<int>(py);
            evt.data.touch.pointerId = idx;
            pushEvent(evt);
          }
        }
        return 1;

      case AMOTION_EVENT_ACTION_CANCEL:
        // Cancel all touches
        for (auto& t : g_mainWindow->touch.touches) {
          if (t.active) {
            evt.type = AppEvent::TouchUp;
            // A cancelled ray press must not activate its button on release.
            evt.data.touch.x = -1;
            evt.data.touch.y = -1;
            evt.data.touch.pointerId = g_mainWindow->touch.find(t.id);
            t.active = false;
            pushEvent(evt);
          }
        }
        return 1;

      default:
        break;
    }
  }
  else if (eventType == AINPUT_EVENT_TYPE_KEY) {
    int32_t action  = AKeyEvent_getAction(event);
    int32_t keyCode = AKeyEvent_getKeyCode(event);

    // Leave volume keys to Android so it can adjust the activity's music stream.
    if(keyCode==AKEYCODE_VOLUME_UP || keyCode==AKEYCODE_VOLUME_DOWN || keyCode==AKEYCODE_VOLUME_MUTE)
      return 0;

    AppEvent evt;
    evt.data.key.keyCode = keyCode;
    if (action == AKEY_EVENT_ACTION_DOWN) {
      evt.type = AppEvent::KeyDown;
      pushEvent(evt);
      return 1;
    }
    else if (action == AKEY_EVENT_ACTION_UP) {
      evt.type = AppEvent::KeyUp;
      pushEvent(evt);
      return 1;
    }
  }

  return 0;
}

static SystemApi::Window* createWindow(Tempest::Window* owner, uint32_t w, uint32_t h, SystemApi::ShowMode mode) {
  if (g_mainWindow == nullptr) {
    g_mainWindow = new AndroidWindow();
  }

  g_mainWindow->owner = owner;

  if (g_app != nullptr && g_app->window != nullptr) {
    g_mainWindow->nativeWindow = g_app->window;
    g_mainWindow->width  = ANativeWindow_getWidth(g_app->window);
    g_mainWindow->height = ANativeWindow_getHeight(g_app->window);
  }

  g_mainWindow->hasPendingFrame.store(true);
  return reinterpret_cast<SystemApi::Window*>(g_mainWindow);
}

AndroidApi::AndroidApi() {
  // Setup key translation table for Android keycodes
  static const TranslateKeyPair k[] = {
    { AKEYCODE_CTRL_LEFT,    Event::K_LControl },
    { AKEYCODE_CTRL_RIGHT,   Event::K_RControl },

    { AKEYCODE_SHIFT_LEFT,   Event::K_LShift   },
    { AKEYCODE_SHIFT_RIGHT,  Event::K_RShift   },

    { AKEYCODE_ALT_LEFT,     Event::K_LAlt     },
    { AKEYCODE_ALT_RIGHT,    Event::K_RAlt     },

    { AKEYCODE_DPAD_LEFT,    Event::K_Left     },
    { AKEYCODE_DPAD_RIGHT,   Event::K_Right    },
    { AKEYCODE_DPAD_UP,      Event::K_Up       },
    { AKEYCODE_DPAD_DOWN,    Event::K_Down     },

    { AKEYCODE_ESCAPE,       Event::K_ESCAPE   },
    { AKEYCODE_BACK,         Event::K_ESCAPE   },  // Android back button as escape
    { AKEYCODE_DEL,          Event::K_Back     },  // Android DEL is backspace
    { AKEYCODE_TAB,          Event::K_Tab      },
    { AKEYCODE_FORWARD_DEL,  Event::K_Delete   },
    { AKEYCODE_INSERT,       Event::K_Insert   },
    { AKEYCODE_MOVE_HOME,    Event::K_Home     },
    { AKEYCODE_MOVE_END,     Event::K_End      },
    { AKEYCODE_BREAK,        Event::K_Pause    },
    { AKEYCODE_ENTER,        Event::K_Return   },
    { AKEYCODE_SPACE,        Event::K_Space    },
    { AKEYCODE_CAPS_LOCK,    Event::K_CapsLock },

    { AKEYCODE_F1,           Event::K_F1       },
    { AKEYCODE_0,            Event::K_0        },
    { AKEYCODE_A,            Event::K_A        },

    { 0,                     Event::K_NoKey    }
    };
  setupKeyTranslate(k, 32);
}

SystemApi::Window* AndroidApi::implCreateWindow(Tempest::Window* owner, uint32_t width, uint32_t height) {
  return ::createWindow(owner, width, height, ShowMode::Maximized);
}

SystemApi::Window* AndroidApi::implCreateWindow(Tempest::Window* owner, SystemApi::ShowMode sm) {
  return ::createWindow(owner, 800, 600, sm);
}

void AndroidApi::implDestroyWindow(SystemApi::Window* w) {
  if (g_mainWindow != nullptr && reinterpret_cast<SystemApi::Window*>(g_mainWindow) == w) {
    g_mainWindow->owner = nullptr;
  }
}

void AndroidApi::implExit() {
  g_isRunning.store(false);
}

Tempest::Rect AndroidApi::implWindowClientRect(Window* w) {
  auto* wnd = reinterpret_cast<AndroidWindow*>(w);
  if (wnd == nullptr)
    return Rect(0, 0, 0, 0);
  return Rect(0, 0, wnd->width, wnd->height);
}

bool AndroidApi::implSetAsFullscreen(Window* w, bool fullScreen) {
  auto* wnd = reinterpret_cast<AndroidWindow*>(w);
  if (wnd != nullptr) {
    wnd->isFullscreen = fullScreen;
  }
  // Android apps are typically always fullscreen
  return true;
}

bool AndroidApi::implIsFullscreen(Window* w) {
  auto* wnd = reinterpret_cast<AndroidWindow*>(w);
  return wnd != nullptr ? wnd->isFullscreen : true;
}

void AndroidApi::implSetCursorPosition(Window* w, int x, int y) {
  // No cursor on Android touch devices
}

void AndroidApi::implShowCursor(Window* w, CursorShape cursor) {
  // No cursor on Android touch devices
}

bool AndroidApi::implIsRunning() {
  return g_isRunning.load();
}

int AndroidApi::implExec(AppCallBack& cb) {
  g_isRunning.store(true);

  while (g_isRunning.load()) {
    // Process Android events
    int events;
    struct android_poll_source* source;

    // Poll with timeout when active, block when inactive
    int timeout = (g_vrRenderLoop || g_isActive.load()) ? 0 : -1;
    while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
      if (source != nullptr) {
        source->process(g_app, source);
      }

      if (g_app->destroyRequested != 0) {
        g_isRunning.store(false);
        break;
      }

      timeout = 0; // Don't block on subsequent polls in this iteration
    }

    if (!g_isRunning.load())
      break;

    // Process our event queue
    implProcessEvents(cb);

    // Run the timer callback (game loop)
    if (g_vrRenderLoop || (g_isActive.load() && g_hasWindow.load())) {
      if (!cb.onTimer()) {
        std::this_thread::yield();
      }
    }
  }

  return 0;
}

void AndroidApi::implProcessEvents(AppCallBack& cb) {
  // Modal Tempest dialogs run their own event loop.
  // Keep Android input and lifecycle events moving while that loop is active.
  int events = 0;
  struct android_poll_source* source = nullptr;
  while(ALooper_pollOnce(0,nullptr,&events,reinterpret_cast<void**>(&source))>=0) {
    if(source!=nullptr)
      source->process(g_app,source);
    if(g_app->destroyRequested!=0) {
      g_isRunning.store(false);
      return;
      }
    }

  if (g_mainWindow == nullptr || g_mainWindow->owner == nullptr)
    return;

  auto& wnd = *g_mainWindow->owner;
  AppEvent evt;

  while (popEvent(evt)) {
    switch (evt.type) {
      case AppEvent::DisplayChanged: {
        if(g_hasWindow.load()) {
          const auto size = implWindowClientRect(reinterpret_cast<SystemApi::Window*>(g_mainWindow));
          SizeEvent e(size.w, size.h);
          AndroidApi::dispatchResize(wnd, e, true);
          }
        break;
      }
      case AppEvent::Resize: {
        SizeEvent e(evt.data.resize.w, evt.data.resize.h);
        AndroidApi::dispatchResize(wnd, e, true);
        break;
      }

      case AppEvent::TouchDown: {
        MouseEvent e(evt.data.touch.x, evt.data.touch.y,
                     Event::ButtonLeft, Event::M_NoModifier,
                     0, evt.data.touch.pointerId, Event::MouseDown);
        AndroidApi::dispatchMouseDown(wnd, e);
        break;
      }

      case AppEvent::TouchMove: {
        MouseEvent e(evt.data.touch.x, evt.data.touch.y,
                     Event::ButtonLeft, Event::M_NoModifier,
                     0, evt.data.touch.pointerId, Event::MouseMove);
        AndroidApi::dispatchMouseMove(wnd, e);
        break;
      }

      case AppEvent::TouchUp: {
        MouseEvent e(evt.data.touch.x, evt.data.touch.y,
                     Event::ButtonLeft, Event::M_NoModifier,
                     0, evt.data.touch.pointerId, Event::MouseUp);
        AndroidApi::dispatchMouseUp(wnd, e);
        break;
      }

      case AppEvent::PointerScroll: {
        MouseEvent e(evt.data.scroll.x,evt.data.scroll.y,Event::ButtonNone,Event::M_NoModifier,
                     evt.data.scroll.delta,0,Event::MouseWheel);
        AndroidApi::dispatchMouseWheel(wnd,e);
        break;
      }

      case AppEvent::KeyDown: {
        // Map Android key codes to Tempest key codes using the translation table
        auto key = Event::KeyType(translateKey(evt.data.key.keyCode));
        KeyEvent e(key, evt.data.key.code, Event::M_NoModifier, Event::KeyDown);
        AndroidApi::dispatchKeyDown(wnd, e, evt.data.key.keyCode);
        break;
      }

      case AppEvent::KeyUp: {
        auto key = Event::KeyType(translateKey(evt.data.key.keyCode));
        KeyEvent e(key, evt.data.key.code, Event::M_NoModifier, Event::KeyUp);
        AndroidApi::dispatchKeyUp(wnd, e, evt.data.key.keyCode);
        break;
      }

      case AppEvent::Focus: {
        FocusEvent e(evt.data.focus.gained, Event::FocusReason::UnknownReason);
        AndroidApi::dispatchFocus(wnd, e);
        break;
      }

      case AppEvent::Close: {
        CloseEvent e;
        AndroidApi::dispatchClose(wnd, e);
        break;
      }

      default:
        break;
    }
  }

  // Trigger render if active
  // OpenXR must process session state changes and submit empty frames while
  // unfocused. Its Vulkan targets do not depend on the Android window surface.
  if ((g_vrRenderLoop || (g_isActive.load() && g_hasWindow.load())) && g_mainWindow->hasPendingFrame.load()) {
    g_mainWindow->hasPendingFrame.store(false);
    AndroidApi::dispatchRender(wnd);
    g_mainWindow->hasPendingFrame.store(true);
  }
}

void AndroidApi::implSetWindowTitle(Window* w, const char* utf8) {
  // Android doesn't have traditional window titles
}

// Entry point called from android_main
extern "C" void tempest_android_main(struct android_app* app);

void tempest_android_main(struct android_app* app) {
  g_app = app;
  app->onAppCmd     = onAppCmd;
  app->onInputEvent = onInputEvent;
  useMusicVolumeControls();

  // Wait for window to be ready
  while (!g_hasWindow.load()) {
    int events;
    struct android_poll_source* source;
    if (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
      if (source != nullptr) {
        source->process(app, source);
      }
    }
    if (app->destroyRequested != 0) {
      return;
    }
  }
}

// Provide access to native window for Vulkan surface creation
extern "C" ANativeWindow* tempest_android_get_native_window() {
  return g_mainWindow != nullptr ? g_mainWindow->nativeWindow : nullptr;
}

extern "C" struct android_app* tempest_android_get_app() {
  return g_app;
}

// The application entry point - calls user's main() after setup
int main(int argc, const char** argv);

extern "C" void android_main(struct android_app* app) {
  tempest_android_main(app);

  const char* argv[] = {"app", nullptr};
  try {
    if(app->destroyRequested==0)
      main(1, argv);
    }
  catch(const std::exception& e) {
    LOGE("Unhandled native exception: %s", e.what());
    }
  catch(...) {
    LOGE("Unhandled native exception");
    }

  // NativeActivity callbacks wait for acknowledgements from this thread.
  // Finish after application cleanup and keep servicing the glue until destruction.
  if(app->destroyRequested==0)
    ANativeActivity_finish(app->activity);
  while(app->destroyRequested==0) {
    int events = 0;
    android_poll_source* source = nullptr;
    if(ALooper_pollOnce(-1,nullptr,&events,reinterpret_cast<void**>(&source))>=0 && source!=nullptr)
      source->process(app,source);
    }

  delete g_mainWindow;
  g_mainWindow = nullptr;
  g_isRunning.store(false);
  g_isActive.store(false);
  g_hasWindow.store(false);
  g_isResumed = false;
  g_hasFocus = false;
  {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    g_eventQueue = {};
    }
  {
    std::lock_guard<std::mutex> lock(g_softInputMutex);
    g_softInputText.clear();
    }
  g_app = nullptr;
}

#endif // __ANDROID__
