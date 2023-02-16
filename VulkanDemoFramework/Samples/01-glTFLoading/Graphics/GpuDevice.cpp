#include "GpuDevice.hpp"
#include "CommandBuffer.hpp"

#include "Externals/SDL2-2.0.18/include/SDL.h"        // SDL_Window
#include "Externals/SDL2-2.0.18/include/SDL_vulkan.h" // SDL_Vulkan_CreateSurface

#include "Foundation/HashMap.hpp"

#include <assert.h>

// VMA link prequisities:
#define VMA_USE_STL_CONTAINERS 0
#define VMA_USE_STL_VECTOR 0
#define VMA_USE_STL_UNORDERED_MAP 0
#define VMA_USE_STL_LIST 0

#define VMA_IMPLEMENTATION
#include "Externals/vk_mem_alloc.h"

namespace Graphics
{
#define CHECKRES(result) assert(result == VK_SUCCESS && "Vulkan assert")
//---------------------------------------------------------------------------//
struct CommandBufferRing
{

  void init(GpuDevice* p_Gpu)
  {
    m_Gpu = p_Gpu;

    for (uint32_t i = 0; i < ms_MaxPools; i++)
    {
      VkCommandPoolCreateInfo cmdPoolCi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
      cmdPoolCi.queueFamilyIndex = m_Gpu->m_VulkanQueueFamily;
      cmdPoolCi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

      CHECKRES(vkCreateCommandPool(
          m_Gpu->m_VulkanDevice, &cmdPoolCi, m_Gpu->m_VulkanAllocCallbacks, &m_VulkanCmdPools[i]));
    }

    for (uint32_t i = 0; i < ms_MaxBuffers; i++)
    {
      VkCommandBufferAllocateInfo cmd = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
      const uint32_t poolIndex = poolFromIndex(i);
      cmd.commandPool = m_VulkanCmdPools[poolIndex];
      cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cmd.commandBufferCount = 1;
      CHECKRES(vkAllocateCommandBuffers(
          m_Gpu->m_VulkanDevice, &cmd, &m_CmdBuffers[i].m_VulkanCmdBuffer));

      m_CmdBuffers[i].m_GpuDevice = m_Gpu;
      m_CmdBuffers[i].m_Handle = i;
      m_CmdBuffers[i].reset();
    }
  }
  void shutdown()
  {
    for (uint32_t i = 0; i < kMaxSwapchainImages * ms_MaxThreads; i++)
    {
      vkDestroyCommandPool(
          m_Gpu->m_VulkanDevice, m_VulkanCmdPools[i], m_Gpu->m_VulkanAllocCallbacks);
    }
  }

  void resetPools(uint32_t p_FrameIndex)
  {
    for (uint32_t i = 0; i < ms_MaxThreads; i++)
    {
      vkResetCommandPool(
          m_Gpu->m_VulkanDevice, m_VulkanCmdPools[p_FrameIndex * ms_MaxThreads + i], 0);
    }
  }

  CommandBuffer* getCmdBuffer(uint32_t p_FrameIndex, bool p_Begin)
  {
    // TODO: take in account threads
    CommandBuffer* cmdBuffer = &m_CmdBuffers[p_FrameIndex * ms_BufferPerPool];

    if (p_Begin)
    {
      cmdBuffer->reset();

      VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(cmdBuffer->m_VulkanCmdBuffer, &beginInfo);
    }

    return cmdBuffer;
  }
  CommandBuffer* getCmdBufferInstant(uint32_t p_FrameIndex, bool p_Begin)
  {
    CommandBuffer* cmdBuffer = &m_CmdBuffers[p_FrameIndex * ms_BufferPerPool + 1];
    return cmdBuffer;
  }

  static uint16_t poolFromIndex(uint32_t p_Index) { return (uint16_t)p_Index / ms_BufferPerPool; }

  static const uint16_t ms_MaxThreads = 1;
  static const uint16_t ms_MaxPools = kMaxSwapchainImages * ms_MaxThreads;
  static const uint16_t ms_BufferPerPool = 4;
  static const uint16_t ms_MaxBuffers = ms_BufferPerPool * ms_MaxPools;

  GpuDevice* m_Gpu;
  VkCommandPool m_VulkanCmdPools[ms_MaxPools];
  CommandBuffer m_CmdBuffers[ms_MaxBuffers];
  uint8_t m_NextFreePerThreadFrame[ms_MaxPools];

}; // struct CommandBufferRing
//---------------------------------------------------------------------------//
DeviceCreation& DeviceCreation::setWindow(uint32_t p_Width, uint32_t p_Height, void* p_Handle)
{
  width = static_cast<uint16_t>(p_Width);
  height = static_cast<uint16_t>(p_Height);
  window = p_Handle;
  return *this;
}
DeviceCreation& DeviceCreation::setAllocator(Framework::Allocator* p_Allocator)
{
  allocator = p_Allocator;
  return *this;
}
DeviceCreation& DeviceCreation::setTemporaryAllocator(Framework::StackAllocator* p_Allocator)
{
  temporaryAllocator = p_Allocator;
  return *this;
}
//---------------------------------------------------------------------------//
// Debug helpers:
//---------------------------------------------------------------------------//

// Enable this to add debugging capabilities.
// https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_debug_utils.html
#define VULKAN_DEBUG_REPORT

//#define VULKAN_SYNCHRONIZATION_VALIDATION

static const char* kRequestedLayers[] = {
    "VK_LAYER_KHRONOS_validation",
    //"VK_LAYER_LUNARG_core_validation",
    //"VK_LAYER_LUNARG_parameter_validation",
};
static const char* kRequestedExtensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    // Platform specific extension
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#if defined(VULKAN_DEBUG_REPORT)
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif // VULKAN_DEBUG_REPORT
};
//---------------------------------------------------------------------------//
static VkBool32 debugUtilsCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT p_Severity,
    VkDebugUtilsMessageTypeFlagsEXT p_Types,
    const VkDebugUtilsMessengerCallbackDataEXT* p_CallbackData,
    void* p_UserData)
{
  char msg[1024]{};
  sprintf(
      msg,
      " MessageID: %s %i\nMessage: %s\n\n",
      p_CallbackData->pMessageIdName,
      p_CallbackData->messageIdNumber,
      p_CallbackData->pMessage);
  OutputDebugStringA(msg);
  return VK_FALSE;
}
//---------------------------------------------------------------------------//
VkDebugUtilsMessengerCreateInfoEXT createDebugUtilsMessengerInfo()
{
  VkDebugUtilsMessengerCreateInfoEXT ci = {};
  ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  ci.pfnUserCallback = debugUtilsCallback;
  ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
  ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
  return ci;
}
//---------------------------------------------------------------------------//

PFN_vkSetDebugUtilsObjectNameEXT pfnSetDebugUtilsObjectNameEXT;
PFN_vkCmdBeginDebugUtilsLabelEXT pfnCmdBeginDebugUtilsLabelEXT;
PFN_vkCmdEndDebugUtilsLabelEXT pfnCmdEndDebugUtilsLabelEXT;

//---------------------------------------------------------------------------//
// Internal context
//---------------------------------------------------------------------------//

static size_t kUboAlignment = 256;
static size_t kSboAlignemnt = 256;

static SDL_Window* g_SdlWindow;

static Framework::FlatHashMap<uint64_t, VkRenderPass> g_RenderPassCache;
static CommandBufferRing g_CmdBufferRing;

static VkPresentModeKHR _toVkPresentMode(PresentMode::Enum p_Mode)
{
  switch (p_Mode)
  {
  case PresentMode::kVSyncFast:
    return VK_PRESENT_MODE_MAILBOX_KHR;
  case PresentMode::kVSyncRelaxed:
    return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
  case PresentMode::kImmediate:
    return VK_PRESENT_MODE_IMMEDIATE_KHR;
  case PresentMode::kVSync:
  default:
    return VK_PRESENT_MODE_FIFO_KHR;
  }
}
//---------------------------------------------------------------------------//
// Local functions:
//---------------------------------------------------------------------------//
static void _transitionImageLayout(
    VkCommandBuffer p_CommandBuffer,
    VkImage p_Image,
    VkImageLayout p_OldLayout,
    VkImageLayout p_NewLayout,
    bool p_IsDepth)
{

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = p_OldLayout;
  barrier.newLayout = p_NewLayout;

  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

  barrier.image = p_Image;
  barrier.subresourceRange.aspectMask =
      p_IsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

  if (p_OldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      p_NewLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
  {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  }
  else if (
      p_OldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
      p_NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
  {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }
  else
  {
    // assert( false, "Unsupported layout transition!\n" );
  }

  vkCmdPipelineBarrier(
      p_CommandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
//---------------------------------------------------------------------------//
static void _vulkanCreateFramebuffer(
    GpuDevice& p_GpuDevice,
    RenderPass* p_RenderPass,
    const TextureHandle* p_OutputTextures,
    uint32_t p_NumRenderTargets,
    TextureHandle p_DepthStencilTexture)
{
  // Create framebuffer
  VkFramebufferCreateInfo framebufferCi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebufferCi.renderPass = p_RenderPass->vkRenderPass;
  framebufferCi.width = p_RenderPass->width;
  framebufferCi.height = p_RenderPass->height;
  framebufferCi.layers = 1;

  VkImageView framebufferAttachments[kMaxImageOutputs + 1]{};
  uint32_t activeAttachments = 0;
  for (; activeAttachments < p_NumRenderTargets; ++activeAttachments)
  {
    TextureHandle handle = p_OutputTextures[activeAttachments];
    Texture* texture = (Texture*)p_GpuDevice.m_Textures.accessResource(handle.index);
    framebufferAttachments[activeAttachments] = texture->vkImageView;
  }

  if (p_DepthStencilTexture.index != kInvalidIndex)
  {
    TextureHandle handle = p_DepthStencilTexture;
    Texture* depthMap = (Texture*)p_GpuDevice.m_Textures.accessResource(handle.index);
    framebufferAttachments[activeAttachments++] = depthMap->vkImageView;
  }
  framebufferCi.pAttachments = framebufferAttachments;
  framebufferCi.attachmentCount = activeAttachments;

  vkCreateFramebuffer(
      p_GpuDevice.m_VulkanDevice, &framebufferCi, nullptr, &p_RenderPass->vkFrameBuffer);
  p_GpuDevice.setResourceName(
      VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)p_RenderPass->vkFrameBuffer, p_RenderPass->name);
}
//---------------------------------------------------------------------------//

static void _vulkanCreateSwapchainPass(
    GpuDevice& p_GpuDevice, const RenderPassCreation& p_Creation, RenderPass* p_RenderPass)
{
  // Color attachment
  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = p_GpuDevice.m_VulkanSurfaceFormat.format;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  // Depth attachment
  VkAttachmentDescription depthAttachment{};

  Texture* depthTextureVk =
      (Texture*)p_GpuDevice.m_Textures.accessResource(p_GpuDevice.m_DepthTexture.index);
  depthAttachment.format = depthTextureVk->vkFormat;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef{};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
  VkRenderPassCreateInfo renderPassInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  renderPassInfo.attachmentCount = 2;
  renderPassInfo.pAttachments = attachments;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;

  CHECKRES(vkCreateRenderPass(
      p_GpuDevice.m_VulkanDevice, &renderPassInfo, nullptr, &p_RenderPass->vkRenderPass));

  p_GpuDevice.setResourceName(
      VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)p_RenderPass->vkRenderPass, p_Creation.name);

  // Create framebuffer into the device.
  VkFramebufferCreateInfo framebufferCi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebufferCi.renderPass = p_RenderPass->vkRenderPass;
  framebufferCi.attachmentCount = 2;
  framebufferCi.width = p_GpuDevice.m_SwapchainWidth;
  framebufferCi.height = p_GpuDevice.m_SwapchainHeight;
  framebufferCi.layers = 1;

  VkImageView framebufferAttachments[2];
  framebufferAttachments[1] = depthTextureVk->vkImageView;

  for (size_t i = 0; i < p_GpuDevice.m_VulkanSwapchainImageCount; i++)
  {
    framebufferAttachments[0] = p_GpuDevice.m_VulkanSwapchainImageViews[i];
    framebufferCi.pAttachments = framebufferAttachments;

    vkCreateFramebuffer(
        p_GpuDevice.m_VulkanDevice,
        &framebufferCi,
        nullptr,
        &p_GpuDevice.m_VulkanSwapchainFramebuffers[i]);
    p_GpuDevice.setResourceName(
        VK_OBJECT_TYPE_FRAMEBUFFER,
        (uint64_t)p_GpuDevice.m_VulkanSwapchainFramebuffers[i],
        p_Creation.name);
  }

  p_RenderPass->width = p_GpuDevice.m_SwapchainWidth;
  p_RenderPass->height = p_GpuDevice.m_SwapchainHeight;

  // Manually transition the texture
  VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  CommandBuffer* cmd = p_GpuDevice.getInstantCommandBuffer();
  vkBeginCommandBuffer(cmd->m_VulkanCmdBuffer, &beginInfo);

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;

  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;

  region.imageOffset = {0, 0, 0};
  region.imageExtent = {p_GpuDevice.m_SwapchainWidth, p_GpuDevice.m_SwapchainHeight, 1};

  // Transition
  for (size_t i = 0; i < p_GpuDevice.m_VulkanSwapchainImageCount; i++)
  {
    _transitionImageLayout(
        cmd->m_VulkanCmdBuffer,
        p_GpuDevice.m_VulkanSwapchainImages[i],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        false);
  }
  _transitionImageLayout(
      cmd->m_VulkanCmdBuffer,
      depthTextureVk->vkImage,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      true);

  vkEndCommandBuffer(cmd->m_VulkanCmdBuffer);

  // Submit command buffer
  VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd->m_VulkanCmdBuffer;

  vkQueueSubmit(p_GpuDevice.m_VulkanQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(p_GpuDevice.m_VulkanQueue);
}
//---------------------------------------------------------------------------//
static RenderPassOutput
_fillRenderPassOutput(GpuDevice& p_GpuDevice, const RenderPassCreation& p_Creation)
{
  RenderPassOutput output;
  output.reset();

  for (uint32_t i = 0; i < p_Creation.numRenderTargets; ++i)
  {
    TextureHandle handle = p_Creation.outputTextures[i];
    Texture* textureVk = (Texture*)p_GpuDevice.m_Textures.accessResource(handle.index);
    output.color(textureVk->vkFormat);
  }
  if (p_Creation.depthStencilTexture.index != kInvalidIndex)
  {
    TextureHandle handle = p_Creation.depthStencilTexture;
    Texture* textureVk = (Texture*)p_GpuDevice.m_Textures.accessResource(handle.index);
    output.depth(textureVk->vkFormat);
  }

  output.colorOperation = p_Creation.colorOperation;
  output.depthOperation = p_Creation.depthOperation;
  output.stencilOperation = p_Creation.stencilOperation;

  return output;
}
//---------------------------------------------------------------------------//
static VkRenderPass _vulkanCreateRenderPass(
    GpuDevice& p_GpuDevice, const RenderPassOutput& p_Output, const char* p_Name)
{
  VkAttachmentDescription colorAttachments[8] = {};
  VkAttachmentReference colorAttachmentsRef[8] = {};

  VkAttachmentLoadOp colorOp, depthOp, stencilOp;
  VkImageLayout colorInitial, depthInitial;

  switch (p_Output.colorOperation)
  {
  case RenderPassOperation::kLoad:
    colorOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorInitial = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    break;
  case RenderPassOperation::kClear:
    colorOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorInitial = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    break;
  default:
    colorOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorInitial = VK_IMAGE_LAYOUT_UNDEFINED;
    break;
  }

  switch (p_Output.depthOperation)
  {
  case RenderPassOperation::kLoad:
    depthOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthInitial = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    break;
  case RenderPassOperation::kClear:
    depthOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthInitial = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    break;
  default:
    depthOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthInitial = VK_IMAGE_LAYOUT_UNDEFINED;
    break;
  }

  switch (p_Output.stencilOperation)
  {
  case RenderPassOperation::kLoad:
    stencilOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    break;
  case RenderPassOperation::kClear:
    stencilOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    break;
  default:
    stencilOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    break;
  }

  // Color attachments
  uint32_t c = 0;
  for (; c < p_Output.numColorFormats; ++c)
  {
    VkAttachmentDescription& colorAttachment = colorAttachments[c];
    colorAttachment.format = p_Output.colorFormats[c];
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = colorOp;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = stencilOp;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = colorInitial;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference& colorAttachmentRef = colorAttachmentsRef[c];
    colorAttachmentRef.attachment = c;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }

  // Depth attachment
  VkAttachmentDescription depthAttachment{};
  VkAttachmentReference depthAttachmentRef{};

  if (p_Output.depthStencilFormat != VK_FORMAT_UNDEFINED)
  {

    depthAttachment.format = p_Output.depthStencilFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = depthOp;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = stencilOp;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = depthInitial;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    depthAttachmentRef.attachment = c;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }

  // Create subpass (just a simple subpass)
  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

  // Calculate active attachments for the subpass
  VkAttachmentDescription attachments[kMaxImageOutputs + 1]{};
  uint32_t activeAttachments = 0;
  for (; activeAttachments < p_Output.numColorFormats; ++activeAttachments)
  {
    attachments[activeAttachments] = colorAttachments[activeAttachments];
    ++activeAttachments;
  }
  subpass.colorAttachmentCount = activeAttachments ? activeAttachments - 1 : 0;
  subpass.pColorAttachments = colorAttachmentsRef;

  subpass.pDepthStencilAttachment = nullptr;

  uint32_t depthStencilCount = 0;
  if (p_Output.depthStencilFormat != VK_FORMAT_UNDEFINED)
  {
    attachments[subpass.colorAttachmentCount] = depthAttachment;

    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    depthStencilCount = 1;
  }

  VkRenderPassCreateInfo renderPassCi = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};

  renderPassCi.attachmentCount =
      (activeAttachments ? activeAttachments - 1 : 0) + depthStencilCount;
  renderPassCi.pAttachments = attachments;
  renderPassCi.subpassCount = 1;
  renderPassCi.pSubpasses = &subpass;

  VkRenderPass ret;
  CHECKRES(vkCreateRenderPass(p_GpuDevice.m_VulkanDevice, &renderPassCi, nullptr, &ret));

  p_GpuDevice.setResourceName(VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)ret, p_Name);

  return ret;
}
//---------------------------------------------------------------------------//
// Device implementation:
//---------------------------------------------------------------------------//

void GpuDevice::init(const DeviceCreation& p_Creation)
{
  OutputDebugStringA("Gpu Device init\n");

  // Init allocators:
  m_Allocator = p_Creation.allocator;
  m_TemporaryAllocator = p_Creation.temporaryAllocator;
  m_StringBuffer.init(1024 * 1024, p_Creation.allocator);

  // Init vulkan instance:
  {
    m_VulkanAllocCallbacks = nullptr;

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = nullptr;
    appInfo.pApplicationName = "Graphics Device";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "Vulkan Demo Framework";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_MAKE_VERSION(1, 2, 0);

    VkInstanceCreateInfo instanceCi = {};
    instanceCi.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCi.pNext = nullptr;
    instanceCi.flags = 0;
    instanceCi.pApplicationInfo = &appInfo;
    instanceCi.enabledLayerCount = arrayCount32(kRequestedLayers);
    instanceCi.ppEnabledLayerNames = kRequestedLayers;
    instanceCi.enabledExtensionCount = arrayCount32(kRequestedExtensions);
    instanceCi.ppEnabledExtensionNames = kRequestedExtensions;

    const VkDebugUtilsMessengerCreateInfoEXT debugCi = createDebugUtilsMessengerInfo();
    instanceCi.pNext = &debugCi;

    VkResult result = vkCreateInstance(&instanceCi, m_VulkanAllocCallbacks, &m_VulkanInstance);
    CHECKRES(result);
  }

  m_SwapchainWidth = p_Creation.width;
  m_SwapchainHeight = p_Creation.height;

  // Choose extensions:
  {
    uint32_t numInstanceExtensions;
    vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, nullptr);
    VkExtensionProperties* extensions = (VkExtensionProperties*)FRAMEWORK_ALLOCA(
        sizeof(VkExtensionProperties) * numInstanceExtensions, m_Allocator);
    vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, extensions);
    for (size_t i = 0; i < numInstanceExtensions; i++)
    {

      if (!strcmp(extensions[i].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
      {
        m_DebugUtilsExtensionPresent = true;
        break;
      }
    }

    FRAMEWORK_FREE(extensions, m_Allocator);

    if (!m_DebugUtilsExtensionPresent)
    {
      char msg[256]{};
      sprintf(msg, "Extension %s for debugging non present.", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      OutputDebugStringA(msg);
    }
    else
    {
      // Create new debug utils callback
      PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT =
          (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
              m_VulkanInstance, "vkCreateDebugUtilsMessengerEXT");
      VkDebugUtilsMessengerCreateInfoEXT debugMessengerCi = createDebugUtilsMessengerInfo();

      vkCreateDebugUtilsMessengerEXT(
          m_VulkanInstance,
          &debugMessengerCi,
          m_VulkanAllocCallbacks,
          &m_VulkanDebugUtilsMessenger);
    }
  }

  // Choose physical device
  {
    uint32_t numPhysicalDevice;
    VkResult result = vkEnumeratePhysicalDevices(m_VulkanInstance, &numPhysicalDevice, NULL);
    CHECKRES(result);

    VkPhysicalDevice* gpus = (VkPhysicalDevice*)FRAMEWORK_ALLOCA(
        sizeof(VkPhysicalDevice) * numPhysicalDevice, m_Allocator);
    result = vkEnumeratePhysicalDevices(m_VulkanInstance, &numPhysicalDevice, gpus);
    CHECKRES(result);

    // TODO: improve - choose the first gpu.
    m_VulkanPhysicalDevice = gpus[0];
    FRAMEWORK_FREE(gpus, m_Allocator);

    vkGetPhysicalDeviceProperties(m_VulkanPhysicalDevice, &m_VulkanPhysicalDeviceProps);

    // print selected gpu:
    {
      char msg[256]{};
      sprintf(msg, "GPU Used: %s\n", m_VulkanPhysicalDeviceProps.deviceName);
      OutputDebugStringA(msg);
    }

    kUboAlignment = m_VulkanPhysicalDeviceProps.limits.minUniformBufferOffsetAlignment;
    kSboAlignemnt = m_VulkanPhysicalDeviceProps.limits.minStorageBufferOffsetAlignment;
  }

  // Create logical device
  {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_VulkanPhysicalDevice, &queueFamilyCount, nullptr);

    VkQueueFamilyProperties* queueFamilies = (VkQueueFamilyProperties*)FRAMEWORK_ALLOCA(
        sizeof(VkQueueFamilyProperties) * queueFamilyCount, m_Allocator);
    vkGetPhysicalDeviceQueueFamilyProperties(
        m_VulkanPhysicalDevice, &queueFamilyCount, queueFamilies);

    uint32_t familyIndex = 0;
    for (; familyIndex < queueFamilyCount; ++familyIndex)
    {
      VkQueueFamilyProperties queue_family = queueFamilies[familyIndex];
      if (queue_family.queueCount > 0 &&
          queue_family.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
      {
        // indices.graphicsFamily = i;
        break;
      }
    }

    FRAMEWORK_FREE(queueFamilies, m_Allocator);

    uint32_t deviceExtensionCount = 1;
    const char* deviceExtensions[] = {"VK_KHR_swapchain"};
    const float queuePriority[] = {1.0f};
    VkDeviceQueueCreateInfo queueInfo[1] = {};
    queueInfo[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo[0].queueFamilyIndex = familyIndex;
    queueInfo[0].queueCount = 1;
    queueInfo[0].pQueuePriorities = queuePriority;

    // Enable all features: just pass the physical features 2 struct.
    VkPhysicalDeviceFeatures2 physicalFeatures2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    vkGetPhysicalDeviceFeatures2(m_VulkanPhysicalDevice, &physicalFeatures2);

    VkDeviceCreateInfo deviceCi = {};
    deviceCi.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCi.queueCreateInfoCount = sizeof(queueInfo) / sizeof(queueInfo[0]);
    deviceCi.pQueueCreateInfos = queueInfo;
    deviceCi.enabledExtensionCount = deviceExtensionCount;
    deviceCi.ppEnabledExtensionNames = deviceExtensions;
    deviceCi.pNext = &physicalFeatures2;

    VkResult result =
        vkCreateDevice(m_VulkanPhysicalDevice, &deviceCi, m_VulkanAllocCallbacks, &m_VulkanDevice);
    CHECKRES(result);

    vkGetDeviceQueue(m_VulkanDevice, familyIndex, 0, &m_VulkanQueue);
    m_VulkanQueueFamily = familyIndex;
  }

  //  Get the function pointers to Debug Utils functions.
  if (m_DebugUtilsExtensionPresent)
  {
    pfnSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(
        m_VulkanDevice, "vkSetDebugUtilsObjectNameEXT");
    pfnCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(
        m_VulkanDevice, "vkCmdBeginDebugUtilsLabelEXT");
    pfnCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(
        m_VulkanDevice, "vkCmdEndDebugUtilsLabelEXT");
  }

  // Create surface and swapchain
  {
    SDL_Window* window = (SDL_Window*)p_Creation.window;
    if (SDL_Vulkan_CreateSurface(window, m_VulkanInstance, &m_VulkanWindowSurface) == SDL_FALSE)
    {
      OutputDebugStringA("Failed to create Vulkan surface.\n");
    }
    g_SdlWindow = window;
    int windowWidth, windowHeight;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    // Select Surface Format
    const VkFormat surfaceImageFormats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_R8G8B8_UNORM};
    const VkColorSpaceKHR surfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;

    uint32_t supportedCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_VulkanPhysicalDevice, m_VulkanWindowSurface, &supportedCount, NULL);
    VkSurfaceFormatKHR* supportedFormats = (VkSurfaceFormatKHR*)FRAMEWORK_ALLOCA(
        sizeof(VkSurfaceFormatKHR) * supportedCount, m_Allocator);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_VulkanPhysicalDevice, m_VulkanWindowSurface, &supportedCount, supportedFormats);

    // Cache render pass output
    m_SwapchainOutput.reset();

    // Check for supported formats
    bool formatFound = false;
    const uint32_t surfaceFormatCount = arrayCount32(surfaceImageFormats);

    for (int i = 0; i < surfaceFormatCount; i++)
    {
      for (uint32_t j = 0; j < supportedCount; j++)
      {
        if (supportedFormats[j].format == surfaceImageFormats[i] &&
            supportedFormats[j].colorSpace == surfaceColorSpace)
        {
          m_VulkanSurfaceFormat = supportedFormats[j];
          m_SwapchainOutput.color(surfaceImageFormats[j]);
          formatFound = true;
          break;
        }
      }

      if (formatFound)
        break;
    }

    // Default to the first format supported.
    if (!formatFound)
    {
      m_VulkanSurfaceFormat = supportedFormats[0];
      assert(false);
    }
    FRAMEWORK_FREE(supportedFormats, m_Allocator);

    setPresentMode(m_PresentMode);

    createSwapchain();
  }

  // Create VMA allocator:
  {
    VmaAllocatorCreateInfo ci = {};
    ci.physicalDevice = m_VulkanPhysicalDevice;
    ci.device = m_VulkanDevice;
    ci.instance = m_VulkanInstance;

    VkResult result = vmaCreateAllocator(&ci, &m_VmaAllocator);
    CHECKRES(result);
  }

  // Create descriptor pool
  {
    static const uint32_t kPoolSize = 128;
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kPoolSize},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kPoolSize},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, kPoolSize}};
    VkDescriptorPoolCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.maxSets = kPoolSize * arrayCount32(poolSizes);
    ci.poolSizeCount = arrayCount32(poolSizes);
    ci.pPoolSizes = poolSizes;
    VkResult result = vkCreateDescriptorPool(
        m_VulkanDevice, &ci, m_VulkanAllocCallbacks, &m_VulkanDescriptorPool);
    CHECKRES(result);
  }

  // Init pools
  m_Buffers.init(m_Allocator, 512, sizeof(Buffer));
  m_Textures.init(m_Allocator, 512, sizeof(Texture));
  m_RenderPasses.init(m_Allocator, 256, sizeof(RenderPass));
  m_DescriptorSetLayouts.init(m_Allocator, 128, sizeof(DesciptorSetLayout));
  m_Pipelines.init(m_Allocator, 128, sizeof(Pipeline));
  m_Shaders.init(m_Allocator, 128, sizeof(ShaderState));
  m_DescriptorSets.init(m_Allocator, 128, sizeof(DesciptorSet));
  m_Samplers.init(m_Allocator, 32, sizeof(Sampler));

  // Create synchronization objects
  VkSemaphoreCreateInfo semaphoreCi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  vkCreateSemaphore(
      m_VulkanDevice, &semaphoreCi, m_VulkanAllocCallbacks, &m_VulkanImageAquiredSempaphore);

  for (size_t i = 0; i < kMaxSwapchainImages; i++)
  {

    vkCreateSemaphore(
        m_VulkanDevice, &semaphoreCi, m_VulkanAllocCallbacks, &m_VulkanRenderCompleteSempaphore[i]);

    VkFenceCreateInfo fenceCi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceCi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(
        m_VulkanDevice, &fenceCi, m_VulkanAllocCallbacks, &m_VulkanCmdBufferExectuedFence[i]);
  }

  // Init the command buffer ring:
  g_CmdBufferRing.init(this);

  // Init frame counters
  m_VulkanImageIndex = 0;
  m_CurrentFrameIndex = 1;
  m_PreviousFrameIndex = 0;
  m_AbsoluteFrameIndex = 0;

  // Init resource delection queue and descrptr set updates
  m_ResourceDeletionQueue.init(m_Allocator, 16);
  m_DescriptorSetUpdates.init(m_Allocator, 16);

  // create sampler, depth images and other fundamentals
  SamplerCreation samplerCreation{};
  samplerCreation
      .setAddressModeUVW(
          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
      .setMinMagMip(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR)
      .setName("Sampler Default");
  m_DefaultSampler = createSampler(samplerCreation);

  BufferCreation fullscreenVbCreation = {
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      ResourceUsageType::kImmutable,
      0,
      nullptr,
      "Fullscreen_vb"};
  m_FullscreenVertexBuffer = createBuffer(fullscreenVbCreation);

  ... // TODOs: Also destroy these gpu resources

      TextureHandle m_DepthTexture;
  BufferHandle m_FullscreenVertexBuffer;
  RenderPassOutput m_SwapchainOutput;
  SamplerHandle m_DefaultSampler;
  RenderPassHandle m_SwapchainPass;

  // Dummy resources
  TextureHandle m_DummyTexture;
  BufferHandle m_DummyConstantBuffer;
}
//---------------------------------------------------------------------------//
void GpuDevice::shutdown()
{
  vkDeviceWaitIdle(m_VulkanDevice);

  g_CmdBufferRing.shutdown();

  for (size_t i = 0; i < kMaxSwapchainImages; i++)
  {
    vkDestroySemaphore(m_VulkanDevice, m_VulkanRenderCompleteSempaphore[i], m_VulkanAllocCallbacks);
    vkDestroyFence(m_VulkanDevice, m_VulkanCmdBufferExectuedFence[i], m_VulkanAllocCallbacks);
  }

  vkDestroySemaphore(m_VulkanDevice, m_VulkanImageAquiredSempaphore, m_VulkanAllocCallbacks);

  MapBufferParameters mapParams = {m_DynamicBuffer, 0, 0};
  unmapBuffer(mapParams);
  destroyBuffer(m_DynamicBuffer);

  destroySwapchain();
  vkDestroySurfaceKHR(m_VulkanInstance, m_VulkanWindowSurface, m_VulkanAllocCallbacks);

  vmaDestroyAllocator(m_VmaAllocator);

  m_Buffers.shutdown();
  m_Textures.shutdown();
  m_RenderPasses.shutdown();
  m_DescriptorSetLayouts.shutdown();
  m_Pipelines.shutdown();
  m_Shaders.shutdown();
  m_DescriptorSets.shutdown();
  m_Samplers.shutdown();

  auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      m_VulkanInstance, "vkDestroyDebugUtilsMessengerEXT");
  vkDestroyDebugUtilsMessengerEXT(
      m_VulkanInstance, m_VulkanDebugUtilsMessenger, m_VulkanAllocCallbacks);

  vkDestroyDescriptorPool(m_VulkanDevice, m_VulkanDescriptorPool, m_VulkanAllocCallbacks);

  vkDestroyDevice(m_VulkanDevice, m_VulkanAllocCallbacks);
  vkDestroyInstance(m_VulkanInstance, m_VulkanAllocCallbacks);

  OutputDebugStringA("Gpu device shutdown\n");
}
//---------------------------------------------------------------------------//
// Creation/Destruction of resources
//---------------------------------------------------------------------------//
BufferHandle GpuDevice::createBuffer(const BufferCreation& p_Creation)
{
  BufferHandle handle = {m_Buffers.obtainResource()};
  if (handle.index == kInvalidIndex)
  {
    return handle;
  }

  Buffer* buffer = (Buffer*)m_Buffers.accessResource(handle.index);

  buffer->name = p_Creation.name;
  buffer->size = p_Creation.size;
  buffer->typeFlags = p_Creation.typeFlags;
  buffer->usage = p_Creation.usage;
  buffer->handle = handle;
  buffer->globalOffset = 0;
  buffer->parentBuffer = kInvalidBuffer;

  // Cache and calculate if dynamic buffer can be used.
  static const VkBufferUsageFlags kDynamicBufferMask = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  const bool useGlobalBuffer = (p_Creation.typeFlags & kDynamicBufferMask) != 0;
  if (p_Creation.usage == ResourceUsageType::kDynamic && useGlobalBuffer)
  {
    buffer->parentBuffer = m_DynamicBuffer;
    return handle;
  }

  VkBufferCreateInfo bufferCi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufferCi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | p_Creation.typeFlags;
  bufferCi.size = p_Creation.size > 0 ? p_Creation.size : 1; // 0 sized creations are not permitted.

  VmaAllocationCreateInfo memoryCi{};
  memoryCi.flags = VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
  memoryCi.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

  VmaAllocationInfo allocationInfo{};
  CHECKRES(vmaCreateBuffer(
      m_VmaAllocator,
      &bufferCi,
      &memoryCi,
      &buffer->vkBuffer,
      &buffer->vmaAllocation,
      &allocationInfo));

  setResourceName(VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer->vkBuffer, p_Creation.name);

  buffer->vkDeviceMemory = allocationInfo.deviceMemory;

  if (p_Creation.initialData)
  {
    void* data;
    vmaMapMemory(m_VmaAllocator, buffer->vmaAllocation, &data);
    memcpy(data, p_Creation.initialData, (size_t)p_Creation.size);
    vmaUnmapMemory(m_VmaAllocator, buffer->vmaAllocation);
  }

  return handle;
}
//---------------------------------------------------------------------------//
TextureHandle GpuDevice::createTexture(const TextureCreation& p_Creation) {}
//---------------------------------------------------------------------------//
PipelineHandle GpuDevice::createPipeline(const PipelineCreation& p_Creation) {}
//---------------------------------------------------------------------------//
SamplerHandle GpuDevice::createSampler(const SamplerCreation& p_Creation)
{
  SamplerHandle handle = {m_Samplers.obtainResource()};
  if (handle.index == kInvalidIndex)
  {
    return handle;
  }

  Sampler* sampler = (Sampler*)m_Samplers.accessResource(handle.index);

  sampler->addressModeU = p_Creation.addressModeU;
  sampler->addressModeV = p_Creation.addressModeV;
  sampler->addressModeW = p_Creation.addressModeW;
  sampler->minFilter = p_Creation.minFilter;
  sampler->magFilter = p_Creation.magFilter;
  sampler->mipFilter = p_Creation.mipFilter;
  sampler->name = p_Creation.name;

  VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  ci.addressModeU = p_Creation.addressModeU;
  ci.addressModeV = p_Creation.addressModeV;
  ci.addressModeW = p_Creation.addressModeW;
  ci.minFilter = p_Creation.minFilter;
  ci.magFilter = p_Creation.magFilter;
  ci.mipmapMode = p_Creation.mipFilter;
  ci.anisotropyEnable = 0;
  ci.compareEnable = 0;
  ci.unnormalizedCoordinates = 0;
  ci.borderColor = VkBorderColor::VK_BORDER_COLOR_INT_OPAQUE_WHITE;

  vkCreateSampler(m_VulkanDevice, &ci, m_VulkanAllocCallbacks, &sampler->vkSampler);

  setResourceName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler->vkSampler, p_Creation.name);

  return handle;
}
//---------------------------------------------------------------------------//
DescriptorSetLayoutHandle
GpuDevice::createDescriptorSetLayout(const DescriptorSetLayoutCreation& p_Creation){.}
//---------------------------------------------------------------------------//
DescriptorSetHandle GpuDevice::createDescriptorSet(const DescriptorSetCreation& p_Creation){.}
//---------------------------------------------------------------------------//
RenderPassHandle GpuDevice::createRenderPass(const RenderPassCreation& p_Creation)
{
  RenderPassHandle handle = {m_RenderPasses.obtainResource()};
  if (handle.index == kInvalidIndex)
  {
    return handle;
  }

  RenderPass* renderPass = (RenderPass*)m_RenderPasses.accessResource(handle.index);
  renderPass->type = p_Creation.type;
  // Init the rest of the struct.
  renderPass->numRenderTargets = (uint8_t)p_Creation.numRenderTargets;
  renderPass->dispatchX = 0;
  renderPass->dispatchY = 0;
  renderPass->dispatchZ = 0;
  renderPass->name = p_Creation.name;
  renderPass->vkFrameBuffer = nullptr;
  renderPass->vkRenderPass = nullptr;
  renderPass->scaleX = p_Creation.scaleX;
  renderPass->scaleY = p_Creation.scaleY;
  renderPass->resize = p_Creation.resize;

  // Cache texture handles
  uint32_t c = 0;
  for (; c < p_Creation.numRenderTargets; ++c)
  {
    TextureHandle texHandle = p_Creation.outputTextures[c];
    Texture* textureVk = (Texture*)m_Textures.accessResource(texHandle.index);
    ;

    renderPass->width = textureVk->width;
    renderPass->height = textureVk->height;

    // Cache texture handles
    renderPass->outputTextures[c] = p_Creation.outputTextures[c];
  }

  renderPass->outputDepth = p_Creation.depthStencilTexture;

  switch (p_Creation.type)
  {
  case RenderPassType::kSwapchain: {
    _vulkanCreateSwapchainPass(*this, p_Creation, renderPass);

    break;
  }

  case RenderPassType::kCompute: {
    break;
  }

  case RenderPassType::kGeometry: {
    renderPass->output = _fillRenderPassOutput(*this, p_Creation);
    renderPass->vkRenderPass = getVulkanRenderPass(renderPass->output, p_Creation.name);

    _vulkanCreateFramebuffer(
        *this,
        renderPass,
        p_Creation.outputTextures,
        p_Creation.numRenderTargets,
        p_Creation.depthStencilTexture);

    break;
  }
  }

  return handle;
}
//---------------------------------------------------------------------------//
ShaderStateHandle GpuDevice::createShaderState(const ShaderStateCreation& p_Creation) { . }
//---------------------------------------------------------------------------//
void GpuDevice::destroyBuffer(BufferHandle p_Buffer)
{
  if (p_Buffer.index < m_Buffers.m_PoolSize)
  {
    m_ResourceDeletionQueue.push(
        {ResourceDeletionType::kBuffer, p_Buffer.index, m_CurrentFrameIndex});
  }
  else
  {
    char msg[256]{};
    sprintf(msg, "Graphics error: trying to free invalid Buffer %u\n", p_Buffer.index);
    OutputDebugStringA(msg);
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyTexture(TextureHandle p_Texture) { . }
//---------------------------------------------------------------------------//
void GpuDevice::destroyPipeline(PipelineHandle p_Pipeline) { . }
//---------------------------------------------------------------------------//
void GpuDevice::destroySampler(SamplerHandle p_Sampler)
{
  if (p_Sampler.index < m_Samplers.m_PoolSize)
  {
    m_ResourceDeletionQueue.push(
        {ResourceDeletionType::kSampler, p_Sampler.index, m_CurrentFrameIndex});
  }
  else
  {
    char msg[256]{};
    sprintf(msg, "Graphics error: trying to free invalid Sampler %u\n", p_Sampler.index);
    OutputDebugStringA(msg);
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyDescriptorSetLayout(DescriptorSetLayoutHandle p_Layout) { . }
//---------------------------------------------------------------------------//
void GpuDevice::destroyDescriptorSet(DescriptorSetHandle p_Set) { . }
//---------------------------------------------------------------------------//
void GpuDevice::destroyRenderPass(RenderPassHandle p_RenderPass) { . }
//---------------------------------------------------------------------------//
void GpuDevice::destroyShaderState(ShaderStateHandle p_Shader) { . }
//---------------------------------------------------------------------------//
void* GpuDevice::mapBuffer(const MapBufferParameters& p_Parameters)
{
  if (p_Parameters.buffer.index == kInvalidIndex)
    return nullptr;

  Buffer* buffer = (Buffer*)m_Buffers.accessResource(p_Parameters.buffer.index);

  if (buffer->parentBuffer.index == m_DynamicBuffer.index)
  {

    buffer->globalOffset = m_DynamicAllocatedSize;

    return dynamicAllocate(p_Parameters.size == 0 ? buffer->size : p_Parameters.size);
  }

  void* data;
  vmaMapMemory(m_VmaAllocator, buffer->vmaAllocation, &data);

  return data;
}
//---------------------------------------------------------------------------//
void GpuDevice::unmapBuffer(const MapBufferParameters& p_Parameters)
{
  if (p_Parameters.buffer.index == kInvalidIndex)
    return;

  Buffer* buffer = (Buffer*)m_Buffers.accessResource(p_Parameters.buffer.index);
  if (buffer->parentBuffer.index == m_DynamicBuffer.index)
    return;

  vmaUnmapMemory(m_VmaAllocator, buffer->vmaAllocation);
}
//---------------------------------------------------------------------------//
void* GpuDevice::dynamicAllocate(uint32_t p_Size)
{
  void* MappedMemory = m_DynamicMappedMemory + m_DynamicAllocatedSize;
  m_DynamicAllocatedSize += (uint32_t)Framework::memoryAlign(p_Size, kUboAlignment);
  return MappedMemory;
}
//---------------------------------------------------------------------------//
void GpuDevice::setPresentMode(PresentMode::Enum p_Mode)
{

  // Request a certain mode and confirm that it is available.
  // If not use VK_PRESENT_MODE_FIFO_KHR which is mandatory
  uint32_t supportedCount = 0;

  static VkPresentModeKHR supportedModeAllocated[8];
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      m_VulkanPhysicalDevice, m_VulkanWindowSurface, &supportedCount, NULL);
  assert(supportedCount < 8);
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      m_VulkanPhysicalDevice, m_VulkanWindowSurface, &supportedCount, supportedModeAllocated);

  bool modeFound = false;
  VkPresentModeKHR requestedMode = _toVkPresentMode(p_Mode);
  for (uint32_t j = 0; j < supportedCount; j++)
  {
    if (requestedMode == supportedModeAllocated[j])
    {
      modeFound = true;
      break;
    }
  }

  // Default to VK_PRESENT_MODE_FIFO_KHR that is guaranteed to always be supported
  m_VulkanPresentMode = modeFound ? requestedMode : VK_PRESENT_MODE_FIFO_KHR;
  // Use 4 for immediate ?
  m_VulkanSwapchainImageCount = 3;

  m_PresentMode = modeFound ? p_Mode : PresentMode::kVSync;
}
//---------------------------------------------------------------------------//
void GpuDevice::createSwapchain()
{
  // Check if surface is supported
  VkBool32 surfaceSupported;
  vkGetPhysicalDeviceSurfaceSupportKHR(
      m_VulkanPhysicalDevice, m_VulkanQueueFamily, m_VulkanWindowSurface, &surfaceSupported);
  if (surfaceSupported != VK_TRUE)
  {
    OutputDebugStringA("Error no WSI support on physical device 0\n");
  }

  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      m_VulkanPhysicalDevice, m_VulkanWindowSurface, &surfaceCapabilities);

  VkExtent2D swapchainExtent = surfaceCapabilities.currentExtent;
  if (swapchainExtent.width == UINT32_MAX)
  {
    swapchainExtent.width = clamp(
        swapchainExtent.width,
        surfaceCapabilities.minImageExtent.width,
        surfaceCapabilities.maxImageExtent.width);
    swapchainExtent.height = clamp(
        swapchainExtent.height,
        surfaceCapabilities.minImageExtent.height,
        surfaceCapabilities.maxImageExtent.height);
  }

  {
    char msg[256]{};
    sprintf(
        msg,
        "Create swapchain %u %u - saved %u %u, min image %u\n",
        swapchainExtent.width,
        swapchainExtent.height,
        m_SwapchainWidth,
        m_SwapchainHeight,
        surfaceCapabilities.minImageCount);
    OutputDebugStringA(msg);
  }

  m_SwapchainWidth = (uint16_t)swapchainExtent.width;
  m_SwapchainHeight = (uint16_t)swapchainExtent.height;

  // m_VulkanSwapchainImageCount = surfaceCapabilities.minImageCount + 2;

  VkSwapchainCreateInfoKHR swapchainCi = {};
  swapchainCi.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainCi.surface = m_VulkanWindowSurface;
  swapchainCi.minImageCount = m_VulkanSwapchainImageCount;
  swapchainCi.imageFormat = m_VulkanSurfaceFormat.format;
  swapchainCi.imageExtent = swapchainExtent;
  swapchainCi.clipped = VK_TRUE;
  swapchainCi.imageArrayLayers = 1;
  swapchainCi.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  swapchainCi.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainCi.preTransform = surfaceCapabilities.currentTransform;
  swapchainCi.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchainCi.presentMode = m_VulkanPresentMode;

  VkResult result = vkCreateSwapchainKHR(m_VulkanDevice, &swapchainCi, 0, &m_VulkanSwapchain);
  CHECKRES(result);

  //// Cache swapchain images
  vkGetSwapchainImagesKHR(m_VulkanDevice, m_VulkanSwapchain, &m_VulkanSwapchainImageCount, NULL);
  vkGetSwapchainImagesKHR(
      m_VulkanDevice, m_VulkanSwapchain, &m_VulkanSwapchainImageCount, m_VulkanSwapchainImages);

  for (size_t iv = 0; iv < m_VulkanSwapchainImageCount; iv++)
  {
    // Create an image view which we can render into.
    VkImageViewCreateInfo imageViewCi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    imageViewCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCi.format = m_VulkanSurfaceFormat.format;
    imageViewCi.image = m_VulkanSwapchainImages[iv];
    imageViewCi.subresourceRange.levelCount = 1;
    imageViewCi.subresourceRange.layerCount = 1;
    imageViewCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCi.components.r = VK_COMPONENT_SWIZZLE_R;
    imageViewCi.components.g = VK_COMPONENT_SWIZZLE_G;
    imageViewCi.components.b = VK_COMPONENT_SWIZZLE_B;
    imageViewCi.components.a = VK_COMPONENT_SWIZZLE_A;

    result = vkCreateImageView(
        m_VulkanDevice, &imageViewCi, m_VulkanAllocCallbacks, &m_VulkanSwapchainImageViews[iv]);
    CHECKRES(result);
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::destroySwapchain()
{
  for (size_t iv = 0; iv < m_VulkanSwapchainImageCount; iv++)
  {
    vkDestroyImageView(m_VulkanDevice, m_VulkanSwapchainImageViews[iv], m_VulkanAllocCallbacks);
    vkDestroyFramebuffer(m_VulkanDevice, m_VulkanSwapchainFramebuffers[iv], m_VulkanAllocCallbacks);
  }

  vkDestroySwapchainKHR(m_VulkanDevice, m_VulkanSwapchain, m_VulkanAllocCallbacks);
}
//---------------------------------------------------------------------------//
void GpuDevice::setResourceName(VkObjectType p_ObjType, uint64_t p_Handle, const char* p_Name)
{
  if (!m_DebugUtilsExtensionPresent)
  {
    return;
  }
  VkDebugUtilsObjectNameInfoEXT nameInfo = {VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
  nameInfo.objectType = p_ObjType;
  nameInfo.objectHandle = p_Handle;
  nameInfo.pObjectName = p_Name;
  pfnSetDebugUtilsObjectNameEXT(m_VulkanDevice, &nameInfo);
}
//---------------------------------------------------------------------------//
CommandBuffer* GpuDevice::getInstantCommandBuffer()
{
  CommandBuffer* cmd = g_CmdBufferRing.getCmdBufferInstant(m_CurrentFrameIndex, false);
  return cmd;
}
//---------------------------------------------------------------------------//
VkRenderPass GpuDevice::getVulkanRenderPass(const RenderPassOutput& p_Output, const char* p_Name)
{
  // Hash the memory output and find a compatible VkRenderPass.
  // In current form RenderPassOutput should track everything needed, including load operations.
  uint64_t hashedMemory = Framework::hashBytes((void*)&p_Output, sizeof(RenderPassOutput));
  VkRenderPass vulkanRenderPass = g_RenderPassCache.get(hashedMemory);
  if (vulkanRenderPass)
  {
    return vulkanRenderPass;
  }
  vulkanRenderPass = _vulkanCreateRenderPass(*this, p_Output, p_Name);
  g_RenderPassCache.insert(hashedMemory, vulkanRenderPass);

  return vulkanRenderPass;
}
//---------------------------------------------------------------------------//
} // namespace Graphics
