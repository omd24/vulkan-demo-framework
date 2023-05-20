#pragma once

#include "Graphics/GpuDevice.hpp"

namespace Graphics
{
//---------------------------------------------------------------------------//
struct CommandBuffer
{
  void init(Graphics::GpuDevice* p_GpuDevice);
  void shutdown();
  void reset();

  DescriptorSetHandle createDescriptorSet(const DescriptorSetCreation& p_Creation);

  void begin();
  void beginSecondary(RenderPass* p_CurrRenderPass, Framebuffer* p_CurrentFramebuffer);
  void end();
  void endCurrentRenderPass();

  void
  bindPass(RenderPassHandle p_Passhandle, FramebufferHandle p_Framebuffer, bool p_UseSecondary);
  void bindPipeline(PipelineHandle p_Handle);
  void bindVertexBuffer(BufferHandle p_Handle, uint32_t p_Binding, uint32_t p_Offset);
  void bindIndexBuffer(BufferHandle p_Handle, uint32_t p_Offset, VkIndexType p_IndexType);
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

  void clear(float p_Red, float p_Green, float p_Blue, float p_Alpha)
  {
    m_Clears[0].color = {p_Red, p_Green, p_Blue, p_Alpha};
  }

  void clearDepthStencil(float p_Depth, uint8_t p_Value)
  {
    m_Clears[1].depthStencil.depth = p_Depth;
    m_Clears[1].depthStencil.stencil = p_Value;
  }

  // Draw methods:
  void draw(
      TopologyType::Enum p_Topology,
      uint32_t p_FirstVertex,
      uint32_t p_VertexCount,
      uint32_t p_FirstInstance,
      uint32_t p_InstanceCount);
  void drawIndexed(
      TopologyType::Enum p_Topology,
      uint32_t p_IndexCount,
      uint32_t p_InstanceCount,
      uint32_t p_FirstIndex,
      int p_VertexOffset,
      uint32_t p_FirstInstance);

  void drawIndirect(BufferHandle p_Handle, uint32_t p_Offset, uint32_t p_Stride);
  void drawIndexedIndirect(BufferHandle p_Handle, uint32_t p_Offset, uint32_t p_Stride);

  void dispatch(uint32_t p_GroupX, uint32_t p_GroupY, uint32_t p_GroupZ);
  void dispatchIndirect(BufferHandle p_Handle, uint32_t p_Offset);

  void barrier(const ExecutionBarrier& p_Barrier);

  void fillBuffer(BufferHandle p_Buffer, uint32_t p_Offset, uint32_t p_Size, uint32_t p_Data);

  // Non-drawing methods
  void uploadTextureData(
      TextureHandle p_Texture,
      void* p_TextureData,
      BufferHandle p_StagingBuffer,
      size_t p_StagingBufferOffset);
  void copyTexture(
      TextureHandle p_Src, ResourceState p_SrcState, TextureHandle p_Dst, ResourceState p_DstState);

  void uploadBufferData(
      BufferHandle p_Buffer,
      void* p_BufferData,
      BufferHandle p_StagingBuffer,
      size_t p_StagingBufferOffset);
  void uploadBufferData(BufferHandle p_Src, BufferHandle p_Dst);

  VkCommandBuffer m_VulkanCmdBuffer;
  GpuDevice* m_GpuDevice;
  VkDescriptorSet m_VulkanDescriptorSets[16];

  RenderPass* m_CurrentRenderPass;
  Framebuffer* m_CurrentFramebuffer;
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
struct CommandBufferManager
{
  void init(GpuDevice* p_GpuDev, uint32_t p_NumThreads);
  void shutdown();

  void resetPools(uint32_t p_FrameIndex);

  CommandBuffer* getCommandBuffer(uint32_t p_Frame, uint32_t p_ThreadIndex, bool p_Begin);
  CommandBuffer* getSecondaryCommandBuffer(uint32_t p_Frame, uint32_t p_ThreadIndex);

  uint16_t poolFromIndex(uint32_t p_Index) { return (uint16_t)p_Index / m_NumPoolsPerFrame; }
  uint32_t poolFromIndices(uint32_t p_FrameIndex, uint32_t thread_index);

  Framework::Array<VkCommandPool> m_VulkanCommandPools;
  Framework::Array<CommandBuffer> m_CommandBuffers;
  Framework::Array<CommandBuffer> m_SecondaryCommandBuffers;
  Framework::Array<uint8_t> m_UsedBuffers; // Track how many buffers were used per thread per frame.
  Framework::Array<uint8_t> m_UsedSecondaryCommandBuffers;

  GpuDevice* m_GpuDevice = nullptr;
  uint32_t m_NumPoolsPerFrame = 0;
  uint32_t m_NumCommandBuffersPerThread = 3;
};
//---------------------------------------------------------------------------//
} // namespace Graphics
