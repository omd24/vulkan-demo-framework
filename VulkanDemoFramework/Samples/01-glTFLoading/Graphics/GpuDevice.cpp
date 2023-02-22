#include "GpuDevice.hpp"
#include "Graphics/CommandBuffer.hpp"

#include "Externals/SDL2-2.0.18/include/SDL.h"        // SDL_Window
#include "Externals/SDL2-2.0.18/include/SDL_vulkan.h" // SDL_Vulkan_CreateSurface

#include "Foundation/HashMap.hpp"
#include "Foundation/String.hpp"
#include "Foundation/File.hpp"
#include "Foundation/Process.hpp"

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

  CommandBuffer* cmd = p_GpuDevice.getCommandBuffer(false);
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

  VkRenderPass ret{};
  CHECKRES(vkCreateRenderPass(p_GpuDevice.m_VulkanDevice, &renderPassCi, nullptr, &ret));

  p_GpuDevice.setResourceName(VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)ret, p_Name);

  return ret;
}
//---------------------------------------------------------------------------//
static void _vulkanCreateTexture(
    GpuDevice& p_GpuDevice,
    const TextureCreation& p_Creation,
    TextureHandle p_Handle,
    Texture* p_Texture)
{

  p_Texture->width = p_Creation.width;
  p_Texture->height = p_Creation.height;
  p_Texture->depth = p_Creation.depth;
  p_Texture->mipmaps = p_Creation.mipmaps;
  p_Texture->type = p_Creation.type;
  p_Texture->name = p_Creation.name;
  p_Texture->vkFormat = p_Creation.format;
  p_Texture->sampler = nullptr;
  p_Texture->flags = p_Creation.flags;

  p_Texture->handle = p_Handle;

  // Create the image
  VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.format = p_Texture->vkFormat;
  imageInfo.flags = 0;
  imageInfo.imageType = toVkImageType(p_Creation.type);
  imageInfo.extent.width = p_Creation.width;
  imageInfo.extent.height = p_Creation.height;
  imageInfo.extent.depth = p_Creation.depth;
  imageInfo.mipLevels = p_Creation.mipmaps;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

  const bool isRenderTarget =
      (p_Creation.flags & TextureFlags::kRenderTargetMask) == TextureFlags::kRenderTargetMask;
  const bool isComputeUsed =
      (p_Creation.flags & TextureFlags::kComputeMask) == TextureFlags::kComputeMask;

  // Default to always readable from shader.
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

  imageInfo.usage |= isComputeUsed ? VK_IMAGE_USAGE_STORAGE_BIT : 0;

  if (TextureFormat::hasDepthOrStencil(p_Creation.format))
  {
    // Depth/Stencil textures are normally textures you render into.
    imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  else
  {
    imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // TODO
    imageInfo.usage |= isRenderTarget ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0;
  }

  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo memoryCi{};
  memoryCi.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  CHECKRES(vmaCreateImage(
      p_GpuDevice.m_VmaAllocator,
      &imageInfo,
      &memoryCi,
      &p_Texture->vkImage,
      &p_Texture->vmaAllocation,
      nullptr));

  p_GpuDevice.setResourceName(VK_OBJECT_TYPE_IMAGE, (uint64_t)p_Texture->vkImage, p_Creation.name);

  // Create the image view
  VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  info.image = p_Texture->vkImage;
  info.viewType = toVkImageViewType(p_Creation.type);
  info.format = imageInfo.format;

  if (TextureFormat::hasDepthOrStencil(p_Creation.format))
  {
    info.subresourceRange.aspectMask =
        TextureFormat::hasDepth(p_Creation.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
  }
  else
  {
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  info.subresourceRange.levelCount = 1;
  info.subresourceRange.layerCount = 1;
  CHECKRES(vkCreateImageView(
      p_GpuDevice.m_VulkanDevice,
      &info,
      p_GpuDevice.m_VulkanAllocCallbacks,
      &p_Texture->vkImageView));

  p_GpuDevice.setResourceName(
      VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)p_Texture->vkImageView, p_Creation.name);

  p_Texture->vkImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}
//---------------------------------------------------------------------------//
static void _vulkanFillWriteDescriptorSets(
    GpuDevice& p_GpuDevice,
    const DesciptorSetLayout* p_DescriptorSetLayout,
    VkDescriptorSet p_VkDescriptorSet,
    VkWriteDescriptorSet* p_DescriptorWrite,
    VkDescriptorBufferInfo* p_BufferInfo,
    VkDescriptorImageInfo* p_ImageInfo,
    VkSampler p_VkDefaultSampler,
    uint32_t& p_NumResources,
    const ResourceHandle* p_Resources,
    const SamplerHandle* p_Samplers,
    const uint16_t* p_Bindings)
{
  uint32_t usedResources = 0;
  for (uint32_t r = 0; r < p_NumResources; r++)
  {
    // Binding array contains the index into the resource layout binding to retrieve
    // the correct binding informations.
    uint32_t layoutBindingIndex = p_Bindings[r];

    const DescriptorBinding& binding = p_DescriptorSetLayout->bindings[layoutBindingIndex];

    uint32_t i = usedResources;
    ++usedResources;

    p_DescriptorWrite[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    p_DescriptorWrite[i].dstSet = p_VkDescriptorSet;
    // Use binding array to get final binding point.
    const uint32_t bindingPoint = binding.start;
    p_DescriptorWrite[i].dstBinding = bindingPoint;
    p_DescriptorWrite[i].dstArrayElement = 0;
    p_DescriptorWrite[i].descriptorCount = 1;

    switch (binding.type)
    {
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
      p_DescriptorWrite[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

      TextureHandle textureHandle = {p_Resources[r]};
      Texture* texture = (Texture*)p_GpuDevice.m_Textures.accessResource(textureHandle.index);

      // Find proper sampler.
      p_ImageInfo[i].sampler = p_VkDefaultSampler;
      if (texture->sampler)
      {
        p_ImageInfo[i].sampler = texture->sampler->vkSampler;
      }
      if (p_Samplers[r].index != kInvalidIndex)
      {
        Sampler* sampler = (Sampler*)p_GpuDevice.m_Samplers.accessResource(p_Samplers[r].index);
        p_ImageInfo[i].sampler = sampler->vkSampler;
      }

      p_ImageInfo[i].imageLayout = TextureFormat::hasDepthOrStencil(texture->vkFormat)
                                       ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                       : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      p_ImageInfo[i].imageView = texture->vkImageView;

      p_DescriptorWrite[i].pImageInfo = &p_ImageInfo[i];

      break;
    }

    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
      p_DescriptorWrite[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

      TextureHandle textureHandle = {p_Resources[r]};
      Texture* texture = (Texture*)p_GpuDevice.m_Textures.accessResource(textureHandle.index);

      p_ImageInfo[i].sampler = nullptr;
      p_ImageInfo[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      p_ImageInfo[i].imageView = texture->vkImageView;

      p_DescriptorWrite[i].pImageInfo = &p_ImageInfo[i];

      break;
    }

    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
      BufferHandle bufferHandle = {p_Resources[r]};
      Buffer* buffer = (Buffer*)p_GpuDevice.m_Buffers.accessResource(bufferHandle.index);

      p_DescriptorWrite[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      p_DescriptorWrite[i].descriptorType = buffer->usage == ResourceUsageType::kDynamic
                                                ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                                : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

      // Bind parent buffer if present, used for dynamic resources.
      if (buffer->parentBuffer.index != kInvalidIndex)
      {
        Buffer* parentBuffer =
            (Buffer*)p_GpuDevice.m_Buffers.accessResource(buffer->parentBuffer.index);
        p_BufferInfo[i].buffer = parentBuffer->vkBuffer;
      }
      else
      {
        p_BufferInfo[i].buffer = buffer->vkBuffer;
      }

      p_BufferInfo[i].offset = 0;
      p_BufferInfo[i].range = buffer->size;

      p_DescriptorWrite[i].pBufferInfo = &p_BufferInfo[i];

      break;
    }

    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
      BufferHandle bufferHandle = {p_Resources[r]};
      Buffer* buffer = (Buffer*)p_GpuDevice.m_Buffers.accessResource(bufferHandle.index);

      p_DescriptorWrite[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      // Bind parent buffer if present, used for dynamic resources.
      if (buffer->parentBuffer.index != kInvalidIndex)
      {
        Buffer* parentBuffer =
            (Buffer*)p_GpuDevice.m_Buffers.accessResource(buffer->parentBuffer.index);

        p_BufferInfo[i].buffer = parentBuffer->vkBuffer;
      }
      else
      {
        p_BufferInfo[i].buffer = buffer->vkBuffer;
      }

      p_BufferInfo[i].offset = 0;
      p_BufferInfo[i].range = buffer->size;

      p_DescriptorWrite[i].pBufferInfo = &p_BufferInfo[i];

      break;
    }

    default: {
      assert(false && "Resource type not supported in descriptor set creation!");
      break;
    }
    }
  }

  p_NumResources = usedResources;
}
//---------------------------------------------------------------------------//
template <class T> constexpr const T& _min(const T& a, const T& b)
{
  // TODO: move this somewhere else
  return (a < b) ? a : b;
}
//---------------------------------------------------------------------------//
template <class T> constexpr const T& _max(const T& a, const T& b)
{
  // TODO: move this somewhere else
  return (a < b) ? b : a;
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

  // Allocate command buffers queue
  {
    uint8_t* memory = FRAMEWORK_ALLOCAM(sizeof(CommandBuffer*) * 128, m_Allocator);
    m_QueuedCommandBuffers = (CommandBuffer**)(memory);

    // Init the command buffer ring:
    g_CmdBufferRing.init(this);
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
      m_VulkanDevice, &semaphoreCi, m_VulkanAllocCallbacks, &m_VulkanImageAcquiredSemaphore);

  for (size_t i = 0; i < kMaxSwapchainImages; i++)
  {

    vkCreateSemaphore(
        m_VulkanDevice, &semaphoreCi, m_VulkanAllocCallbacks, &m_VulkanRenderCompleteSemaphore[i]);

    VkFenceCreateInfo fenceCi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceCi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(
        m_VulkanDevice, &fenceCi, m_VulkanAllocCallbacks, &m_VulkanCmdBufferExectuedFence[i]);
  }

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

  // Create depth image
  TextureCreation depthTextureCreation = {
      nullptr,
      m_SwapchainWidth,
      m_SwapchainHeight,
      1,
      1,
      0,
      VK_FORMAT_D32_SFLOAT,
      TextureType::kTexture2D,
      "DepthImage_Texture"};
  m_DepthTexture = createTexture(depthTextureCreation);

  // Cache depth texture format
  m_SwapchainOutput.depth(VK_FORMAT_D32_SFLOAT);

  RenderPassCreation swapchainPassCreation = {};
  swapchainPassCreation.setType(RenderPassType::kSwapchain).setName("Swapchain");
  swapchainPassCreation.setOperations(
      RenderPassOperation::kClear, RenderPassOperation::kClear, RenderPassOperation::kClear);
  m_SwapchainPass = createRenderPass(swapchainPassCreation);

  // Init Dummy resources
  TextureCreation dummyTextureCreation = {
      nullptr, 1, 1, 1, 1, 0, VK_FORMAT_R8_UINT, TextureType::kTexture2D};
  m_DummyTexture = createTexture(dummyTextureCreation);

  BufferCreation dummyConstantBufferCreation = {
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      ResourceUsageType::kImmutable,
      16,
      nullptr,
      "Dummy Constant Buffer"};
  m_DummyConstantBuffer = createBuffer(dummyConstantBufferCreation);

  // Get binaries path
  char* vulkanEnv = m_StringBuffer.reserve(512);
  ExpandEnvironmentStringsA("%VULKAN_SDK%", vulkanEnv, 512);
  char* compilerPath = m_StringBuffer.appendUseFormatted("%s\\Bin\\", vulkanEnv);

  strcpy(m_VulkanBinariesPath, compilerPath);
  m_StringBuffer.clear();

  // Dynamic buffer handling
  m_DynamicPerFrameSize = 1024 * 1024 * 10;
  BufferCreation bc;
  bc.set(
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        ResourceUsageType::kImmutable,
        m_DynamicPerFrameSize * kMaxFrames)
      .setName("Dynamic Persistent Buffer");
  m_DynamicBuffer = createBuffer(bc);

  MapBufferParameters mapParams = {m_DynamicBuffer, 0, 0};
  m_DynamicMappedMemory = (uint8_t*)mapBuffer(mapParams);

  // Init render pass cache
  g_RenderPassCache.init(m_Allocator, 16);

  // Cache working directory
  Framework::directoryCurrent(&m_Cwd);
}
//---------------------------------------------------------------------------//
void GpuDevice::shutdown()
{
  vkDeviceWaitIdle(m_VulkanDevice);

  g_CmdBufferRing.shutdown();

  for (size_t i = 0; i < kMaxSwapchainImages; i++)
  {
    vkDestroySemaphore(m_VulkanDevice, m_VulkanRenderCompleteSemaphore[i], m_VulkanAllocCallbacks);
    vkDestroyFence(m_VulkanDevice, m_VulkanCmdBufferExectuedFence[i], m_VulkanAllocCallbacks);
  }

  vkDestroySemaphore(m_VulkanDevice, m_VulkanImageAcquiredSemaphore, m_VulkanAllocCallbacks);

  MapBufferParameters mapParams = {m_DynamicBuffer, 0, 0};
  unmapBuffer(mapParams);

  destroyTexture(m_DepthTexture);
  destroyBuffer(m_FullscreenVertexBuffer);
  destroyBuffer(m_DynamicBuffer);
  destroyRenderPass(m_SwapchainPass);
  destroyTexture(m_DummyTexture);
  destroyBuffer(m_DummyConstantBuffer);
  destroySampler(m_DefaultSampler);

  // Destroy all pending resources.
  for (uint32_t i = 0; i < m_ResourceDeletionQueue.m_Size; i++)
  {
    ResourceUpdate& resourceDeletion = m_ResourceDeletionQueue[i];

    // Skip just freed resources.
    if (resourceDeletion.currentFrame == -1)
      continue;

    // Real resource destruction
    releaseResource(resourceDeletion);
  }

  // Destroy render passes from the cache.
  Framework::FlatHashMapIterator it = g_RenderPassCache.iteratorBegin();
  while (it.isValid())
  {
    VkRenderPass renderPass = g_RenderPassCache.get(it);
    vkDestroyRenderPass(m_VulkanDevice, renderPass, m_VulkanAllocCallbacks);
    g_RenderPassCache.iteratorAdvance(it);
  }
  g_RenderPassCache.shutdown();

  // Destroy swapchain render pass, not present in the cache.
  {
    RenderPass* swapchainPass = (RenderPass*)m_RenderPasses.accessResource(m_SwapchainPass.index);
    vkDestroyRenderPass(m_VulkanDevice, swapchainPass->vkRenderPass, m_VulkanAllocCallbacks);
  }

  // Destroy swapchain
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
void GpuDevice::newFrame()
{
  // Fence wait and reset
  VkFence* renderCompleteFence = &m_VulkanCmdBufferExectuedFence[m_CurrentFrameIndex];

  if (vkGetFenceStatus(m_VulkanDevice, *renderCompleteFence) != VK_SUCCESS)
  {
    vkWaitForFences(m_VulkanDevice, 1, renderCompleteFence, VK_TRUE, UINT64_MAX);
  }
  vkResetFences(m_VulkanDevice, 1, renderCompleteFence);

  // Command pool reset
  g_CmdBufferRing.resetPools(m_CurrentFrameIndex);
  // Dynamic memory update
  const uint32_t usedSize = m_DynamicAllocatedSize - (m_DynamicPerFrameSize * m_PreviousFrameIndex);
  m_DynamicMaxPerFrameSize = _min(usedSize, m_DynamicMaxPerFrameSize);
  m_DynamicAllocatedSize = m_DynamicPerFrameSize * m_CurrentFrameIndex;

  // Descriptor Set Updates
  if (m_DescriptorSetUpdates.m_Size > 0)
  {
    for (int i = m_DescriptorSetUpdates.m_Size - 1; i >= 0; i--)
    {
      DescriptorSetUpdate& update = m_DescriptorSetUpdates[i];

#pragma region Update descriptor set
      // Use a dummy descriptor set to delete the vulkan descriptor set handle
      DescriptorSetHandle dummyDeleteDescriptorSetHandle = {m_DescriptorSets.obtainResource()};
      DesciptorSet* dummyDeleteDescriptorSet =
          (DesciptorSet*)m_DescriptorSets.accessResource(dummyDeleteDescriptorSetHandle.index);

      DesciptorSet* descriptorSet =
          (DesciptorSet*)m_DescriptorSets.accessResource(update.descriptorSet.index);
      const DesciptorSetLayout* descriptorSetLayout = descriptorSet->layout;

      dummyDeleteDescriptorSet->vkDescriptorSet = descriptorSet->vkDescriptorSet;
      dummyDeleteDescriptorSet->bindings = nullptr;
      dummyDeleteDescriptorSet->resources = nullptr;
      dummyDeleteDescriptorSet->samplers = nullptr;
      dummyDeleteDescriptorSet->numResources = 0;

      destroyDescriptorSet(dummyDeleteDescriptorSetHandle);

      // Allocate the new descriptor set and update its content.
      VkWriteDescriptorSet descriptor_write[8];
      VkDescriptorBufferInfo buffer_info[8];
      VkDescriptorImageInfo image_info[8];

      Sampler* defaultSampler = (Sampler*)m_Samplers.accessResource(m_DefaultSampler.index);

      VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      allocInfo.descriptorPool = m_VulkanDescriptorPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &descriptorSet->layout->vkDescriptorSetLayout;
      vkAllocateDescriptorSets(m_VulkanDevice, &allocInfo, &descriptorSet->vkDescriptorSet);

      uint32_t numResources = descriptorSetLayout->numBindings;
      _vulkanFillWriteDescriptorSets(
          *this,
          descriptorSetLayout,
          descriptorSet->vkDescriptorSet,
          descriptor_write,
          buffer_info,
          image_info,
          defaultSampler->vkSampler,
          numResources,
          descriptorSet->resources,
          descriptorSet->samplers,
          descriptorSet->bindings);

      vkUpdateDescriptorSets(m_VulkanDevice, numResources, descriptor_write, 0, nullptr);
#pragma endregion End Update descriptor set

      update.frameIssued = UINT32_MAX;
      m_DescriptorSetUpdates.deleteSwap(i);
    }
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::present()
{
  VkResult result = vkAcquireNextImageKHR(
      m_VulkanDevice,
      m_VulkanSwapchain,
      UINT64_MAX,
      m_VulkanImageAcquiredSemaphore,
      VK_NULL_HANDLE,
      &m_VulkanImageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR)
  {
    resizeSwapchain();

    // Advance frame counters that are skipped during this frame.
    frameCountersAdvance();

    return;
  }
  VkFence* renderCompleteFence = &m_VulkanCmdBufferExectuedFence[m_CurrentFrameIndex];
  VkSemaphore* renderCompleteSemaphore = &m_VulkanRenderCompleteSemaphore[m_CurrentFrameIndex];

  // Copy all commands
  VkCommandBuffer enqueuedCommandBuffers[4];
  for (uint32_t c = 0; c < m_NumQueuedCommandBuffers; c++)
  {
    CommandBuffer* commandBuffer = m_QueuedCommandBuffers[c];

    enqueuedCommandBuffers[c] = commandBuffer->m_VulkanCmdBuffer;
    // NOTE: why it was needing current_pipeline to be setup ?
    if (commandBuffer->m_IsRecording && commandBuffer->m_CurrentRenderPass &&
        (commandBuffer->m_CurrentRenderPass->type != RenderPassType::kCompute))
      vkCmdEndRenderPass(commandBuffer->m_VulkanCmdBuffer);

    vkEndCommandBuffer(commandBuffer->m_VulkanCmdBuffer);
  }

  // Submit command buffers
  VkSemaphore wait_semaphores[] = {m_VulkanImageAcquiredSemaphore};
  VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = wait_semaphores;
  submit_info.pWaitDstStageMask = wait_stages;
  submit_info.commandBufferCount = m_NumQueuedCommandBuffers;
  submit_info.pCommandBuffers = enqueuedCommandBuffers;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = renderCompleteSemaphore;

  vkQueueSubmit(m_VulkanQueue, 1, &submit_info, *renderCompleteFence);

  VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = renderCompleteSemaphore;

  VkSwapchainKHR swap_chains[] = {m_VulkanSwapchain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swap_chains;
  presentInfo.pImageIndices = &m_VulkanImageIndex;
  presentInfo.pResults = nullptr; // Optional
  result = vkQueuePresentKHR(m_VulkanQueue, &presentInfo);

  m_NumQueuedCommandBuffers = 0;

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_Resized)
  {
    m_Resized = false;
    resizeSwapchain();

    // Advance frame counters that are skipped during this frame.
    frameCountersAdvance();

    return;
  }

  // TODO:
  // This is called inside resizeSwapchain as well to correctly work.
  // frameCountersAdvance();

  // Resource deletion using reverse iteration and swap with last element.
  if (m_ResourceDeletionQueue.m_Size > 0)
  {
    for (int i = m_ResourceDeletionQueue.m_Size - 1; i >= 0; i--)
    {
      ResourceUpdate& resourceDeletion = m_ResourceDeletionQueue[i];

      if (resourceDeletion.currentFrame == m_CurrentFrameIndex)
      {
        // Real release:
        releaseResource(resourceDeletion);

        // Mark resource as free
        resourceDeletion.currentFrame = UINT32_MAX;

        // Swap element
        m_ResourceDeletionQueue.deleteSwap(i);
      }
    }
  }
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
TextureHandle GpuDevice::createTexture(const TextureCreation& p_Creation)
{
  uint32_t resourceIndex = m_Textures.obtainResource();
  TextureHandle handle = {resourceIndex};
  if (resourceIndex == kInvalidIndex)
  {
    return handle;
  }

  Texture* texture = (Texture*)m_Textures.accessResource(handle.index);

  _vulkanCreateTexture(*this, p_Creation, handle, texture);

  // Copy buffer-data if present
  if (p_Creation.initialData)
  {
    // Create stating buffer
    VkBufferCreateInfo bufferCi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferCi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    uint32_t imageSize = p_Creation.width * p_Creation.height * 4;
    bufferCi.size = imageSize;

    VmaAllocationCreateInfo memoryCi{};
    memoryCi.flags = VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
    memoryCi.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VmaAllocationInfo allocationInfo{};
    VkBuffer stagingBuffer{};
    VmaAllocation stagingAllocation{};
    CHECKRES(vmaCreateBuffer(
        m_VmaAllocator, &bufferCi, &memoryCi, &stagingBuffer, &stagingAllocation, &allocationInfo));

    // Copy buffer-data
    void* destinationData;
    vmaMapMemory(m_VmaAllocator, stagingAllocation, &destinationData);
    memcpy(destinationData, p_Creation.initialData, static_cast<size_t>(imageSize));
    vmaUnmapMemory(m_VmaAllocator, stagingAllocation);

    // Execute command buffer
    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    CommandBuffer* cmdBuffer = getCommandBuffer(false);
    vkBeginCommandBuffer(cmdBuffer->m_VulkanCmdBuffer, &beginInfo);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {p_Creation.width, p_Creation.height, p_Creation.depth};

    // Transition
    _transitionImageLayout(
        cmdBuffer->m_VulkanCmdBuffer,
        texture->vkImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        false);
    // Copy
    vkCmdCopyBufferToImage(
        cmdBuffer->m_VulkanCmdBuffer,
        stagingBuffer,
        texture->vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
    // Transition
    _transitionImageLayout(
        cmdBuffer->m_VulkanCmdBuffer,
        texture->vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        false);

    vkEndCommandBuffer(cmdBuffer->m_VulkanCmdBuffer);

    // Submit command buffer
    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer->m_VulkanCmdBuffer;

    vkQueueSubmit(m_VulkanQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_VulkanQueue);

    vmaDestroyBuffer(m_VmaAllocator, stagingBuffer, stagingAllocation);

    // TODO: free command buffer
    vkResetCommandBuffer(
        cmdBuffer->m_VulkanCmdBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

    texture->vkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  return handle;
}
//---------------------------------------------------------------------------//
PipelineHandle GpuDevice::createPipeline(const PipelineCreation& p_Creation)
{
  PipelineHandle handle = {m_Pipelines.obtainResource()};
  if (handle.index == kInvalidIndex)
  {
    return handle;
  }

  ShaderStateHandle shaderState = createShaderState(p_Creation.shaders);
  if (shaderState.index == kInvalidIndex)
  {
    // Shader did not compile.
    m_Pipelines.releaseResource(handle.index);
    handle.index = kInvalidIndex;

    return handle;
  }

  // Now that shaders have compiled we can create the pipeline.
  Pipeline* pipeline = (Pipeline*)m_Pipelines.accessResource(handle.index);
  ShaderState* shaderStateData = (ShaderState*)m_Shaders.accessResource(shaderState.index);

  pipeline->shaderState = shaderState;

  VkDescriptorSetLayout vkLayouts[kMaxDescriptorSetLayouts];

  // Create VkPipelineLayout
  for (uint32_t l = 0; l < p_Creation.numActiveLayouts; ++l)
  {
    pipeline->descriptorSetLayout[l] = (DesciptorSetLayout*)m_DescriptorSetLayouts.accessResource(
        p_Creation.descriptorSetLayouts[l].index);
    pipeline->descriptorSetLayoutHandle[l] = p_Creation.descriptorSetLayouts[l];

    vkLayouts[l] = pipeline->descriptorSetLayout[l]->vkDescriptorSetLayout;
  }

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutInfo.pSetLayouts = vkLayouts;
  pipelineLayoutInfo.setLayoutCount = p_Creation.numActiveLayouts;

  VkPipelineLayout pipelineLayout{};
  CHECKRES(vkCreatePipelineLayout(
      m_VulkanDevice, &pipelineLayoutInfo, m_VulkanAllocCallbacks, &pipelineLayout));
  // Cache pipeline layout
  pipeline->vkPipelineLayout = pipelineLayout;
  pipeline->numActiveLayouts = p_Creation.numActiveLayouts;

  // Create full pipeline
  if (shaderStateData->graphicsPipeline)
  {
    VkGraphicsPipelineCreateInfo pipelineCi = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};

    //// Shader stage
    pipelineCi.pStages = shaderStateData->shaderStageInfo;
    pipelineCi.stageCount = shaderStateData->activeShaders;
    //// PipelineLayout
    pipelineCi.layout = pipelineLayout;

    //// Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputCi = {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    // Vertex attributes.
    VkVertexInputAttributeDescription vertexAttributes[8];
    if (p_Creation.vertexInput.numVertexAttributes)
    {

      for (uint32_t i = 0; i < p_Creation.vertexInput.numVertexAttributes; ++i)
      {
        const VertexAttribute& vertexAttribute = p_Creation.vertexInput.vertexAttributes[i];
        vertexAttributes[i] = {
            vertexAttribute.location,
            vertexAttribute.binding,
            toVkVertexFormat(vertexAttribute.format),
            vertexAttribute.offset};
      }

      vertexInputCi.vertexAttributeDescriptionCount = p_Creation.vertexInput.numVertexAttributes;
      vertexInputCi.pVertexAttributeDescriptions = vertexAttributes;
    }
    else
    {
      vertexInputCi.vertexAttributeDescriptionCount = 0;
      vertexInputCi.pVertexAttributeDescriptions = nullptr;
    }
    // Vertex bindings
    VkVertexInputBindingDescription vertexBindings[8];
    if (p_Creation.vertexInput.numVertexStreams)
    {
      vertexInputCi.vertexBindingDescriptionCount = p_Creation.vertexInput.numVertexStreams;

      for (uint32_t i = 0; i < p_Creation.vertexInput.numVertexStreams; ++i)
      {
        const VertexStream& vertexStream = p_Creation.vertexInput.vertexStreams[i];
        VkVertexInputRate vertexRate = vertexStream.inputRate == VertexInputRate::kPerVertex
                                           ? VkVertexInputRate::VK_VERTEX_INPUT_RATE_VERTEX
                                           : VkVertexInputRate::VK_VERTEX_INPUT_RATE_INSTANCE;
        vertexBindings[i] = {vertexStream.binding, vertexStream.stride, vertexRate};
      }
      vertexInputCi.pVertexBindingDescriptions = vertexBindings;
    }
    else
    {
      vertexInputCi.vertexBindingDescriptionCount = 0;
      vertexInputCi.pVertexBindingDescriptions = nullptr;
    }

    pipelineCi.pVertexInputState = &vertexInputCi;

    //// Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    pipelineCi.pInputAssemblyState = &inputAssembly;

    //// Color Blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment[8];

    if (p_Creation.blendState.activeStates)
    {
      for (size_t i = 0; i < p_Creation.blendState.activeStates; i++)
      {
        const BlendState& blendState = p_Creation.blendState.blendStates[i];

        colorBlendAttachment[i].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment[i].blendEnable = blendState.blendEnabled ? VK_TRUE : VK_FALSE;
        colorBlendAttachment[i].srcColorBlendFactor = blendState.sourceColor;
        colorBlendAttachment[i].dstColorBlendFactor = blendState.destinationColor;
        colorBlendAttachment[i].colorBlendOp = blendState.colorOperation;

        if (blendState.separateBlend)
        {
          colorBlendAttachment[i].srcAlphaBlendFactor = blendState.sourceAlpha;
          colorBlendAttachment[i].dstAlphaBlendFactor = blendState.destinationAlpha;
          colorBlendAttachment[i].alphaBlendOp = blendState.alphaOperation;
        }
        else
        {
          colorBlendAttachment[i].srcAlphaBlendFactor = blendState.sourceColor;
          colorBlendAttachment[i].dstAlphaBlendFactor = blendState.destinationColor;
          colorBlendAttachment[i].alphaBlendOp = blendState.colorOperation;
        }
      }
    }
    else
    {
      // Default non blended state
      colorBlendAttachment[0] = {};
      colorBlendAttachment[0].blendEnable = VK_FALSE;
      colorBlendAttachment[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = p_Creation.blendState.activeStates
                                        ? p_Creation.blendState.activeStates
                                        : 1; // Always have 1 blend defined.
    colorBlending.pAttachments = colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    pipelineCi.pColorBlendState = &colorBlending;

    //// Depth Stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    depthStencil.depthWriteEnable = p_Creation.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.stencilTestEnable = p_Creation.depthStencil.stencilEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthTestEnable = p_Creation.depthStencil.depthEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = p_Creation.depthStencil.depthComparison;
    if (p_Creation.depthStencil.stencilEnable)
    {
      assert(false && "Stencil not supported yet!");
    }

    pipelineCi.pDepthStencilState = &depthStencil;

    //// Multisample
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;          // Optional
    multisampling.pSampleMask = nullptr;            // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE;      // Optional

    pipelineCi.pMultisampleState = &multisampling;

    //// Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = p_Creation.rasterization.cullMode;
    rasterizer.frontFace = p_Creation.rasterization.front;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f;          // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional

    pipelineCi.pRasterizationState = &rasterizer;

    //// Tessellation
    pipelineCi.pTessellationState;

    //// Viewport state
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_SwapchainWidth;
    viewport.height = (float)m_SwapchainHeight;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {m_SwapchainWidth, m_SwapchainHeight};

    VkPipelineViewportStateCreateInfo viewportStateCi{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportStateCi.viewportCount = 1;
    viewportStateCi.pViewports = &viewport;
    viewportStateCi.scissorCount = 1;
    viewportStateCi.pScissors = &scissor;

    pipelineCi.pViewportState = &viewportStateCi;

    //// Render Pass
    pipelineCi.renderPass = getVulkanRenderPass(p_Creation.renderPass, p_Creation.name);

    //// Dynamic states
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicStateCi{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicStateCi.dynamicStateCount = arrayCount32(dynamicStates);
    dynamicStateCi.pDynamicStates = dynamicStates;

    pipelineCi.pDynamicState = &dynamicStateCi;

    vkCreateGraphicsPipelines(
        m_VulkanDevice,
        VK_NULL_HANDLE,
        1,
        &pipelineCi,
        m_VulkanAllocCallbacks,
        &pipeline->vkPipeline);

    pipeline->vkBindPoint = VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS;
  }
  else
  {
    VkComputePipelineCreateInfo pipelineCi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};

    pipelineCi.stage = shaderStateData->shaderStageInfo[0];
    pipelineCi.layout = pipelineLayout;

    vkCreateComputePipelines(
        m_VulkanDevice,
        VK_NULL_HANDLE,
        1,
        &pipelineCi,
        m_VulkanAllocCallbacks,
        &pipeline->vkPipeline);

    pipeline->vkBindPoint = VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE;
  }

  return handle;
}
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
GpuDevice::createDescriptorSetLayout(const DescriptorSetLayoutCreation& p_Creation)
{
  DescriptorSetLayoutHandle handle = {m_DescriptorSetLayouts.obtainResource()};
  if (handle.index == kInvalidIndex)
  {
    return handle;
  }

  DesciptorSetLayout* descriptorSetLayout =
      (DesciptorSetLayout*)m_DescriptorSetLayouts.accessResource(handle.index);

  // Create flattened binding list
  descriptorSetLayout->numBindings = (uint16_t)p_Creation.numBindings;
  uint8_t* memory = FRAMEWORK_ALLOCAM(
      (sizeof(VkDescriptorSetLayoutBinding) + sizeof(DescriptorBinding)) * p_Creation.numBindings,
      m_Allocator);
  descriptorSetLayout->bindings = (DescriptorBinding*)memory;
  descriptorSetLayout->vkBinding =
      (VkDescriptorSetLayoutBinding*)(memory + sizeof(DescriptorBinding) * p_Creation.numBindings);
  descriptorSetLayout->handle = handle;
  descriptorSetLayout->setIndex = uint16_t(p_Creation.setIndex);

  uint32_t usedBindings = 0;
  for (uint32_t r = 0; r < p_Creation.numBindings; ++r)
  {
    DescriptorBinding& binding = descriptorSetLayout->bindings[r];
    const DescriptorSetLayoutCreation::Binding& inputBinding = p_Creation.bindings[r];
    binding.start = inputBinding.start == UINT16_MAX ? (uint16_t)r : inputBinding.start;
    binding.count = 1;
    binding.type = inputBinding.type;
    binding.name = inputBinding.name;

    VkDescriptorSetLayoutBinding& vkBinding = descriptorSetLayout->vkBinding[usedBindings];
    ++usedBindings;

    vkBinding.binding = binding.start;
    vkBinding.descriptorType = inputBinding.type;
    vkBinding.descriptorType = vkBinding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                   : vkBinding.descriptorType;
    vkBinding.descriptorCount = 1;
    vkBinding.stageFlags = VK_SHADER_STAGE_ALL;
    vkBinding.pImmutableSamplers = nullptr;
  }

  // Create the descriptor set layout
  VkDescriptorSetLayoutCreateInfo layoutInfo = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = usedBindings; // p_Creation.numBindings;
  layoutInfo.pBindings = descriptorSetLayout->vkBinding;

  vkCreateDescriptorSetLayout(
      m_VulkanDevice,
      &layoutInfo,
      m_VulkanAllocCallbacks,
      &descriptorSetLayout->vkDescriptorSetLayout);

  return handle;
}
//---------------------------------------------------------------------------//
DescriptorSetHandle GpuDevice::createDescriptorSet(const DescriptorSetCreation& p_Creation)
{
  DescriptorSetHandle handle = {m_DescriptorSets.obtainResource()};
  if (handle.index == kInvalidIndex)
  {
    return handle;
  }

  DesciptorSet* descriptorSet = (DesciptorSet*)m_DescriptorSets.accessResource(handle.index);
  const DesciptorSetLayout* descriptorSetLayout =
      (DesciptorSetLayout*)m_DescriptorSetLayouts.accessResource(p_Creation.layout.index);

  // Allocate descriptor set
  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = m_VulkanDescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &descriptorSetLayout->vkDescriptorSetLayout;

  CHECKRES(vkAllocateDescriptorSets(m_VulkanDevice, &allocInfo, &descriptorSet->vkDescriptorSet));
  // Cache data
  uint8_t* memory = FRAMEWORK_ALLOCAM(
      (sizeof(ResourceHandle) + sizeof(SamplerHandle) + sizeof(uint16_t)) * p_Creation.numResources,
      m_Allocator);
  descriptorSet->resources = (ResourceHandle*)memory;
  descriptorSet->samplers =
      (SamplerHandle*)(memory + sizeof(ResourceHandle) * p_Creation.numResources);
  descriptorSet->bindings =
      (uint16_t*)(memory + (sizeof(ResourceHandle) + sizeof(SamplerHandle)) * p_Creation.numResources);
  descriptorSet->numResources = p_Creation.numResources;
  descriptorSet->layout = descriptorSetLayout;

  // Update descriptor set
  VkWriteDescriptorSet descriptorWrite[8];
  VkDescriptorBufferInfo bufferInfo[8];
  VkDescriptorImageInfo imageInfo[8];

  Sampler* defaultSampler = (Sampler*)m_Samplers.accessResource(m_DefaultSampler.index);

  uint32_t numResources = p_Creation.numResources;
  _vulkanFillWriteDescriptorSets(
      *this,
      descriptorSetLayout,
      descriptorSet->vkDescriptorSet,
      descriptorWrite,
      bufferInfo,
      imageInfo,
      defaultSampler->vkSampler,
      numResources,
      p_Creation.resources,
      p_Creation.samplers,
      p_Creation.bindings);

  // Cache resources
  for (uint32_t r = 0; r < p_Creation.numResources; r++)
  {
    descriptorSet->resources[r] = p_Creation.resources[r];
    descriptorSet->samplers[r] = p_Creation.samplers[r];
    descriptorSet->bindings[r] = p_Creation.bindings[r];
  }

  vkUpdateDescriptorSets(m_VulkanDevice, numResources, descriptorWrite, 0, nullptr);

  return handle;
}
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
ShaderStateHandle GpuDevice::createShaderState(const ShaderStateCreation& p_Creation)
{
  ShaderStateHandle handle = {kInvalidIndex};

  if (p_Creation.stagesCount == 0 || p_Creation.stages == nullptr)
  {
    char msg[256]{};
    sprintf(msg, "Shader %s does not contain shader stages.\n", p_Creation.name);
    OutputDebugStringA(msg);
    return handle;
  }

  handle.index = m_Shaders.obtainResource();
  if (handle.index == kInvalidIndex)
  {
    return handle;
  }

  // For each shader stage, compile them individually.
  uint32_t compiledShaders = 0;

  ShaderState* shaderState = (ShaderState*)m_Shaders.accessResource(handle.index);
  shaderState->graphicsPipeline = true;
  shaderState->activeShaders = 0;

  size_t currentTemporaryMarker = m_TemporaryAllocator->getMarker();

  for (compiledShaders = 0; compiledShaders < p_Creation.stagesCount; ++compiledShaders)
  {
    const ShaderStage& stage = p_Creation.stages[compiledShaders];

    // Gives priority to compute: if any is present (and it should not be) then it is not a graphics
    // pipeline.
    if (stage.type == VK_SHADER_STAGE_COMPUTE_BIT)
    {
      shaderState->graphicsPipeline = false;
    }

    VkShaderModuleCreateInfo shaderCi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};

    if (p_Creation.spvInput)
    {
      shaderCi.codeSize = stage.codeSize;
      shaderCi.pCode = reinterpret_cast<const uint32_t*>(stage.code);
    }
    else
    {
      shaderCi = compileShader(stage.code, stage.codeSize, stage.type, p_Creation.name);
    }

    // Compile shader module
    VkPipelineShaderStageCreateInfo& shaderStageCi = shaderState->shaderStageInfo[compiledShaders];
    memset(&shaderStageCi, 0, sizeof(VkPipelineShaderStageCreateInfo));
    shaderStageCi.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageCi.pName = "main";
    shaderStageCi.stage = stage.type;

    if (vkCreateShaderModule(
            m_VulkanDevice,
            &shaderCi,
            nullptr,
            &shaderState->shaderStageInfo[compiledShaders].module) != VK_SUCCESS)
    {

      break;
    }

    setResourceName(
        VK_OBJECT_TYPE_SHADER_MODULE,
        (uint64_t)shaderState->shaderStageInfo[compiledShaders].module,
        p_Creation.name);
  }

  m_TemporaryAllocator->freeMarker(currentTemporaryMarker);

  bool creationFailed = compiledShaders != p_Creation.stagesCount;
  if (!creationFailed)
  {
    shaderState->activeShaders = compiledShaders;
    shaderState->name = p_Creation.name;
  }

  if (creationFailed)
  {
    destroyShaderState(handle);
    handle.index = kInvalidIndex;

    // Dump shader code
    OutputDebugStringA("Error in creation of shader. Dumping all shader informations.\n");
    for (compiledShaders = 0; compiledShaders < p_Creation.stagesCount; ++compiledShaders)
    {
      const ShaderStage& stage = p_Creation.stages[compiledShaders];
      char msg[512]{};
      sprintf(msg, "%u:\n%s\n", stage.type, stage.code);
      OutputDebugStringA(msg);
    }
  }

  return handle;
}
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
void GpuDevice::destroyTexture(TextureHandle p_Texture)
{
  if (p_Texture.index < m_Textures.m_PoolSize)
  {
    m_ResourceDeletionQueue.push(
        {ResourceDeletionType::kTexture, p_Texture.index, m_CurrentFrameIndex});
  }
  else
  {
    OutputDebugStringA("Graphics error: trying to free invalid Texture\n");
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyPipeline(PipelineHandle p_Pipeline)
{
  if (p_Pipeline.index < m_Pipelines.m_PoolSize)
  {
    m_ResourceDeletionQueue.push(
        {ResourceDeletionType::kPipeline, p_Pipeline.index, m_CurrentFrameIndex});
    // Shader state creation is handled internally when creating a pipeline, thus add this to track
    // correctly.
    Pipeline* pipeline = (Pipeline*)m_Pipelines.accessResource(p_Pipeline.index);
    destroyShaderState(pipeline->shaderState);
  }
  else
  {
    OutputDebugStringA("Graphics error: trying to free invalid Pipeline\n");
  }
}
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
void GpuDevice::destroyDescriptorSetLayout(DescriptorSetLayoutHandle p_Layout)
{
  if (p_Layout.index < m_DescriptorSetLayouts.m_PoolSize)
  {
    m_ResourceDeletionQueue.push(
        {ResourceDeletionType::kDescriptorSetLayout, p_Layout.index, m_CurrentFrameIndex});
  }
  else
  {
    OutputDebugStringA("Graphics error: trying to free invalid DescriptorSetLayout\n");
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyDescriptorSet(DescriptorSetHandle p_Set)
{
  if (p_Set.index < m_DescriptorSets.m_PoolSize)
  {
    m_ResourceDeletionQueue.push(
        {ResourceDeletionType::kDescriptorSet, p_Set.index, m_CurrentFrameIndex});
  }
  else
  {
    OutputDebugStringA("Graphics error: trying to free invalid DescriptorSet\n");
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyRenderPass(RenderPassHandle p_RenderPass)
{
  if (p_RenderPass.index < m_RenderPasses.m_PoolSize)
  {
    m_ResourceDeletionQueue.push(
        {ResourceDeletionType::kRenderPass, p_RenderPass.index, m_CurrentFrameIndex});
  }
  else
  {
    OutputDebugStringA("Graphics error: trying to free invalid RenderPass\n");
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyShaderState(ShaderStateHandle p_Shader)
{
  if (p_Shader.index < m_Shaders.m_PoolSize)
  {
    m_ResourceDeletionQueue.push(
        {ResourceDeletionType::kShaderState, p_Shader.index, m_CurrentFrameIndex});
  }
  else
  {
    OutputDebugStringA("Graphics error: trying to free invalid Shader\n");
  }
}
void GpuDevice::releaseResource(ResourceUpdate& p_ResourceDeletion)
{

  switch (p_ResourceDeletion.type)
  {

  case ResourceDeletionType::kBuffer: {
    Buffer* buffer = (Buffer*)m_Buffers.accessResource(p_ResourceDeletion.handle);
    if (buffer && buffer->parentBuffer.index == kInvalidBuffer.index)
    {
      vmaDestroyBuffer(m_VmaAllocator, buffer->vkBuffer, buffer->vmaAllocation);
    }
    m_Buffers.releaseResource(p_ResourceDeletion.handle);
    break;
  }

  case ResourceDeletionType::kPipeline: {
    Pipeline* pipeline = (Pipeline*)m_Pipelines.accessResource(p_ResourceDeletion.handle);
    if (pipeline)
    {
      vkDestroyPipeline(m_VulkanDevice, pipeline->vkPipeline, m_VulkanAllocCallbacks);

      vkDestroyPipelineLayout(m_VulkanDevice, pipeline->vkPipelineLayout, m_VulkanAllocCallbacks);
    }
    m_Pipelines.releaseResource(p_ResourceDeletion.handle);
    break;
  }

  case ResourceDeletionType::kRenderPass: {
    RenderPass* renderPass = (RenderPass*)m_RenderPasses.accessResource(p_ResourceDeletion.handle);
    if (renderPass)
    {
      if (renderPass->numRenderTargets)
        vkDestroyFramebuffer(m_VulkanDevice, renderPass->vkFrameBuffer, m_VulkanAllocCallbacks);
    }
    m_RenderPasses.releaseResource(p_ResourceDeletion.handle);
    break;
  }

  case ResourceDeletionType::kDescriptorSet: {
    DesciptorSet* descriptorSet =
        (DesciptorSet*)m_DescriptorSets.accessResource(p_ResourceDeletion.handle);
    if (descriptorSet)
    {
      // Contains the allocation for all the resources, binding and samplers arrays.
      FRAMEWORK_FREE(descriptorSet->resources, m_Allocator);
    }
    m_DescriptorSets.releaseResource(p_ResourceDeletion.handle);
    break;
  }

  case ResourceDeletionType::kDescriptorSetLayout: {
    DesciptorSetLayout* descriptorSetLayout =
        (DesciptorSetLayout*)m_DescriptorSetLayouts.accessResource(p_ResourceDeletion.handle);
    if (descriptorSetLayout)
    {
      vkDestroyDescriptorSetLayout(
          m_VulkanDevice, descriptorSetLayout->vkDescriptorSetLayout, m_VulkanAllocCallbacks);

      // This contains also vkBinding allocation.
      FRAMEWORK_FREE(descriptorSetLayout->bindings, m_Allocator);
    }
    m_DescriptorSetLayouts.releaseResource(p_ResourceDeletion.handle);
    break;
  }

  case ResourceDeletionType::kSampler: {
    Sampler* sampler = (Sampler*)m_Samplers.accessResource(p_ResourceDeletion.handle);
    if (sampler)
    {
      vkDestroySampler(m_VulkanDevice, sampler->vkSampler, m_VulkanAllocCallbacks);
    }
    m_Samplers.releaseResource(p_ResourceDeletion.handle);
    break;
  }

  case ResourceDeletionType::kShaderState: {
    ShaderState* shaderState = (ShaderState*)m_Shaders.accessResource(p_ResourceDeletion.handle);
    if (shaderState)
    {
      for (size_t i = 0; i < shaderState->activeShaders; i++)
      {
        vkDestroyShaderModule(
            m_VulkanDevice, shaderState->shaderStageInfo[i].module, m_VulkanAllocCallbacks);
      }
    }
    m_Shaders.releaseResource(p_ResourceDeletion.handle);
    break;
  }

  case ResourceDeletionType::kTexture: {
    Texture* texture = (Texture*)m_Textures.accessResource(p_ResourceDeletion.handle);
    if (texture)
    {
      vkDestroyImageView(m_VulkanDevice, texture->vkImageView, m_VulkanAllocCallbacks);
      vmaDestroyImage(m_VmaAllocator, texture->vkImage, texture->vmaAllocation);
    }
    m_Textures.releaseResource(p_ResourceDeletion.handle);
    break;
  }
  }
}
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

  // Cache swapchain images
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
void GpuDevice::resizeSwapchain()
{
  // TODO:
  assert(false && "Not implemented");
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
CommandBuffer* GpuDevice::getCommandBuffer(bool p_Begin)
{
  CommandBuffer* cmd = g_CmdBufferRing.getCmdBuffer(m_CurrentFrameIndex, p_Begin);
  return cmd;
}
//---------------------------------------------------------------------------//
void GpuDevice::queueCommandBuffer(CommandBuffer* p_CommandBuffer)
{
  m_QueuedCommandBuffers[m_NumQueuedCommandBuffers++] = p_CommandBuffer;
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
VkShaderModuleCreateInfo GpuDevice::compileShader(
    const char* p_Code, uint32_t p_CodeSize, VkShaderStageFlagBits p_Stage, const char* p_Name)
{
  VkShaderModuleCreateInfo shaderCi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};

  // Compile from glsl to SpirV.
  // TODO: detect if input is HLSL.
  const char* tempFilename = "temp.shader";

  // Write current shader to file.
  FILE* tempShaderFile = fopen(tempFilename, "w");
  fwrite(p_Code, p_CodeSize, 1, tempShaderFile);
  fclose(tempShaderFile);

  size_t currentMarker = m_TemporaryAllocator->getMarker();
  Framework::StringBuffer tempStringBuffer;
  tempStringBuffer.init(FRAMEWORK_KILO(1), m_TemporaryAllocator);

  // Add uppercase define as STAGE_NAME
  char* stageDefine = tempStringBuffer.appendUseFormatted("%s_%s", toStageDefines(p_Stage), p_Name);
  size_t stageDefineLength = strlen(stageDefine);
  for (uint32_t i = 0; i < stageDefineLength; ++i)
  {
    stageDefine[i] = toupper(stageDefine[i]);
  }
  // Compile to SPV
  char* glslCompilerPath =
      tempStringBuffer.appendUseFormatted("%sglslangValidator.exe", m_VulkanBinariesPath);
  char* finalSpirvFilename = tempStringBuffer.appendUse("shader_final.spv");
  // TODO: add optional debug information in shaders (option -g).
  char* arguments = tempStringBuffer.appendUseFormatted(
      "glslangValidator.exe %s -V --target-env vulkan1.2 -o %s -S %s --D %s --D %s",
      tempFilename,
      finalSpirvFilename,
      toCompilerExtension(p_Stage),
      stageDefine,
      toStageDefines(p_Stage));

  Framework::processExecute(".", glslCompilerPath, arguments, "");

  bool optimize_shaders = false;
  if (optimize_shaders)
  {
    // TODO: add optional optimization stage
    //"spirv-opt -O input -o output
    char* spirvOptimizerPath =
        tempStringBuffer.appendUseFormatted("%sspirv-opt.exe", m_VulkanBinariesPath);
    char* optimizedSpirvFilename = tempStringBuffer.appendUseFormatted("shader_opt.spv");
    char* spirvOptArguments = tempStringBuffer.appendUseFormatted(
        "spirv-opt.exe -O --preserve-bindings %s -o %s",
        finalSpirvFilename,
        optimizedSpirvFilename);

    Framework::processExecute(".", spirvOptimizerPath, spirvOptArguments, "");

    // Read back SPV file.
    shaderCi.pCode = reinterpret_cast<const uint32_t*>(Framework::fileReadBinary(
        optimizedSpirvFilename, m_TemporaryAllocator, &shaderCi.codeSize));

    Framework::fileDelete(optimizedSpirvFilename);
  }
  else
  {
    // Read back SPV file.
    shaderCi.pCode = reinterpret_cast<const uint32_t*>(
        Framework::fileReadBinary(finalSpirvFilename, m_TemporaryAllocator, &shaderCi.codeSize));
  }

  // TODO: Handling compilation error
  if (shaderCi.pCode == nullptr)
  {
    assert(false && "Failed to compile shader!");
  }

  // Temporary files cleanup
  Framework::fileDelete(tempFilename);
  Framework::fileDelete(finalSpirvFilename);

  return shaderCi;
}
//---------------------------------------------------------------------------//
void GpuDevice::frameCountersAdvance()
{
  // TODO:
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void GpuDevice::querySampler(SamplerHandle p_Sampler, SamplerDescription& p_OutDescription)
{
  if (p_Sampler.index != kInvalidIndex)
  {
    const Sampler* samplerData = (Sampler*)m_Samplers.accessResource(p_Sampler.index);

    p_OutDescription.addressModeU = samplerData->addressModeU;
    p_OutDescription.addressModeV = samplerData->addressModeV;
    p_OutDescription.addressModeW = samplerData->addressModeW;

    p_OutDescription.minFilter = samplerData->minFilter;
    p_OutDescription.magFilter = samplerData->magFilter;
    p_OutDescription.mipFilter = samplerData->mipFilter;

    p_OutDescription.name = samplerData->name;
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::queryTexture(TextureHandle p_Texture, TextureDescription& p_OutDescription)
{
  if (p_Texture.index != kInvalidIndex)
  {
    const Texture* textureData = (Texture*)m_Textures.accessResource(p_Texture.index);

    p_OutDescription.width = textureData->width;
    p_OutDescription.height = textureData->height;
    p_OutDescription.depth = textureData->depth;
    p_OutDescription.format = textureData->vkFormat;
    p_OutDescription.mipmaps = textureData->mipmaps;
    p_OutDescription.type = textureData->type;
    p_OutDescription.renderTarget =
        (textureData->flags & TextureFlags::kRenderTargetMask) == TextureFlags::kRenderTargetMask;
    p_OutDescription.computeAccess =
        (textureData->flags & TextureFlags::kComputeMask) == TextureFlags::kComputeMask;
    p_OutDescription.nativeHandle = (void*)&textureData->vkImage;
    p_OutDescription.name = textureData->name;
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::queryBuffer(BufferHandle p_Buffer, BufferDescription& p_OutDescription)
{
  if (p_Buffer.index != kInvalidIndex)
  {
    const Buffer* bufferData = (Buffer*)m_Buffers.accessResource(p_Buffer.index);

    p_OutDescription.name = bufferData->name;
    p_OutDescription.size = bufferData->size;
    p_OutDescription.typeFlags = bufferData->typeFlags;
    p_OutDescription.usage = bufferData->usage;
    p_OutDescription.parentHandle = bufferData->parentBuffer;
    p_OutDescription.nativeHandle = (void*)&bufferData->vkBuffer;
  }
}
//---------------------------------------------------------------------------//
} // namespace Graphics
