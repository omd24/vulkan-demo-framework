#include "CommandBuffer.hpp"

namespace Graphics
{
//---------------------------------------------------------------------------//
void CommandBuffer::init(QueueType::Enum p_Type, uint32_t p_BufferSize, uint32_t p_SubmitSize)
{
  this->m_Type = p_Type;
  this->m_BufferSize = p_BufferSize;

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
  poolCi.maxSets = kGlobalPoolElements * arrayCount32(poolSizes);
  poolCi.poolSizeCount = arrayCount32(poolSizes);
  poolCi.pPoolSizes = poolSizes;
  VkResult result = vkCreateDescriptorPool(
      m_GpuDevice->m_VulkanDevice,
      &poolCi,
      m_GpuDevice->m_VulkanAllocCallbacks,
      &m_VulkanDescriptorPool);
  assert(result == VK_SUCCESS);

  m_DescriptorSets.init(m_GpuDevice->m_Allocator, 256, sizeof(DesciptorSet));

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
  m_CurrentPipeline = nullptr;
  m_CurrentCommand = 0;

  vkResetDescriptorPool(m_GpuDevice->m_VulkanDevice, m_VulkanDescriptorPool, 0);

  uint32_t resourceCount = m_DescriptorSets.m_FreeIndicesHead;
  for (uint32_t i = 0; i < resourceCount; ++i)
  {
    DesciptorSet* descriptorSet = (DesciptorSet*)m_DescriptorSets.accessResource(i);

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
    DesciptorSet* descriptorSet =
        (DesciptorSet*)m_DescriptorSets.accessResource(p_Handles[l].index);
    m_VulkanDescriptorSets[l] = descriptorSet->vkDescriptorSet;

    // Search for dynamic buffers
    const DesciptorSetLayout* descriptorSetLayout = descriptorSet->layout;
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
        &m_GpuDevice->m_VulkanBindlessDescriptorSet,
        0,
        nullptr);
  }
}
//---------------------------------------------------------------------------//
void CommandBuffer::bindPass(RenderPassHandle p_Passhandle)
{
  m_IsRecording = true;

  RenderPass* renderPass =
      (RenderPass*)m_GpuDevice->m_RenderPasses.accessResource(p_Passhandle.index);

  // Begin/End render pass are valid only for graphics render passes.
  if (m_CurrentRenderPass && (m_CurrentRenderPass->type != RenderPassType::kCompute) &&
      (renderPass != m_CurrentRenderPass))
  {
    vkCmdEndRenderPass(m_VulkanCmdBuffer);
  }

  if (renderPass != m_CurrentRenderPass && (renderPass->type != RenderPassType::kCompute))
  {
    VkRenderPassBeginInfo renderPassBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBegin.framebuffer =
        renderPass->type == RenderPassType::kSwapchain
            ? m_GpuDevice->m_VulkanSwapchainFramebuffers[m_GpuDevice->m_VulkanImageIndex]
            : renderPass->vkFrameBuffer;
    renderPassBegin.renderPass = renderPass->vkRenderPass;

    renderPassBegin.renderArea.offset = {0, 0};
    renderPassBegin.renderArea.extent = {renderPass->width, renderPass->height};

    // TODO: this breaks?
    renderPassBegin.clearValueCount = 2;
    renderPassBegin.pClearValues = m_Clears;

    vkCmdBeginRenderPass(m_VulkanCmdBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
  }

  // Cache render pass
  m_CurrentRenderPass = renderPass;
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
void CommandBuffer::bindIndexBuffer(BufferHandle p_Handle, uint32_t p_Offset)
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
  vkCmdBindIndexBuffer(m_VulkanCmdBuffer, vkBuffer, offset, VkIndexType::VK_INDEX_TYPE_UINT16);
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
    DesciptorSet* descriptorSet =
        (DesciptorSet*)m_GpuDevice->m_DescriptorSets.accessResource(p_Handles[l].index);
    m_VulkanDescriptorSets[l] = descriptorSet->vkDescriptorSet;

    // Search for dynamic buffers
    const DesciptorSetLayout* descriptorSetLayout = descriptorSet->layout;
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

  const uint32_t firstSet = 0;
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
        1,
        1,
        &m_GpuDevice->m_VulkanBindlessDescriptorSet,
        0,
        nullptr);
  }
}
//---------------------------------------------------------------------------//
void CommandBuffer::setViewport(const Viewport* p_Viewport)
{
  VkViewport viewport;

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
      viewport.width = m_CurrentRenderPass->width * 1.f;
      // Invert Y with negative height and proper offset - Vulkan has unique Clipping Y.
      viewport.y = m_CurrentRenderPass->height * 1.f;
      viewport.height = -m_CurrentRenderPass->height * 1.f;
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
DescriptorSetHandle CommandBuffer::createDescriptorSet(const DescriptorSetCreation& creation)
{
  DescriptorSetHandle handle = {m_DescriptorSets.obtainResource()};
  if (handle.index == kInvalidIndex)
  {
    return handle;
  }

  DesciptorSet* descriptorSet = (DesciptorSet*)m_DescriptorSets.accessResource(handle.index);
  const DesciptorSetLayout* descriptorSetLayout =
      (DesciptorSetLayout*)m_GpuDevice->m_DescriptorSetLayouts.accessResource(
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
} // namespace Graphics
