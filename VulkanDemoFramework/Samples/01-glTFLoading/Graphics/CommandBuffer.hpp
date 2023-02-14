#pragma once

#include "GpuDevice.hpp"

namespace Graphics
{
struct CommandBuffer
{
  void init(QueueType::Enum p_Type, uint32_t p_BufferSize, uint32_t p_SubmitSize);
  void terminate();
  void reset();

  VkCommandBuffer m_VulkanCmdBuffer;
  GpuDevice* m_GpuDevice;
  VkCopyDescriptorSet m_VulkanDescriptorSets[16];

  RenderPass* m_CurrentRenderPass;
  Pipeline* m_CurrentPipeline;
  VkClearValue m_Clears[2]; // 0 = Color, 1 = Depth
  bool m_IsRecording;

  uint32_t m_Handle;

  uint32_t m_CurrentCommand;
  ResourceHandle m_ResourceHandle;
  QueueType::Enum m_Type = QueueType::kGraphics;
  uint32_t m_BufferSize = 0;
};
} // namespace Graphics
