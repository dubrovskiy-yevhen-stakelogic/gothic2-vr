#include "questxr.h"
#include "vr/vrhudrect.h"
#if defined(GOTHIC2VR_OPENXR)
#include "system/api/androidapi.h"
#include <android_native_app_glue.h>
#include <Tempest/Log>
#include <Tempest/VulkanApi>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include "vrprofiler.h"
#include "utils/inifile.h"

namespace {
QuestXr* active=nullptr;
void checkVk(VkResult r,const char* op) {
  if(r!=VK_SUCCESS) throw std::runtime_error(std::string(op)+": Vulkan "+std::to_string(r));
}
constexpr XrSpaceLocationFlags validPose=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
}

QuestXr& QuestXr::inst() { return *active; }
void QuestXr::check(XrResult r,const char* op) const {
  if(XR_SUCCEEDED(r)) return;
  char name[XR_MAX_RESULT_STRING_SIZE]={};
  if(instance!=XR_NULL_HANDLE) xrResultToString(instance,r,name);
  throw std::runtime_error(std::string(op)+": OpenXR "+std::to_string(r)+" "+name);
}

QuestXr::QuestXr() {
  try {
    auto app=Tempest::AndroidApi::nativeApp();
    if(app==nullptr || app->activity==nullptr) throw std::runtime_error("OpenXR: Android activity missing");
    PFN_xrInitializeLoaderKHR init=nullptr;
    check(xrGetInstanceProcAddr(XR_NULL_HANDLE,"xrInitializeLoaderKHR",reinterpret_cast<PFN_xrVoidFunction*>(&init)),"loader entry");
    XrLoaderInitInfoAndroidKHR loader{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loader.applicationVM=app->activity->vm; loader.applicationContext=app->activity->clazz;
    check(init(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loader)),"initialize loader");
    std::vector<const char*> extensions={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    uint32_t extensionCount=0;
    check(xrEnumerateInstanceExtensionProperties(nullptr,0,&extensionCount,nullptr),"extension count");
    std::vector<XrExtensionProperties> available(extensionCount,{XR_TYPE_EXTENSION_PROPERTIES});
    check(xrEnumerateInstanceExtensionProperties(nullptr,extensionCount,&extensionCount,available.data()),"extensions");
    visibilityMaskAvailable=std::any_of(available.begin(),available.end(),[](const auto& e){return std::strcmp(e.extensionName,XR_KHR_VISIBILITY_MASK_EXTENSION_NAME)==0;});
    formatListAvailable=std::any_of(available.begin(),available.end(),[](const auto& e){return std::strcmp(e.extensionName,XR_KHR_VULKAN_SWAPCHAIN_FORMAT_LIST_EXTENSION_NAME)==0;});
    Tempest::Log::i("OpenXR visibility mask extension available=",visibilityMaskAvailable);
    // what this runtime offers, logged once.
    const auto listed=[&](const char* name){return std::any_of(available.begin(),available.end(),[&](const auto& e){return std::strcmp(e.extensionName,name)==0;});};
    {
      std::string names; uint32_t onLine=0;
      for(const auto& e:available) {
        names+=(onLine?" ":"")+std::string(e.extensionName);
        if(++onLine==10) { Tempest::Log::i("OpenXR extensions: ",names.c_str()); names.clear(); onLine=0; }
      }
      if(onLine) Tempest::Log::i("OpenXR extensions: ",names.c_str());
    }
    // XrSystemSpaceWarpPropertiesFB (recommended motion-vector size) is only
    // valid with XR_FB_space_warp enabled; enabling it alone changes nothing
    // until a frame submits space-warp layer info. Gothic.ini [ENGINE]
    // vrSpaceWarpProbeOff=1 skips it (read directly: Gothic does not exist yet).
    const bool spaceWarpListed=listed(XR_FB_SPACE_WARP_EXTENSION_NAME);
    const bool spaceWarpProbe=spaceWarpListed && IniFile(u"Gothic.ini").getI("ENGINE","vrSpaceWarpProbeOff")==0;
    if(spaceWarpProbe) extensions.push_back(XR_FB_SPACE_WARP_EXTENSION_NAME);
    Tempest::Log::i("OpenXR extension count=",extensionCount," XR_FB_space_warp=",spaceWarpListed,
                    " XR_META_performance_metrics=",listed(XR_META_PERFORMANCE_METRICS_EXTENSION_NAME),
                    " space warp probe: ",spaceWarpProbe?"active":spaceWarpListed?"off (vrSpaceWarpProbeOff=1)":"off (not listed)");
    for(const char* optional:{XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME,XR_KHR_VISIBILITY_MASK_EXTENSION_NAME,XR_KHR_VULKAN_SWAPCHAIN_FORMAT_LIST_EXTENSION_NAME})
      if(std::any_of(available.begin(),available.end(),[&](const auto& e){return std::strcmp(e.extensionName,optional)==0;})) extensions.push_back(optional);
    XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android.applicationVM=app->activity->vm; android.applicationActivity=app->activity->clazz;
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO}; info.next=&android;
    std::strcpy(info.applicationInfo.applicationName,"Gothic II VR");
    std::strcpy(info.applicationInfo.engineName,"OpenGothic Tempest");
    info.applicationInfo.applicationVersion=61; info.applicationInfo.engineVersion=1;
    info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,34);
    info.enabledExtensionCount=uint32_t(extensions.size()); info.enabledExtensionNames=extensions.data();
    check(xrCreateInstance(&info,&instance),"create instance");
    const auto optionalFunction=[&](const char* extension,const char* name,PFN_xrVoidFunction* function) {
      if(std::any_of(extensions.begin(),extensions.end(),[&](const char* e){return std::strcmp(e,extension)==0;}))
        xrGetInstanceProcAddr(instance,name,function);
    };
    optionalFunction(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,"xrGetDisplayRefreshRateFB",reinterpret_cast<PFN_xrVoidFunction*>(&getRefresh));
    optionalFunction(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,"xrRequestDisplayRefreshRateFB",reinterpret_cast<PFN_xrVoidFunction*>(&requestRefresh));
    optionalFunction(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,"xrEnumerateDisplayRefreshRatesFB",reinterpret_cast<PFN_xrVoidFunction*>(&enumerateRefresh));
    optionalFunction(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,"xrPerfSettingsSetPerformanceLevelEXT",reinterpret_cast<PFN_xrVoidFunction*>(&setPerformance));
    optionalFunction(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME,"xrSetAndroidApplicationThreadKHR",reinterpret_cast<PFN_xrVoidFunction*>(&setThread));
    optionalFunction(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME,"xrGetVisibilityMaskKHR",reinterpret_cast<PFN_xrVoidFunction*>(&getVisibilityMask));
    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
    check(xrGetInstanceProperties(instance,&properties),"runtime properties");
    Tempest::Log::i("OpenXR runtime: ",properties.runtimeName);
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO}; systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    check(xrGetSystem(instance,&systemInfo,&system),"get HMD");
    {
      XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};
      XrSystemSpaceWarpPropertiesFB warp{XR_TYPE_SYSTEM_SPACE_WARP_PROPERTIES_FB};
      if(spaceWarpProbe) props.next=&warp;
      const auto result=xrGetSystemProperties(instance,system,&props);
      Tempest::Log::i("OpenXR system result=",int(result)," name=",XR_SUCCEEDED(result)?props.systemName:"?",
                      " maxSwapchain=",props.graphicsProperties.maxSwapchainImageWidth,"x",props.graphicsProperties.maxSwapchainImageHeight,
                      " maxLayers=",props.graphicsProperties.maxLayerCount);
      if(spaceWarpProbe)
        Tempest::Log::i("OpenXR space warp recommended motion vectors ",warp.recommendedMotionVectorImageRectWidth,"x",warp.recommendedMotionVectorImageRectHeight);
    }
    check(xrGetInstanceProcAddr(instance,"xrCreateVulkanInstanceKHR",reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateVkInstance)),"Vulkan instance entry");
    check(xrGetInstanceProcAddr(instance,"xrGetVulkanGraphicsDevice2KHR",reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVkDevice)),"Vulkan physical device entry");
    check(xrGetInstanceProcAddr(instance,"xrCreateVulkanDeviceKHR",reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateVkDevice)),"Vulkan device entry");
    PFN_xrGetVulkanGraphicsRequirements2KHR requirementsFn=nullptr;
    check(xrGetInstanceProcAddr(instance,"xrGetVulkanGraphicsRequirements2KHR",reinterpret_cast<PFN_xrVoidFunction*>(&requirementsFn)),"Vulkan requirements entry");
    XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    check(requirementsFn(instance,system,&requirements),"Vulkan requirements");
    const auto maximum=VK_MAKE_API_VERSION(0,XR_VERSION_MAJOR(requirements.maxApiVersionSupported),XR_VERSION_MINOR(requirements.maxApiVersionSupported),0);
    if(requirements.minApiVersionSupported>XR_MAKE_VERSION(1,3,0)) throw std::runtime_error("OpenXR needs Vulkan newer than Tempest supports");
    Tempest::vulkanCreateHooks={this,maximum,createInstance,physicalDevice,createDevice};
    createActions();
    active=this;
    Tempest::AndroidApi::setVrRenderLoop(true);
  } catch(...) {
    Tempest::vulkanCreateHooks={};
    if(actions!=XR_NULL_HANDLE) xrDestroyActionSet(actions);
    if(instance!=XR_NULL_HANDLE) xrDestroyInstance(instance);
    throw;
  }
}

QuestXr::~QuestXr() {
  detach();
  Tempest::AndroidApi::setVrRenderLoop(false);
  Tempest::vulkanCreateHooks={};
  if(actions!=XR_NULL_HANDLE) xrDestroyActionSet(actions);
  if(instance!=XR_NULL_HANDLE) xrDestroyInstance(instance);
  active=nullptr;
}

VkResult QuestXr::createInstance(void* ctx,const VkInstanceCreateInfo* source,VkInstance* result) {
  auto& self=*static_cast<QuestXr*>(ctx);
  XrVulkanInstanceCreateInfoKHR info{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
  info.systemId=self.system; info.pfnGetInstanceProcAddr=vkGetInstanceProcAddr; info.vulkanCreateInfo=source;
  VkResult r=VK_ERROR_INITIALIZATION_FAILED;
  self.check(self.xrCreateVkInstance(self.instance,&info,result,&r),"create Vulkan instance");
  return r;
}
VkPhysicalDevice QuestXr::physicalDevice(void* ctx,VkInstance source) {
  auto& self=*static_cast<QuestXr*>(ctx);
  XrVulkanGraphicsDeviceGetInfoKHR info{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
  info.systemId=self.system; info.vulkanInstance=source;
  VkPhysicalDevice result=VK_NULL_HANDLE;
  self.check(self.xrGetVkDevice(self.instance,&info,&result),"select Vulkan physical device");
  return result;
}
VkResult QuestXr::createDevice(void* ctx,VkPhysicalDevice physical,const VkDeviceCreateInfo* source,VkDevice* result) {
  auto& self=*static_cast<QuestXr*>(ctx);
  XrVulkanDeviceCreateInfoKHR info{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
  info.systemId=self.system; info.pfnGetInstanceProcAddr=vkGetInstanceProcAddr;
  info.vulkanPhysicalDevice=physical; info.vulkanCreateInfo=source;
  VkResult r=VK_ERROR_INITIALIZATION_FAILED;
  self.check(self.xrCreateVkDevice(self.instance,&info,result,&r),"create Vulkan device");
  return r;
}

XrPath QuestXr::path(const char* name) {
  XrPath result=XR_NULL_PATH; check(xrStringToPath(instance,name,&result),name); return result;
}
XrAction QuestXr::action(const char* name,XrActionType type,bool both) {
  XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
  std::strncpy(info.actionName,name,XR_MAX_ACTION_NAME_SIZE-1);
  std::strncpy(info.localizedActionName,name,XR_MAX_LOCALIZED_ACTION_NAME_SIZE-1);
  info.actionType=type; info.countSubactionPaths=both?2u:0u; info.subactionPaths=both?hands:nullptr;
  XrAction result=XR_NULL_HANDLE; check(xrCreateAction(actions,&info,&result),"create action"); return result;
}
void QuestXr::createActions() {
  hands[0]=path("/user/hand/left"); hands[1]=path("/user/hand/right");
  XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
  std::strcpy(set.actionSetName,"gothic"); std::strcpy(set.localizedActionSetName,"Gothic controls");
  check(xrCreateActionSet(instance,&set,&actions),"create action set");
  stick=action("move_turn",XR_ACTION_TYPE_VECTOR2F_INPUT);
  trigger=action("trigger",XR_ACTION_TYPE_FLOAT_INPUT); grip=action("grip",XR_ACTION_TYPE_FLOAT_INPUT);
  primary=action("primary",XR_ACTION_TYPE_BOOLEAN_INPUT); secondary=action("secondary",XR_ACTION_TYPE_BOOLEAN_INPUT);
  stickClick=action("stick_click",XR_ACTION_TYPE_BOOLEAN_INPUT); menu=action("menu",XR_ACTION_TYPE_BOOLEAN_INPUT,false);
  aim=action("aim",XR_ACTION_TYPE_POSE_INPUT);
  gripPoseAction=action("hand_grip_pose",XR_ACTION_TYPE_POSE_INPUT);
  hapticAction=action("hand_haptic",XR_ACTION_TYPE_VIBRATION_OUTPUT);
  std::vector<XrActionSuggestedBinding> bindings;
  for(uint32_t i=0;i<2;++i) {
    const std::string h=i==0?"/user/hand/left/input/":"/user/hand/right/input/";
    const auto bind=[&](XrAction a,const char* component) { bindings.push_back({a,path((h+component).c_str())}); };
    bind(stick,"thumbstick"); bind(stickClick,"thumbstick/click"); bind(trigger,"trigger/value");
    bind(grip,"squeeze/value"); bind(aim,"aim/pose");
    bind(gripPoseAction,"grip/pose");
    bindings.push_back({hapticAction,path((std::string(i==0?"/user/hand/left/":"/user/hand/right/")+"output/haptic").c_str())});
    bind(primary,i==0?"x/click":"a/click"); bind(secondary,i==0?"y/click":"b/click");
  }
  bindings.push_back({menu,path("/user/hand/left/input/menu/click")});
  XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  info.interactionProfile=path("/interaction_profiles/oculus/touch_controller");
  info.countSuggestedBindings=uint32_t(bindings.size()); info.suggestedBindings=bindings.data();
  check(xrSuggestInteractionProfileBindings(instance,&info),"Touch bindings");
}

void QuestXr::attach(Tempest::Device& device) {
  vk=Tempest::VulkanApi::nativeContext(device);
  try {
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance=vk.instance; binding.physicalDevice=vk.physicalDevice; binding.device=vk.device;
    binding.queueFamilyIndex=vk.queueFamily; binding.queueIndex=0;
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO}; info.next=&binding; info.systemId=system;
    check(xrCreateSession(instance,&info,&session),"create Vulkan session");
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO}; space.poseInReferenceSpace.orientation.w=1;
    space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
    check(xrCreateReferenceSpace(session,&space,&localSpace),"local space");
    space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
    check(xrCreateReferenceSpace(session,&space,&viewSpace),"view space");
    for(uint32_t i=0;i<2;++i) {
      XrActionSpaceCreateInfo hand{XR_TYPE_ACTION_SPACE_CREATE_INFO}; hand.action=aim; hand.subactionPath=hands[i]; hand.poseInActionSpace.orientation.w=1;
      check(xrCreateActionSpace(session,&hand,&handSpace[i]),"hand space");
      hand.action=gripPoseAction;
      check(xrCreateActionSpace(session,&hand,&gripSpace[i]),"grip space");
    }
    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets=1; attachInfo.actionSets=&actions;
    check(xrAttachSessionActionSets(session,&attachInfo),"attach Touch actions");
    uint32_t count=0;
    check(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"view count");
    if(count!=2) throw std::runtime_error("OpenXR requires exactly two stereo views");
    XrViewConfigurationView config[2]={{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    check(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,config),"stereo views");
    // A conservative first stereo target. OpenXR still supplies the complete per-eye FOV.
    extent.width=std::min(1280u,std::min(config[0].recommendedImageRectWidth,config[1].recommendedImageRectWidth));
    extent.height=std::min(config[0].recommendedImageRectHeight,config[1].recommendedImageRectHeight)*extent.width/config[0].recommendedImageRectWidth;
    extent.height=(extent.height+1u)&~1u;
    check(xrEnumerateSwapchainFormats(session,0,&count,nullptr),"format count");
    std::vector<int64_t> formats(count);
    check(xrEnumerateSwapchainFormats(session,count,&count,formats.data()),"formats");
    {
      // every swapchain format, depth ones by
      // name (space warp needs a depth swapchain), and the refresh rates.
      std::string all,depth;
      for(const auto f:formats) {
        all+=" "+std::to_string(f);
        const char* name=f==VK_FORMAT_D16_UNORM?"D16_UNORM":f==VK_FORMAT_X8_D24_UNORM_PACK32?"X8_D24_UNORM":f==VK_FORMAT_D32_SFLOAT?"D32_SFLOAT":
                         f==VK_FORMAT_D16_UNORM_S8_UINT?"D16_UNORM_S8":f==VK_FORMAT_D24_UNORM_S8_UINT?"D24_UNORM_S8":f==VK_FORMAT_D32_SFLOAT_S8_UINT?"D32_SFLOAT_S8":nullptr;
        if(name!=nullptr) depth+=std::string(" ")+name;
      }
      Tempest::Log::i("OpenXR swapchain formats:",all.c_str()," | depth:",depth.empty()?" none":depth.c_str());
      std::vector<float> rates; uint32_t rateCount=0;
      if(enumerateRefresh && XR_SUCCEEDED(enumerateRefresh(session,0,&rateCount,nullptr)) && rateCount>0 && rateCount<64) {
        rates.resize(rateCount);
        if(XR_FAILED(enumerateRefresh(session,rateCount,&rateCount,rates.data()))) rates.clear();
        rates.resize(std::min<size_t>(rates.size(),rateCount));
      }
      std::string list; for(const float r:rates) list+=" "+std::to_string(r);
      Tempest::Log::i("OpenXR display refresh rates:",list.empty()?" unavailable":list.c_str());
    }
    {
      // GPU timestamps around the XR copies (profiler): the queue's tick and valid bits.
      VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(vk.physicalDevice,&props);
      uint32_t families=0; vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice,&families,nullptr);
      std::vector<VkQueueFamilyProperties> queues(families);
      vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice,&families,queues.data());
      const uint32_t bits=vk.queueFamily<families?queues[vk.queueFamily].timestampValidBits:0;
      if(bits>0 && bits<=64 && props.limits.timestampPeriod>0) {
        timestampPeriod=props.limits.timestampPeriod; timestampMask=bits==64?~uint64_t(0):((uint64_t(1)<<bits)-1);
      }
    }
    if(std::find(formats.begin(),formats.end(),int64_t(VK_FORMAT_R8G8B8A8_SRGB))==formats.end())
      throw std::runtime_error("OpenXR does not expose RGBA8 sRGB swapchains");
    createSwapchains(device);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex=vk.queueFamily; pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    checkVk(vkCreateCommandPool(vk.device,&pool,nullptr,&copyPool),"copy pool");
    for(auto& eye:eyes) eye.copies.resize(eye.images.size());
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    for(auto& eye:eyes) for(auto& copy:eye.copies)
      checkVk(vkCreateFence(vk.device,&fence,nullptr,&copy.fence),"copy fence");
    Tempest::Log::i("OpenXR session ready: two eyes ",extent.width,"x",extent.height,"; Touch actions attached");
    Tempest::Log::i("OpenXR direct output capability L/R=",!eyes[0].targets.empty(),"/",!eyes[1].targets.empty()," formatList=",formatListAvailable);
  } catch(...) { detach(); throw; }
}

void QuestXr::createSwapchains(Tempest::Device& device) {
  uint32_t count=0;
  for(auto& eye:eyes) {
    XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    sc.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    sc.format=VK_FORMAT_R8G8B8A8_SRGB; sc.sampleCount=1; sc.width=extent.width; sc.height=extent.height;
    sc.faceCount=1; sc.arraySize=1; sc.mipCount=1;
    bool mutableColor=(&eye!=&eyes[2]);
    const VkFormat viewFormats[]={VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_R8G8B8A8_UNORM};
    XrVulkanSwapchainFormatListCreateInfoKHR formatList{XR_TYPE_VULKAN_SWAPCHAIN_FORMAT_LIST_CREATE_INFO_KHR};
    formatList.viewFormatCount=2; formatList.viewFormats=viewFormats;
    if(mutableColor) {
      sc.usageFlags|=XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
      if(formatListAvailable) sc.next=&formatList;
    }
    auto created=xrCreateSwapchain(session,&sc,&eye.handle);
    if(XR_FAILED(created) && mutableColor) {
      Tempest::Log::i("OpenXR mutable eye unavailable; copy fallback result=",int(created));
      mutableColor=false; sc.next=nullptr; sc.usageFlags&=~XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
      eye.handle=XR_NULL_HANDLE;
      created=xrCreateSwapchain(session,&sc,&eye.handle);
    }
    check(created,"create eye swapchain");
    check(xrEnumerateSwapchainImages(eye.handle,0,&count,nullptr),"eye image count");
    eye.images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    check(xrEnumerateSwapchainImages(eye.handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())),"eye images");
    if(mutableColor) {
      try {
        eye.targets.reserve(eye.images.size());
        for(const auto& image:eye.images)
          eye.targets.push_back(Tempest::VulkanApi::borrowColorAttachment(device,
            {image.image,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},extent.width,extent.height));
      } catch(const std::exception& e) {
        eye.targets.clear();
        Tempest::Log::i("OpenXR UNORM target view unavailable; copy fallback: ",e.what());
      }
    }
  }
}

void QuestXr::detach() noexcept {
  if(vk.device!=VK_NULL_HANDLE) vkDeviceWaitIdle(vk.device);
  endFrame(false,false);
  for(auto& eye:eyes) for(auto& copy:eye.copies) {
    if(copy.fence!=VK_NULL_HANDLE) vkDestroyFence(vk.device,copy.fence,nullptr);
    if(copy.query!=VK_NULL_HANDLE) vkDestroyQueryPool(vk.device,copy.query,nullptr);
  }
  if(copyPool!=VK_NULL_HANDLE) vkDestroyCommandPool(vk.device,copyPool,nullptr);
  copyPool=VK_NULL_HANDLE;
  for(auto& eye:eyes) {
    // Framebuffers/views are app-owned; images and memory belong to OpenXR.
    eye.targets.clear();
    if(eye.handle!=XR_NULL_HANDLE) xrDestroySwapchain(eye.handle);
    eye={};
  }
  for(auto& hand:handSpace) { if(hand!=XR_NULL_HANDLE) xrDestroySpace(hand); hand=XR_NULL_HANDLE; }
  for(auto& hand:gripSpace) { if(hand!=XR_NULL_HANDLE) xrDestroySpace(hand); hand=XR_NULL_HANDLE; }
  if(viewSpace!=XR_NULL_HANDLE) xrDestroySpace(viewSpace);
  if(localSpace!=XR_NULL_HANDLE) xrDestroySpace(localSpace);
  viewSpace=localSpace=XR_NULL_HANDLE;
  if(session!=XR_NULL_HANDLE) xrDestroySession(session);
  session=XR_NULL_HANDLE; running=false; pad={}; vk={};
}

bool QuestXr::beginFrame() {
  if(exitRequested) return false;
  XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
  while(xrPollEvent(instance,&event)==XR_SUCCESS) {
    if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
      const auto& change=*reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
      state=change.state;
      Tempest::Log::i("OpenXR state=",int(state));
      if(state==XR_SESSION_STATE_READY && !running) {
        XrSessionBeginInfo info{XR_TYPE_SESSION_BEGIN_INFO}; info.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        check(xrBeginSession(session,&info),"begin session"); running=true; roomMotion.invalidate();
        visibilityFovLogged=false;
        visibilityMasks={}; visibilityMaskFrames=visibilityMaskAttempts=0;
        reportVisibilityMasks();
        if(requestRefresh) Tempest::Log::i("OpenXR request 72 Hz result=",int(requestRefresh(session,72.f)));
        if(getRefresh) getRefresh(session,&refreshHz);
        if(setThread) Tempest::Log::i("OpenXR main thread result=",int(setThread(session,XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR,uint32_t(gettid()))));
        perfCpuApplied=perfGpuApplied=-1; applyPerformanceLevel();
      } else if(state==XR_SESSION_STATE_STOPPING && running) {
        drainCopies(); // Complete the last submitted frame before stopping its session.
        check(xrEndSession(session),"end session"); running=false;
      } else if(state==XR_SESSION_STATE_EXITING || state==XR_SESSION_STATE_LOSS_PENDING) exitRequested=true;
    } else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) exitRequested=true;
    else if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) { referenceValid=false; roomMotion.invalidate(); }
    else if(event.type==XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB) refreshHz=reinterpret_cast<XrEventDataDisplayRefreshRateChangedFB*>(&event)->toDisplayRefreshRate;
    else if(event.type==XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT) {
      // Runtime clock/thermal notification: domain 1 = CPU, 2 = GPU; sub-domain
      // 1 = compositing, 2 = rendering, 3 = thermal; levels 0 normal, 25 warning, 75 impaired.
      const auto& perf=*reinterpret_cast<XrEventDataPerfSettingsEXT*>(&event);
      Tempest::Log::i("OpenXR perf notification domain=",int(perf.domain)," subDomain=",int(perf.subDomain),
                      " level ",int(perf.fromLevel)," -> ",int(perf.toLevel));
    }
    event={XR_TYPE_EVENT_DATA_BUFFER};
  }
  pad={}; handValid[0]=handValid[1]=false; gripValid[0]=gripValid[1]=false; trackingValid=false;
  if(!running || exitRequested) return false;
  XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState frame{XR_TYPE_FRAME_STATE};
  check(xrWaitFrame(session,&wait,&frame),"wait frame");
  XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
  {
    std::lock_guard<std::mutex> lock(*vk.queueMutex);
    check(xrBeginFrame(session,&begin),"begin frame");
  }
  begun=true; displayTime=frame.predictedDisplayTime;
  if(refreshHz<=0 && frame.predictedDisplayPeriod>0) refreshHz=float(1e9/double(frame.predictedDisplayPeriod));
  if(!frame.shouldRender) { roomMotion.invalidate(); return false; }
  XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO}; locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  locate.displayTime=displayTime; locate.space=localSpace;
  XrViewState viewState{XR_TYPE_VIEW_STATE}; uint32_t count=0;
  check(xrLocateViews(session,&locate,&viewState,2,&count,views),"locate eyes");
  constexpr XrViewStateFlags validViews=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
  XrSpaceLocation head{XR_TYPE_SPACE_LOCATION}; check(xrLocateSpace(viewSpace,localSpace,displayTime,&head),"locate head");
  trackingValid=count==2 && (viewState.viewStateFlags&validViews)==validViews && (head.locationFlags&validPose)==validPose;
  if(!trackingValid) { roomMotion.invalidate(); return false; }
  if(!visibilityFovLogged) {
    // The READY mask is view-space geometry. Retain the first actual valid FOV
    // for its offline projection; diagnostics never change frame acceptance.
    const auto validFov=[](const XrFovf& f) {
      constexpr float halfPi=1.5707963267948966f;
      return std::isfinite(f.angleLeft) && std::isfinite(f.angleRight) &&
             std::isfinite(f.angleDown) && std::isfinite(f.angleUp) &&
             -halfPi<f.angleLeft && f.angleLeft<f.angleRight && f.angleRight<halfPi &&
             -halfPi<f.angleDown && f.angleDown<f.angleUp && f.angleUp<halfPi;
    };
    if(validFov(views[0].fov) && validFov(views[1].fov)) {
      for(uint32_t eye=0;eye<2;++eye) {
        const auto& f=views[eye].fov;
        Tempest::Log::i("OpenXR visibility FOV eye=",eye," radians left=",f.angleLeft,
                        " right=",f.angleRight," down=",f.angleDown," up=",f.angleUp);
      }
      visibilityFovLogged=true;
    }
  }
  retryVisibilityMasks();
  headPose=pose(head.pose);
  if(!referenceValid) { referencePose=XrMath::reference(headPose); referenceValid=true; roomMotion.reset(); Tempest::Log::i("OpenXR tracking origin calibrated"); }
  updateInput();
  return true;
}

void QuestXr::retryVisibilityMasks() noexcept {
  // Quest may return empty masks at READY before physical views exist. Retry
  // only after valid tracking/FOV, with a finite per-session budget. No mask
  // from this diagnostic is consumed by rendering.
  if(!trackingValid || !visibilityMaskAvailable || !getVisibilityMask || visibilityMaskAttempts>=8 ||
     (!visibilityMasks[0].vertices.empty() && !visibilityMasks[1].vertices.empty())) return;
  for(const auto& view:views) {
    const auto& f=view.fov;
    constexpr float halfPi=1.5707963267948966f;
    if(!std::isfinite(f.angleLeft) || !std::isfinite(f.angleRight) ||
       !std::isfinite(f.angleDown) || !std::isfinite(f.angleUp) ||
       !(-halfPi<f.angleLeft && f.angleLeft<f.angleRight && f.angleRight<halfPi) ||
       !(-halfPi<f.angleDown && f.angleDown<f.angleUp && f.angleUp<halfPi)) return;
  }
  if(visibilityMaskFrames++%30!=0) return;
  ++visibilityMaskAttempts;
  reportVisibilityMasks(visibilityMaskAttempts==8);
}

void QuestXr::reportVisibilityMasks(bool finalAttempt) noexcept {
  // Diagnostic only: retain runtime-provided view-space data for an offline
  // coverage audit. Rendering, depth and the shared HUD do not consume it.
  try {
    std::array<VisibilityMaskDiagnostic,2> masks;
    bool newlyReady=false;
    Tempest::Log::i("OpenXR visibility mask query phase=",visibilityMaskAttempts==0?"ready":"located",
                   " attempt=",visibilityMaskAttempts,"/8 validFrames=",visibilityMaskFrames);
    constexpr uint32_t MaxVertices=4096,MaxIndices=12288;
    for(uint32_t eye=0;eye<2;++eye) {
      auto& out=masks[eye];
      if(visibilityMaskAvailable && getVisibilityMask) {
        XrVisibilityMaskKHR mask{XR_TYPE_VISIBILITY_MASK_KHR};
        out.result=getVisibilityMask(session,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,eye,
                                     XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR,&mask);
        out.requiredVertices=mask.vertexCountOutput; out.requiredIndices=mask.indexCountOutput;
        if(XR_FAILED(out.result)) out.status="count-query-failed";
        else if(mask.vertexCountOutput==0 && mask.indexCountOutput==0) out.status="empty";
        else if(mask.vertexCountOutput<3 || mask.indexCountOutput==0 || mask.indexCountOutput%3!=0 ||
                mask.vertexCountOutput>MaxVertices || mask.indexCountOutput>MaxIndices) out.status="invalid-or-over-capacity";
        else {
          out.vertices.resize(mask.vertexCountOutput); out.indices.resize(mask.indexCountOutput);
          mask.vertexCapacityInput=uint32_t(out.vertices.size()); mask.vertices=out.vertices.data();
          mask.indexCapacityInput=uint32_t(out.indices.size()); mask.indices=out.indices.data();
          out.result=getVisibilityMask(session,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,eye,
                                       XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR,&mask);
          if(XR_FAILED(out.result)) out.status="data-query-failed";
          else if(mask.vertexCountOutput==0 && mask.indexCountOutput==0) out.status="empty";
          else if(mask.vertexCountOutput>mask.vertexCapacityInput || mask.indexCountOutput>mask.indexCapacityInput ||
                  mask.vertexCountOutput<3 || mask.indexCountOutput==0 || mask.indexCountOutput%3!=0) out.status="invalid-data-counts";
          else {
            out.vertices.resize(mask.vertexCountOutput); out.indices.resize(mask.indexCountOutput);
            const bool valid=std::all_of(out.vertices.begin(),out.vertices.end(),[](const auto& v){return std::isfinite(v.x)&&std::isfinite(v.y);}) &&
                std::all_of(out.indices.begin(),out.indices.end(),[&](uint32_t index){return index<out.vertices.size();});
            out.status=valid?"ready":"invalid-data";
          }
          if(std::strcmp(out.status,"ready")!=0) { out.vertices.clear(); out.indices.clear(); }
        }
      }
      Tempest::Log::i("OpenXR visibility mask eye=",eye," status=",out.status," result=",int(out.result),
                     " requiredVertices=",out.requiredVertices," requiredIndices=",out.requiredIndices,
                     " vertices=",out.vertices.size()," indices=",out.indices.size());
      const bool valid=std::strcmp(out.status,"ready")==0;
      newlyReady|=valid && visibilityMasks[eye].vertices.empty();
      // A transient failure/empty response for one eye must not overwrite a
      // valid diagnostic already obtained for it during this session.
      if(valid || visibilityMasks[eye].vertices.empty()) visibilityMasks[eye]=std::move(out);
    }
    // Empty retries log bounded counts, but do not rewrite the same JSON every
    // thirty frames. Persist READY, newly usable data, and final exhaustion.
    if(visibilityMaskAttempts!=0 && !newlyReady && !finalAttempt) return;
    std::ofstream file("vr-visibility-mask.json",std::ios::trunc);
    file.precision(std::numeric_limits<float>::max_digits10);
    file<<"{\n  \"diagnosticOnly\":true,\n  \"extensionAvailable\":"<<(visibilityMaskAvailable?"true":"false")
        <<",\n  \"entrypointAvailable\":"<<(getVisibilityMask?"true":"false")
        <<",\n  \"queryPhase\":\""<<(visibilityMaskAttempts==0?"ready":"located")<<"\",\n  \"postLocateAttempt\":"<<visibilityMaskAttempts
        <<",\n  \"postLocateEligibleFrames\":"<<visibilityMaskFrames
        <<",\n  \"type\":\"hidden-triangle-mesh\",\n  \"space\":\"rendered-view plane z=-1 metre; project separately for each eye\",\n"
        <<"  \"maxVertices\":"<<MaxVertices<<",\n  \"maxIndices\":"<<MaxIndices<<",\n  \"views\":[\n";
    for(uint32_t eye=0;eye<2;++eye) {
      const auto& mask=visibilityMasks[eye];
      file<<(eye?",\n":"")<<"    {\"eye\":"<<eye<<",\"status\":\""<<mask.status<<"\",\"result\":"<<int(mask.result)
          <<",\"requiredVertices\":"<<mask.requiredVertices<<",\"requiredIndices\":"<<mask.requiredIndices<<",\"vertices\":[";
      for(size_t i=0;i<mask.vertices.size();++i) file<<(i?",":"")<<'['<<mask.vertices[i].x<<','<<mask.vertices[i].y<<']';
      file<<"],\"indices\":[";
      for(size_t i=0;i<mask.indices.size();++i) file<<(i?",":"")<<mask.indices[i];
      file<<"]}";
    }
    file<<"\n  ]\n}\n"; file.flush();
    if(!file) Tempest::Log::e("OpenXR visibility mask diagnostic file could not be written");
  } catch(const std::exception& error) { Tempest::Log::e("OpenXR visibility mask diagnostic failed: ",error.what()); }
  catch(...) { Tempest::Log::e("OpenXR visibility mask diagnostic failed"); }
}

void QuestXr::updateInput() {
  for(uint32_t h=0;h<2;++h) { gripValid[h]=false; gripValues[h]=0; }
  if(state!=XR_SESSION_STATE_FOCUSED) return;
  XrActiveActionSet set{actions,XR_NULL_PATH}; XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
  sync.countActiveActionSets=1; sync.activeActionSets=&set;
  const auto synced=xrSyncActions(session,&sync);
  if(synced==XR_SESSION_NOT_FOCUSED) return;
  check(synced,"sync Touch"); pad.connected=true;
  for(uint32_t h=0;h<2;++h) {
    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO}; get.subactionPath=hands[h];
    get.action=aim;
    XrActionStatePose activePose{XR_TYPE_ACTION_STATE_POSE}; check(xrGetActionStatePose(session,&get,&activePose),"aim state");
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if(activePose.isActive) check(xrLocateSpace(handSpace[h],localSpace,displayTime,&location),"locate Touch");
    handValid[h]=activePose.isActive && (location.locationFlags&validPose)==validPose;
    if(!handValid[h]) continue;
    handPose[h]=location.pose;
    get.action=gripPoseAction;
    XrActionStatePose gripState{XR_TYPE_ACTION_STATE_POSE};
    check(xrGetActionStatePose(session,&get,&gripState),"grip pose state");
    XrSpaceLocation gripLocation{XR_TYPE_SPACE_LOCATION};
    if(gripState.isActive) check(xrLocateSpace(gripSpace[h],localSpace,displayTime,&gripLocation),"locate grip");
    gripValid[h]=gripState.isActive && (gripLocation.locationFlags&validPose)==validPose;
    if(gripValid[h]) gripPoses[h]=gripLocation.pose;
    const auto button=[&](XrAction a,uint32_t mask) {
      get.action=a; XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};
      check(xrGetActionStateBoolean(session,&get,&value),"Touch button");
      if(value.isActive && value.currentState) pad.buttons|=mask;
    };
    const auto analog=[&](XrAction a) {
      get.action=a; XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
      check(xrGetActionStateFloat(session,&get,&value),"Touch analog");
      return value.isActive?value.currentState:0.f;
    };
    button(primary,h==0?Tempest::GamepadState::X:Tempest::GamepadState::A);
    button(secondary,h==0?Tempest::GamepadState::Select:Tempest::GamepadState::B);
    button(stickClick,h==0?Tempest::GamepadState::L3:Tempest::GamepadState::R3);
    gripValues[h]=analog(grip);
    if(gripValues[h]>0.55f) pad.buttons|=h==0?Tempest::GamepadState::L1:Tempest::GamepadState::R1;
    const float t=analog(trigger);
    get.action=stick; XrActionStateVector2f value{XR_TYPE_ACTION_STATE_VECTOR2F};
    check(xrGetActionStateVector2f(session,&get,&value),"Touch stick");
    const auto v=value.isActive?value.currentState:XrVector2f{};
    if(h==0) { pad.leftStickX=v.x; pad.leftStickY=-v.y; pad.leftTrigger=t; }
    else { pad.rightStickX=v.x; pad.rightStickY=-v.y; pad.rightTrigger=t; }
  }
  XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO}; get.action=menu;
  XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};
  check(xrGetActionStateBoolean(session,&get,&value),"menu button");
  if(value.isActive && value.currentState) pad.buttons|=Tempest::GamepadState::Start;
}

XrMath::Pose QuestXr::pose(const XrPosef& p) { return {{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},{p.position.x,p.position.y,p.position.z}}; }
void QuestXr::recenter() { referenceValid=false; roomMotion.invalidate();cinemaAnchor.reset(); }
Tempest::Vec3 QuestXr::roomDelta(bool enabled) {
  const auto h=XrMath::relative(headPose,referencePose,1.f);
  return roomMotion.delta(Tempest::Vec3(h[3][0],0,h[3][2]),enabled&&focused());
}
float QuestXr::roomBlocked() const {
  const auto h=XrMath::relative(headPose,referencePose,1.f);
  return roomMotion.blocked(Tempest::Vec3(h[3][0],0,h[3][2]),unitsPerMeter);
}
Tempest::Matrix4x4 QuestXr::trackedView(const XrMath::Pose& p,const Tempest::Matrix4x4& base) const {
  auto local=XrMath::relative(p,referencePose,unitsPerMeter);
  const auto h=XrMath::relative(headPose,referencePose,1.f);
  const auto correction=roomMotion.correction(Tempest::Vec3(h[3][0],0,h[3][2]),unitsPerMeter);
  local[3][0]-=correction.x; local[3][2]-=correction.z;
  local.inverse();
  auto c=Tempest::Matrix4x4::mkIdentity(); c.scale(1,-1,-1);
  return c*local*c*base;
}
Tempest::Matrix4x4 QuestXr::headView(const Tempest::Matrix4x4& base) const { return trackedView(headPose,base); }
Tempest::Matrix4x4 QuestXr::handWorld(uint32_t hand,const Tempest::Matrix4x4& base,bool useAim) const {
  if(hand>=2) return Tempest::Matrix4x4::mkIdentity();
  auto result=trackedView(pose(useAim?handPose[hand]:gripPoses[hand]),base);
  result.inverse();
  return result;
}
Tempest::Vec3 QuestXr::trackingPoint(uint32_t hand,const Tempest::Matrix4x4& handWorld,const Tempest::Vec3& point) const {
  if(hand>=2)return {};
  auto inverse=handWorld;inverse.inverse();auto local=point;inverse.project(local);
  local=Tempest::Vec3(local.x,-local.y,-local.z)/unitsPerMeter;
  XrMath::poseMatrix(pose(gripPoses[hand]),1.f).project(local);return local;
}
Tempest::Vec3 QuestXr::trackingVector(uint32_t hand,const Tempest::Matrix4x4& handWorld,const Tempest::Vec3& vector) const {
  if(hand>=2)return {};
  auto inverse=XrMath::poseMatrix(pose(gripPoses[hand]),1.f);inverse.inverse();
  auto v=vector,z=Tempest::Vec3();inverse.project(v);inverse.project(z);v-=z;
  v=Tempest::Vec3(v.x,-v.y,-v.z)*unitsPerMeter;z={};
  handWorld.project(v);handWorld.project(z);return v-z;
}
void QuestXr::haptic(uint32_t hand,float strength,float seconds) noexcept {
  if(hand>=2 || !focused()) return;
  XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO}; info.action=hapticAction; info.subactionPath=hands[hand];
  XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
  vibration.amplitude=std::clamp(strength,0.f,1.f); vibration.frequency=XR_FREQUENCY_UNSPECIFIED;
  vibration.duration=XrDuration(std::clamp(seconds,0.005f,0.2f)*1e9f);
  xrApplyHapticFeedback(session,&info,reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}
Tempest::Matrix4x4 QuestXr::eyeView(uint32_t eye,const Tempest::Matrix4x4& base) const { return trackedView(pose(views[eye].pose),base); }
void QuestXr::setPerformanceLevels(int cpu,int gpu) {
  cpu=std::clamp(cpu,0,4); gpu=std::clamp(gpu,0,4);
  if(cpu==perfCpu && gpu==perfGpu) return;
  perfCpu=cpu; perfGpu=gpu;
  if(running) applyPerformanceLevel();
  }

void QuestXr::applyPerformanceLevel() {
  if(!setPerformance) return;
  static const XrPerfSettingsLevelEXT levels[]={XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT,XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT,
                                                XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT,XR_PERF_SETTINGS_LEVEL_BOOST_EXT};
  static const char* names[]={"runtime default","power savings","sustained low","sustained high","boost"};
  const struct { XrPerfSettingsDomainEXT domain; const char* name; int level; int* applied; } domains[]={
    {XR_PERF_SETTINGS_DOMAIN_CPU_EXT,"CPU",perfCpu,&perfCpuApplied},{XR_PERF_SETTINGS_DOMAIN_GPU_EXT,"GPU",perfGpu,&perfGpuApplied}};
  for(const auto& d:domains) {
    if(d.level<0 || d.level==*d.applied) continue;
    *d.applied=d.level;
    if(d.level==0) { Tempest::Log::i("OpenXR ",d.name," performance level: runtime default (no request; a previous request stays until restart)"); continue; }
    const auto result=setPerformance(session,d.domain,levels[d.level]);
    Tempest::Log::i("OpenXR ",d.name," performance level ",names[d.level]," result=",int(result));
    }
  }

Tempest::Matrix4x4 QuestXr::projection(uint32_t eye) const {
  const auto f=views[eye].fov; return XrMath::projection(f.angleLeft,f.angleRight,f.angleDown,f.angleUp,2.f,farPlane);
}
Tempest::Matrix4x4 QuestXr::headProjection() const {
  const auto a=views[0].fov,b=views[1].fov;
  return XrMath::projection(std::min(a.angleLeft,b.angleLeft),std::max(a.angleRight,b.angleRight),
                            std::min(a.angleDown,b.angleDown),std::max(a.angleUp,b.angleUp),2.f,farPlane);
}
float QuestXr::headYawDegrees() const {
  auto relative=XrMath::relative(headPose,referencePose);
  return std::atan2(relative[2][0],relative[2][2])*180.f/3.14159265358979323846f;
}

void QuestXr::invalidateCopyCache() {
  // The caller invokes this whenever the source Attachment is replaced.
  // Existing commands are reset lazily; each previous copy has completed its fence.
  for(auto& eye:eyes) for(auto& copy:eye.copies) copy.source=VK_NULL_HANDLE;
}

void QuestXr::copyEye(uint32_t index,const Tempest::Texture2d& texture,Vr::CopyTimings* timings) {
  queueEyeCopy(index,texture,timings);
  finishEyeCopy(index,timings);
}

Tempest::Attachment* QuestXr::acquireRenderTarget(uint32_t index,Vr::CopyTimings* timings) {
  if(index>=2 || !directOutputRequested || eyes[index].targets.empty()) return nullptr;
  auto& eye=eyes[index];
  if(eye.acquired || eye.copyPending) throw std::logic_error("Previous eye output has not completed");
  Vr::CopyTimings measured; double phase=Vr::milliseconds();
  uint32_t imageIndex=0;
  XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  {
    std::lock_guard<std::mutex> lock(*vk.queueMutex);
    check(xrAcquireSwapchainImage(eye.handle,&acquire,&imageIndex),"acquire direct eye");
  }
  eye.acquired=true; eye.imageIndex=imageIndex; eye.copySubmitted=false; eye.direct=true;
  measured.acquire=Vr::milliseconds()-phase; phase=Vr::milliseconds();
  XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout=XR_INFINITE_DURATION;
  check(xrWaitSwapchainImage(eye.handle,&wait),"wait direct eye"); eye.waited=true;
  measured.wait=Vr::milliseconds()-phase;
  if(imageIndex>=eye.targets.size()) throw std::runtime_error("OpenXR direct image index out of range");
  eye.copyTimings=measured;
  if(timings) *timings=measured;
  return &eye.targets[imageIndex];
}

void QuestXr::submitRenderTarget(uint32_t index,Tempest::Fence&& completion,Vr::CopyTimings* timings) {
  if(index>=2) throw std::out_of_range("Direct output requires a world eye");
  auto& eye=eyes[index];
  if(!eye.acquired || !eye.waited || !eye.direct || eye.copyPending || eye.copySubmitted || completion.isEmpty())
    throw std::logic_error("Direct eye must be acquired, waited and successfully submitted once");
  eye.directFence=std::move(completion);
  eye.copyPending=true; eye.copySubmitted=true;
  if(timings) *timings=eye.copyTimings;
}

void QuestXr::queueEyeCopy(uint32_t index,const Tempest::Texture2d& texture,Vr::CopyTimings* timings) {
  Vr::CopyTimings measured; double phase=Vr::milliseconds();
  auto& eye=eyes[index]; uint32_t imageIndex=0;
  if(eye.acquired || eye.copyPending) throw std::logic_error("Previous eye copy has not completed");
  eye.copyGpu=-1;
  XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  {
    std::lock_guard<std::mutex> lock(*vk.queueMutex);
    check(xrAcquireSwapchainImage(eye.handle,&acquire,&imageIndex),"acquire eye");
  }
  eye.acquired=true; eye.imageIndex=imageIndex; eye.copySubmitted=false; eye.direct=false;
  measured.acquire=Vr::milliseconds()-phase; phase=Vr::milliseconds();
  XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout=XR_INFINITE_DURATION;
  check(xrWaitSwapchainImage(eye.handle,&wait),"wait eye"); eye.waited=true;
  measured.wait=Vr::milliseconds()-phase; phase=Vr::milliseconds();
  const auto source=Tempest::VulkanApi::nativeImage(texture);
  // Tempest's tonemapper already writes sRGB encoded bytes into RGBA8_UNORM.
  // Copy compatible texels to the sRGB XR image; a blit would decode/encode twice.
  if(source.format!=VK_FORMAT_R8G8B8A8_UNORM) throw std::runtime_error("OpenXR source must be RGBA8_UNORM");
  auto& program=eye.copies[imageIndex];
  if(program.command==VK_NULL_HANDLE) {
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool=copyPool; allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount=1;
    checkVk(vkAllocateCommandBuffers(vk.device,&allocation,&program.command),"allocate cached eye copy");
  }
  const auto copyCommand=program.command;
  // The HUD image (2) copies only the layer's rectangle (vr/vrhudrect.h): the compositor samples nothing else. The eyes copy
  // the rectangle the renderer wrote: the projection view's
  // imageRect, the whole image at render scale 1 and on menu/cinema frames.
  const auto region=index==2 && hudRectCopy ? Vr::hudCopyRegion({hudRect.x,hudRect.y,hudRect.w,hudRect.h},extent.width,extent.height)
                   : index<2 ? Vr::eyeImageRegion(eyeRect[index].w,eyeRect[index].h,extent.width,extent.height)
                             : Vr::HudRect{0,0,int(extent.width),int(extent.height)};
  // Two timestamps around the copy (profiler): pool created on first use.
  if(copyTimestamps && timestampPeriod>0 && program.query==VK_NULL_HANDLE) {
    VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; queries.queryType=VK_QUERY_TYPE_TIMESTAMP; queries.queryCount=2;
    if(vkCreateQueryPool(vk.device,&queries,nullptr,&program.query)!=VK_SUCCESS) program.query=VK_NULL_HANDLE;
  }
  const bool timed=copyTimestamps && timestampPeriod>0 && program.query!=VK_NULL_HANDLE;
  measured.reused=program.source==source.image && program.layout==source.restingLayout && program.timed==timed &&
                  program.offset.x==region.x && program.offset.y==region.y &&
                  program.size.width==uint32_t(region.w) && program.size.height==uint32_t(region.h);
  if(!measured.reused) {
  checkVk(vkResetCommandBuffer(copyCommand,0),"reset eye copy");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=0; // Reusable only after the completion fence below.
  checkVk(vkBeginCommandBuffer(copyCommand,&begin),"begin eye copy");
  if(timed) {
    vkCmdResetQueryPool(copyCommand,program.query,0,2);
    vkCmdWriteTimestamp(copyCommand,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,program.query,0);
  }
  VkImageMemoryBarrier barriers[2]={{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER},{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}};
  for(auto& b:barriers) {
    b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  }
  barriers[0].image=source.image; barriers[0].oldLayout=source.restingLayout; barriers[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barriers[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT; barriers[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  barriers[1].image=eye.images[imageIndex].image; barriers[1].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(copyCommand,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,barriers);
  VkImageCopy copy{}; copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.dstSubresource=copy.srcSubresource;
  copy.srcOffset={region.x,region.y,0}; copy.dstOffset=copy.srcOffset;
  copy.extent={uint32_t(region.w),uint32_t(region.h),1};
  vkCmdCopyImage(copyCommand,source.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,eye.images[imageIndex].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  barriers[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barriers[0].newLayout=source.restingLayout;
  barriers[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT; barriers[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  barriers[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barriers[1].newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barriers[1].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  vkCmdPipelineBarrier(copyCommand,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,barriers);
  if(timed) vkCmdWriteTimestamp(copyCommand,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,program.query,1);
  checkVk(vkEndCommandBuffer(copyCommand),"end eye copy");
  program.source=source.image; program.layout=source.restingLayout;
  program.offset={region.x,region.y}; program.size={uint32_t(region.w),uint32_t(region.h)}; program.timed=timed;
  }
  checkVk(vkResetFences(vk.device,1,&program.fence),"reset copy fence");
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&copyCommand;
  measured.record=Vr::milliseconds()-phase; phase=Vr::milliseconds();
  {
    std::lock_guard<std::mutex> lock(*vk.queueMutex);
    checkVk(vkQueueSubmit(vk.queue,1,&submit,program.fence),"submit eye copy");
  }
  measured.submit=Vr::milliseconds()-phase;
  eye.copyPending=true; eye.copySubmitted=true;
  eye.copyTimings=measured;
  if(timings) *timings=measured;
}

void QuestXr::finishEyeCopy(uint32_t index,Vr::CopyTimings* timings) {
  auto& eye=eyes[index];
  if(!eye.copyPending) return;
  auto& measured=eye.copyTimings;
  const double waitStart=Vr::milliseconds();
  if(eye.direct) eye.directFence.wait();
  else {
    const auto fence=eye.copies[eye.imageIndex].fence;
    checkVk(vkWaitForFences(vk.device,1,&fence,VK_TRUE,UINT64_MAX),"complete eye copy");
    readCopyGpu(index);
  }
  eye.copyPending=false;
  measured.fence=Vr::milliseconds()-waitStart;
  releaseEyeCopy(index,timings);
  if(timings) *timings=measured;
}

void QuestXr::releaseEyeCopy(uint32_t index,Vr::CopyTimings* timings) {
  auto& eye=eyes[index];
  if(!eye.acquired) return;
  if(!eye.waited || !eye.copySubmitted) throw std::logic_error("Cannot release an unsubmitted eye copy");
  const double phase=Vr::milliseconds();
  XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  {
    // XR_KHR_vulkan_enable2 explicitly permits submitted GPU commands to remain
    // unfinished on release. The runtime shares and synchronizes this VkQueue.
    std::lock_guard<std::mutex> lock(*vk.queueMutex);
    check(xrReleaseSwapchainImage(eye.handle,&release),"release eye");
  }
  eye.acquired=false; eye.waited=false; eye.copySubmitted=false;
  eye.copyTimings.release=Vr::milliseconds()-phase;
  if(timings) *timings=eye.copyTimings;
}

void QuestXr::readCopyGpu(uint32_t index) noexcept {
  // Called after the copy's fence: its two timestamps without waiting;
  // unavailable, untimed or direct (no copy) = -1.
  auto& eye=eyes[index];
  eye.copyGpu=-1;
  if(eye.direct || eye.imageIndex>=eye.copies.size()) return;
  const auto& program=eye.copies[eye.imageIndex];
  if(!program.timed || program.query==VK_NULL_HANDLE) return;
  uint64_t values[4]={}; // tick, availability for each query
  if(vkGetQueryPoolResults(vk.device,program.query,0,2,sizeof(values),values,2*sizeof(uint64_t),
                           VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)!=VK_SUCCESS || values[1]==0 || values[3]==0) return;
  eye.copyGpu=double((values[2]-values[0])&timestampMask)*double(timestampPeriod)/1e6;
}

void QuestXr::drainCopies() {
  // A released XR image may still have our copy command/fence in flight.
  // Keep this separate from release and wait before any next-frame reuse.
  for(auto& eye:eyes) if(eye.copyPending) {
    if(eye.direct) eye.directFence.wait();
    else {
      const auto fence=eye.copies[eye.imageIndex].fence;
      checkVk(vkWaitForFences(vk.device,1,&fence,VK_TRUE,UINT64_MAX),"drain previous eye copy");
      readCopyGpu(uint32_t(&eye-eyes));
    }
    eye.copyPending=false;
  }
}

void QuestXr::drainCopy(uint32_t index) {
  // Single-output variant of drainCopies(): same fences, same pending rule.
  if(index>=3) return;
  auto& eye=eyes[index];
  if(!eye.copyPending) return;
  if(eye.direct) eye.directFence.wait();
  else {
    const auto fence=eye.copies[eye.imageIndex].fence;
    checkVk(vkWaitForFences(vk.device,1,&fence,VK_TRUE,UINT64_MAX),"drain eye copy");
    readCopyGpu(index);
  }
  eye.copyPending=false;
}

void QuestXr::endFrame(bool world,bool complete,bool overlay,float hudDistance) noexcept {
  // Complete frames have explicitly released every image; their GPU work may
  // remain pending until next-frame drainCopies(). Error cleanup still drains.
  for(const auto& eye:eyes) if(eye.acquired) complete=false;
  for(auto& eye:eyes) if(!complete && eye.copyPending) {
    VkResult result=VK_SUCCESS;
    if(eye.direct) {
      try { eye.directFence.wait(); } catch(...) { result=VK_ERROR_DEVICE_LOST; }
    } else {
      const auto fence=eye.copies[eye.imageIndex].fence;
      result=vkWaitForFences(vk.device,1,&fence,VK_TRUE,UINT64_MAX);
    }
    if(result!=VK_SUCCESS) {
      complete=false; exitRequested=true;
      Tempest::Log::e("OpenXR copy fence failed during cleanup: ",int(result));
      continue; // Never release an image whose queued copy is still unconfirmed.
    }
    eye.copyPending=false;
  }
  for(auto& eye:eyes) if(eye.acquired && !eye.copyPending) {
    complete=false;
    if(!eye.waited) {
      XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout=XR_INFINITE_DURATION;
      const auto result=xrWaitSwapchainImage(eye.handle,&wait);
      if(result!=XR_SUCCESS) {
        exitRequested=true;
        Tempest::Log::e("OpenXR image wait failed during cleanup: ",int(result));
        continue;
      }
      eye.waited=true;
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    XrResult result;
    {
      std::lock_guard<std::mutex> lock(*vk.queueMutex);
      result=xrReleaseSwapchainImage(eye.handle,&release);
    }
    if(XR_FAILED(result)) {
      exitRequested=true;
      Tempest::Log::e("OpenXR image release failed during cleanup: ",int(result));
      continue;
    }
    eye.acquired=false; eye.waited=false; eye.copySubmitted=false;
  }
  if(!begun) return;
  XrCompositionLayerProjectionView projectionViews[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  for(uint32_t i=0;i<2;++i) {
    projectionViews[i].pose=views[i].pose; projectionViews[i].fov=views[i].fov;
    projectionViews[i].subImage.swapchain=eyes[i].handle;
    projectionViews[i].subImage.imageRect={{0,0},{int32_t(extent.width),int32_t(extent.height)}};
  }
  // The menu/cinema panel and the HUD start from the whole image; the eyes
  // show only the rectangle the renderer wrote (render scale < 1): same FOV, the compositor stretches it, no upscale pass of ours.
  const XrSwapchainSubImage wholeImage=projectionViews[0].subImage;
  for(uint32_t i=0;i<2;++i) {
    const auto r=Vr::eyeImageRegion(eyeRect[i].w,eyeRect[i].h,extent.width,extent.height);
    projectionViews[i].subImage.imageRect={{r.x,r.y},{r.w,r.h}};
  }
  XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  projection.space=localSpace; projection.viewCount=2; projection.views=projectionViews;
  XrCompositionLayerQuad panel{XR_TYPE_COMPOSITION_LAYER_QUAD}; panel.space=viewSpace;
  panel.eyeVisibility=XR_EYE_VISIBILITY_BOTH; panel.subImage=wholeImage;
  panel.pose.orientation.w=1; panel.pose.position.z=-2.f;
  const auto& anchor=cinemaAnchor.update(headPose,!world);
  if(!world) {
    panel.space=localSpace;
    panel.pose.orientation={anchor.orientation.x,anchor.orientation.y,anchor.orientation.z,anchor.orientation.w};
    panel.pose.position={anchor.position.x,anchor.position.y,anchor.position.z};
  }

  panel.size={2.2f,2.2f*float(extent.height)/float(std::max(1u,extent.width))};
  XrCompositionLayerQuad hud{XR_TYPE_COMPOSITION_LAYER_QUAD}; hud.space=viewSpace;
  hud.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
  hud.eyeVisibility=XR_EYE_VISIBILITY_BOTH; hud.subImage=wholeImage; hud.subImage.swapchain=eyes[2].handle;
  // Only the painted rectangle of the HUD image (vr/vrhudrect.h): the
  // compositor shades every display pixel a layer covers.
  const Vr::HudRect rect{hudRect.x,hudRect.y,hudRect.w,hudRect.h};
  const auto quad=Vr::hudQuad(rect,extent.width,extent.height,hudDistance);
  if(!rect.empty()) hud.subImage.imageRect={{rect.x,rect.y},{rect.w,rect.h}};
  hud.pose.orientation.w=1; hud.pose.position={quad.posX,quad.posY,-hudDistance};
  hud.size={quad.sizeX,quad.sizeY};
  const XrCompositionLayerBaseHeader* layers[]={world?reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection):reinterpret_cast<const XrCompositionLayerBaseHeader*>(&panel),reinterpret_cast<const XrCompositionLayerBaseHeader*>(&hud)};
  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO}; end.displayTime=displayTime; end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end.layerCount=complete?(overlay?2u:1u):0u; end.layers=complete?layers:nullptr;
  XrResult result;
  {
    std::lock_guard<std::mutex> lock(*vk.queueMutex);
    result=xrEndFrame(session,&end);
  }
  begun=false;
  if(XR_FAILED(result)) Tempest::Log::e("OpenXR end frame failed: ",int(result));
  if(complete && (++frameCount<=3 || frameCount%300==0))
    Tempest::Log::i("OpenXR frame=",frameCount," layer=",world?"stereo":"menu"," hands=",handValid[0],",",handValid[1]);
}
#endif
