#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "Externals/vk_mem_alloc.h"

#include "Graphics/GpuResources.hpp"

#include "Foundation/Prerequisites.hpp"
#include "Foundation/ResourcePool.hpp"
#include "Foundation/String.hpp"
#include "Foundation/Service.hpp"
#include "Foundation/Array.hpp"
#include "Foundation/File.hpp"

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
  uint16_t numThreads = 1;

  DeviceCreation& setWindow(uint32_t p_Width, uint32_t p_Height, void* p_Handle);
  DeviceCreation& setAllocator(Framework::Allocator* p_Allocator);
  DeviceCreation& setTemporaryAllocator(Framework::StackAllocator* p_Allocator);
  DeviceCreation& setNumThreads(uint32_t p_NumThreads);
};
//---------------------------------------------------------------------------//
struct GpuDevice : public Framework::Service
{
  void init(const DeviceCreation& p_Creation);
  void shutdown();

  void newFrame();
  void present();

  // Creation/Destruction of resources
  BufferHandle createBuffer(const BufferCreation& p_Creation);
  TextureHandle createTexture(const TextureCreation& p_Creation);
  PipelineHandle
  createPipeline(const PipelineCreation& p_Creation, const char* p_CachePath = nullptr);
  SamplerHandle createSampler(const SamplerCreation& p_Creation);
  DescriptorSetLayoutHandle
  createDescriptorSetLayout(const DescriptorSetLayoutCreation& p_Creation);
  DescriptorSetHandle createDescriptorSet(const DescriptorSetCreation& p_Creation);
  RenderPassHandle createRenderPass(const RenderPassCreation& p_Creation);
  FramebufferHandle createFramebuffer(const FramebufferCreation& p_Creation);
  ShaderStateHandle createShaderState(const ShaderStateCreation& p_Creation);

  void destroyBuffer(BufferHandle p_Buffer);
  void destroyTexture(TextureHandle p_Texture);
  void destroyPipeline(PipelineHandle p_Pipeline);
  void destroySampler(SamplerHandle p_Sampler);
  void destroyDescriptorSetLayout(DescriptorSetLayoutHandle p_Layout);
  void destroyDescriptorSet(DescriptorSetHandle p_Set);
  void destroyRenderPass(RenderPassHandle p_RenderPass);
  void destroyFramebuffer(FramebufferHandle p_Framebuffer);
  void destroyShaderState(ShaderStateHandle p_Shader);

  void releaseResource(ResourceUpdate& p_ResourceDeletion);

  // Instant methods
  void destroyBufferInstant(ResourceHandle buffer);
  void destroyTextureInstant(ResourceHandle texture);
  void destroyPipelineInstant(ResourceHandle pipeline);
  void destroySamplerInstant(ResourceHandle sampler);
  void destroyDescriptorSetLayoutInstant(ResourceHandle layout);
  void destroyDescriptorSetInstant(ResourceHandle set);
  void destroyRenderPassInstant(ResourceHandle p_RenderPass);
  void destroyFramebufferInstant(ResourceHandle framebuffer);
  void destroyShaderStateInstant(ResourceHandle shader);

  void updateDescriptorSetInstant(const DescriptorSetUpdate& update);

  // Map/Unmap
  void* mapBuffer(const MapBufferParameters& p_Parameters);
  void unmapBuffer(const MapBufferParameters& p_Parameters);

  void* dynamicAllocate(uint32_t p_Size);

  // Swapchain helpers
  void setPresentMode(PresentMode::Enum p_Mode);
  void createSwapchain();
  void destroySwapchain();
  void resizeSwapchain();

  // Commands helpers
  CommandBuffer* getCommandBuffer(uint32_t p_ThreadIndex, bool p_Begin);
  CommandBuffer* getSecondaryCommandBuffer(uint32_t p_ThreadIndex);
  void queueCommandBuffer(CommandBuffer* p_CommandBuffer);

  // Query resources
  void querySampler(SamplerHandle p_Sampler, SamplerDescription& p_OutDescription);
  void queryTexture(TextureHandle p_Texture, TextureDescription& p_OutDescription);
  void queryBuffer(BufferHandle p_Buffer, BufferDescription& p_OutDescription);

  // Other utility
  void setResourceName(VkObjectType p_ObjType, uint64_t p_Handle, const char* p_Name);
  VkRenderPass getVulkanRenderPass(const RenderPassOutput& p_Output, const char* p_Name);
  VkShaderModuleCreateInfo compileShader(
      const char* p_Code, uint32_t p_CodeSize, VkShaderStageFlagBits p_Stage, const char* p_Name);
  void frameCountersAdvance();
  void resize(uint16_t p_Width, uint16_t p_Height)
  {
    m_SwapchainWidth = p_Width;
    m_SwapchainHeight = p_Height;
    m_Resized = true;
  }

  static void fillWriteDescriptorSets(
      GpuDevice& p_Gpu,
      const DescriptorSetLayout* p_DescriptorSetLayout,
      VkDescriptorSet p_VkDescriptorSet,
      VkWriteDescriptorSet* p_DescriptorWrite,
      VkDescriptorBufferInfo* p_BufferInfo,
      VkDescriptorImageInfo* p_ImageInfo,
      VkSampler p_VkDefaultSampler,
      uint32_t& p_NumResources,
      const ResourceHandle* p_Resources,
      const SamplerHandle* p_Samplers,
      const uint16_t* p_Bindings);

  FramebufferHandle getCurrentFramebuffer() const
  {
    return m_VulkanSwapchainFramebuffers[m_VulkanImageIndex];
  }

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
  VkQueue m_VulkanMainQueue;
  VkQueue m_VulkanTransferQueue;
  uint32_t m_VulkanMainQueueFamily;
  uint32_t m_VulkanTransferQueueFamily;
  VkDescriptorPool m_VulkanDescriptorPool;

  // Swapchain
  FramebufferHandle m_VulkanSwapchainFramebuffers[kMaxSwapchainImages]{
      kInvalidIndex, kInvalidIndex, kInvalidIndex};

  uint16_t m_SwapchainWidth = 1;
  uint16_t m_SwapchainHeight = 1;
  bool m_Resized = false;
  RenderPassOutput m_SwapchainOutput;
  VkSwapchainKHR m_VulkanSwapchain;

  // Windows specific
  VkSurfaceKHR m_VulkanWindowSurface;
  VkSurfaceFormatKHR m_VulkanSurfaceFormat;
  VkPresentModeKHR m_VulkanPresentMode;
  uint32_t m_VulkanSwapchainImageCount;
  PresentMode::Enum m_PresentMode = PresentMode::kVSync;

  VkDebugReportCallbackEXT m_VulkanDebugCallback;
  VkDebugUtilsMessengerEXT m_VulkanDebugUtilsMessenger;

  uint32_t m_VulkanImageIndex;
  uint32_t m_CurrentFrameIndex;
  uint32_t m_PreviousFrameIndex;
  uint32_t m_AbsoluteFrameIndex;

  // Fundamental resources
  BufferHandle m_FullscreenVertexBuffer;
  SamplerHandle m_DefaultSampler;
  RenderPassHandle m_SwapchainRenderPass{kInvalidIndex};

  // Dummy resources
  TextureHandle m_DummyTexture;
  BufferHandle m_DummyConstantBuffer;

  VmaAllocator m_VmaAllocator;

  // Extension functions
  PFN_vkCmdBeginRenderingKHR m_CmdBeginRendering;
  PFN_vkCmdEndRenderingKHR m_CmdEndRendering;

  Framework::Array<ResourceUpdate> m_ResourceDeletionQueue;
  Framework::Array<DescriptorSetUpdate> m_DescriptorSetUpdates;

  // Per-frame synchronization
  VkSemaphore m_VulkanRenderCompleteSemaphore[kMaxSwapchainImages];
  VkSemaphore m_VulkanImageAcquiredSemaphore;
  VkFence m_VulkanCmdBufferExectuedFence[kMaxSwapchainImages];

  // Resource pools
  Framework::ResourcePool m_Buffers;
  Framework::ResourcePool m_Textures;
  Framework::ResourcePool m_Pipelines;
  Framework::ResourcePool m_Samplers;
  Framework::ResourcePool m_DescriptorSetLayouts;
  Framework::ResourcePool m_DescriptorSets;
  Framework::ResourcePool m_RenderPasses;
  Framework::ResourcePool m_Framebuffers;
  Framework::ResourcePool m_CommandBuffers;
  Framework::ResourcePool m_Shaders;

  // Dynamic buffer
  uint32_t m_DynamicMaxPerFrameSize;
  BufferHandle m_DynamicBuffer;
  uint8_t* m_DynamicMappedMemory;
  uint32_t m_DynamicAllocatedSize;
  uint32_t m_DynamicPerFrameSize;

  uint32_t m_NumQueuedCommandBuffers = 0;
  CommandBuffer** m_QueuedCommandBuffers = nullptr;

  static constexpr const char* kName = "Gpu-Service";

  bool m_DebugUtilsExtensionPresent = false;
  bool m_DynamicRenderingExtensionPresent = false;

  size_t m_UboAlignment = 256;
  size_t m_SboAlignment = 256;

  char m_VulkanBinariesPath[512];

  Framework::Directory m_Cwd;

  // Bindless stuff
  bool m_BindlessSupported = false;
  VkDescriptorPool m_VulkanBindlessDescriptorPool;
  VkDescriptorSet m_VulkanBindlessDescriptorSetCached;
  DescriptorSetHandle m_BindlessDescriptorSet;
  DescriptorSetLayoutHandle m_BindlessDescriptorSetLayout;
  Framework::Array<ResourceUpdate> m_TextureToUpdateBindless;

  void linkTextureSampler(TextureHandle p_Texture, SamplerHandle p_Sampler);
  DescriptorSetLayoutHandle getDescriptorSetLayout(PipelineHandle handle, int layoutIndex)
  {
    Pipeline* pipeline = (Pipeline*)m_Pipelines.accessResource(handle.index);
    assert(pipeline != nullptr);
    return pipeline->descriptorSetLayoutHandle[layoutIndex];
  }

  // Update/Reload resources
  void resizeOutputTextures(FramebufferHandle framebuffer, uint32_t width, uint32_t height);
  void resizeTexture(TextureHandle texture, uint32_t width, uint32_t height);

  uint32_t getMemoryHeapCount();
};
//---------------------------------------------------------------------------//
} // namespace Graphics
