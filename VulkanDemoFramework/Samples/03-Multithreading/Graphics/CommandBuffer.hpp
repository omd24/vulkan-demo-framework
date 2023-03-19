#pragma once

#include "Graphics/GpuDevice.hpp"

namespace Graphics
{
//---------------------------------------------------------------------------//
struct CommandBuffer
{
  void init(QueueType::Enum p_Type, uint32_t p_BufferSize, uint32_t p_SubmitSize);
  void shutdown();
  void reset();

  DescriptorSetHandle createDescriptorSet(const DescriptorSetCreation& p_Creation);

  void bindPass(RenderPassHandle p_Passhandle);
  void bindPipeline(PipelineHandle p_Handle);
  void bindVertexBuffer(BufferHandle p_Handle, uint32_t p_Binding, uint32_t p_Offset);
  void bindIndexBuffer(BufferHandle p_Handle, uint32_t p_Offset);
  void bindDescriptorSet(
      DescriptorSetHandle* p_Handles,
      uint32_t p_NumLists,
      uint32_t* p_Offsets,
      uint32_t p_NumOffsets);
  void bindLocalDescriptorSet(
      DescriptorSetHandle* p_Handles,
      uint32_t p_NumLists,
      uint32_t* p_Offsets,
      uint32_t p_NumOffsets);

  void setViewport(const Viewport* p_Viewport);
  void setScissor(const Rect2DInt* p_Rect);

  void drawIndexed(
      TopologyType::Enum p_Topology,
      uint32_t p_IndexCount,
      uint32_t p_InstanceCount,
      uint32_t p_FirstIndex,
      int p_VertexOffset,
      uint32_t p_FirstInstance);

  void clear(float p_Red, float p_Green, float p_Blue, float p_Alpha)
  {
    m_Clears[0].color = {p_Red, p_Green, p_Blue, p_Alpha};
  }

  void clearDepthStencil(float p_Depth, uint8_t p_Value)
  {
    m_Clears[1].depthStencil.depth = p_Depth;
    m_Clears[1].depthStencil.stencil = p_Value;
  }

  VkCommandBuffer m_VulkanCmdBuffer;
  GpuDevice* m_GpuDevice;
  VkDescriptorSet m_VulkanDescriptorSets[16];

  RenderPass* m_CurrentRenderPass;
  Pipeline* m_CurrentPipeline;
  VkClearValue m_Clears[2]; // 0 = Color, 1 = Depth
  bool m_IsRecording;

  uint32_t m_Handle;

  uint32_t m_CurrentCommand;
  ResourceHandle m_ResourceHandle;
  QueueType::Enum m_Type = QueueType::kGraphics;
  uint32_t m_BufferSize = 0;

  VkDescriptorPool m_VulkanDescriptorPool;
  Framework::ResourcePool m_DescriptorSets;
};
//---------------------------------------------------------------------------//
} // namespace Graphics
