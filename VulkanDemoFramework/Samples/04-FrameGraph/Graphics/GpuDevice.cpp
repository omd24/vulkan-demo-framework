#include "GpuDevice.hpp"
#include "Graphics/CommandBuffer.hpp"
#include "Graphics/SpirvParser.hpp"

#include "Externals/SDL2-2.0.18/include/SDL.h"        // SDL_Window
#include "Externals/SDL2-2.0.18/include/SDL_vulkan.h" // SDL_Vulkan_CreateSurface

#include "Foundation/HashMap.hpp"
#include "Foundation/String.hpp"
#include "Foundation/File.hpp"
#include "Foundation/Process.hpp"

#include <assert.h>

template <class T> constexpr const T& _min(const T& a, const T& b) { return (a < b) ? a : b; }

template <class T> constexpr const T& _max(const T& a, const T& b) { return (a < b) ? b : a; }

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
struct Framework::FlatHashMap<uint64_t, VkRenderPass> g_RenderPassCache;
struct CommandBufferManager g_CmdBufferRing;
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
DeviceCreation& DeviceCreation::setNumThreads(uint32_t p_NumThreads)
{
  numThreads = p_NumThreads;
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
  char msg[2048]{};
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

static SDL_Window* g_SdlWindow;

static const uint32_t kMaxBindlessResources = 1024u;
static const uint32_t kBindlessTextureBinding = 10u;

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
static void _vulkanCreateFramebuffer(GpuDevice& p_GpuDevice, Framebuffer* p_Framebuffer)
{
  RenderPass* renderPass =
      (RenderPass*)p_GpuDevice.m_RenderPasses.accessResource(p_Framebuffer->renderPass.index);

  // Create framebuffer
  VkFramebufferCreateInfo framebufferCi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebufferCi.renderPass = renderPass->vkRenderPass;
  framebufferCi.width = p_Framebuffer->width;
  framebufferCi.height = p_Framebuffer->height;
  framebufferCi.layers = 1;

  VkImageView framebufferAttachments[kMaxImageOutputs + 1]{};
  uint32_t activeAttachments = 0;
  for (; activeAttachments < p_Framebuffer->numColorAttachments; ++activeAttachments)
  {
    TextureHandle handle = p_Framebuffer->colorAttachments[activeAttachments];
    Texture* texture = (Texture*)p_GpuDevice.m_Textures.accessResource(handle.index);
    framebufferAttachments[activeAttachments] = texture->vkImageView;
  }

  if (p_Framebuffer->depthStencilAttachment.index != kInvalidIndex)
  {
    TextureHandle handle = p_Framebuffer->depthStencilAttachment;
    Texture* depthMap = (Texture*)p_GpuDevice.m_Textures.accessResource(handle.index);
    framebufferAttachments[activeAttachments++] = depthMap->vkImageView;
  }
  framebufferCi.pAttachments = framebufferAttachments;
  framebufferCi.attachmentCount = activeAttachments;

  vkCreateFramebuffer(
      p_GpuDevice.m_VulkanDevice, &framebufferCi, nullptr, &p_Framebuffer->vkFramebuffer);
  p_GpuDevice.setResourceName(
      VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)p_Framebuffer->vkFramebuffer, renderPass->name);
}
//---------------------------------------------------------------------------//
static RenderPassOutput
_fillRenderPassOutput(GpuDevice& p_GpuDevice, const RenderPassCreation& p_Creation)
{
  RenderPassOutput output;
  output.reset();

  for (uint32_t i = 0; i < p_Creation.numRenderTargets; ++i)
  {
    output.color(
        p_Creation.colorFormats[i], p_Creation.colorFinalLayouts[i], p_Creation.colorOperations[i]);
  }
  if (p_Creation.depthStencilFormat != VK_FORMAT_UNDEFINED)
  {
    output.depth(p_Creation.depthStencilFormat, p_Creation.depthStencilFinalLayout);
  }

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

  VkAttachmentLoadOp depthOp, stencilOp;
  VkImageLayout depthInitial;

  switch (p_Output.depthOperation)
  {
  case RenderPassOperation::kLoad:
    depthOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthInitial = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    break;
  case RenderPassOperation::kClear:
    depthOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthInitial = VK_IMAGE_LAYOUT_UNDEFINED;
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
    VkAttachmentLoadOp colorOp;
    VkImageLayout colorInitial;
    switch (p_Output.colorOperations[c])
    {
    case RenderPassOperation::kLoad:
      colorOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      colorInitial = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      break;
    case RenderPassOperation::kClear:
      colorOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      colorInitial = VK_IMAGE_LAYOUT_UNDEFINED;
      break;
    default:
      colorOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      colorInitial = VK_IMAGE_LAYOUT_UNDEFINED;
      break;
    }

    VkAttachmentDescription& colorAttachment = colorAttachments[c];
    colorAttachment.format = p_Output.colorFormats[c];
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = colorOp;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = stencilOp;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = colorInitial;
    colorAttachment.finalLayout = p_Output.colorFinalLayouts[c];

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
    depthAttachment.finalLayout = p_Output.depthStencilFinalLayout;

    depthAttachmentRef.attachment = c;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }

  // Create subpass.
  // TODO: for now is just a simple subpass, evolve API.
  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

  // Calculate active attachments for the subpass
  VkAttachmentDescription attachments[kMaxImageOutputs + 1]{};
  for (uint32_t activeAttachments = 0; activeAttachments < p_Output.numColorFormats;
       ++activeAttachments)
  {
    attachments[activeAttachments] = colorAttachments[activeAttachments];
  }
  subpass.colorAttachmentCount = p_Output.numColorFormats;
  subpass.pColorAttachments = colorAttachmentsRef;

  subpass.pDepthStencilAttachment = nullptr;

  uint32_t depthStencilCount = 0;
  if (p_Output.depthStencilFormat != VK_FORMAT_UNDEFINED)
  {
    attachments[subpass.colorAttachmentCount] = depthAttachment;

    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    depthStencilCount = 1;
  }

  VkRenderPassCreateInfo renderPassInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};

  renderPassInfo.attachmentCount = (p_Output.numColorFormats) + depthStencilCount;
  renderPassInfo.pAttachments = attachments;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;

  // Create external subpass dependencies
  // VkSubpassDependency external_dependencies[ 16 ];
  // uint32_t num_external_dependencies = 0;

  VkRenderPass vkRenderPass;
  CHECKRES(vkCreateRenderPass(p_GpuDevice.m_VulkanDevice, &renderPassInfo, nullptr, &vkRenderPass));

  p_GpuDevice.setResourceName(VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)vkRenderPass, p_Name);

  return vkRenderPass;
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
    imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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

  // Deferred bindless update:
  if (p_GpuDevice.m_BindlessSupported)
  {
    ResourceUpdate resourceUpdate = {};
    resourceUpdate.type = ResourceUpdateType::kTexture;
    resourceUpdate.handle = p_Texture->handle.index;
    resourceUpdate.currentFrame = p_GpuDevice.m_CurrentFrameIndex;
    p_GpuDevice.m_TextureToUpdateBindless.push(resourceUpdate);
  }
}
//---------------------------------------------------------------------------//
static void uploadTextureData(Texture* p_Texture, void* p_UploadData, GpuDevice& p_Gpu)
{
  // Create stating buffer
  VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

  uint32_t imageSize = p_Texture->width * p_Texture->height * 4;
  bufferInfo.size = imageSize;

  VmaAllocationCreateInfo memoryInfo{};
  memoryInfo.flags = VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
  memoryInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

  VmaAllocationInfo allocationInfo{};
  VkBuffer stagingBuffer;
  VmaAllocation stagingAllocation;
  (vmaCreateBuffer(
      p_Gpu.m_VmaAllocator,
      &bufferInfo,
      &memoryInfo,
      &stagingBuffer,
      &stagingAllocation,
      &allocationInfo));

  // Copy buffer_data
  void* destinationData;
  vmaMapMemory(p_Gpu.m_VmaAllocator, stagingAllocation, &destinationData);
  memcpy(destinationData, p_UploadData, static_cast<size_t>(imageSize));
  vmaUnmapMemory(p_Gpu.m_VmaAllocator, stagingAllocation);

  // Execute command buffer
  VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  // TODO: threading
  CommandBuffer* commandBuffer = p_Gpu.getCommandBuffer(false, 0);
  vkBeginCommandBuffer(commandBuffer->m_VulkanCmdBuffer, &beginInfo);

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;

  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;

  region.imageOffset = {0, 0, 0};
  region.imageExtent = {p_Texture->width, p_Texture->height, p_Texture->depth};

  // Copy from the staging buffer to the image
  utilAddImageBarrier(
      commandBuffer->m_VulkanCmdBuffer,
      p_Texture->vkImage,
      RESOURCE_STATE_UNDEFINED,
      RESOURCE_STATE_COPY_DEST,
      0,
      1,
      false);

  vkCmdCopyBufferToImage(
      commandBuffer->m_VulkanCmdBuffer,
      stagingBuffer,
      p_Texture->vkImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1,
      &region);
  // Prepare first mip to create lower mipmaps
  if (p_Texture->mipmaps > 1)
  {
    utilAddImageBarrier(
        commandBuffer->m_VulkanCmdBuffer,
        p_Texture->vkImage,
        RESOURCE_STATE_COPY_DEST,
        RESOURCE_STATE_COPY_SOURCE,
        0,
        1,
        false);
  }

  int w = p_Texture->width;
  int h = p_Texture->height;

  for (int mipIndex = 1; mipIndex < p_Texture->mipmaps; ++mipIndex)
  {
    utilAddImageBarrier(
        commandBuffer->m_VulkanCmdBuffer,
        p_Texture->vkImage,
        RESOURCE_STATE_UNDEFINED,
        RESOURCE_STATE_COPY_DEST,
        mipIndex,
        1,
        false);

    VkImageBlit blitRegion{};
    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.mipLevel = mipIndex - 1;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;

    blitRegion.srcOffsets[0] = {0, 0, 0};
    blitRegion.srcOffsets[1] = {w, h, 1};

    w /= 2;
    h /= 2;

    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.mipLevel = mipIndex;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;

    blitRegion.dstOffsets[0] = {0, 0, 0};
    blitRegion.dstOffsets[1] = {w, h, 1};

    vkCmdBlitImage(
        commandBuffer->m_VulkanCmdBuffer,
        p_Texture->vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        p_Texture->vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blitRegion,
        VK_FILTER_LINEAR);

    // Prepare current mip for next level
    utilAddImageBarrier(
        commandBuffer->m_VulkanCmdBuffer,
        p_Texture->vkImage,
        RESOURCE_STATE_COPY_DEST,
        RESOURCE_STATE_COPY_SOURCE,
        mipIndex,
        1,
        false);
  }

  // Transition
  utilAddImageBarrier(
      commandBuffer->m_VulkanCmdBuffer,
      p_Texture->vkImage,
      (p_Texture->mipmaps > 1) ? RESOURCE_STATE_COPY_SOURCE : RESOURCE_STATE_COPY_DEST,
      RESOURCE_STATE_SHADER_RESOURCE,
      0,
      p_Texture->mipmaps,
      false);

  vkEndCommandBuffer(commandBuffer->m_VulkanCmdBuffer);

  // Submit command buffer
  VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer->m_VulkanCmdBuffer;

  vkQueueSubmit(p_Gpu.m_VulkanMainQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(p_Gpu.m_VulkanMainQueue);

  vmaDestroyBuffer(p_Gpu.m_VmaAllocator, stagingBuffer, stagingAllocation);

  // TODO: free command buffer
  vkResetCommandBuffer(
      commandBuffer->m_VulkanCmdBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

  p_Texture->vkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
//---------------------------------------------------------------------------//
// helper method
bool isEndOfLine(char c)
{
  bool result = ((c == '\n') || (c == '\r'));
  return (result);
}

void dumpShaderCode(
    Framework::StringBuffer& tempStringBuffer,
    const char* code,
    VkShaderStageFlagBits stage,
    const char* name)
{
  printf(
      "Error in creation of shader %s, stage %s. Writing shader:\n", name, toStageDefines(stage));

  const char* currentCode = code;
  uint32_t lineIndex = 1;
  while (currentCode)
  {

    const char* endOfLine = currentCode;
    if (!endOfLine || *endOfLine == 0)
    {
      break;
    }
    while (!isEndOfLine(*endOfLine))
    {
      ++endOfLine;
    }
    if (*endOfLine == '\r')
    {
      ++endOfLine;
    }
    if (*endOfLine == '\n')
    {
      ++endOfLine;
    }

    tempStringBuffer.clear();
    char* line = tempStringBuffer.appendUseSubstring(currentCode, 0, (endOfLine - currentCode));
    printf("%u: %s", lineIndex++, line);

    currentCode = endOfLine;
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::fillWriteDescriptorSets(
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
    const uint16_t* p_Bindings)
{

  uint32_t usedResources = 0;
  const bool skipBindlessBindings = p_Gpu.m_BindlessSupported && !p_DescriptorSetLayout->bindless;

  for (uint32_t r = 0; r < p_NumResources; r++)
  {

    // Binding array contains the binding point as written in the shader.
    uint32_t layoutBindingIndex = p_Bindings[r];
    // index_to_binding array contains the mapping between a binding point and its
    // correct binding informations.
    uint32_t bindingDataIndex = p_DescriptorSetLayout->indexToBinding[layoutBindingIndex];
    const DescriptorBinding& binding = p_DescriptorSetLayout->bindings[bindingDataIndex];

    // [TAG: BINDLESS]
    // Skip bindless descriptors as they are bound in the global bindless arrays.
    if (skipBindlessBindings && (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                                 binding.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE))
    {
      continue;
    }

    uint32_t i = usedResources;
    ++usedResources;

    p_DescriptorWrite[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    p_DescriptorWrite[i].dstSet = p_VkDescriptorSet;
    // Use binding array to get final binding point.
    const uint32_t bindingPoint = binding.index;
    p_DescriptorWrite[i].dstBinding = bindingPoint;
    p_DescriptorWrite[i].dstArrayElement = 0;
    p_DescriptorWrite[i].descriptorCount = 1;

    switch (binding.type)
    {
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
      p_DescriptorWrite[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

      TextureHandle textureHandle = {p_Resources[r]};
      Texture* textureData = (Texture*)p_Gpu.m_Textures.accessResource(textureHandle.index);

      // Find proper sampler.
      // TODO: improve. Remove the single texture interface ?
      p_ImageInfo[i].sampler = p_VkDefaultSampler;
      if (textureData->sampler)
      {
        p_ImageInfo[i].sampler = textureData->sampler->vkSampler;
      }
      // TODO: else ?
      if (p_Samplers[r].index != kInvalidIndex)
      {
        SamplerHandle handle = {p_Samplers[r]};
        Sampler* sampler = (Sampler*)p_Gpu.m_Samplers.accessResource(handle.index);
        p_ImageInfo[i].sampler = sampler->vkSampler;
      }

      p_ImageInfo[i].imageLayout = TextureFormat::hasDepthOrStencil(textureData->vkFormat)
                                       ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                       : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      p_ImageInfo[i].imageView = textureData->vkImageView;

      p_DescriptorWrite[i].pImageInfo = &p_ImageInfo[i];

      break;
    }

    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
      p_DescriptorWrite[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

      TextureHandle textureHandle = {p_Resources[r]};
      Texture* textureData = (Texture*)p_Gpu.m_Textures.accessResource(textureHandle.index);

      p_ImageInfo[i].sampler = nullptr;
      p_ImageInfo[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      p_ImageInfo[i].imageView = textureData->vkImageView;

      p_DescriptorWrite[i].pImageInfo = &p_ImageInfo[i];

      break;
    }

    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
      BufferHandle bufferHandle = {p_Resources[r]};
      Buffer* buffer = (Buffer*)p_Gpu.m_Buffers.accessResource(bufferHandle.index);

      p_DescriptorWrite[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      p_DescriptorWrite[i].descriptorType = buffer->usage == ResourceUsageType::kDynamic
                                                ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                                : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

      // Bind parent buffer if present, used for dynamic resources.
      if (buffer->parentBuffer.index != kInvalidIndex)
      {
        Buffer* parentBuffer = (Buffer*)p_Gpu.m_Buffers.accessResource(buffer->parentBuffer.index);

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
      Buffer* buffer = (Buffer*)p_Gpu.m_Buffers.accessResource(bufferHandle.index);

      p_DescriptorWrite[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      // Bind parent buffer if present, used for dynamic resources.
      if (buffer->parentBuffer.index != kInvalidIndex)
      {
        Buffer* parentBuffer = (Buffer*)p_Gpu.m_Buffers.accessResource(buffer->parentBuffer.index);
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
      assert(false, "Resource type %d not supported in descriptor set creation!\n", binding.type);
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

  Framework::StackAllocator* tempAllocator = p_Creation.temporaryAllocator;
  size_t initialTempAllocatorMarker = tempAllocator->getMarker();

  // Choose extensions:
  {
    uint32_t numInstanceExtensions;
    vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, nullptr);
    VkExtensionProperties* extensions = (VkExtensionProperties*)FRAMEWORK_ALLOCA(
        sizeof(VkExtensionProperties) * numInstanceExtensions, tempAllocator);
    vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, extensions);
    for (size_t i = 0; i < numInstanceExtensions; i++)
    {

      if (!strcmp(extensions[i].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
      {
        m_DebugUtilsExtensionPresent = true;
        break;
      }
    }

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
        sizeof(VkPhysicalDevice) * numPhysicalDevice, tempAllocator);
    result = vkEnumeratePhysicalDevices(m_VulkanInstance, &numPhysicalDevice, gpus);
    CHECKRES(result);

    // TODO: improve - choose the first gpu.
    m_VulkanPhysicalDevice = gpus[0];
    tempAllocator->freeMarker(initialTempAllocatorMarker);

    vkGetPhysicalDeviceProperties(m_VulkanPhysicalDevice, &m_VulkanPhysicalDeviceProps);

    // print selected gpu:
    {
      char msg[256]{};
      sprintf(msg, "GPU Used: %s\n", m_VulkanPhysicalDeviceProps.deviceName);
      OutputDebugStringA(msg);
    }

    // Dynamic rendering extension
    {
      initialTempAllocatorMarker = tempAllocator->getMarker();

      uint32_t deviceExtensionCount = 0;
      vkEnumerateDeviceExtensionProperties(
          m_VulkanPhysicalDevice, nullptr, &deviceExtensionCount, nullptr);
      VkExtensionProperties* extensions = (VkExtensionProperties*)FRAMEWORK_ALLOCA(
          sizeof(VkExtensionProperties) * deviceExtensionCount, tempAllocator);
      vkEnumerateDeviceExtensionProperties(
          m_VulkanPhysicalDevice, nullptr, &deviceExtensionCount, extensions);
      for (size_t i = 0; i < deviceExtensionCount; i++)
      {

        if (!strcmp(extensions[i].extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
        {
          m_DynamicRenderingExtensionPresent = true;
          continue;
        }
      }

      tempAllocator->freeMarker(initialTempAllocatorMarker);
    }

    m_UboAlignment = m_VulkanPhysicalDeviceProps.limits.minUniformBufferOffsetAlignment;
    m_SboAlignment = m_VulkanPhysicalDeviceProps.limits.minStorageBufferOffsetAlignment;
  }

  // Check for bindless support
  VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
  {
    indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    VkPhysicalDeviceFeatures2 deviceFeatures = {};
    deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures.pNext = &indexingFeatures;
    vkGetPhysicalDeviceFeatures2(m_VulkanPhysicalDevice, &deviceFeatures);

    m_BindlessSupported =
        indexingFeatures.descriptorBindingPartiallyBound && indexingFeatures.runtimeDescriptorArray;
    assert(m_BindlessSupported && "Bindless not supported");
  }

  // Create logical device
  {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_VulkanPhysicalDevice, &queueFamilyCount, nullptr);

    VkQueueFamilyProperties* queueFamilies = (VkQueueFamilyProperties*)FRAMEWORK_ALLOCA(
        sizeof(VkQueueFamilyProperties) * queueFamilyCount, tempAllocator);
    vkGetPhysicalDeviceQueueFamilyProperties(
        m_VulkanPhysicalDevice, &queueFamilyCount, queueFamilies);

    uint32_t mainQueueIndex = UINT_MAX, transferQueueIndex = UINT_MAX, computeQueueIndex = UINT_MAX,
             presentQueueIndex = UINT_MAX;
    for (uint32_t fi = 0; fi < queueFamilyCount; ++fi)
    {
      VkQueueFamilyProperties queueFamily = queueFamilies[fi];

      if (queueFamily.queueCount == 0)
      {
        continue;
      }
#if defined(_DEBUG)
      printf(
          "Family %u, flags %u queue count %u\n",
          fi,
          queueFamily.queueFlags,
          queueFamily.queueCount);
#endif // DEBUG

      // Search for main queue that should be able to do all work (graphics, compute and transfer)
      if ((queueFamily.queueFlags &
           (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) ==
          (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))
      {
        mainQueueIndex = fi;
      }
      // Search for transfer queue
      if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) == 0 &&
          (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT))
      {
        transferQueueIndex = fi;
      }
    }

    // Cache family indices
    m_VulkanMainQueueFamily = mainQueueIndex;
    m_VulkanTransferQueueFamily = transferQueueIndex;

    Framework::Array<const char*> deviceExtensions;
    deviceExtensions.init(m_Allocator, 2);
    deviceExtensions.push(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (m_DynamicRenderingExtensionPresent)
    {
      deviceExtensions.push(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    }

    const float queuePriority[] = {1.0f};
    VkDeviceQueueCreateInfo queueInfo[2] = {};

    VkDeviceQueueCreateInfo& mainQueueCi = queueInfo[0];
    mainQueueCi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    mainQueueCi.queueFamilyIndex = mainQueueIndex;
    mainQueueCi.queueCount = 1;
    mainQueueCi.pQueuePriorities = queuePriority;

    if (m_VulkanTransferQueueFamily < queueFamilyCount)
    {
      VkDeviceQueueCreateInfo& transferQueueCi = queueInfo[1];
      transferQueueCi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      transferQueueCi.queueFamilyIndex = transferQueueIndex;
      transferQueueCi.queueCount = 1;
      transferQueueCi.pQueuePriorities = queuePriority;
    }

    // Enable all features: just pass the physical features 2 struct.
    VkPhysicalDeviceFeatures2 physicalFeatures2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    if (m_DynamicRenderingExtensionPresent)
    {
      physicalFeatures2.pNext = &dynamicRenderingFeatures;
    }
    vkGetPhysicalDeviceFeatures2(m_VulkanPhysicalDevice, &physicalFeatures2);

    VkDeviceCreateInfo deviceCi = {};
    deviceCi.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCi.queueCreateInfoCount = m_VulkanTransferQueueFamily < queueFamilyCount ? 2 : 1;
    deviceCi.pQueueCreateInfos = queueInfo;
    deviceCi.enabledExtensionCount = deviceExtensions.m_Size;
    deviceCi.ppEnabledExtensionNames = deviceExtensions.m_Data;
    deviceCi.pNext = &physicalFeatures2;

    // We also add the bindless needed feature on the device creation.
    if (m_BindlessSupported)
    {
      indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
      indexingFeatures.runtimeDescriptorArray = VK_TRUE;

      // TODO: more generic chaining
      if (m_DynamicRenderingExtensionPresent)
      {
        dynamicRenderingFeatures.pNext = &indexingFeatures;
      }
      else
      {
        physicalFeatures2.pNext = &indexingFeatures;
      }
    }

    VkResult result =
        vkCreateDevice(m_VulkanPhysicalDevice, &deviceCi, m_VulkanAllocCallbacks, &m_VulkanDevice);
    CHECKRES(result);

    deviceExtensions.shutdown();

    if (m_DynamicRenderingExtensionPresent)
    {
      m_CmdBeginRendering =
          (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(m_VulkanDevice, "vkCmdBeginRenderingKHR");
      m_CmdEndRendering =
          (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(m_VulkanDevice, "vkCmdEndRenderingKHR");
    }

    vkGetDeviceQueue(m_VulkanDevice, mainQueueIndex, 0, &m_VulkanMainQueue);
    if (m_VulkanTransferQueueFamily < queueFamilyCount)
      vkGetDeviceQueue(m_VulkanDevice, transferQueueIndex, 0, &m_VulkanTransferQueue);
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
        sizeof(VkSurfaceFormatKHR) * supportedCount, tempAllocator);
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
          m_SwapchainOutput.color(
              surfaceImageFormats[j], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, RenderPassOperation::kClear);
          formatFound = true;
          break;
        }
      }

      if (formatFound)
        break;
    }

    m_SwapchainOutput.depth(VK_FORMAT_D32_SFLOAT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    m_SwapchainOutput.setDepthStencilOperations(
        RenderPassOperation::kClear, RenderPassOperation::kClear);

    // Default to the first format supported.
    if (!formatFound)
    {
      m_VulkanSurfaceFormat = supportedFormats[0];
      assert(false);
    }

    // Final use of temp allocator, free all temporary memory created here.
    tempAllocator->freeMarker(initialTempAllocatorMarker);

    setPresentMode(m_PresentMode);
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

  // Create descriptor pool for bindless textures
  if (m_BindlessSupported)
  {
    VkDescriptorPoolSize poolSizesBindless[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxBindlessResources},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kMaxBindlessResources},
    };
    {
      VkDescriptorPoolCreateInfo ci = {};
      ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      // Update after bind is needed here, for each binding and in the descriptor set layout
      // creation.
      ci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
      ci.maxSets = kMaxBindlessResources * arrayCount32(poolSizesBindless);
      ci.poolSizeCount = arrayCount32(poolSizesBindless);
      ci.pPoolSizes = poolSizesBindless;
      VkResult result = vkCreateDescriptorPool(
          m_VulkanDevice, &ci, m_VulkanAllocCallbacks, &m_VulkanBindlessDescriptorPool);
      CHECKRES(result);
    }
  }

  // Init pools
  m_Buffers.init(m_Allocator, kBuffersPoolSize, sizeof(Buffer));
  m_Textures.init(m_Allocator, kTexturesPoolSize, sizeof(Texture));
  m_RenderPasses.init(m_Allocator, kRenderPassesPoolSize, sizeof(RenderPass));
  m_Framebuffers.init(m_Allocator, 256, sizeof(RenderPass));
  m_DescriptorSetLayouts.init(
      m_Allocator, kDescriptorSetLayoutsPoolSize, sizeof(DescriptorSetLayout));
  m_Pipelines.init(m_Allocator, kPipelinesPoolSize, sizeof(Pipeline));
  m_Shaders.init(m_Allocator, kShadersPoolSize, sizeof(ShaderState));
  m_DescriptorSets.init(m_Allocator, kDescriptorSetsPoolSize, sizeof(DescriptorSet));
  m_Samplers.init(m_Allocator, kSamplersPoolSize, sizeof(Sampler));

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

  // Allocate command buffers queue
  {
    uint8_t* memory = FRAMEWORK_ALLOCAM(sizeof(CommandBuffer*) * 128, m_Allocator);
    m_QueuedCommandBuffers = (CommandBuffer**)(memory);
  }

  // Init the command buffer ring:
  g_CmdBufferRing.init(this, p_Creation.numThreads);

  // Init frame counters
  m_VulkanImageIndex = 0;
  m_CurrentFrameIndex = 1;
  m_PreviousFrameIndex = 0;
  m_AbsoluteFrameIndex = 0;

  // Init resource deletion queue and descriptor set updates
  m_ResourceDeletionQueue.init(m_Allocator, 16);
  m_DescriptorSetUpdates.init(m_Allocator, 16);
  m_TextureToUpdateBindless.init(m_Allocator, 16);

  // Init render pass cache
  g_RenderPassCache.init(m_Allocator, 16);

  // This should be after initializing the command lists:
  createSwapchain();

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
      0,
      0,
      nullptr,
      "Fullscreen_vb"};
  m_FullscreenVertexBuffer = createBuffer(fullscreenVbCreation);

  // Init Dummy resources
  TextureCreation dummyTextureCreation = {
      nullptr, 1, 1, 1, 1, 0, VK_FORMAT_R8_UINT, TextureType::kTexture2D};
  m_DummyTexture = createTexture(dummyTextureCreation);

  BufferCreation dummyConstantBufferCreation = {
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      ResourceUsageType::kImmutable,
      16,
      0,
      0,
      nullptr,
      "Dummy Constant Buffer"};
  m_DummyConstantBuffer = createBuffer(dummyConstantBufferCreation);

  // Get binaries path
  char* vulkanEnv = m_StringBuffer.reserve(512);
  ExpandEnvironmentStringsA("%VULKAN_SDK%", vulkanEnv, 512);
  char* compilerPath = m_StringBuffer.appendUseFormatted("%s\\Bin\\", vulkanEnv);

  strcpy(m_VulkanBinariesPath, compilerPath);
  m_StringBuffer.clear();

  // Bindless resources creation
  if (m_BindlessSupported)
  {
    DescriptorSetLayoutCreation bindlessLayoutCreation;
    bindlessLayoutCreation.reset()
        .addBinding(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            kBindlessTextureBinding,
            kMaxBindlessResources,
            "BindlessTextures")
        .addBinding(
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            kBindlessTextureBinding + 1,
            kMaxBindlessResources,
            "BindlessImages")
        .setSetIndex(0)
        .setName("BindlessLayout");
    bindlessLayoutCreation.bindless = true;

    m_BindlessDescriptorSetLayout = createDescriptorSetLayout(bindlessLayoutCreation);

    DescriptorSetCreation bindlessSetCreation;
    bindlessSetCreation.reset().setLayout(m_BindlessDescriptorSetLayout);
    m_BindlessDescriptorSet = createDescriptorSet(bindlessSetCreation);

    DescriptorSet* bindlessSet =
        (DescriptorSet*)m_DescriptorSets.accessResource(m_BindlessDescriptorSet.index);
    m_VulkanBindlessDescriptorSetCached = bindlessSet->vkDescriptorSet;
  }

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

  destroyDescriptorSetLayout(m_BindlessDescriptorSetLayout);
  destroyDescriptorSet(m_BindlessDescriptorSet);
  destroyBuffer(m_FullscreenVertexBuffer);
  destroyBuffer(m_DynamicBuffer);
  destroyRenderPass(m_SwapchainRenderPass);
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
  // Swapchain vkRenderPass is also present.
  if (!m_DynamicRenderingExtensionPresent)
  {
    Framework::FlatHashMapIterator it = g_RenderPassCache.iteratorBegin();
    while (it.isValid())
    {
      VkRenderPass renderPass = g_RenderPassCache.get(it);
      vkDestroyRenderPass(m_VulkanDevice, renderPass, m_VulkanAllocCallbacks);
      g_RenderPassCache.iteratorAdvance(it);
    }
    g_RenderPassCache.shutdown();
  }

  // Destroy swapchain
  destroySwapchain();
  vkDestroySurfaceKHR(m_VulkanInstance, m_VulkanWindowSurface, m_VulkanAllocCallbacks);

  vmaDestroyAllocator(m_VmaAllocator);

  m_TextureToUpdateBindless.shutdown();
  m_ResourceDeletionQueue.shutdown();
  m_DescriptorSetUpdates.shutdown();

  m_Buffers.shutdown();
  m_Textures.shutdown();
  m_RenderPasses.shutdown();
  m_DescriptorSetLayouts.shutdown();
  m_Pipelines.shutdown();
  m_Shaders.shutdown();
  m_DescriptorSets.shutdown();
  m_Samplers.shutdown();
  m_Framebuffers.shutdown();

  auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      m_VulkanInstance, "vkDestroyDebugUtilsMessengerEXT");
  vkDestroyDebugUtilsMessengerEXT(
      m_VulkanInstance, m_VulkanDebugUtilsMessenger, m_VulkanAllocCallbacks);

  if (m_BindlessSupported)
  {
    vkDestroyDescriptorPool(m_VulkanDevice, m_VulkanBindlessDescriptorPool, m_VulkanAllocCallbacks);
  }

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
      DescriptorSet* dummyDeleteDescriptorSet =
          (DescriptorSet*)m_DescriptorSets.accessResource(dummyDeleteDescriptorSetHandle.index);

      DescriptorSet* descriptorSet =
          (DescriptorSet*)m_DescriptorSets.accessResource(update.descriptorSet.index);
      const DescriptorSetLayout* descriptorSetLayout = descriptorSet->layout;

      dummyDeleteDescriptorSet->vkDescriptorSet = descriptorSet->vkDescriptorSet;
      dummyDeleteDescriptorSet->bindings = nullptr;
      dummyDeleteDescriptorSet->resources = nullptr;
      dummyDeleteDescriptorSet->samplers = nullptr;
      dummyDeleteDescriptorSet->numResources = 0;

      destroyDescriptorSet(dummyDeleteDescriptorSetHandle);

      // Allocate the new descriptor set and update its content.
      VkWriteDescriptorSet descriptorWrite[8];
      VkDescriptorBufferInfo bufferInfo[8];
      VkDescriptorImageInfo imageInfo[8];

      Sampler* defaultSampler = (Sampler*)m_Samplers.accessResource(m_DefaultSampler.index);

      VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      allocInfo.descriptorPool = m_VulkanDescriptorPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &descriptorSet->layout->vkDescriptorSetLayout;
      vkAllocateDescriptorSets(m_VulkanDevice, &allocInfo, &descriptorSet->vkDescriptorSet);

      uint32_t numResources = descriptorSetLayout->numBindings;
      fillWriteDescriptorSets(
          *this,
          descriptorSetLayout,
          descriptorSet->vkDescriptorSet,
          descriptorWrite,
          bufferInfo,
          imageInfo,
          defaultSampler->vkSampler,
          numResources,
          descriptorSet->resources,
          descriptorSet->samplers,
          descriptorSet->bindings);

      vkUpdateDescriptorSets(m_VulkanDevice, numResources, descriptorWrite, 0, nullptr);
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
    // TODO: store queue type in command buffer to avoid this if not needed
    commandBuffer->endCurrentRenderPass();

    vkEndCommandBuffer(commandBuffer->m_VulkanCmdBuffer);
    commandBuffer->m_IsRecording = false;
    commandBuffer->m_CurrentRenderPass = nullptr;
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

  vkQueueSubmit(m_VulkanMainQueue, 1, &submit_info, *renderCompleteFence);

  VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = renderCompleteSemaphore;

  VkSwapchainKHR swap_chains[] = {m_VulkanSwapchain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swap_chains;
  presentInfo.pImageIndices = &m_VulkanImageIndex;
  presentInfo.pResults = nullptr; // Optional
  result = vkQueuePresentKHR(m_VulkanMainQueue, &presentInfo);

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

  // Update bindless descriptor sets:
  if (m_TextureToUpdateBindless.m_Size > 0)
  {
    // Handle deferred writes to bindless textures.
    VkWriteDescriptorSet bindlessDescriptorWrites[kMaxBindlessResources];
    VkDescriptorImageInfo bindlessImageInfo[kMaxBindlessResources];

    Texture* vkDummyTexture = (Texture*)m_Textures.accessResource(m_DummyTexture.index);

    uint32_t currentWriteIndex = 0;
    for (int it = m_TextureToUpdateBindless.m_Size - 1; it >= 0; it--)
    {
      ResourceUpdate& textureToUpdate = m_TextureToUpdateBindless[it];

      {
        Texture* texture = (Texture*)m_Textures.accessResource(textureToUpdate.handle);
        VkWriteDescriptorSet& descriptorWrite = bindlessDescriptorWrites[currentWriteIndex];
        descriptorWrite = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.dstArrayElement = textureToUpdate.handle;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.dstSet = m_VulkanBindlessDescriptorSetCached;
        descriptorWrite.dstBinding = kBindlessTextureBinding;

        // Handles should be the same.
        assert(texture->handle.index == textureToUpdate.handle);

        Sampler* vkDefaultSampler = (Sampler*)m_Samplers.accessResource(m_DefaultSampler.index);
        VkDescriptorImageInfo& descriptorImageInfo = bindlessImageInfo[currentWriteIndex];

        // Update image view and sampler if valid
        if (!textureToUpdate.deleting)
        {
          descriptorImageInfo.imageView = texture->vkImageView;

          if (texture->sampler != nullptr)
          {
            descriptorImageInfo.sampler = texture->sampler->vkSampler;
          }
          else
          {
            descriptorImageInfo.sampler = vkDefaultSampler->vkSampler;
          }
        }
        else
        {
          // Deleting: set to default image view and sampler in the current slot.
          descriptorImageInfo.imageView = vkDummyTexture->vkImageView;
          descriptorImageInfo.sampler = vkDefaultSampler->vkSampler;
        }

        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorWrite.pImageInfo = &descriptorImageInfo;

        textureToUpdate.currentFrame = UINT_MAX;

        m_TextureToUpdateBindless.deleteSwap(it);

        ++currentWriteIndex;
      }
    }

    if (currentWriteIndex > 0)
    {
      vkUpdateDescriptorSets(
          m_VulkanDevice, currentWriteIndex, bindlessDescriptorWrites, 0, nullptr);
    }
  }

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

  // NOTE! Don't allow rebar
  assert(!(p_Creation.persistent && p_Creation.deviceOnly));

  VmaAllocationCreateInfo memoryCi{};
  memoryCi.flags = VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
  if (p_Creation.persistent)
    memoryCi.flags = memoryCi.flags | VMA_ALLOCATION_CREATE_MAPPED_BIT;

  if (p_Creation.deviceOnly)
    memoryCi.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  else
    memoryCi.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

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

  if (p_Creation.persistent)
  {
    buffer->mappedData = static_cast<uint8_t*>(allocationInfo.pMappedData);
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

  //// Copy buffer_data if present
  if (p_Creation.initialData)
  {
    uploadTextureData(texture, p_Creation.initialData, *this);
  }

  return handle;
}
//---------------------------------------------------------------------------//
PipelineHandle
GpuDevice::createPipeline(const PipelineCreation& p_Creation, const char* p_CachePath)
{
  PipelineHandle handle = {m_Pipelines.obtainResource()};
  if (handle.index == kInvalidIndex)
  {
    return handle;
  }

  // Set up pipeline cache
  VkPipelineCache pipelineCache = VK_NULL_HANDLE;
  bool cacheExists = Framework::fileExists(p_CachePath);
  {
    VkPipelineCacheCreateInfo pipelineCacheCi{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};

    if (p_CachePath != nullptr && cacheExists)
    {
      Framework::FileReadResult readResult = Framework::fileReadBinary(p_CachePath, m_Allocator);

      VkPipelineCacheHeaderVersionOne* cacheHeader =
          (VkPipelineCacheHeaderVersionOne*)readResult.data;

      if (cacheHeader->deviceID == m_VulkanPhysicalDeviceProps.deviceID &&
          cacheHeader->vendorID == m_VulkanPhysicalDeviceProps.vendorID &&
          memcmp(
              cacheHeader->pipelineCacheUUID,
              m_VulkanPhysicalDeviceProps.pipelineCacheUUID,
              VK_UUID_SIZE) == 0)
      {
        pipelineCacheCi.initialDataSize = readResult.size;
        pipelineCacheCi.pInitialData = readResult.data;
      }
      else
      {
        cacheExists = false;
      }

      CHECKRES(vkCreatePipelineCache(
          m_VulkanDevice, &pipelineCacheCi, m_VulkanAllocCallbacks, &pipelineCache));

      m_Allocator->deallocate(readResult.data);
    }
    else
    {
      CHECKRES(vkCreatePipelineCache(
          m_VulkanDevice, &pipelineCacheCi, m_VulkanAllocCallbacks, &pipelineCache));
    }
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

  uint32_t numActiveLayouts = shaderStateData->parseResult->setCount;

  // Create VkPipelineLayout
  for (uint32_t l = 0; l < numActiveLayouts; ++l)
  {
    // At index 0 there is the bindless layout.
    // TODO: improve API.
    if (l == 0)
    {
      DescriptorSetLayout* s = (DescriptorSetLayout*)m_DescriptorSetLayouts.accessResource(
          m_BindlessDescriptorSetLayout.index);
      // Avoid deletion of this set as it is global and will be freed after.
      pipeline->descriptorSetLayoutHandle[l] = kInvalidLayout;
      vkLayouts[l] = s->vkDescriptorSetLayout;
      continue;
    }
    else
    {
      pipeline->descriptorSetLayoutHandle[l] =
          createDescriptorSetLayout(shaderStateData->parseResult->sets[l]);
    }

    pipeline->descriptorSetLayout[l] = (DescriptorSetLayout*)m_DescriptorSetLayouts.accessResource(
        pipeline->descriptorSetLayoutHandle[l].index);

    vkLayouts[l] = pipeline->descriptorSetLayout[l]->vkDescriptorSetLayout;
  }

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutInfo.pSetLayouts = vkLayouts;
  pipelineLayoutInfo.setLayoutCount = numActiveLayouts;

  VkPipelineLayout pipelineLayout{};
  CHECKRES(vkCreatePipelineLayout(
      m_VulkanDevice, &pipelineLayoutInfo, m_VulkanAllocCallbacks, &pipelineLayout));
  // Cache pipeline layout
  pipeline->vkPipelineLayout = pipelineLayout;
  pipeline->numActiveLayouts = numActiveLayouts;

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
      assert(
          p_Creation.blendState.activeStates == p_Creation.renderPass.numColorFormats,
          "Blend states (count: %u) mismatch with output targets (count %u)!If blend states are "
          "active, they must be defined for all outputs",
          p_Creation.blendState.activeStates,
          p_Creation.renderPass.numColorFormats);
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
      for (uint32_t i = 0; i < p_Creation.renderPass.numColorFormats; ++i)
      {
        colorBlendAttachment[i] = {};
        colorBlendAttachment[i].blendEnable = VK_FALSE;
        colorBlendAttachment[i].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
      }
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = p_Creation.blendState.activeStates
                                        ? p_Creation.blendState.activeStates
                                        : p_Creation.renderPass.numColorFormats;
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
    VkPipelineRenderingCreateInfoKHR pipelineRenderingCi{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    if (m_DynamicRenderingExtensionPresent)
    {
      pipelineRenderingCi.viewMask = 0;
      pipelineRenderingCi.colorAttachmentCount = p_Creation.renderPass.numColorFormats;
      pipelineRenderingCi.pColorAttachmentFormats =
          p_Creation.renderPass.numColorFormats > 0 ? p_Creation.renderPass.colorFormats : nullptr;
      pipelineRenderingCi.depthAttachmentFormat = p_Creation.renderPass.depthStencilFormat;
      pipelineRenderingCi.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

      pipelineCi.pNext = &pipelineRenderingCi;
    }
    else
    {
      pipelineCi.renderPass = getVulkanRenderPass(p_Creation.renderPass, p_Creation.name);
    }

    //// Dynamic states
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicStateCi{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicStateCi.dynamicStateCount = arrayCount32(dynamicStates);
    dynamicStateCi.pDynamicStates = dynamicStates;

    pipelineCi.pDynamicState = &dynamicStateCi;

    CHECKRES(vkCreateGraphicsPipelines(
        m_VulkanDevice,
        pipelineCache,
        1,
        &pipelineCi,
        m_VulkanAllocCallbacks,
        &pipeline->vkPipeline));

    pipeline->vkBindPoint = VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS;
  }
  else
  {
    VkComputePipelineCreateInfo pipelineCi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};

    pipelineCi.stage = shaderStateData->shaderStageInfo[0];
    pipelineCi.layout = pipelineLayout;

    CHECKRES(vkCreateComputePipelines(
        m_VulkanDevice,
        pipelineCache,
        1,
        &pipelineCi,
        m_VulkanAllocCallbacks,
        &pipeline->vkPipeline));

    pipeline->vkBindPoint = VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE;
  }

  if (p_CachePath != nullptr && !cacheExists)
  {
    size_t cacheDataSize = 0;
    CHECKRES(vkGetPipelineCacheData(m_VulkanDevice, pipelineCache, &cacheDataSize, nullptr));

    void* cacheData = m_Allocator->allocate(cacheDataSize, 64);
    CHECKRES(vkGetPipelineCacheData(m_VulkanDevice, pipelineCache, &cacheDataSize, cacheData));

    Framework::fileWriteBinary(p_CachePath, cacheData, cacheDataSize);

    m_Allocator->deallocate(cacheData);
  }

  vkDestroyPipelineCache(m_VulkanDevice, pipelineCache, m_VulkanAllocCallbacks);

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
  ci.minLod = 0;
  ci.maxLod = 16;

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

  DescriptorSetLayout* descriptorSetLayout =
      (DescriptorSetLayout*)m_DescriptorSetLayouts.accessResource(handle.index);

  uint16_t maxBinding = 0;
  for (uint32_t r = 0; r < p_Creation.numBindings; ++r)
  {
    const DescriptorSetLayoutCreation::Binding& inputBinding = p_Creation.bindings[r];
    maxBinding = _max(maxBinding, inputBinding.index);
  }
  maxBinding += 1;

  // Create flattened binding list
  descriptorSetLayout->numBindings = (uint16_t)p_Creation.numBindings;
  uint8_t* memory = FRAMEWORK_ALLOCAM(
      ((sizeof(VkDescriptorSetLayoutBinding) + sizeof(DescriptorBinding)) *
       p_Creation.numBindings) +
          (sizeof(uint8_t) * maxBinding),
      m_Allocator);
  descriptorSetLayout->bindings = (DescriptorBinding*)memory;
  descriptorSetLayout->vkBinding =
      (VkDescriptorSetLayoutBinding*)(memory + sizeof(DescriptorBinding) * p_Creation.numBindings);
  descriptorSetLayout->indexToBinding =
      (uint8_t*)(descriptorSetLayout->vkBinding + p_Creation.numBindings);
  descriptorSetLayout->handle = handle;
  descriptorSetLayout->setIndex = uint16_t(p_Creation.setIndex);
  descriptorSetLayout->bindless = p_Creation.bindless ? 1 : 0;
  descriptorSetLayout->dynamic = p_Creation.dynamic ? 1 : 0;

  const bool skipBindlessBindings = m_BindlessSupported && !p_Creation.bindless;
  uint32_t usedBindings = 0;
  for (uint32_t r = 0; r < p_Creation.numBindings; ++r)
  {
    DescriptorBinding& binding = descriptorSetLayout->bindings[r];
    const DescriptorSetLayoutCreation::Binding& inputBinding = p_Creation.bindings[r];
    binding.index = inputBinding.index == UINT16_MAX ? (uint16_t)r : inputBinding.index;
    binding.count = inputBinding.count;
    binding.type = inputBinding.type;
    binding.name = inputBinding.name;

    // Add binding index to binding data
    descriptorSetLayout->indexToBinding[binding.index] = r;

    // Skip bindings for images and textures as they are bindless, thus bound in the global
    // bindless arrays (one for images, one for textures).
    if (skipBindlessBindings && (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                                 binding.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE))
    {
      continue;
    }
    VkDescriptorSetLayoutBinding& vkBinding = descriptorSetLayout->vkBinding[usedBindings];
    ++usedBindings;

    vkBinding.binding = binding.index;
    vkBinding.descriptorType = inputBinding.type;
    vkBinding.descriptorType = vkBinding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                   : vkBinding.descriptorType;
    vkBinding.descriptorCount = inputBinding.count;

    vkBinding.stageFlags = VK_SHADER_STAGE_ALL;
    vkBinding.pImmutableSamplers = nullptr;
  }

  // Create the descriptor set layout
  VkDescriptorSetLayoutCreateInfo layoutInfo = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = usedBindings; // p_Creation.numBindings;
  layoutInfo.pBindings = descriptorSetLayout->vkBinding;

  if (p_Creation.bindless)
  {
    // Needs update after bind flag.
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;

    // TODO: reenable variable descriptor count
    // Binding flags
    VkDescriptorBindingFlags bindlessFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT; // VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT
    VkDescriptorBindingFlags bindingFlags[16];

    for (uint32_t r = 0; r < p_Creation.numBindings; ++r)
    {
      bindingFlags[r] = bindlessFlags;
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT, nullptr};
    extendedInfo.bindingCount = usedBindings;
    extendedInfo.pBindingFlags = bindingFlags;

    layoutInfo.pNext = &extendedInfo;
    vkCreateDescriptorSetLayout(
        m_VulkanDevice,
        &layoutInfo,
        m_VulkanAllocCallbacks,
        &descriptorSetLayout->vkDescriptorSetLayout);
  }
  else
  {
    vkCreateDescriptorSetLayout(
        m_VulkanDevice,
        &layoutInfo,
        m_VulkanAllocCallbacks,
        &descriptorSetLayout->vkDescriptorSetLayout);
  }

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

  DescriptorSet* descriptorSet = (DescriptorSet*)m_DescriptorSets.accessResource(handle.index);
  const DescriptorSetLayout* descriptorSetLayout =
      (DescriptorSetLayout*)m_DescriptorSetLayouts.accessResource(p_Creation.layout.index);

  // Allocate descriptor set
  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool =
      descriptorSetLayout->bindless ? m_VulkanBindlessDescriptorPool : m_VulkanDescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &descriptorSetLayout->vkDescriptorSetLayout;

  if (descriptorSetLayout->bindless)
  {
    VkDescriptorSetVariableDescriptorCountAllocateInfoEXT countInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT};
    uint32_t maxBinding = kMaxBindlessResources - 1;
    countInfo.descriptorSetCount = 1;
    // This number is the max allocatable count
    countInfo.pDescriptorCounts = &maxBinding;
    allocInfo.pNext = &countInfo;
    CHECKRES(vkAllocateDescriptorSets(m_VulkanDevice, &allocInfo, &descriptorSet->vkDescriptorSet));
  }
  else
  {
    CHECKRES(vkAllocateDescriptorSets(m_VulkanDevice, &allocInfo, &descriptorSet->vkDescriptorSet));
  }

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
  fillWriteDescriptorSets(
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
  // Init the rest of the struct.
  renderPass->numRenderTargets = (uint8_t)p_Creation.numRenderTargets;
  renderPass->dispatchX = 0;
  renderPass->dispatchY = 0;
  renderPass->dispatchZ = 0;
  renderPass->name = p_Creation.name;
  renderPass->vkRenderPass = VK_NULL_HANDLE;

  renderPass->output = _fillRenderPassOutput(*this, p_Creation);

  // Always use render pass cache with method getVulkanRenderPass instead of creating one.
  // Render pass cache will create a pass if needed.
  // renderPass->vkRenderPass = vulkanCreateRenderPass( *this, renderPass->output,
  // p_Creation.name );

  if (!m_DynamicRenderingExtensionPresent)
  {
    renderPass->vkRenderPass = getVulkanRenderPass(renderPass->output, p_Creation.name);
  }

  return handle;
}
FramebufferHandle GpuDevice::createFramebuffer(const FramebufferCreation& p_Creation)
{
  FramebufferHandle handle = {m_Framebuffers.obtainResource()};
  if (handle.index == kInvalidIndex)
  {
    return handle;
  }

  Framebuffer* framebuffer = (Framebuffer*)m_Framebuffers.accessResource(handle.index);
  // Init the rest of the struct.
  framebuffer->numColorAttachments = p_Creation.numRenderTargets;
  for (uint32_t a = 0; a < p_Creation.numRenderTargets; ++a)
  {
    framebuffer->colorAttachments[a] = p_Creation.outputTextures[a];
  }
  framebuffer->depthStencilAttachment = p_Creation.depthStencilTexture;
  framebuffer->width = p_Creation.width;
  framebuffer->height = p_Creation.height;
  framebuffer->scaleX = p_Creation.scaleX;
  framebuffer->scaleY = p_Creation.scaleY;
  framebuffer->resize = p_Creation.resize;
  framebuffer->name = p_Creation.name;
  framebuffer->renderPass = p_Creation.renderPass;

  if (!m_DynamicRenderingExtensionPresent)
  {
    _vulkanCreateFramebuffer(*this, framebuffer);
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

  Framework::StringBuffer nameBuffer;
  nameBuffer.init(4096, m_TemporaryAllocator);

  // Parse result needs to be always in memory as its used to free descriptor sets.
  shaderState->parseResult =
      (Spirv::ParseResult*)m_Allocator->allocate(sizeof(Spirv::ParseResult), 64);
  memset(shaderState->parseResult, 0, sizeof(Spirv::ParseResult));

  for (compiledShaders = 0; compiledShaders < p_Creation.stagesCount; ++compiledShaders)
  {
    const ShaderStage& stage = p_Creation.stages[compiledShaders];

    // Gives priority to compute: if any is present (and it should not be) then it is not a
    // graphics pipeline.
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

    Spirv::parseBinary(shaderCi.pCode, shaderCi.codeSize, nameBuffer, shaderState->parseResult);

    //
    // Note - temp allocator freed at the end.
    //

    setResourceName(
        VK_OBJECT_TYPE_SHADER_MODULE,
        (uint64_t)shaderState->shaderStageInfo[compiledShaders].module,
        p_Creation.name);
  }
  // Not needed anymore - temp allocator freed at the end.
  // nameBuffer.shutdown();
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
    {
      char msg[256]{};
      sprintf(
          msg,
          "Error in creation of shader %s. Dumping all shader informations.\n",
          p_Creation.name);
      OutputDebugStringA(msg);
    }
    for (compiledShaders = 0; compiledShaders < p_Creation.stagesCount; ++compiledShaders)
    {
      const ShaderStage& stage = p_Creation.stages[compiledShaders];
      char msg[256]{};
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
        {ResourceUpdateType::kBuffer, p_Buffer.index, m_CurrentFrameIndex, 1});
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
        {ResourceUpdateType::kTexture, p_Texture.index, m_CurrentFrameIndex, 1});
    m_TextureToUpdateBindless.push(
        {ResourceUpdateType::kTexture, p_Texture.index, m_CurrentFrameIndex, 1});
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
        {ResourceUpdateType::kPipeline, p_Pipeline.index, m_CurrentFrameIndex, 1});
    // Shader state creation is handled internally when creating a pipeline, thus add this to
    // track correctly.
    Pipeline* pipeline = (Pipeline*)m_Pipelines.accessResource(p_Pipeline.index);

    ShaderState* shaderStateData =
        (ShaderState*)m_Shaders.accessResource(pipeline->shaderState.index);
    for (uint32_t l = 0; l < shaderStateData->parseResult->setCount; ++l)
    {
      destroyDescriptorSetLayout(pipeline->descriptorSetLayoutHandle[l]);
    }

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
        {ResourceUpdateType::kSampler, p_Sampler.index, m_CurrentFrameIndex, 1});
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
        {ResourceUpdateType::kDescriptorSetLayout, p_Layout.index, m_CurrentFrameIndex, 1});
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
        {ResourceUpdateType::kDescriptorSet, p_Set.index, m_CurrentFrameIndex, 1});
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
        {ResourceUpdateType::kRenderPass, p_RenderPass.index, m_CurrentFrameIndex, 1});
  }
  else
  {
    OutputDebugStringA("Graphics error: trying to free invalid RenderPass\n");
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyFramebuffer(FramebufferHandle p_Framebuffer)
{
  if (p_Framebuffer.index < m_Framebuffers.m_PoolSize)
  {
    m_ResourceDeletionQueue.push(
        {ResourceUpdateType::kFramebuffer, p_Framebuffer.index, m_CurrentFrameIndex, 1});
  }
  else
  {
    printf("Graphics error: trying to free invalid Framebuffer %u\n", p_Framebuffer.index);
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyShaderState(ShaderStateHandle p_Shader)
{
  if (p_Shader.index < m_Shaders.m_PoolSize)
  {
    m_ResourceDeletionQueue.push(
        {ResourceUpdateType::kShaderState, p_Shader.index, m_CurrentFrameIndex, 1});

    ShaderState* state = (ShaderState*)m_Shaders.accessResource(p_Shader.index);
    m_Allocator->deallocate(state->parseResult);
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
  case ResourceUpdateType::kBuffer: {
    destroyBufferInstant(p_ResourceDeletion.handle);
    break;
  }

  case ResourceUpdateType::kPipeline: {
    destroyPipelineInstant(p_ResourceDeletion.handle);
    break;
  }

  case ResourceUpdateType::kRenderPass: {
    destroyRenderPassInstant(p_ResourceDeletion.handle);
    break;
  }

  case ResourceUpdateType::kDescriptorSet: {
    destroyDescriptorSetInstant(p_ResourceDeletion.handle);
    break;
  }

  case ResourceUpdateType::kDescriptorSetLayout: {
    destroyDescriptorSetLayoutInstant(p_ResourceDeletion.handle);
    break;
  }

  case ResourceUpdateType::kSampler: {
    destroySamplerInstant(p_ResourceDeletion.handle);
    break;
  }

  case ResourceUpdateType::kShaderState: {
    destroyShaderStateInstant(p_ResourceDeletion.handle);
    break;
  }

  case ResourceUpdateType::kTexture: {
    destroyTextureInstant(p_ResourceDeletion.handle);
    break;
  }

  case ResourceUpdateType::kFramebuffer: {
    destroyFramebufferInstant(p_ResourceDeletion.handle);
    break;
  }

  default: {
    assert(false, "Cannot process resource type %u\n", p_ResourceDeletion.type);
    break;
  }
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyBufferInstant(ResourceHandle buffer)
{
  Buffer* vbuffer = (Buffer*)m_Buffers.accessResource(buffer);

  if (vbuffer && vbuffer->parentBuffer.index == kInvalidBuffer.index)
  {
    vmaDestroyBuffer(m_VmaAllocator, vbuffer->vkBuffer, vbuffer->vmaAllocation);
  }
  m_Buffers.releaseResource(buffer);
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyTextureInstant(ResourceHandle texture)
{
  Texture* vTexture = (Texture*)m_Textures.accessResource(texture);

  // Skip double frees
  if (!vTexture->vkImageView)
  {
    return;
  }

  if (vTexture)
  {
    vkDestroyImageView(m_VulkanDevice, vTexture->vkImageView, m_VulkanAllocCallbacks);
    vTexture->vkImageView = VK_NULL_HANDLE;

    if (vTexture->vmaAllocation != 0)
    {
      vmaDestroyImage(m_VmaAllocator, vTexture->vkImage, vTexture->vmaAllocation);
    }
    else if (vTexture->vmaAllocation == nullptr)
    {
      // Aliased textures
      vkDestroyImage(m_VulkanDevice, vTexture->vkImage, m_VulkanAllocCallbacks);
    }
  }
  m_Textures.releaseResource(texture);
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyPipelineInstant(ResourceHandle pipeline)
{
  Pipeline* vPipeline = (Pipeline*)m_Pipelines.accessResource(pipeline);

  if (vPipeline)
  {
    vkDestroyPipeline(m_VulkanDevice, vPipeline->vkPipeline, m_VulkanAllocCallbacks);

    vkDestroyPipelineLayout(m_VulkanDevice, vPipeline->vkPipelineLayout, m_VulkanAllocCallbacks);
  }
  m_Pipelines.releaseResource(pipeline);
}
//---------------------------------------------------------------------------//
void GpuDevice::destroySamplerInstant(ResourceHandle sampler)
{
  Sampler* vSampler = (Sampler*)m_Samplers.accessResource(sampler);

  if (vSampler)
  {
    vkDestroySampler(m_VulkanDevice, vSampler->vkSampler, m_VulkanAllocCallbacks);
  }
  m_Samplers.releaseResource(sampler);
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyDescriptorSetLayoutInstant(ResourceHandle layout)
{
  DescriptorSetLayout* vDescriptorSetLayout =
      (DescriptorSetLayout*)m_DescriptorSetLayouts.accessResource(layout);

  if (vDescriptorSetLayout)
  {
    vkDestroyDescriptorSetLayout(
        m_VulkanDevice, vDescriptorSetLayout->vkDescriptorSetLayout, m_VulkanAllocCallbacks);

    // This contains also vk_binding allocation.
    FRAMEWORK_FREE(vDescriptorSetLayout->bindings, m_Allocator);
  }
  m_DescriptorSetLayouts.releaseResource(layout);
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyDescriptorSetInstant(ResourceHandle set)
{
  DescriptorSet* vDescriptorSet = (DescriptorSet*)m_DescriptorSets.accessResource(set);

  if (vDescriptorSet)
  {
    // Contains the allocation for all the resources, binding and samplers arrays.
    FRAMEWORK_FREE(vDescriptorSet->resources, m_Allocator);
    // This is freed with the DescriptorSet pool.
    // vkFreeDescriptorSets
  }
  m_DescriptorSets.releaseResource(set);
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyRenderPassInstant(ResourceHandle p_RenderPass)
{
  RenderPass* vRenderPass = (RenderPass*)m_RenderPasses.accessResource(p_RenderPass);

  if (vRenderPass)
  {

    // NOTE: this is now destroyed with the render pass cache, to avoid double deletes.
    // vkDestroyRenderPass( m_VulkanDevice, vRenderPass->vkRenderPass,
    // m_VulkanAllocCallbacks );
  }
  m_RenderPasses.releaseResource(p_RenderPass);
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyFramebufferInstant(ResourceHandle p_Framebuffer)
{
  Framebuffer* vFramebuffer = (Framebuffer*)m_Framebuffers.accessResource(p_Framebuffer);

  if (vFramebuffer)
  {

    for (uint32_t a = 0; a < vFramebuffer->numColorAttachments; ++a)
    {
      destroyTextureInstant(vFramebuffer->colorAttachments[a].index);
    }

    if (vFramebuffer->depthStencilAttachment.index != kInvalidIndex)
    {
      destroyTextureInstant(vFramebuffer->depthStencilAttachment.index);
    }

    if (!m_DynamicRenderingExtensionPresent)
    {
      vkDestroyFramebuffer(m_VulkanDevice, vFramebuffer->vkFramebuffer, m_VulkanAllocCallbacks);
    }
  }
  m_Framebuffers.releaseResource(p_Framebuffer);
}
//---------------------------------------------------------------------------//
void GpuDevice::destroyShaderStateInstant(ResourceHandle p_Shader)
{
  ShaderState* vShaderState = (ShaderState*)m_Shaders.accessResource(p_Shader);
  if (vShaderState)
  {

    for (size_t i = 0; i < vShaderState->activeShaders; i++)
    {
      vkDestroyShaderModule(
          m_VulkanDevice, vShaderState->shaderStageInfo[i].module, m_VulkanAllocCallbacks);
    }
  }
  m_Shaders.releaseResource(p_Shader);
}
//---------------------------------------------------------------------------//
void GpuDevice::updateDescriptorSetInstant(const DescriptorSetUpdate& update)
{
  // Use a dummy descriptor set to delete the vulkan descriptor set handle
  DescriptorSetHandle dummyDeleteDescriptorSetHandle = {m_DescriptorSets.obtainResource()};
  DescriptorSet* dummyDeleteDescriptorSet =
      (DescriptorSet*)m_DescriptorSets.accessResource(dummyDeleteDescriptorSetHandle.index);

  DescriptorSet* descriptorSet =
      (DescriptorSet*)m_DescriptorSets.accessResource(update.descriptorSet.index);

  const DescriptorSetLayout* descriptorSetLayout = descriptorSet->layout;

  dummyDeleteDescriptorSet->vkDescriptorSet = descriptorSet->vkDescriptorSet;
  dummyDeleteDescriptorSet->bindings = nullptr;
  dummyDeleteDescriptorSet->resources = nullptr;
  dummyDeleteDescriptorSet->samplers = nullptr;
  dummyDeleteDescriptorSet->numResources = 0;

  destroyDescriptorSet(dummyDeleteDescriptorSetHandle);

  // Allocate the new descriptor set and update its content.
  VkWriteDescriptorSet descriptorWrite[8];
  VkDescriptorBufferInfo bufferInfo[8];
  VkDescriptorImageInfo imageInfo[8];

  Sampler* vkDefaultSampler = (Sampler*)m_Samplers.accessResource(m_DefaultSampler.index);

  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = m_VulkanDescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &descriptorSet->layout->vkDescriptorSetLayout;
  vkAllocateDescriptorSets(m_VulkanDevice, &allocInfo, &descriptorSet->vkDescriptorSet);

  uint32_t p_NumResources = descriptorSetLayout->numBindings;
  fillWriteDescriptorSets(
      *this,
      descriptorSetLayout,
      descriptorSet->vkDescriptorSet,
      descriptorWrite,
      bufferInfo,
      imageInfo,
      vkDefaultSampler->vkSampler,
      p_NumResources,
      descriptorSet->resources,
      descriptorSet->samplers,
      descriptorSet->bindings);

  vkUpdateDescriptorSets(m_VulkanDevice, p_NumResources, descriptorWrite, 0, nullptr);
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
  m_DynamicAllocatedSize += (uint32_t)Framework::memoryAlign(p_Size, m_UboAlignment);
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
      m_VulkanPhysicalDevice, m_VulkanMainQueueFamily, m_VulkanWindowSurface, &surfaceSupported);
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

  // TODO(OM): Investigate the validation errors here:
  m_VulkanSwapchain = VK_NULL_HANDLE;
  VkResult result = vkCreateSwapchainKHR(m_VulkanDevice, &swapchainCi, 0, &m_VulkanSwapchain);
  CHECKRES(result);

  // Cache swapchain images
  vkGetSwapchainImagesKHR(m_VulkanDevice, m_VulkanSwapchain, &m_VulkanSwapchainImageCount, NULL);

  Framework::Array<VkImage> swapchainImages;
  swapchainImages.init(m_Allocator, m_VulkanSwapchainImageCount, m_VulkanSwapchainImageCount);
  vkGetSwapchainImagesKHR(
      m_VulkanDevice, m_VulkanSwapchain, &m_VulkanSwapchainImageCount, swapchainImages.m_Data);

  // Manually transition the texture
  VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  CommandBuffer* cmd = getCommandBuffer(0, false);
  vkBeginCommandBuffer(cmd->m_VulkanCmdBuffer, &beginInfo);

  for (size_t iv = 0; iv < m_VulkanSwapchainImageCount; iv++)
  {
    m_VulkanSwapchainFramebuffers[iv].index = m_Framebuffers.obtainResource();
    Framebuffer* vkFramebuffer =
        (Framebuffer*)m_Framebuffers.accessResource(m_VulkanSwapchainFramebuffers[iv].index);

    vkFramebuffer->renderPass = m_SwapchainRenderPass;

    vkFramebuffer->scaleX = 1.0f;
    vkFramebuffer->scaleY = 1.0f;
    vkFramebuffer->resize = 0;

    vkFramebuffer->numColorAttachments = 1;
    vkFramebuffer->colorAttachments[0].index = m_Textures.obtainResource();

    vkFramebuffer->name = "Swapchain";

    vkFramebuffer->width = m_SwapchainWidth;
    vkFramebuffer->height = m_SwapchainHeight;

    Texture* color = (Texture*)m_Textures.accessResource(vkFramebuffer->colorAttachments[0].index);
    color->vkImage = swapchainImages[iv];

    TextureCreation depthTextureCreation = {
        nullptr,
        m_SwapchainWidth,
        m_SwapchainHeight,
        1,
        1,
        0,
        VK_FORMAT_D32_SFLOAT,
        TextureType::kTexture2D,
        kInvalidTexture,
        "DepthImageTexture"};
    vkFramebuffer->depthStencilAttachment = createTexture(depthTextureCreation);

    Texture* depthStencilTexture =
        (Texture*)m_Textures.accessResource(vkFramebuffer->depthStencilAttachment.index);

    // Create an image view which we can render into.
    VkImageViewCreateInfo imageViewCi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    imageViewCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCi.format = m_VulkanSurfaceFormat.format;
    imageViewCi.image = swapchainImages[iv];
    imageViewCi.subresourceRange.levelCount = 1;
    imageViewCi.subresourceRange.layerCount = 1;
    imageViewCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCi.components.r = VK_COMPONENT_SWIZZLE_R;
    imageViewCi.components.g = VK_COMPONENT_SWIZZLE_G;
    imageViewCi.components.b = VK_COMPONENT_SWIZZLE_B;
    imageViewCi.components.a = VK_COMPONENT_SWIZZLE_A;

    result = vkCreateImageView(
        m_VulkanDevice, &imageViewCi, m_VulkanAllocCallbacks, &color->vkImageView);
    CHECKRES(result);

    if (!m_DynamicRenderingExtensionPresent)
    {
      _vulkanCreateFramebuffer(*this, vkFramebuffer);
    }

    utilAddImageBarrier(
        cmd->m_VulkanCmdBuffer,
        color->vkImage,
        RESOURCE_STATE_UNDEFINED,
        RESOURCE_STATE_PRESENT,
        0,
        1,
        false);
  }

  vkEndCommandBuffer(cmd->m_VulkanCmdBuffer);

  // Submit command buffer
  VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd->m_VulkanCmdBuffer;

  vkQueueSubmit(m_VulkanMainQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(m_VulkanMainQueue);

  swapchainImages.shutdown();
}
//---------------------------------------------------------------------------//
void GpuDevice::destroySwapchain()
{
  for (size_t iv = 0; iv < m_VulkanSwapchainImageCount; iv++)
  {
    Framebuffer* vkFramebuffer =
        (Framebuffer*)m_Framebuffers.accessResource(m_VulkanSwapchainFramebuffers[iv].index);

    if (!vkFramebuffer)
    {
      continue;
    }

    for (uint32_t a = 0; a < vkFramebuffer->numColorAttachments; ++a)
    {
      Texture* vkTexture =
          (Texture*)m_Textures.accessResource(vkFramebuffer->colorAttachments[a].index);

      vkDestroyImageView(m_VulkanDevice, vkTexture->vkImageView, m_VulkanAllocCallbacks);

      m_Textures.releaseResource(vkFramebuffer->colorAttachments[a].index);
    }

    if (vkFramebuffer->depthStencilAttachment.index != kInvalidIndex)
    {
      destroyTextureInstant(vkFramebuffer->depthStencilAttachment.index);
    }

    if (!m_DynamicRenderingExtensionPresent)
    {
      vkDestroyFramebuffer(m_VulkanDevice, vkFramebuffer->vkFramebuffer, m_VulkanAllocCallbacks);
    }

    m_Framebuffers.releaseResource(m_VulkanSwapchainFramebuffers[iv].index);
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
CommandBuffer* GpuDevice::getCommandBuffer(uint32_t p_ThreadIndex, bool p_Begin)
{
  CommandBuffer* cmd =
      g_CmdBufferRing.getCommandBuffer(m_CurrentFrameIndex, p_ThreadIndex, p_Begin);
  return cmd;
}
//---------------------------------------------------------------------------//
CommandBuffer* GpuDevice::getSecondaryCommandBuffer(uint32_t p_ThreadIndex)
{
  CommandBuffer* cmd =
      g_CmdBufferRing.getSecondaryCommandBuffer(m_CurrentFrameIndex, p_ThreadIndex);
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

  bool optimizeShaders = false;
  if (optimizeShaders)
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
    dumpShaderCode(tempStringBuffer, p_Code, p_Stage, p_Name);
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
void GpuDevice::linkTextureSampler(TextureHandle p_Texture, SamplerHandle p_Sampler)
{
  Texture* texture = (Texture*)m_Textures.accessResource(p_Texture.index);
  Sampler* sampler = (Sampler*)m_Samplers.accessResource(p_Sampler.index);

  texture->sampler = sampler;
}
//---------------------------------------------------------------------------//
void GpuDevice::resizeOutputTextures(FramebufferHandle framebuffer, uint32_t width, uint32_t height)
{
  // For each texture, create a temporary pooled texture and cache the handles to delete.
  // This is because we substitute just the Vulkan texture when resizing so that
  // external users don't need to update the handle.

  Framebuffer* vkFramebuffer = (Framebuffer*)m_Framebuffers.accessResource(framebuffer.index);
  if (vkFramebuffer)
  {
    // No need to resize!
    if (!vkFramebuffer->resize)
    {
      return;
    }

    // Calculate new width and height based on render pass sizing informations.
    uint16_t newWidth = (uint16_t)(width * vkFramebuffer->scaleX);
    uint16_t newHeight = (uint16_t)(height * vkFramebuffer->scaleY);

    // Resize textures if needed
    const uint32_t rts = vkFramebuffer->numColorAttachments;
    for (uint32_t i = 0; i < rts; ++i)
    {
      resizeTexture(vkFramebuffer->colorAttachments[i], newWidth, newHeight);
    }

    if (vkFramebuffer->depthStencilAttachment.index != kInvalidIndex)
    {
      resizeTexture(vkFramebuffer->depthStencilAttachment, newWidth, newHeight);
    }

    // Again: create temporary resource to use the standard deferred deletion mechanism.
    FramebufferHandle framebufferToDestroy = {m_Framebuffers.obtainResource()};
    Framebuffer* vkFramebufferToDestroy =
        (Framebuffer*)m_Framebuffers.accessResource(framebufferToDestroy.index);
    // Cache framebuffer to be deleted
    vkFramebufferToDestroy->vkFramebuffer = vkFramebuffer->vkFramebuffer;
    // Textures are manually destroyed few lines above, so avoid doing it again.
    vkFramebufferToDestroy->numColorAttachments = 0;
    vkFramebufferToDestroy->depthStencilAttachment.index = kInvalidIndex;

    destroyFramebuffer(framebufferToDestroy);

    // Update render pass size
    vkFramebuffer->width = newWidth;
    vkFramebuffer->height = newHeight;

    // Recreate framebuffer if present (mainly for dispatch only passes)
    if (vkFramebuffer->vkFramebuffer)
    {
      _vulkanCreateFramebuffer(*this, vkFramebuffer);
    }
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::resizeTexture(TextureHandle texture, uint32_t width, uint32_t height)
{
  Texture* vkTexture = (Texture*)m_Textures.accessResource(texture.index);

  if (vkTexture->width == width && vkTexture->height == height)
  {
    return;
  }

  // Queue deletion of texture by creating a temporary one
  TextureHandle textureToDelete = {m_Textures.obtainResource()};
  Texture* vkTextureToDelete = (Texture*)m_Textures.accessResource(textureToDelete.index);

  // Cache all informations (image, image view, flags, ...) into texture to delete.
  // Missing even one information (like it is a texture view, sparse, ...)
  // can lead to memory leaks.
  Framework::memoryCopy(vkTextureToDelete, vkTexture, sizeof(Texture));
  // Update handle so it can be used to update bindless to dummy texture
  // and delete the old image and image view.
  vkTextureToDelete->handle = textureToDelete;

  // Re-create image in place.
  TextureCreation tc;
  tc.setFlags(vkTexture->mipmaps, vkTexture->flags)
      .setFormatType(vkTexture->vkFormat, vkTexture->type)
      .setName(vkTexture->name)
      .setSize(width, height, vkTexture->depth);
  _vulkanCreateTexture(*this, tc, vkTexture->handle, vkTexture);

  destroyTexture(textureToDelete);
}
//---------------------------------------------------------------------------//
uint32_t GpuDevice::getMemoryHeapCount() { return m_VmaAllocator->GetMemoryHeapCount(); }
//---------------------------------------------------------------------------//
} // namespace Graphics
