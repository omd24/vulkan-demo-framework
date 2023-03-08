#include "CommandBuffer.hpp"

namespace Graphics
{
//---------------------------------------------------------------------------//
void CommandBuffer::init(QueueType::Enum p_Type, uint32_t p_BufferSize, uint32_t p_SubmitSize)
{
  this->m_Type = p_Type;
  this->m_BufferSize = p_BufferSize;

  static const u32 k_global_pool_elements = 128;
  VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, k_global_pool_elements},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, k_global_pool_elements},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, k_global_pool_elements},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, k_global_pool_elements},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, k_global_pool_elements},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, k_global_pool_elements},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, k_global_pool_elements},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, k_global_pool_elements},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, k_global_pool_elements},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, k_global_pool_elements},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, k_global_pool_elements}};
  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = k_global_pool_elements * ArraySize(pool_sizes);
  pool_info.poolSizeCount = (u32)ArraySize(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;
  VkResult result = vkCreateDescriptorPool(
      gpu_device->vulkan_device,
      &pool_info,
      gpu_device->vulkan_allocation_callbacks,
      &vk_descriptor_pool);
  RASSERT(result == VK_SUCCESS);

  descriptor_sets.init(gpu_device->allocator, 256, sizeof(DesciptorSet));

  reset();
}
//---------------------------------------------------------------------------//
void CommandBuffer::shutdown()
{
  m_IsRecording = false;
  reset();

  m_descriptorSet.shutdown

  vkDestroyDescriptorPool()
}
//---------------------------------------------------------------------------//
void CommandBuffer::reset()
{
  m_IsRecording = false;
  m_CurrentRenderPass = nullptr;
  m_CurrentPipeline = nullptr;
  m_CurrentCommand = 0;

  vkResetDescriptorPool(gpu_device->vulkan_device, vk_descriptor_pool, 0);

  u32 resource_count = descriptor_sets.free_indices_head;
  for (u32 i = 0; i < resource_count; ++i)
  {
    DesciptorSet* v_descriptor_set = (DesciptorSet*)descriptor_sets.access_resource(i);

    if (v_descriptor_set)
    {
      // Contains the allocation for all the resources, binding and samplers arrays.
      rfree(v_descriptor_set->resources, gpu_device->allocator);
    }
    descriptor_sets.release_resource(i);
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

  if (gpu_device->bindless_supported)
  {
    vkCmdBindDescriptorSets(
        vk_command_buffer,
        current_pipeline->vk_bind_point,
        current_pipeline->vk_pipeline_layout,
        1,
        1,
        &gpu_device->vulkan_bindless_descriptor_set,
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
} // namespace Graphics
