#include "CommandBuffer.hpp"

namespace Graphics
{
static const uint32_t g_SecondaryCommandBuffersCount = 2;
//---------------------------------------------------------------------------//
// DrawIndirect = 0, VertexInput = 1, VertexShader = 2, FragmentShader = 3, RenderTarget = 4,
// ComputeShader = 5, Transfer = 6
static ResourceState toResourceState(PipelineStage::Enum stage)
{
  static ResourceState s_states[] = {
      RESOURCE_STATE_INDIRECT_ARGUMENT,
      RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
      RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
      RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
      RESOURCE_STATE_RENDER_TARGET,
      RESOURCE_STATE_UNORDERED_ACCESS,
      RESOURCE_STATE_COPY_DEST};
  return s_states[stage];
}
//---------------------------------------------------------------------------//
void CommandBuffer::init(Graphics::GpuDevice* p_GpuDevice)
{
  this->m_GpuDevice = p_GpuDevice;

  static const uint32_t kGlobalPoolElements = 128;
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, kGlobalPoolElements},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kGlobalPoolElements},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kGlobalPoolElements},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kGlobalPoolElements},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, kGlobalPoolElements},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, kGlobalPoolElements},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kGlobalPoolElements},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGlobalPoolElements},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kGlobalPoolElements},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kGlobalPoolElements},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, kGlobalPoolElements}};
  VkDescriptorPoolCreateInfo poolCi = {};
  poolCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolCi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolCi.maxSets = kDescriptorSetsPoolSize;
  poolCi.poolSizeCount = arrayCount32(poolSizes);
  poolCi.pPoolSizes = poolSizes;
  VkResult result = vkCreateDescriptorPool(
      m_GpuDevice->m_VulkanDevice,
      &poolCi,
      m_GpuDevice->m_VulkanAllocCallbacks,
      &m_VulkanDescriptorPool);
  assert(result == VK_SUCCESS);

  m_DescriptorSets.init(m_GpuDevice->m_Allocator, kDescriptorSetsPoolSize, sizeof(DescriptorSet));

  reset();
}
//---------------------------------------------------------------------------//
void CommandBuffer::shutdown()
{
  m_IsRecording = false;
  reset();

  m_DescriptorSets.shutdown();

  vkDestroyDescriptorPool(
      m_GpuDevice->m_VulkanDevice, m_VulkanDescriptorPool, m_GpuDevice->m_VulkanAllocCallbacks);
}
//---------------------------------------------------------------------------//
void CommandBuffer::reset()
{
  m_IsRecording = false;
  m_CurrentRenderPass = nullptr;
  m_CurrentFramebuffer = nullptr;
  m_CurrentPipeline = nullptr;
  m_CurrentCommand = 0;

  vkResetDescriptorPool(m_GpuDevice->m_VulkanDevice, m_VulkanDescriptorPool, 0);

  uint32_t resourceCount = m_DescriptorSets.m_FreeIndicesHead;
  for (uint32_t i = 0; i < resourceCount; ++i)
  {
    DescriptorSet* descriptorSet = (DescriptorSet*)m_DescriptorSets.accessResource(i);

    if (descriptorSet)
    {
      // Contains the allocation for all the resources, binding and samplers arrays.
      FRAMEWORK_FREE(descriptorSet->resources, m_GpuDevice->m_Allocator);
    }
    m_DescriptorSets.releaseResource(i);
  }
}
//---------------------------------------------------------------------------//
void CommandBuffer::bindLocalDescriptorSet(
    DescriptorSetHandle* p_Handles, uint32_t p_NumLists, uint32_t* p_Offsets, uint32_t p_NumOffsets)
{
  // TODO:
  uint32_t offsetsCache[8];
  p_NumOffsets = 0;

  for (uint32_t l = 0; l < p_NumLists; ++l)
  {
    DescriptorSet* descriptorSet =
        (DescriptorSet*)m_DescriptorSets.accessResource(p_Handles[l].index);
    m_VulkanDescriptorSets[l] = descriptorSet->vkDescriptorSet;

    // Search for dynamic buffers
    const DescriptorSetLayout* descriptorSetLayout = descriptorSet->layout;
    for (uint32_t i = 0; i < descriptorSetLayout->numBindings; ++i)
    {
      const DescriptorBinding& rb = descriptorSetLayout->bindings[i];

      if (rb.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
      {
        // Search for the actual buffer offset
        const uint32_t resource_index = descriptorSet->bindings[i];
        ResourceHandle bufferHandle = descriptorSet->resources[resource_index];
        Buffer* buffer = (Buffer*)m_GpuDevice->m_Buffers.accessResource(bufferHandle);

        offsetsCache[p_NumOffsets++] = buffer->globalOffset;
      }
    }
  }

  const uint32_t kFirstSet = 0;
  vkCmdBindDescriptorSets(
      m_VulkanCmdBuffer,
      m_CurrentPipeline->vkBindPoint,
      m_CurrentPipeline->vkPipelineLayout,
      kFirstSet,
      p_NumLists,
      m_VulkanDescriptorSets,
      p_NumOffsets,
      offsetsCache);

  if (m_GpuDevice->m_BindlessSupported)
  {
    vkCmdBindDescriptorSets(
        m_VulkanCmdBuffer,
        m_CurrentPipeline->vkBindPoint,
        m_CurrentPipeline->vkPipelineLayout,
        1,
        1,
        &m_GpuDevice->m_VulkanBindlessDescriptorSetCached,
        0,
        nullptr);
  }
}
//---------------------------------------------------------------------------//
void CommandBuffer::bindPass(
    RenderPassHandle p_Passhandle, FramebufferHandle p_Framebuffer, bool p_UseSecondary)
{
  m_IsRecording = true;

  RenderPass* renderPass =
      (RenderPass*)m_GpuDevice->m_RenderPasses.accessResource(p_Passhandle.index);

  // Begin/End render pass are valid only for graphics render passes.
  if (m_CurrentRenderPass && (renderPass != m_CurrentRenderPass))
  {
    endCurrentRenderPass();
  }

  Framebuffer* framebuffer =
      (Framebuffer*)m_GpuDevice->m_Framebuffers.accessResource(p_Framebuffer.index);

  if (renderPass != m_CurrentRenderPass)
  {
    if (m_GpuDevice->m_DynamicRenderingExtensionPresent)
    {
      Framework::Array<VkRenderingAttachmentInfoKHR> colorAttachmentsInfo;
      colorAttachmentsInfo.init(
          m_GpuDevice->m_Allocator,
          framebuffer->numColorAttachments,
          framebuffer->numColorAttachments);
      memset(
          colorAttachmentsInfo.m_Data,
          0,
          sizeof(VkRenderingAttachmentInfoKHR) * framebuffer->numColorAttachments);

      for (uint32_t a = 0; a < framebuffer->numColorAttachments; ++a)
      {
        Texture* texture = (Texture*)m_GpuDevice->m_Textures.accessResource(
            framebuffer->colorAttachments[a].index);

        VkAttachmentLoadOp color_op;
        switch (renderPass->output.colorOperations[a])
        {
        case RenderPassOperation::kLoad:
          color_op = VK_ATTACHMENT_LOAD_OP_LOAD;
          break;
        case RenderPassOperation::kClear:
          color_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
          break;
        default:
          color_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
          break;
        }

        VkRenderingAttachmentInfoKHR& colorAttachmentInfo = colorAttachmentsInfo[a];
        colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
        colorAttachmentInfo.imageView = texture->vkImageView;
        colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
        colorAttachmentInfo.loadOp = color_op;
        colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentInfo.clearValue =
            renderPass->output.colorOperations[a] == RenderPassOperation::Enum::kClear
                ? m_ClearValues[a]
                : VkClearValue{};
      }

      VkRenderingAttachmentInfoKHR depth_attachment_info{
          VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};

      bool hasDepth_attachment = framebuffer->depthStencilAttachment.index != kInvalidIndex;

      if (hasDepth_attachment)
      {
        Texture* texture = (Texture*)m_GpuDevice->m_Textures.accessResource(
            framebuffer->depthStencilAttachment.index);

        VkAttachmentLoadOp depth_op;
        switch (renderPass->output.depthOperation)
        {
        case RenderPassOperation::kLoad:
          depth_op = VK_ATTACHMENT_LOAD_OP_LOAD;
          break;
        case RenderPassOperation::kClear:
          depth_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
          break;
        default:
          depth_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
          break;
        }

        depth_attachment_info.imageView = texture->vkImageView;
        depth_attachment_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;
        depth_attachment_info.loadOp = depth_op;
        depth_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment_info.clearValue =
            renderPass->output.depthOperation == RenderPassOperation::Enum::kClear
                ? m_ClearValues[kDepthStencilClearIndex]
                : VkClearValue{};
      }

      VkRenderingInfoKHR renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
      renderingInfo.flags =
          p_UseSecondary ? VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT_KHR : 0;
      renderingInfo.renderArea = {0, 0, framebuffer->width, framebuffer->height};
      renderingInfo.layerCount = 1;
      renderingInfo.viewMask = 0;
      renderingInfo.colorAttachmentCount = framebuffer->numColorAttachments;
      renderingInfo.pColorAttachments =
          framebuffer->numColorAttachments > 0 ? colorAttachmentsInfo.m_Data : nullptr;
      renderingInfo.pDepthAttachment = hasDepth_attachment ? &depth_attachment_info : nullptr;
      renderingInfo.pStencilAttachment = nullptr;

      m_GpuDevice->m_CmdBeginRendering(m_VulkanCmdBuffer, &renderingInfo);

      colorAttachmentsInfo.shutdown();
    }
    else
    {
      VkRenderPassBeginInfo renderPassBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      renderPassBegin.framebuffer = framebuffer->vkFramebuffer;
      renderPassBegin.renderPass = renderPass->vkRenderPass;

      renderPassBegin.renderArea.offset = {0, 0};
      renderPassBegin.renderArea.extent = {framebuffer->width, framebuffer->height};

      uint32_t clearValuesCount = renderPass->output.numColorFormats;

      if (renderPass->output.depthStencilFormat != VK_FORMAT_UNDEFINED)
      {
        if (renderPass->output.depthOperation == RenderPassOperation::Enum::kClear)
        {
          m_ClearValues[clearValuesCount++] = m_ClearValues[kDepthStencilClearIndex];
        }
      }

      renderPassBegin.clearValueCount = clearValuesCount;
      renderPassBegin.pClearValues = m_ClearValues;

      vkCmdBeginRenderPass(
          m_VulkanCmdBuffer,
          &renderPassBegin,
          p_UseSecondary ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS
                         : VK_SUBPASS_CONTENTS_INLINE);
    }
  }

  // Cache render pass
  m_CurrentRenderPass = renderPass;
  m_CurrentFramebuffer = framebuffer;
}
//---------------------------------------------------------------------------//
void CommandBuffer::bindPipeline(PipelineHandle p_Handle)
{
  Pipeline* pipeline = (Pipeline*)m_GpuDevice->m_Pipelines.accessResource(p_Handle.index);
  vkCmdBindPipeline(m_VulkanCmdBuffer, pipeline->vkBindPoint, pipeline->vkPipeline);

  // Cache pipeline
  m_CurrentPipeline = pipeline;
}
//---------------------------------------------------------------------------//
void CommandBuffer::bindVertexBuffer(BufferHandle p_Handle, uint32_t p_Binding, uint32_t p_Offset)
{
  Buffer* buffer = (Buffer*)m_GpuDevice->m_Buffers.accessResource(p_Handle.index);
  VkDeviceSize offsets[] = {p_Offset};

  VkBuffer vkBuffer = buffer->vkBuffer;
  // TODO: add global vertex buffer ?
  if (buffer->parentBuffer.index != kInvalidIndex)
  {
    Buffer* parentBuffer =
        (Buffer*)m_GpuDevice->m_Buffers.accessResource(buffer->parentBuffer.index);
    vkBuffer = parentBuffer->vkBuffer;
    offsets[0] = buffer->globalOffset;
  }

  vkCmdBindVertexBuffers(m_VulkanCmdBuffer, p_Binding, 1, &vkBuffer, offsets);
}
//---------------------------------------------------------------------------//
void CommandBuffer::bindVertexBuffers(
    BufferHandle* p_Handles, uint32_t p_FirstBinding, uint32_t p_BindingCount, uint32_t* p_Offsets)
{
  VkBuffer vkBuffers[8];
  VkDeviceSize offsets[8];

  for (uint32_t i = 0; i < p_BindingCount; ++i)
  {
    Buffer* buffer = (Buffer*)m_GpuDevice->m_Buffers.accessResource(p_Handles[i].index);

    VkBuffer vkBuffer = buffer->vkBuffer;
    // TODO: add global vertex buffer ?
    if (buffer->parentBuffer.index != kInvalidIndex)
    {
      Buffer* parentBuffer =
          (Buffer*)m_GpuDevice->m_Buffers.accessResource(buffer->parentBuffer.index);
      vkBuffer = parentBuffer->vkBuffer;
      offsets[i] = buffer->globalOffset;
    }
    else
    {
      offsets[i] = p_Offsets[i];
    }

    vkBuffers[i] = vkBuffer;
  }

  vkCmdBindVertexBuffers(m_VulkanCmdBuffer, p_FirstBinding, p_BindingCount, vkBuffers, offsets);
}
//---------------------------------------------------------------------------//
void CommandBuffer::bindIndexBuffer(
    BufferHandle p_Handle, uint32_t p_Offset, VkIndexType p_IndexType)
{
  Buffer* buffer = (Buffer*)m_GpuDevice->m_Buffers.accessResource(p_Handle.index);

  VkBuffer vkBuffer = buffer->vkBuffer;
  VkDeviceSize offset = p_Offset;
  if (buffer->parentBuffer.index != kInvalidIndex)
  {
    Buffer* parentBuffer =
        (Buffer*)m_GpuDevice->m_Buffers.accessResource(buffer->parentBuffer.index);
    vkBuffer = parentBuffer->vkBuffer;
    offset = buffer->globalOffset;
  }
  vkCmdBindIndexBuffer(m_VulkanCmdBuffer, vkBuffer, offset, p_IndexType);
}
//---------------------------------------------------------------------------//
void CommandBuffer::bindDescriptorSet(
    DescriptorSetHandle* p_Handles, uint32_t p_NumLists, uint32_t* p_Offsets, uint32_t p_NumOffsets)
{
  // TODO
  uint32_t offsetsCache[8];
  p_NumOffsets = 0;

  for (uint32_t l = 0; l < p_NumLists; ++l)
  {
    DescriptorSet* descriptorSet =
        (DescriptorSet*)m_GpuDevice->m_DescriptorSets.accessResource(p_Handles[l].index);
    m_VulkanDescriptorSets[l] = descriptorSet->vkDescriptorSet;

    // Search for dynamic buffers
    const DescriptorSetLayout* descriptorSetLayout = descriptorSet->layout;
    for (uint32_t i = 0; i < descriptorSetLayout->numBindings; ++i)
    {
      const DescriptorBinding& rb = descriptorSetLayout->bindings[i];

      if (rb.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
      {
        // Search for the actual buffer offset
        const uint32_t resourceIndex = descriptorSet->bindings[i];
        ResourceHandle bufferHandle = descriptorSet->resources[resourceIndex];
        Buffer* buffer = (Buffer*)m_GpuDevice->m_Buffers.accessResource(bufferHandle);

        offsetsCache[p_NumOffsets++] = buffer->globalOffset;
      }
    }
  }

  const uint32_t firstSet = 1;
  vkCmdBindDescriptorSets(
      m_VulkanCmdBuffer,
      m_CurrentPipeline->vkBindPoint,
      m_CurrentPipeline->vkPipelineLayout,
      firstSet,
      p_NumLists,
      m_VulkanDescriptorSets,
      p_NumOffsets,
      offsetsCache);

  if (m_GpuDevice->m_BindlessSupported)
  {
    vkCmdBindDescriptorSets(
        m_VulkanCmdBuffer,
        m_CurrentPipeline->vkBindPoint,
        m_CurrentPipeline->vkPipelineLayout,
        0,
        1,
        &m_GpuDevice->m_VulkanBindlessDescriptorSetCached,
        0,
        nullptr);
  }
}
//---------------------------------------------------------------------------//
void CommandBuffer::setViewport(const Viewport* p_Viewport)
{
  VkViewport viewport{};

  if (p_Viewport)
  {
    viewport.x = p_Viewport->rect.x * 1.f;
    viewport.width = p_Viewport->rect.width * 1.f;
    // Invert Y with negative height and proper offset - Vulkan has unique Clipping Y.
    viewport.y = p_Viewport->rect.height * 1.f - p_Viewport->rect.y;
    viewport.height = -p_Viewport->rect.height * 1.f;
    viewport.minDepth = p_Viewport->minDepth;
    viewport.maxDepth = p_Viewport->maxDepth;
  }
  else
  {
    viewport.x = 0.f;

    if (m_CurrentRenderPass)
    {
      viewport.width = m_CurrentFramebuffer->width * 1.f;
      // Invert Y with negative height and proper offset - Vulkan has unique Clipping Y.
      viewport.y = m_CurrentFramebuffer->height * 1.f;
      viewport.height = -m_CurrentFramebuffer->height * 1.f;
    }
    else
    {
      viewport.width = m_GpuDevice->m_SwapchainWidth * 1.f;
      // Invert Y with negative height and proper offset - Vulkan has unique Clipping Y.
      viewport.y = m_GpuDevice->m_SwapchainHeight * 1.f;
      viewport.height = -m_GpuDevice->m_SwapchainHeight * 1.f;
    }
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
  }

  vkCmdSetViewport(m_VulkanCmdBuffer, 0, 1, &viewport);
}
//---------------------------------------------------------------------------//
void CommandBuffer::setScissor(const Rect2DInt* p_Rect)
{
  VkRect2D scissor;

  if (p_Rect)
  {
    scissor.offset.x = p_Rect->x;
    scissor.offset.y = p_Rect->y;
    scissor.extent.width = p_Rect->width;
    scissor.extent.height = p_Rect->height;
  }
  else
  {
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = m_GpuDevice->m_SwapchainWidth;
    scissor.extent.height = m_GpuDevice->m_SwapchainHeight;
  }

  vkCmdSetScissor(m_VulkanCmdBuffer, 0, 1, &scissor);
}
//---------------------------------------------------------------------------//
void CommandBuffer::draw(
    TopologyType::Enum p_Topology,
    uint32_t p_FirstVertex,
    uint32_t p_VertexCount,
    uint32_t p_FirstInstance,
    uint32_t p_InstanceCount)
{
  vkCmdDraw(m_VulkanCmdBuffer, p_VertexCount, p_InstanceCount, p_FirstVertex, p_FirstInstance);
}
//---------------------------------------------------------------------------//
void CommandBuffer::drawIndexed(
    TopologyType::Enum p_Topology,
    uint32_t p_IndexCount,
    uint32_t p_InstanceCount,
    uint32_t p_FirstIndex,
    int p_VertexOffset,
    uint32_t p_FirstInstance)
{
  vkCmdDrawIndexed(
      m_VulkanCmdBuffer,
      p_IndexCount,
      p_InstanceCount,
      p_FirstIndex,
      p_VertexOffset,
      p_FirstInstance);
}
//---------------------------------------------------------------------------//
void CommandBuffer::drawIndirect(
    BufferHandle p_Handle, uint32_t p_DrawCount, uint32_t p_Offset, uint32_t p_Stride)
{
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void CommandBuffer::drawIndexedIndirect(BufferHandle p_Handle, uint32_t p_Offset, uint32_t p_Stride)
{
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void CommandBuffer::dispatch(uint32_t p_GroupX, uint32_t p_GroupY, uint32_t p_GroupZ)
{
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void CommandBuffer::dispatchIndirect(BufferHandle p_Handle, uint32_t p_Offset)
{
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void CommandBuffer::barrier(const ExecutionBarrier& p_Barrier)
{
  if (m_CurrentRenderPass)
  {
    vkCmdEndRenderPass(m_VulkanCmdBuffer);

    m_CurrentRenderPass = nullptr;
    m_CurrentFramebuffer = nullptr;
  }

  static VkImageMemoryBarrier imageBarriers[8];
  // TODO: subpass
  if (p_Barrier.newBarrierExperimental != UINT_MAX)
  {
    VkPipelineStageFlags sourceStageMask = 0;
    VkPipelineStageFlags destinationStageMask = 0;
    VkAccessFlags sourceAccessFlags = VK_ACCESS_NONE_KHR,
                  destinationAccessFlags = VK_ACCESS_NONE_KHR;

    for (uint32_t i = 0; i < p_Barrier.numImageBarriers; ++i)
    {

      Texture* textureVulkan = (Texture*)m_GpuDevice->m_Textures.accessResource(
          p_Barrier.imageBarriers[i].texture.index);

      VkImageMemoryBarrier& vkBarrier = imageBarriers[i];
      const bool isColor = !TextureFormat::hasDepthOrStencil(textureVulkan->vkFormat);

      {
        VkImageMemoryBarrier* pImageBarrier = &vkBarrier;
        pImageBarrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        pImageBarrier->pNext = NULL;

        ResourceState currentState = p_Barrier.sourcePipelineStage == PipelineStage::kRenderTarget
                                         ? RESOURCE_STATE_RENDER_TARGET
                                         : RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ResourceState nextState = p_Barrier.destinationPipelineStage == PipelineStage::kRenderTarget
                                      ? RESOURCE_STATE_RENDER_TARGET
                                      : RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if (!isColor)
        {
          currentState = p_Barrier.sourcePipelineStage == PipelineStage::kRenderTarget
                             ? RESOURCE_STATE_DEPTH_WRITE
                             : RESOURCE_STATE_DEPTH_READ;
          nextState = p_Barrier.destinationPipelineStage == PipelineStage::kRenderTarget
                          ? RESOURCE_STATE_DEPTH_WRITE
                          : RESOURCE_STATE_DEPTH_READ;
        }

        pImageBarrier->srcAccessMask = utilToVkAccessFlags(currentState);
        pImageBarrier->dstAccessMask = utilToVkAccessFlags(nextState);
        pImageBarrier->oldLayout = utilToVkImageLayout(currentState);
        pImageBarrier->newLayout = utilToVkImageLayout(nextState);

        pImageBarrier->image = textureVulkan->vkImage;
        pImageBarrier->subresourceRange.aspectMask =
            isColor ? VK_IMAGE_ASPECT_COLOR_BIT
                    : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        pImageBarrier->subresourceRange.baseMipLevel = 0;
        pImageBarrier->subresourceRange.levelCount = 1;
        pImageBarrier->subresourceRange.baseArrayLayer = 0;
        pImageBarrier->subresourceRange.layerCount = 1;

        {
          pImageBarrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          pImageBarrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        }

        sourceAccessFlags |= pImageBarrier->srcAccessMask;
        destinationAccessFlags |= pImageBarrier->dstAccessMask;

        textureVulkan->state = nextState;
      }
    }

    static VkBufferMemoryBarrier bufferMemoryBarriers[8];
    for (uint32_t i = 0; i < p_Barrier.numMemoryBarriers; ++i)
    {
      VkBufferMemoryBarrier& vkBarrier = bufferMemoryBarriers[i];
      vkBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;

      Buffer* buffer =
          (Buffer*)m_GpuDevice->m_Buffers.accessResource(p_Barrier.memoryBarriers[i].buffer.index);

      vkBarrier.buffer = buffer->vkBuffer;
      vkBarrier.offset = 0;
      vkBarrier.size = buffer->size;

      ResourceState currentState = toResourceState(p_Barrier.sourcePipelineStage);
      ResourceState nextState = toResourceState(p_Barrier.destinationPipelineStage);
      vkBarrier.srcAccessMask = utilToVkAccessFlags(currentState);
      vkBarrier.dstAccessMask = utilToVkAccessFlags(nextState);

      sourceAccessFlags |= vkBarrier.srcAccessMask;
      destinationAccessFlags |= vkBarrier.dstAccessMask;

      vkBarrier.srcQueueFamilyIndex = 0;
      vkBarrier.dstQueueFamilyIndex = 0;
    }

    sourceStageMask = utilDeterminePipelineStageFlags(
        sourceAccessFlags,
        p_Barrier.sourcePipelineStage == PipelineStage::kComputeShader ? QueueType::kCompute
                                                                       : QueueType::kGraphics);
    destinationStageMask = utilDeterminePipelineStageFlags(
        destinationAccessFlags,
        p_Barrier.destinationPipelineStage == PipelineStage::kComputeShader ? QueueType::kCompute
                                                                            : QueueType::kGraphics);

    vkCmdPipelineBarrier(
        m_VulkanCmdBuffer,
        sourceStageMask,
        destinationStageMask,
        0,
        0,
        nullptr,
        p_Barrier.numMemoryBarriers,
        bufferMemoryBarriers,
        p_Barrier.numImageBarriers,
        imageBarriers);
    return;
  }

  VkImageLayout newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkImageLayout newDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  VkAccessFlags sourceAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
  VkAccessFlags sourceBufferAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
  VkAccessFlags sourceDepthAccessMask =
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  VkAccessFlags destinationAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
  VkAccessFlags destinationBufferAccessMask =
      VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
  VkAccessFlags destinationDepthAccessMask =
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  switch (p_Barrier.destinationPipelineStage)
  {

  case PipelineStage::kFragmentShader: {
    // newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    break;
  }

  case PipelineStage::kComputeShader: {
    newLayout = VK_IMAGE_LAYOUT_GENERAL;

    break;
  }

  case PipelineStage::kRenderTarget: {
    newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    newDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    destinationAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    destinationDepthAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    break;
  }

  case PipelineStage::kDrawIndirect: {
    destinationBufferAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    break;
  }
  }

  switch (p_Barrier.sourcePipelineStage)
  {

  case PipelineStage::kFragmentShader: {
    // sourceAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    break;
  }

  case PipelineStage::kComputeShader: {

    break;
  }

  case PipelineStage::kRenderTarget: {
    sourceAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    sourceDepthAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    break;
  }

  case PipelineStage::kDrawIndirect: {
    sourceBufferAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    break;
  }
  }

  bool hasDepth = false;

  for (uint32_t i = 0; i < p_Barrier.numImageBarriers; ++i)
  {

    Texture* textureVulkan =
        (Texture*)m_GpuDevice->m_Textures.accessResource(p_Barrier.imageBarriers[i].texture.index);

    VkImageMemoryBarrier& vkBarrier = imageBarriers[i];
    vkBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

    vkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    const bool isColor = !TextureFormat::hasDepthOrStencil(textureVulkan->vkFormat);
    hasDepth = hasDepth || !isColor;

    vkBarrier.image = textureVulkan->vkImage;
    vkBarrier.subresourceRange.aspectMask =
        isColor ? VK_IMAGE_ASPECT_COLOR_BIT
                : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    vkBarrier.subresourceRange.baseMipLevel = 0;
    vkBarrier.subresourceRange.levelCount = 1;
    vkBarrier.subresourceRange.baseArrayLayer = 0;
    vkBarrier.subresourceRange.layerCount = 1;

    vkBarrier.oldLayout = utilToVkImageLayout(textureVulkan->state);

    // Transition to...
    vkBarrier.newLayout = isColor ? newLayout : newDepthLayout;

    vkBarrier.srcAccessMask = isColor ? sourceAccessMask : sourceDepthAccessMask;
    vkBarrier.dstAccessMask = isColor ? destinationAccessMask : destinationDepthAccessMask;

    assert(false && "Reimplement!");
    textureVulkan->state = RESOURCE_STATE_GENERIC_READ;
  }

  VkPipelineStageFlags sourceStageMask =
      toVkPipelineStage((PipelineStage::Enum)p_Barrier.sourcePipelineStage);
  VkPipelineStageFlags destinationStageMask =
      toVkPipelineStage((PipelineStage::Enum)p_Barrier.destinationPipelineStage);

  if (hasDepth)
  {
    sourceStageMask |=
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    destinationStageMask |=
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  }

  static VkBufferMemoryBarrier bufferMemoryBarriers[8];
  for (uint32_t i = 0; i < p_Barrier.numMemoryBarriers; ++i)
  {
    VkBufferMemoryBarrier& vkBarrier = bufferMemoryBarriers[i];
    vkBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;

    Buffer* buffer =
        (Buffer*)m_GpuDevice->m_Buffers.accessResource(p_Barrier.memoryBarriers[i].buffer.index);

    vkBarrier.buffer = buffer->vkBuffer;
    vkBarrier.offset = 0;
    vkBarrier.size = buffer->size;
    vkBarrier.srcAccessMask = sourceBufferAccessMask;
    vkBarrier.dstAccessMask = destinationBufferAccessMask;

    vkBarrier.srcQueueFamilyIndex = 0;
    vkBarrier.dstQueueFamilyIndex = 0;
  }

  vkCmdPipelineBarrier(
      m_VulkanCmdBuffer,
      sourceStageMask,
      destinationStageMask,
      0,
      0,
      nullptr,
      p_Barrier.numMemoryBarriers,
      bufferMemoryBarriers,
      p_Barrier.numImageBarriers,
      imageBarriers);
}
//---------------------------------------------------------------------------//
void CommandBuffer::fillBuffer(
    BufferHandle p_Buffer, uint32_t p_Offset, uint32_t p_Size, uint32_t p_Data)
{
  Buffer* vkBuffer = (Buffer*)m_GpuDevice->m_Buffers.accessResource(p_Buffer.index);

  vkCmdFillBuffer(
      m_VulkanCmdBuffer,
      vkBuffer->vkBuffer,
      VkDeviceSize(p_Offset),
      p_Size ? VkDeviceSize(p_Size) : VkDeviceSize(vkBuffer->size),
      p_Data);
}
//---------------------------------------------------------------------------//
DescriptorSetHandle CommandBuffer::createDescriptorSet(const DescriptorSetCreation& creation)
{
  DescriptorSetHandle handle = {m_DescriptorSets.obtainResource()};
  if (handle.index == kInvalidIndex)
  {
    return handle;
  }

  DescriptorSet* descriptorSet = (DescriptorSet*)m_DescriptorSets.accessResource(handle.index);
  const DescriptorSetLayout* descriptorSetLayout =
      (DescriptorSetLayout*)m_GpuDevice->m_DescriptorSetLayouts.accessResource(
          creation.layout.index);

  // Allocate descriptor set
  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = m_VulkanDescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &descriptorSetLayout->vkDescriptorSetLayout;

  VkResult result = vkAllocateDescriptorSets(
      m_GpuDevice->m_VulkanDevice, &allocInfo, &descriptorSet->vkDescriptorSet);
  assert(result == VK_SUCCESS);

  // Cache data
  uint8_t* memory = FRAMEWORK_ALLOCAM(
      (sizeof(ResourceHandle) + sizeof(SamplerHandle) + sizeof(uint16_t)) * creation.numResources,
      m_GpuDevice->m_Allocator);
  descriptorSet->resources = (ResourceHandle*)memory;
  descriptorSet->samplers =
      (SamplerHandle*)(memory + sizeof(ResourceHandle) * creation.numResources);
  descriptorSet->bindings =
      (uint16_t*)(memory + (sizeof(ResourceHandle) + sizeof(SamplerHandle)) * creation.numResources);
  descriptorSet->numResources = creation.numResources;
  descriptorSet->layout = descriptorSetLayout;

  // Update descriptor set
  VkWriteDescriptorSet descriptorWrite[8];
  VkDescriptorBufferInfo bufferInfo[8];
  VkDescriptorImageInfo imageInfo[8];

  Sampler* vkDefaultSampler =
      (Sampler*)m_GpuDevice->m_Samplers.accessResource(m_GpuDevice->m_DefaultSampler.index);

  uint32_t numResources = creation.numResources;
  GpuDevice::fillWriteDescriptorSets(
      *m_GpuDevice,
      descriptorSetLayout,
      descriptorSet->vkDescriptorSet,
      descriptorWrite,
      bufferInfo,
      imageInfo,
      vkDefaultSampler->vkSampler,
      numResources,
      creation.resources,
      creation.samplers,
      creation.bindings);

  // Cache resources
  for (uint32_t r = 0; r < creation.numResources; r++)
  {
    descriptorSet->resources[r] = creation.resources[r];
    descriptorSet->samplers[r] = creation.samplers[r];
    descriptorSet->bindings[r] = creation.bindings[r];
  }

  vkUpdateDescriptorSets(m_GpuDevice->m_VulkanDevice, numResources, descriptorWrite, 0, nullptr);

  return handle;
}
//---------------------------------------------------------------------------//
void CommandBuffer::begin()
{
  if (!m_IsRecording)
  {
    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_VulkanCmdBuffer, &beginInfo);

    m_IsRecording = true;
  }
}
//---------------------------------------------------------------------------//
void CommandBuffer::beginSecondary(RenderPass* p_CurrRenderPass, Framebuffer* p_CurrentFramebuffer)
{
  if (!m_IsRecording)
  {
    VkCommandBufferInheritanceInfo inheritance{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    inheritance.renderPass = p_CurrRenderPass->vkRenderPass;
    inheritance.subpass = 0;
    inheritance.framebuffer = p_CurrentFramebuffer->vkFramebuffer;

    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                      VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    beginInfo.pInheritanceInfo = &inheritance;

    vkBeginCommandBuffer(m_VulkanCmdBuffer, &beginInfo);

    m_IsRecording = true;

    m_CurrentRenderPass = p_CurrRenderPass;
  }
}
//---------------------------------------------------------------------------//
void CommandBuffer::end()
{
  if (m_IsRecording)
  {
    vkEndCommandBuffer(m_VulkanCmdBuffer);
    m_IsRecording = false;
  }
}
//---------------------------------------------------------------------------//
void CommandBuffer::endCurrentRenderPass()
{
  if (m_IsRecording && m_CurrentRenderPass != nullptr)
  {
    if (m_GpuDevice->m_DynamicRenderingExtensionPresent)
    {
      m_GpuDevice->m_CmdEndRendering(m_VulkanCmdBuffer);
    }
    else
    {
      vkEndCommandBuffer(m_VulkanCmdBuffer);
    }
    m_CurrentRenderPass = nullptr;
  }
}
//---------------------------------------------------------------------------//
void CommandBuffer::uploadTextureData(
    TextureHandle p_Texture,
    void* p_TextureData,
    BufferHandle p_StagingBuffer,
    size_t p_StagingBufferOffset)
{
  Texture* texture = static_cast<Texture*>(m_GpuDevice->m_Textures.accessResource(p_Texture.index));
  Buffer* stagingBuffer =
      static_cast<Buffer*>(m_GpuDevice->m_Buffers.accessResource(p_StagingBuffer.index));
  uint32_t imageSize = texture->width * texture->height * 4;

  // Copy buffer_data to staging buffer
  memcpy(
      stagingBuffer->mappedData + p_StagingBufferOffset,
      p_TextureData,
      static_cast<size_t>(imageSize));

  VkBufferImageCopy region = {};
  region.bufferOffset = p_StagingBufferOffset;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;

  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;

  region.imageOffset = {0, 0, 0};
  region.imageExtent = {texture->width, texture->height, texture->depth};

  // Pre copy memory barrier to perform layout transition
  utilAddImageBarrier(
      m_GpuDevice, m_VulkanCmdBuffer, texture, RESOURCE_STATE_COPY_DEST, 0, 1, false);
  // Copy from the staging buffer to the image
  vkCmdCopyBufferToImage(
      m_VulkanCmdBuffer,
      stagingBuffer->vkBuffer,
      texture->vkImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1,
      &region);

  // Post copy memory barrier
  utilAddImageBarrierExt(
      m_GpuDevice,
      m_VulkanCmdBuffer,
      texture,
      RESOURCE_STATE_COPY_DEST,
      0,
      1,
      false,
      m_GpuDevice->m_VulkanTransferQueueFamily,
      m_GpuDevice->m_VulkanMainQueueFamily,
      QueueType::kCopyTransfer,
      QueueType::kGraphics);
}
//---------------------------------------------------------------------------//
void CommandBuffer::copyTexture(TextureHandle p_Src, TextureHandle p_Dst, ResourceState p_DstState)
{
  Texture* src = (Texture*)m_GpuDevice->m_Textures.accessResource(p_Src.index);
  Texture* dst = (Texture*)m_GpuDevice->m_Textures.accessResource(p_Dst.index);

  VkImageCopy region = {};
  region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.srcSubresource.mipLevel = 0;
  region.srcSubresource.baseArrayLayer = 0;
  region.srcSubresource.layerCount = 1;

  region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.dstSubresource.mipLevel = 0;
  region.dstSubresource.baseArrayLayer = 0;
  region.dstSubresource.layerCount = 1;

  region.dstOffset = {0, 0, 0};
  region.extent = {src->width, src->height, src->depth};

  // Copy from the staging buffer to the image
  utilAddImageBarrier(m_GpuDevice, m_VulkanCmdBuffer, src, RESOURCE_STATE_COPY_SOURCE, 0, 1, false);
  ResourceState oldState = dst->state;
  utilAddImageBarrier(m_GpuDevice, m_VulkanCmdBuffer, dst, RESOURCE_STATE_COPY_DEST, 0, 1, false);

  vkCmdCopyImage(
      m_VulkanCmdBuffer,
      src->vkImage,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      dst->vkImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1,
      &region);

  // Prepare first mip to create lower mipmaps
  if (dst->mipmaps > 1)
  {
    utilAddImageBarrier(
        m_GpuDevice, m_VulkanCmdBuffer, dst, RESOURCE_STATE_COPY_SOURCE, 0, 1, false);
  }

  int w = dst->width;
  int h = dst->height;

  for (int mipIndex = 1; mipIndex < dst->mipmaps; ++mipIndex)
  {
    utilAddImageBarrier(
        m_GpuDevice,
        m_VulkanCmdBuffer,
        dst->vkImage,
        oldState,
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
        m_VulkanCmdBuffer,
        dst->vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst->vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blitRegion,
        VK_FILTER_LINEAR);

    // Prepare current mip for next level
    utilAddImageBarrier(
        m_GpuDevice,
        m_VulkanCmdBuffer,
        dst->vkImage,
        RESOURCE_STATE_COPY_DEST,
        RESOURCE_STATE_COPY_SOURCE,
        mipIndex,
        1,
        false);
  }

  // Transition
  utilAddImageBarrier(m_GpuDevice, m_VulkanCmdBuffer, dst, p_DstState, 0, dst->mipmaps, false);
}
//---------------------------------------------------------------------------//
void CommandBuffer::uploadBufferData(
    BufferHandle p_Buffer,
    void* p_BufferData,
    BufferHandle p_StagingBuffer,
    size_t p_StagingBufferOffset)
{
  Buffer* buffer = static_cast<Buffer*>(m_GpuDevice->m_Buffers.accessResource(p_Buffer.index));
  Buffer* stagingBuffer =
      static_cast<Buffer*>(m_GpuDevice->m_Buffers.accessResource(p_StagingBuffer.index));
  uint32_t copySize = buffer->size;

  // Copy buffer_data to staging buffer
  memcpy(
      stagingBuffer->mappedData + p_StagingBufferOffset,
      p_BufferData,
      static_cast<size_t>(copySize));

  VkBufferCopy region{};
  region.srcOffset = p_StagingBufferOffset;
  region.dstOffset = 0;
  region.size = copySize;

  vkCmdCopyBuffer(m_VulkanCmdBuffer, stagingBuffer->vkBuffer, buffer->vkBuffer, 1, &region);

  utilAddBufferBarrierExt(
      m_GpuDevice,
      m_VulkanCmdBuffer,
      buffer->vkBuffer,
      RESOURCE_STATE_COPY_DEST,
      RESOURCE_STATE_UNDEFINED,
      copySize,
      m_GpuDevice->m_VulkanTransferQueueFamily,
      m_GpuDevice->m_VulkanMainQueueFamily,
      QueueType::kCopyTransfer,
      QueueType::kGraphics);
}
//---------------------------------------------------------------------------//
void CommandBuffer::uploadBufferData(BufferHandle p_Src, BufferHandle p_Dst)
{
  Buffer* src = static_cast<Buffer*>(m_GpuDevice->m_Buffers.accessResource(p_Src.index));
  Buffer* dst = static_cast<Buffer*>(m_GpuDevice->m_Buffers.accessResource(p_Dst.index));

  assert(src->size == dst->size);

  uint32_t copySize = src->size;

  VkBufferCopy region{};
  region.srcOffset = 0;
  region.dstOffset = 0;
  region.size = copySize;

  vkCmdCopyBuffer(m_VulkanCmdBuffer, src->vkBuffer, dst->vkBuffer, 1, &region);
}
//---------------------------------------------------------------------------//
void CommandBufferManager::init(GpuDevice* p_GpuDevice, uint32_t p_NumThreads)
{
  m_GpuDevice = p_GpuDevice;
  m_NumPoolsPerFrame = p_NumThreads;

  // Create pools: num frames * num threads;
  const uint32_t totalPools = m_NumPoolsPerFrame * kMaxFrames;
  // Init per thread-frame used buffers
  m_UsedBuffers.init(m_GpuDevice->m_Allocator, totalPools, totalPools);
  m_UsedSecondaryCommandBuffers.init(m_GpuDevice->m_Allocator, totalPools, totalPools);

  for (uint32_t i = 0; i < totalPools; i++)
  {
    m_UsedBuffers[i] = 0;
    m_UsedSecondaryCommandBuffers[i] = 0;
  }

  // Create command buffers: pools * buffers per pool
  const uint32_t totalBuffers = totalPools * m_NumCommandBuffersPerThread;
  m_CommandBuffers.init(m_GpuDevice->m_Allocator, totalBuffers, totalBuffers);

  const uint32_t totalSecondaryBuffers = totalPools * g_SecondaryCommandBuffersCount;
  m_SecondaryCommandBuffers.init(m_GpuDevice->m_Allocator, totalSecondaryBuffers);

  const uint32_t totalComputeBuffers = kMaxFrames;
  m_ComputeCommandBuffers.init(m_GpuDevice->m_Allocator, kMaxFrames, kMaxFrames);

  for (uint32_t i = 0; i < totalBuffers; i++)
  {
    VkCommandBufferAllocateInfo cmd = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};

    const uint32_t frameIndex = i / (m_NumCommandBuffersPerThread * m_NumPoolsPerFrame);
    const uint32_t threadIndex = (i / m_NumCommandBuffersPerThread) % m_NumPoolsPerFrame;
    const uint32_t poolIndex = poolFromIndices(frameIndex, threadIndex);
    // printf( "Indices i:%u f:%u t:%u p:%u\n", i, frameIndex, threadIndex, poolIndex );
    cmd.commandPool = m_GpuDevice->m_ThreadFramePools[poolIndex].vulkanCommandPool;
    cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd.commandBufferCount = 1;

    CommandBuffer& currentCommandBuffer = m_CommandBuffers[i];
    vkAllocateCommandBuffers(
        m_GpuDevice->m_VulkanDevice, &cmd, &currentCommandBuffer.m_VulkanCmdBuffer);

    // TODO: move to have a ring per queue per thread
    currentCommandBuffer.m_Handle = i;
    currentCommandBuffer.m_ThreadFramePool = &m_GpuDevice->m_ThreadFramePools[poolIndex];
    currentCommandBuffer.init(m_GpuDevice);
  }

  uint32_t handle = totalBuffers;
  for (uint32_t poolIndex = 0; poolIndex < totalPools; ++poolIndex)
  {
    VkCommandBufferAllocateInfo cmd = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};

    cmd.commandPool = m_GpuDevice->m_ThreadFramePools[poolIndex].vulkanCommandPool;
    cmd.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    cmd.commandBufferCount = g_SecondaryCommandBuffersCount;

    VkCommandBuffer secondaryBuffers[g_SecondaryCommandBuffersCount];
    vkAllocateCommandBuffers(m_GpuDevice->m_VulkanDevice, &cmd, secondaryBuffers);

    for (uint32_t cmdIndex = 0; cmdIndex < g_SecondaryCommandBuffersCount; ++cmdIndex)
    {
      CommandBuffer cmdBuf{};
      cmdBuf.m_VulkanCmdBuffer = secondaryBuffers[cmdIndex];

      cmdBuf.m_Handle = handle++;
      cmdBuf.m_ThreadFramePool = &m_GpuDevice->m_ThreadFramePools[poolIndex];
      cmdBuf.init(m_GpuDevice);

      // NOTE: access to the descriptor pool has to be synchronized
      // across threads. Don't allow for now
      m_SecondaryCommandBuffers.push(cmdBuf);
    }
  }

  for (uint32_t i = 0; i < totalComputeBuffers; i++)
  {
    VkCommandBufferAllocateInfo cmd = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};

    cmd.commandPool = m_GpuDevice->m_ComputeFramePools[i].vulkanCommandPool;
    cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd.commandBufferCount = 1;

    CommandBuffer& currentCommandBuffer = m_ComputeCommandBuffers[i];
    vkAllocateCommandBuffers(
        m_GpuDevice->m_VulkanDevice, &cmd, &currentCommandBuffer.m_VulkanCmdBuffer);

    currentCommandBuffer.m_Handle = i;
    currentCommandBuffer.m_ThreadFramePool = &m_GpuDevice->m_ComputeFramePools[i];
    currentCommandBuffer.init(m_GpuDevice);
  }

  // printf( "Done\n" );
}
//---------------------------------------------------------------------------//
void CommandBufferManager::shutdown()
{
  for (uint32_t i = 0; i < m_CommandBuffers.m_Size; i++)
  {
    m_CommandBuffers[i].shutdown();
  }

  for (uint32_t i = 0; i < m_SecondaryCommandBuffers.m_Size; ++i)
  {
    m_SecondaryCommandBuffers[i].shutdown();
  }

  for (uint32_t i = 0; i < m_ComputeCommandBuffers.m_Size; ++i)
  {
    m_ComputeCommandBuffers[i].shutdown();
  }

  m_SecondaryCommandBuffers.shutdown();
  m_ComputeCommandBuffers.shutdown();
  m_CommandBuffers.shutdown();
  m_UsedBuffers.shutdown();
  m_UsedSecondaryCommandBuffers.shutdown();
}
//---------------------------------------------------------------------------//
void CommandBufferManager::resetPools(uint32_t p_FrameIndex)
{

  for (uint32_t i = 0; i < m_NumPoolsPerFrame; i++)
  {
    const uint32_t poolIndex = poolFromIndices(p_FrameIndex, i);
    vkResetCommandPool(
        m_GpuDevice->m_VulkanDevice,
        m_GpuDevice->m_ThreadFramePools[poolIndex].vulkanCommandPool,
        0);

    m_UsedBuffers[poolIndex] = 0;
    m_UsedSecondaryCommandBuffers[poolIndex] = 0;
  }
}
//---------------------------------------------------------------------------//
CommandBuffer* CommandBufferManager::getCommandBuffer(
    uint32_t p_Frame, uint32_t p_ThreadIndex, bool p_Begin, bool p_Compute)
{
  CommandBuffer* cb = nullptr;

  if (p_Compute)
  {
    assert(p_ThreadIndex == 0);
    cb = &m_ComputeCommandBuffers[p_Frame];
  }
  else
  {
    const uint32_t poolIndex = poolFromIndices(p_Frame, p_ThreadIndex);
    uint32_t currentUsedBuffer = m_UsedBuffers[poolIndex];
    // TODO: how to handle fire-and-forget command buffers ?
    assert(currentUsedBuffer < m_NumCommandBuffersPerThread);
    if (p_Begin)
    {
      m_UsedBuffers[poolIndex] = currentUsedBuffer + 1;
    }

    cb = &m_CommandBuffers[(poolIndex * m_NumCommandBuffersPerThread) + currentUsedBuffer];
  }

  if (p_Begin)
  {
    cb->reset();
    cb->begin();

    // Timestamp queries
    GpuThreadFramePools* threadPools = cb->m_ThreadFramePool;

    if (!p_Compute)
    {
      // Pipeline statistics
      // TODO...
    }
  }

  return cb;
}
//---------------------------------------------------------------------------//
CommandBuffer*
CommandBufferManager::getSecondaryCommandBuffer(uint32_t p_Frame, uint32_t p_ThreadIndex)
{
  const uint32_t poolIndex = poolFromIndices(p_Frame, p_ThreadIndex);
  uint32_t currentUsedBuffer = m_UsedSecondaryCommandBuffers[poolIndex];
  m_UsedSecondaryCommandBuffers[poolIndex] = currentUsedBuffer + 1;

  assert(currentUsedBuffer < g_SecondaryCommandBuffersCount);

  CommandBuffer* cmdBuf =
      &m_SecondaryCommandBuffers[(poolIndex * g_SecondaryCommandBuffersCount) + currentUsedBuffer];
  return cmdBuf;
}
//---------------------------------------------------------------------------//
uint32_t CommandBufferManager::poolFromIndices(uint32_t p_FrameIndex, uint32_t p_ThreadIndex)
{
  return (p_FrameIndex * m_NumPoolsPerFrame) + p_ThreadIndex;
}
//---------------------------------------------------------------------------//
} // namespace Graphics
