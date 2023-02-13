#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "Externals/vk_mem_alloc.h"

#include "GpuResources.hpp"

#include "Foundation/Prerequisites.hpp"
#include "Foundation/ResourcePool.hpp"
#include "Foundation/String.hpp"
#include "Foundation/Service.hpp"
#include "Foundation/Array.hpp"

// TODOs:
// 1. gpu timing

namespace Graphics
{
// Forward declarations:
struct CommandBuffer;
struct GpuDevice;
//---------------------------------------------------------------------------//
struct DeviceCreation
{
  Framework::Allocator* allocator = nullptr;
  Framework::StackAllocator* temporaryAllocator = nullptr;
  void* window = nullptr; // api-specific (SDL, GLFW, etc)
  uint16_t width = 1;
  uint16_t height = 1;

  DeviceCreation& setWindow(uint32_t p_Width, uint32_t p_Height, void* p_Handle);
  DeviceCreation& setAllocator(Framework::Allocator* p_Allocator);
  DeviceCreation& setTemporaryAllocator(Framework::StackAllocator* p_Allocator);
};
//---------------------------------------------------------------------------//
struct GpuDevice : public Framework::Service
{
  void init(const DeviceCreation& p_Creation);
  void shutdown();

  void setPresentMode(PresentMode::Enum p_Mode);
  void createSwapchain();
  void destroySwapchain();

  // Common members
  Framework::StringBuffer m_StringBuffer;
  Framework::Allocator* m_Allocator;
  Framework::StackAllocator* m_TemporaryAllocator;

  // Vulkan members
  VkAllocationCallbacks* m_VulkanAllocCallbacks;
  VkInstance m_VulkanInstance;
  VkPhysicalDevice m_VulkanPhysicalDevice;
  VkPhysicalDeviceProperties m_VulkanPhysicalDeviceProps;
  VkDevice m_VulkanDevice;
  VkQueue m_VulkanQueue;
  uint32_t m_VulkanQueueFamily;
  VkDescriptorPool m_VulkanDescriptorPool;

  // Swapchain
  VkImage m_VulkanSwapchainImages[kMaxSwapchainImages];
  VkImageView m_VulkanSwapchainImageViews[kMaxSwapchainImages];
  VkFramebuffer m_VulkanSwapchainFramebuffers[kMaxSwapchainImages];
  uint16_t m_SwapchainWidth = 1;
  uint16_t m_SwapchainHeight = 1;

  RenderPassOutput m_SwapchainOutput;

  // Windows specific
  VkSurfaceKHR m_VulkanWindowSurface;
  VkSurfaceFormatKHR m_VulkanSurfaceFormat;
  VkPresentModeKHR m_VulkanPresentMode;
  VkSwapchainKHR m_VulkanSwapchain;
  uint32_t m_VulkanSwapchainImageCount;
  PresentMode::Enum m_PresentMode = PresentMode::kVSync;

  VkDebugReportCallbackEXT m_VulkanDebugCallback;
  VkDebugUtilsMessengerEXT m_VulkanDebugUtilsMessenger;

  uint32_t m_VulkanImageIndex;

  VmaAllocator m_VmaAllocator;

  // Per-frame synchronization
  VkSemaphore m_VulkanRenderCompleteSempaphore[kMaxSwapchainImages];
  VkSemaphore m_VulkanImageAquiredSempaphore;
  VkFence m_VulkanCmdBufferExectuedFence[kMaxSwapchainImages];

  // Resource pools
  Framework::ResourcePool m_Buffers;
  Framework::ResourcePool m_Textures;
  Framework::ResourcePool m_Pipelines;
  Framework::ResourcePool m_Samplers;
  Framework::ResourcePool m_DescriptorSetLayouts;
  Framework::ResourcePool m_DescriptorSets;
  Framework::ResourcePool m_RenderPasses;
  Framework::ResourcePool m_CommandBuffers;
  Framework::ResourcePool m_Shaders;

  static const uint32_t kMaxFrames = 3;
  static constexpr const char* kName = "Gpu-Service";

  bool m_DebugUtilsExtensionPresent = false;
};
//---------------------------------------------------------------------------//
} // namespace Graphics
