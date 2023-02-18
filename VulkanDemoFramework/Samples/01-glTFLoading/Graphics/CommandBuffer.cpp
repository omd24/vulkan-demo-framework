#include "CommandBuffer.hpp"

namespace Graphics
{
//---------------------------------------------------------------------------//
void CommandBuffer::init(QueueType::Enum p_Type, uint32_t p_BufferSize, uint32_t p_SubmitSize)
{
  this->m_Type = p_Type;
  this->m_BufferSize = p_BufferSize;

  reset();
}
//---------------------------------------------------------------------------//
void CommandBuffer::terminate() { m_IsRecording = false; }
//---------------------------------------------------------------------------//
void CommandBuffer::reset()
{
  m_IsRecording = false;
  m_CurrentRenderPass = nullptr;
  m_CurrentPipeline = nullptr;
  m_CurrentCommand = 0;
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
} // namespace Graphics
