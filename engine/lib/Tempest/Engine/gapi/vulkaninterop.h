#pragma once

#include "vulkan/vulkan_sdk.h"
#include <mutex>

namespace Tempest {
// Install before constructing VulkanApi; callbacks preserve Tempest's requested
// features while letting an external runtime own device compatibility decisions.
struct VulkanCreateHooks {
  void* context=nullptr;
  uint32_t maxApiVersion=VK_API_VERSION_1_3;
  VkResult (*createInstance)(void*,const VkInstanceCreateInfo*,VkInstance*)=nullptr;
  VkPhysicalDevice (*physicalDevice)(void*,VkInstance)=nullptr;
  VkResult (*createDevice)(void*,VkPhysicalDevice,const VkDeviceCreateInfo*,VkDevice*)=nullptr;
  };
inline VulkanCreateHooks vulkanCreateHooks;

struct VulkanNativeContext {
  VkInstance instance=VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice=VK_NULL_HANDLE;
  VkDevice device=VK_NULL_HANDLE;
  VkQueue queue=VK_NULL_HANDLE;
  uint32_t queueFamily=0;
  std::mutex* queueMutex=nullptr;
  };
struct VulkanNativeImage {
  VkImage image=VK_NULL_HANDLE;
  VkFormat format=VK_FORMAT_UNDEFINED;
  VkImageLayout restingLayout=VK_IMAGE_LAYOUT_UNDEFINED;
  };
}
