#include "vrinfo.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

#if defined(GOTHIC2VR_OPENXR)
#include "vr/vrplatform.h"
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>
#endif

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
// The Win32 target links with /SUBSYSTEM:WINDOWS, so a console launch leaves
// this process with no standard handles and printf goes nowhere. Borrow the
// launching console when there is one; the report is written to vrinfo.txt
// either way, so a launch from Explorer still leaves the answer on disk.
void openReportConsole() {
#if defined(_WIN32)
  if(AttachConsole(ATTACH_PARENT_PROCESS)==0) return;
#if defined(_MSC_VER)
  FILE* stream = nullptr;
  if(freopen_s(&stream,"CONOUT$","w",stdout)==0 && stream!=nullptr) std::setvbuf(stdout,nullptr,_IONBF,0);
  if(freopen_s(&stream,"CONOUT$","w",stderr)==0 && stream!=nullptr) std::setvbuf(stderr,nullptr,_IONBF,0);
#else
  if(std::freopen("CONOUT$","w",stdout)!=nullptr) std::setvbuf(stdout,nullptr,_IONBF,0);
  if(std::freopen("CONOUT$","w",stderr)!=nullptr) std::setvbuf(stderr,nullptr,_IONBF,0);
#endif
#endif
  }

// Reports go to stdout, failures to stderr, so a shell can redirect them
// apart; both land in vrinfo.txt. Writing to both streams would double the
// text on a console, where stdout and stderr share the window.
void emit(const std::string& text,bool failure=false) {
  FILE* stream = failure ? stderr : stdout;
  std::fwrite(text.data(),1,text.size(),stream);
  std::fflush(stream);
  std::ofstream file("vrinfo.txt",std::ios::binary|std::ios::trunc);
  file.write(text.data(),std::streamsize(text.size()));
  }

const int FailureExit = 3;
}

#if defined(GOTHIC2VR_OPENXR)
namespace {
// xrResultToString needs a live instance, so name the results -vrinfo can hit
// before xrCreateInstance returns. An if-chain rather than a switch, so that
// duplicate enumerator values in the OpenXR headers cannot break the build.
const char* resultName(XrResult r) {
  if(r==XR_ERROR_RUNTIME_UNAVAILABLE)      return "XR_ERROR_RUNTIME_UNAVAILABLE";
  if(r==XR_ERROR_INITIALIZATION_FAILED)    return "XR_ERROR_INITIALIZATION_FAILED";
  if(r==XR_ERROR_EXTENSION_NOT_PRESENT)    return "XR_ERROR_EXTENSION_NOT_PRESENT";
  if(r==XR_ERROR_API_VERSION_UNSUPPORTED)  return "XR_ERROR_API_VERSION_UNSUPPORTED";
  if(r==XR_ERROR_FORM_FACTOR_UNAVAILABLE)  return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
  if(r==XR_ERROR_FORM_FACTOR_UNSUPPORTED)  return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
  if(r==XR_ERROR_VALIDATION_FAILURE)       return "XR_ERROR_VALIDATION_FAILURE";
  if(r==XR_ERROR_RUNTIME_FAILURE)          return "XR_ERROR_RUNTIME_FAILURE";
  if(r==XR_ERROR_INSTANCE_LOST)            return "XR_ERROR_INSTANCE_LOST";
  if(r==XR_ERROR_LIMIT_REACHED)            return "XR_ERROR_LIMIT_REACHED";
  if(r==XR_ERROR_OUT_OF_MEMORY)            return "XR_ERROR_OUT_OF_MEMORY";
  return "unnamed OpenXR result";
  }

const char* advice(XrResult r) {
  if(r==XR_ERROR_RUNTIME_UNAVAILABLE)
    return "No OpenXR runtime is installed or active. Start SteamVR (or set the Oculus PC runtime\n"
           "as the active OpenXR runtime) and try again.";
  if(r==XR_ERROR_FORM_FACTOR_UNAVAILABLE)
    return "The runtime is present but reports no head-mounted display right now. Connect the\n"
           "headset, wake it, and make sure the runtime sees it before retrying.";
  if(r==XR_ERROR_FORM_FACTOR_UNSUPPORTED)
    return "This runtime does not support head-mounted displays at all.";
  if(r==XR_ERROR_EXTENSION_NOT_PRESENT)
    return "The runtime does not offer XR_KHR_vulkan_enable2, which this build requires.";
  if(r==XR_ERROR_API_VERSION_UNSUPPORTED)
    return "The runtime does not support the OpenXR API version this build asks for.";
  return "Check that openxr_loader.dll sits next to Gothic2Notr.exe and that a runtime is active.";
  }

[[noreturn]] void die(std::string text, XrInstance instance, const char* op, XrResult result) {
  char name[XR_MAX_RESULT_STRING_SIZE] = {};
  if(instance!=XR_NULL_HANDLE) xrResultToString(instance,result,name);
  const char* label = name[0]=='\0' ? resultName(result) : name;
  text += "\n-vrinfo failed: ";
  text += op;
  text += " returned ";
  text += label;
  text += " (" + std::to_string(result) + ").\n";
  text += advice(result);
  text += "\n";
  emit(text,true);
  std::exit(FailureExit);
  }

const char* viewConfigName(XrViewConfigurationType type) {
  if(type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO)   return "XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO";
  if(type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) return "XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO";
  return "vendor view configuration";
  }

std::string versionText(XrVersion v) {
  return std::to_string(XR_VERSION_MAJOR(v))+"."+std::to_string(XR_VERSION_MINOR(v))+"."+std::to_string(XR_VERSION_PATCH(v));
  }
}
#endif

void VrInfo::preflight(int argc, const char** argv) {
  bool requested = false;
  for(int i=1; i<argc; ++i)
    if(std::string_view(argv[i])=="-vrinfo") requested = true;
  if(!requested) return;
  openReportConsole();

#if !defined(GOTHIC2VR_OPENXR)
  const std::string text =
      "-vrinfo: this binary was built without OpenXR support.\n"
      "Bootstrap the loader with tools/prepare-openxr.ps1 -Windows and reconfigure\n"
      "CMake with -DGOTHIC2VR_BUILD_PCVR=ON.\n";
  emit(text,true);
  std::exit(FailureExit);
#else
  std::ostringstream out;
  out << "Gothic II VR - OpenXR probe (-vrinfo)\n";
  out << "No session, no Vulkan device and no window are created.\n\n";

  XrInstance instance = XR_NULL_HANDLE;
  try {
    Vr::Platform::initializeLoader();
    }
  catch(const std::exception& e) {
    std::string text = out.str();
    text += std::string("\n-vrinfo failed: ")+e.what()+"\n";
    emit(text,true);
    std::exit(FailureExit);
    }

  uint32_t extensionCount = 0;
  XrResult r = xrEnumerateInstanceExtensionProperties(nullptr,0,&extensionCount,nullptr);
  if(XR_FAILED(r)) die(out.str(),instance,"xrEnumerateInstanceExtensionProperties",r);
  std::vector<XrExtensionProperties> available(extensionCount,{XR_TYPE_EXTENSION_PROPERTIES});
  r = xrEnumerateInstanceExtensionProperties(nullptr,extensionCount,&extensionCount,available.data());
  if(XR_FAILED(r)) die(out.str(),instance,"xrEnumerateInstanceExtensionProperties",r);
  const auto listed = [&](const char* name) {
    for(const auto& e:available)
      if(std::strcmp(e.extensionName,name)==0) return true;
    return false;
    };

  // Only the required set is enabled here; the optional ones QuestXr probes are
  // reported below but not turned on, so instance creation stays as forgiving
  // as possible on an unfamiliar runtime.
  const std::vector<const char*> required = Vr::Platform::requiredExtensions();
  XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
  info.next = Vr::Platform::instanceCreateNext();
  std::snprintf(info.applicationInfo.applicationName,sizeof(info.applicationInfo.applicationName),"%s","Gothic II VR");
  std::snprintf(info.applicationInfo.engineName,sizeof(info.applicationInfo.engineName),"%s","OpenGothic Tempest");
  info.applicationInfo.applicationVersion = 61; info.applicationInfo.engineVersion = 1;
  info.applicationInfo.apiVersion = XR_MAKE_VERSION(1,0,34);
  info.enabledExtensionCount = uint32_t(required.size()); info.enabledExtensionNames = required.data();
  r = xrCreateInstance(&info,&instance);
  if(XR_FAILED(r)) die(out.str(),XR_NULL_HANDLE,"xrCreateInstance",r);

  XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
  r = xrGetInstanceProperties(instance,&properties);
  if(XR_FAILED(r)) die(out.str(),instance,"xrGetInstanceProperties",r);
  out << "Runtime:            " << properties.runtimeName << "\n";
  out << "Runtime version:    " << versionText(properties.runtimeVersion) << "\n";
  out << "API version asked:  " << versionText(info.applicationInfo.apiVersion) << "\n\n";

  XrSystemId system = XR_NULL_SYSTEM_ID;
  XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO}; systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  r = xrGetSystem(instance,&systemInfo,&system);
  if(XR_FAILED(r)) die(out.str(),instance,"xrGetSystem",r);

  XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};
  r = xrGetSystemProperties(instance,system,&props);
  if(XR_FAILED(r)) die(out.str(),instance,"xrGetSystemProperties",r);
  out << "System:             " << props.systemName << "\n";
  out << "Vendor id:          " << props.vendorId << "\n";
  out << "Max swapchain:      " << props.graphicsProperties.maxSwapchainImageWidth
      << " x " << props.graphicsProperties.maxSwapchainImageHeight << "\n";
  out << "Max layers:         " << props.graphicsProperties.maxLayerCount << "\n";
  out << "Tracking:           orientation=" << (props.trackingProperties.orientationTracking ? "yes" : "no")
      << " position=" << (props.trackingProperties.positionTracking ? "yes" : "no") << "\n\n";

  uint32_t configCount = 0;
  r = xrEnumerateViewConfigurations(instance,system,0,&configCount,nullptr);
  if(XR_FAILED(r)) die(out.str(),instance,"xrEnumerateViewConfigurations",r);
  std::vector<XrViewConfigurationType> configs(configCount,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO);
  r = xrEnumerateViewConfigurations(instance,system,configCount,&configCount,configs.data());
  if(XR_FAILED(r)) die(out.str(),instance,"xrEnumerateViewConfigurations",r);
  out << "View configurations: " << configCount << "\n";
  for(const auto type:configs) {
    const bool used = type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    out << (used ? "  * " : "  - ") << viewConfigName(type) << " (" << int(type) << ")";
    out << (used ? "  <- the configuration this build renders\n" : "\n");
    XrViewConfigurationProperties configProps{XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
    if(XR_SUCCEEDED(xrGetViewConfigurationProperties(instance,system,type,&configProps)))
      out << "      fov mutable: " << (configProps.fovMutable ? "yes" : "no") << "\n";
    uint32_t viewCount = 0;
    if(XR_FAILED(xrEnumerateViewConfigurationViews(instance,system,type,0,&viewCount,nullptr))) {
      out << "      views: unavailable\n";
      continue;
      }
    std::vector<XrViewConfigurationView> views(viewCount,{XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if(XR_FAILED(xrEnumerateViewConfigurationViews(instance,system,type,viewCount,&viewCount,views.data()))) {
      out << "      views: unavailable\n";
      continue;
      }
    for(uint32_t i=0; i<viewCount; ++i) {
      const auto& v = views[i];
      out << "      view " << i << ": recommended " << v.recommendedImageRectWidth << " x " << v.recommendedImageRectHeight
          << " (max " << v.maxImageRectWidth << " x " << v.maxImageRectHeight << ")"
          << ", samples " << v.recommendedSwapchainSampleCount << " (max " << v.maxSwapchainSampleCount << ")\n";
      }
    }
  out << "\n";

  // xrEnumerateDisplayRefreshRatesFB takes an XrSession, and -vrinfo creates none.
  out << "Display refresh rates: ";
  if(listed(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
    out << "unavailable here - " << XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME
        << " is offered, but enumerating rates needs an XrSession, which this probe does not create.\n\n";
  else
    out << "unavailable - the runtime does not offer " << XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME << ".\n\n";

  out << "Extensions this build uses:\n";
  for(const char* name:required)
    out << "  " << name << " - required, " << (listed(name) ? "present" : "MISSING") << "\n";
  const char* optional[] = {
    XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
    XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,
#if defined(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME)
    XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME,
#endif
    XR_KHR_VISIBILITY_MASK_EXTENSION_NAME,
    XR_KHR_VULKAN_SWAPCHAIN_FORMAT_LIST_EXTENSION_NAME,
    };
  for(const char* name:optional)
    out << "  " << name << " - optional, " << (listed(name) ? "present, enabled by the session" : "not offered, skipped") << "\n";
  out << "  " << XR_FB_SPACE_WARP_EXTENSION_NAME << " - optional, "
      << (listed(XR_FB_SPACE_WARP_EXTENSION_NAME) ? "present, enabled unless Gothic.ini [ENGINE] vrSpaceWarpProbeOff=1" : "not offered, skipped") << "\n\n";

  out << "Extensions offered by this runtime (" << available.size() << "):\n";
  for(const auto& e:available)
    out << "  " << e.extensionName << " (revision " << e.extensionVersion << ")\n";
  out << "\nvrinfo: OK\n";

  xrDestroyInstance(instance);
  emit(out.str());
  std::exit(0);
#endif
}
