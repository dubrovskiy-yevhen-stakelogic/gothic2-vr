#pragma once
// Platform-specific OpenXR glue for the Quest (Android) and PCVR (Win32)
// targets, kept out of questxr.cpp so the session code keeps one call site per
// platform difference instead of scattered #ifdefs.
//
// Include order: this header establishes XR_USE_PLATFORM_* and
// XR_USE_GRAPHICS_API_VULKAN itself when they are not set yet, so it is
// self-contained and may be included first (diagnostics/vrinfo.cpp does).
// questxr.h sets the same macros to the same values before it includes
// <openxr/openxr.h>, so including this header afterwards is a no-op for both
// the macros and the OpenXR headers (questxr.cpp does that).
//
// Vulkan arrives through Tempest's gapi/vulkaninterop.h and never through
// <vulkan/vulkan.h> directly: that header redefines
// VK_DEFINE_NON_DISPATCHABLE_HANDLE, so mixing the two routes in one build
// would change VkImage's type between translation units.
#if defined(GOTHIC2VR_OPENXR)
#include "gapi/vulkaninterop.h"

#if defined(__ANDROID__)
#include <jni.h>
#include <unistd.h>
#if !defined(XR_USE_PLATFORM_ANDROID)
#define XR_USE_PLATFORM_ANDROID
#endif
#elif defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <unknwn.h>
// windows.h leaks object-like macros whose names are also enumerators and
// constants in ZenKit's public headers, which every VR translation unit reaches
// through gothic.h: ERROR (wingdi.h; zenkit::LogLevel, game/main.cpp),
// VOID (winnt.h) and CONST (minwindef.h) (zenkit::DaedalusDataType and
// DaedalusScript), TRANSPARENT/OPAQUE (wingdi.h; zenkit::MenuItemFlag) and
// small (rpcndr.h, via <unknwn.h>). OpenGothic uses none of the Windows ones,
// and no Windows header is included past this point. #undef of an undefined
// macro is a no-op, so no defined() guard is needed.
#undef ERROR
#undef VOID
#undef CONST
#undef TRANSPARENT
#undef OPAQUE
#undef small
#if !defined(XR_USE_PLATFORM_WIN32)
#define XR_USE_PLATFORM_WIN32
#endif
#endif
#if !defined(XR_USE_GRAPHICS_API_VULKAN)
#define XR_USE_GRAPHICS_API_VULKAN
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#if defined(__ANDROID__)
#include "system/api/androidapi.h"
#include <android_native_app_glue.h>
#elif defined(_WIN32)
#include "system/api/windowsapi.h"
#endif

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace Vr {
namespace Platform {

// Android needs the loader pointed at the VM and activity before any other
// OpenXR call; the Windows loader initialises itself on first use.
inline void initializeLoader() {
#if defined(__ANDROID__)
  auto app=Tempest::AndroidApi::nativeApp();
  if(app==nullptr || app->activity==nullptr) throw std::runtime_error("OpenXR: Android activity missing");
  PFN_xrInitializeLoaderKHR init=nullptr;
  auto r=xrGetInstanceProcAddr(XR_NULL_HANDLE,"xrInitializeLoaderKHR",reinterpret_cast<PFN_xrVoidFunction*>(&init));
  if(XR_FAILED(r)) throw std::runtime_error("loader entry: OpenXR "+std::to_string(r)+" ");
  XrLoaderInitInfoAndroidKHR loader{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
  loader.applicationVM=app->activity->vm; loader.applicationContext=app->activity->clazz;
  r=init(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loader));
  if(XR_FAILED(r)) throw std::runtime_error("initialize loader: OpenXR "+std::to_string(r)+" ");
#endif
  }

// Extensions the instance cannot be created without. Optional ones stay in
// questxr.cpp, which probes them against xrEnumerateInstanceExtensionProperties.
inline std::vector<const char*> requiredExtensions() {
#if defined(__ANDROID__)
  return {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
#else
  return {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
#endif
  }

// XrInstanceCreateInfo::next. The returned struct has static storage, so it
// outlives the xrCreateInstance call that reads it.
inline const void* instanceCreateNext() {
#if defined(__ANDROID__)
  static XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
  auto app=Tempest::AndroidApi::nativeApp();
  if(app==nullptr || app->activity==nullptr) throw std::runtime_error("OpenXR: Android activity missing");
  android.applicationVM=app->activity->vm; android.applicationActivity=app->activity->clazz;
  return &android;
#else
  return nullptr;
#endif
  }

// Thread id in the form xrSetAndroidApplicationThreadKHR expects. Desktop
// runtimes never list that extension, so the value is only ever logged there.
inline uint32_t currentThreadId() {
#if defined(__ANDROID__)
  return uint32_t(gettid());
#elif defined(_WIN32)
  return uint32_t(GetCurrentThreadId());
#else
  return 0;
#endif
  }

// Tells the host loop that a VR frame loop owns rendering. Android uses it to
// keep pumping while the activity is not resumed; Win32 uses it to keep
// dispatching render while the mirror window is iconic, because
// windowsapi.cpp's loop otherwise skips dispatchRender for a minimized window
// and the headset would freeze for as long as the desktop window is minimized.
inline void setVrRenderLoop(bool enabled) {
#if defined(__ANDROID__)
  Tempest::AndroidApi::setVrRenderLoop(enabled);
#elif defined(_WIN32)
  Tempest::WindowsApi::setVrRenderLoop(enabled);
#else
  (void)enabled;
#endif
  }

// Whether the desktop mirror window is worth presenting to. A minimized window
// has no visible surface, and the VR loop keeps running without it (see
// setVrRenderLoop above), so the mirror simply skips those frames.
inline bool mirrorVisible(Tempest::SystemApi::Window* window) {
#if defined(_WIN32) && !defined(__ANDROID__)
  return window!=nullptr && IsIconic(HWND(window))==FALSE;
#else
  (void)window;
  return false;
#endif
  }

}
}
#endif
