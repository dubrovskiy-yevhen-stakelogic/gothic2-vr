#pragma once
#if defined(GOTHIC2VR_OPENXR)
#include <Tempest/Device>
#include <Tempest/Attachment>
#include <Tempest/Fence>
#include <Tempest/SystemApi>
#include "gapi/vulkaninterop.h"
#include <jni.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "xrmath.h"
#include "vrroomscale.h"
#include "vrprofiler.h"
#include <array>
#include <vector>

class QuestXr final {
  public:
    QuestXr();
    ~QuestXr();
    QuestXr(const QuestXr&)=delete;
    QuestXr& operator=(const QuestXr&)=delete;
    static QuestXr& inst();
    void attach(Tempest::Device& device);
    void detach() noexcept;
    bool beginFrame();
    void endFrame(bool world,bool complete,bool overlay=false,float hudDistance=2.f) noexcept;
    // HUD composition layer: only this image rectangle (pixels); zero size = the whole image.
    void setHudRect(int x,int y,int w,int h) { hudRect={x,y,w,h}; }
    // The HUD copy (image 2) covers only that rectangle (Gothic.ini [ENGINE] vrHudRectCopyOff=1 restores the full copy).
    void setHudRectCopy(bool enabled) { hudRectCopy=enabled; }
    // World eye rectangle from the image origin that the renderer tonemapped
    // (Renderer::vrEyeRect): the projection view's imageRect
    // and the eye copy region; 0 = the whole image (render scale 1, menu and
    // cinema frames). The menu/cinema panel and the HUD keep the whole image.
    void setEyeRect(uint32_t eye,int w,int h) { if(eye<2) eyeRect[eye]={w,h}; }
    // GPU timestamps around the QuestXr copies (Gothic.ini vrProfilerExtrasOff=1 disables);
    // copyGpuMs() = GPU ms of the output's last completed copy, -1 unknown.
    void setCopyTimestamps(bool enabled) { copyTimestamps=enabled; }
    double copyGpuMs(uint32_t index) const { return index<3 ? eyes[index].copyGpu : -1; }
    void invalidateCopyCache();
    void setDirectOutput(bool enabled) { directOutputRequested=enabled; }
    // Null means copy fallback and does not acquire an image. Only world eyes
    // 0/1 are eligible; menu/video/HUD callers keep using the copy route.
    Tempest::Attachment* acquireRenderTarget(uint32_t eye,Vr::CopyTimings* timings=nullptr);
    void submitRenderTarget(uint32_t eye,Tempest::Fence&& completion,Vr::CopyTimings* timings=nullptr);
    void copyEye(uint32_t eye,const Tempest::Texture2d& texture,Vr::CopyTimings* timings=nullptr);
    void queueEyeCopy(uint32_t eye,const Tempest::Texture2d& texture,Vr::CopyTimings* timings=nullptr);
    void finishEyeCopy(uint32_t eye,Vr::CopyTimings* timings=nullptr);
    // Release submitted images without a CPU wait; retain copy resources until
    // drainCopies(), which must precede the next frame's GPU resource reuse.
    void releaseEyeCopy(uint32_t eye,Vr::CopyTimings* timings=nullptr);
    void drainCopies();
    // One output's pending copy/render completion (0/1 eyes, 2 HUD); the early
    // left eye waits only for eye 0 before recording the next left eye.
    void drainCopy(uint32_t eye);
    // Both eyes render straight into their XR images (no vrOutput copy).
    bool directOutputReady() const { return directOutputRequested && !eyes[0].targets.empty() && !eyes[1].targets.empty(); }
    Tempest::Matrix4x4 headView(const Tempest::Matrix4x4& base) const;
    void setWorldScale(float scale) { unitsPerMeter=100.f/scale; }
    void recenter();
    void setCinema(bool enabled) { cinemaRequested=enabled; }
    bool cinema() const { return cinemaRequested; }
    Tempest::Vec3 roomDelta(bool enabled);
    void consumeRoom(const Tempest::Vec3& delta) { roomMotion.consume(delta); }
    float roomBlocked() const;
    float refreshRate() const { return refreshHz; }
    Tempest::Matrix4x4 eyeView(uint32_t eye,const Tempest::Matrix4x4& base) const;
    Tempest::Matrix4x4 projection(uint32_t eye) const;
    Tempest::Matrix4x4 headProjection() const;
    // Far plane of the eye projections in cm (Performance -> Horizon).
    void setFarPlane(float cm) { farPlane=cm; }
    float farPlaneCm() const { return farPlane; }
    // XR_EXT_performance_settings level per domain: 0 = runtime default (no
    // request), 1 = power savings, 2 = sustained low, 3 = sustained high,
    // 4 = boost. Applied when the session begins and whenever a value
    // changes; the runtime's clock/thermal notifications are logged.
    void setPerformanceLevels(int cpu,int gpu);
    int cpuPerformanceLevel() const { return perfCpu; }
    int gpuPerformanceLevel() const { return perfGpu; }
    Tempest::GamepadState gamepad() const { return pad; }
    float headYawDegrees() const;
    uint32_t width() const { return extent.width; }
    uint32_t height() const { return extent.height; }
    bool focused() const { return state==XR_SESSION_STATE_FOCUSED && trackingValid; }
    bool shouldExit() const { return exitRequested; }
    bool handTracked(uint32_t hand) const { return handValid[hand]; }
    bool gripTracked(uint32_t hand) const { return hand<2 && gripValid[hand] && focused(); }
    float gripValue(uint32_t hand) const { return hand<2?gripValues[hand]:0.f; }
    Tempest::Matrix4x4 handWorld(uint32_t hand,const Tempest::Matrix4x4& base,bool aimPose=false) const;
    Tempest::Vec3 trackingVector(uint32_t hand,const Tempest::Matrix4x4& handWorld,const Tempest::Vec3& vector) const;
    Tempest::Vec3 trackingPoint(uint32_t hand,const Tempest::Matrix4x4& handWorld,const Tempest::Vec3& point) const;
    void haptic(uint32_t hand,float strength=0.4f,float seconds=0.045f) noexcept;
  private:
    void check(XrResult result,const char* operation) const;
    void createActions();
    void createSwapchains(Tempest::Device& device);
    void updateInput();
    void reportVisibilityMasks(bool finalAttempt=false) noexcept;
    void retryVisibilityMasks() noexcept;
    XrPath path(const char* name);
    XrAction action(const char* name,XrActionType type,bool hands=true);
    static VkResult createInstance(void*,const VkInstanceCreateInfo*,VkInstance*);
    static VkPhysicalDevice physicalDevice(void*,VkInstance);
    static VkResult createDevice(void*,VkPhysicalDevice,const VkDeviceCreateInfo*,VkDevice*);
    static XrMath::Pose pose(const XrPosef& p);
    XrInstance instance=XR_NULL_HANDLE;
    XrSystemId system=XR_NULL_SYSTEM_ID;
    XrSession session=XR_NULL_HANDLE;
    XrSpace localSpace=XR_NULL_HANDLE,viewSpace=XR_NULL_HANDLE;
    XrSessionState state=XR_SESSION_STATE_UNKNOWN;
    bool running=false,begun=false,exitRequested=false,trackingValid=false,referenceValid=false;
    XrTime displayTime=0;
    XrMath::Pose referencePose,headPose;
    bool cinemaRequested=false;
    XrMath::CinemaAnchor cinemaAnchor;
    struct { int x=0,y=0,w=0,h=0; } hudRect;
    struct { int w=0,h=0; } eyeRect[2];
    Vr::RoomMotion roomMotion;
    float unitsPerMeter=100.f,refreshHz=0,farPlane=100000.f;
    Tempest::Matrix4x4 trackedView(const XrMath::Pose& pose,const Tempest::Matrix4x4& base) const;
    XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
    VkExtent2D extent={};
    struct Swapchain {
      XrSwapchain handle=XR_NULL_HANDLE;
      std::vector<XrSwapchainImageVulkanKHR> images;
      struct CopyProgram {
        VkCommandBuffer command=VK_NULL_HANDLE;
        VkFence fence=VK_NULL_HANDLE;
        VkImage source=VK_NULL_HANDLE;
        VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
        VkOffset2D offset{};                  // recorded copy region (part of the cache key)
        VkExtent2D size{};
        VkQueryPool query=VK_NULL_HANDLE;     // 2 timestamps around the copy
        bool timed=false;
      };
      std::vector<CopyProgram> copies;
      std::vector<Tempest::Attachment> targets;
      Tempest::Fence directFence;
      bool direct=false;
      bool acquired=false,waited=false,copyPending=false,copySubmitted=false;
      uint32_t imageIndex=0;
      Vr::CopyTimings copyTimings;
      double copyGpu=-1; // GPU ms of the last completed copy, -1 unknown
    };
    Swapchain eyes[3]; // left, right, shared transparent HUD/menu overlay
    bool directOutputRequested=true,formatListAvailable=false;
    bool hudRectCopy=true,copyTimestamps=false;
    float timestampPeriod=0;   // ns per tick of the graphics queue, 0 = no timestamps
    uint64_t timestampMask=0;  // valid timestamp bits
    void readCopyGpu(uint32_t index) noexcept;
    PFN_xrEnumerateDisplayRefreshRatesFB enumerateRefresh=nullptr;
    Tempest::VulkanNativeContext vk;
    VkCommandPool copyPool=VK_NULL_HANDLE;
    XrActionSet actions=XR_NULL_HANDLE;
    XrPath hands[2]={};
    XrAction stick=XR_NULL_HANDLE,trigger=XR_NULL_HANDLE,grip=XR_NULL_HANDLE;
    XrAction primary=XR_NULL_HANDLE,secondary=XR_NULL_HANDLE,stickClick=XR_NULL_HANDLE;
    XrAction menu=XR_NULL_HANDLE,aim=XR_NULL_HANDLE;
    XrAction gripPoseAction=XR_NULL_HANDLE,hapticAction=XR_NULL_HANDLE;
    XrSpace gripSpace[2]={XR_NULL_HANDLE,XR_NULL_HANDLE};
    XrPosef gripPoses[2]={};
    bool gripValid[2]={};
    float gripValues[2]={};
    XrSpace handSpace[2]={XR_NULL_HANDLE,XR_NULL_HANDLE};
    XrPosef handPose[2]={};
    bool handValid[2]={};
    Tempest::GamepadState pad;
    uint64_t frameCount=0;
    PFN_xrGetDisplayRefreshRateFB getRefresh=nullptr;
    PFN_xrRequestDisplayRefreshRateFB requestRefresh=nullptr;
    PFN_xrPerfSettingsSetPerformanceLevelEXT setPerformance=nullptr;
    int perfCpu=-1,perfGpu=-1,perfCpuApplied=-1,perfGpuApplied=-1;
    void applyPerformanceLevel();
    PFN_xrSetAndroidApplicationThreadKHR setThread=nullptr;
    bool visibilityMaskAvailable=false;
    bool visibilityFovLogged=false;
    struct VisibilityMaskDiagnostic {
      const char* status="unavailable";
      XrResult result=XR_SUCCESS;
      uint32_t requiredVertices=0,requiredIndices=0;
      std::vector<XrVector2f> vertices;
      std::vector<uint32_t> indices;
    };
    std::array<VisibilityMaskDiagnostic,2> visibilityMasks;
    uint32_t visibilityMaskFrames=0,visibilityMaskAttempts=0;
    PFN_xrGetVisibilityMaskKHR getVisibilityMask=nullptr;
    PFN_xrCreateVulkanInstanceKHR xrCreateVkInstance=nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR xrGetVkDevice=nullptr;
    PFN_xrCreateVulkanDeviceKHR xrCreateVkDevice=nullptr;
};
#endif
