#pragma once

#include "Graphics/GpuResources.hpp"
#include "Graphics/GpuDevice.hpp"

#include "Foundation/ResourceManager.hpp"

#include <mutex>

namespace Graphics
{
//---------------------------------------------------------------------------//
namespace RendererUtil
{
//---------------------------------------------------------------------------//
struct BufferResource : public Framework::Resource
{

  BufferHandle m_Handle;
  uint32_t m_PoolIndex;
  BufferDescription m_Desc;

  static constexpr const char* ms_TypeName = "Renderer buffer type";
  static uint64_t ms_TypeHash;
};
//---------------------------------------------------------------------------//
struct TextureResource : public Framework::Resource
{

  TextureHandle m_Handle;
  uint32_t m_PoolIndex;
  TextureDescription m_Desc;

  static constexpr const char* ms_TypeName = "Renderer texture type";
  static uint64_t ms_TypeHash;
};
//---------------------------------------------------------------------------//
struct SamplerResource : public Framework::Resource
{

  SamplerHandle m_Handle;
  uint32_t m_PoolIndex;
  SamplerDescription m_Desc;

  static constexpr const char* ms_TypeName = "Renderer sampler type";
  static uint64_t ms_TypeHash;
};
//---------------------------------------------------------------------------//
// Material and Shaders
//---------------------------------------------------------------------------//
struct GpuTechniqueCreation
{

  PipelineCreation creations[8];
  uint32_t numCreations = 0;

  const char* name = nullptr;

  GpuTechniqueCreation& reset();
  GpuTechniqueCreation& addPipeline(const PipelineCreation& pipeline);
  GpuTechniqueCreation& setName(const char* name);
};
//---------------------------------------------------------------------------//
struct GpuTechniquePass
{

  PipelineHandle pipeline;
};
//---------------------------------------------------------------------------//
struct GpuTechnique : public Framework::Resource
{
  Framework::Array<GpuTechniquePass> passes;
  Framework::FlatHashMap<uint64_t, uint16_t> nameHashToIndex;

  uint32_t m_PoolIndex;

  static constexpr const char* ms_TypeName = "gpu_technique_type";
  static uint64_t ms_TypeHash;
};
//---------------------------------------------------------------------------//
struct MaterialCreation
{
  MaterialCreation& reset();
  MaterialCreation& setTechnique(GpuTechnique* technique);
  MaterialCreation& setName(const char* name);
  MaterialCreation& setRenderIndex(uint32_t renderIndex);

  GpuTechnique* technique = nullptr;
  const char* name = nullptr;
  uint32_t renderIndex = ~0u;
};
//---------------------------------------------------------------------------//
struct Material : public Framework::Resource
{
  GpuTechnique* m_Technique;

  uint32_t m_RenderIndex;

  uint32_t m_PoolIndex;

  static constexpr const char* ms_TypeName = "material_type";
  static uint64_t ms_TypeHash;
};
//---------------------------------------------------------------------------//
// Forward declare
struct Renderer;
//---------------------------------------------------------------------------//
struct ResourceCache
{
  void init(Framework::Allocator* p_Allocator)
  {
    m_Textures.init(p_Allocator, 16);
    m_Buffers.init(p_Allocator, 16);
    m_Samplers.init(p_Allocator, 16);
    m_Techniques.init(p_Allocator, 16);
    m_Materials.init(p_Allocator, 16);
  }
  void shutdown(Renderer* p_Renderer);

  Framework::FlatHashMap<uint64_t, TextureResource*> m_Textures;
  Framework::FlatHashMap<uint64_t, BufferResource*> m_Buffers;
  Framework::FlatHashMap<uint64_t, SamplerResource*> m_Samplers;
  Framework::FlatHashMap<uint64_t, GpuTechnique*> m_Techniques;
  Framework::FlatHashMap<uint64_t, Material*> m_Materials;
};
//---------------------------------------------------------------------------//
struct RendererCreation
{
  Graphics::GpuDevice* gpu;
  Framework::Allocator* alloc;
};
//---------------------------------------------------------------------------//
struct Renderer : public Framework::Service
{
  static Renderer* instance();

  void init(const RendererCreation& p_Creation);
  void shutdown();

  void setLoaders(Framework::ResourceManager* p_Manager);

  void imguiDraw();

  void setPresentationMode(PresentMode::Enum value);
  void resizeSwapchain(uint32_t width, uint32_t height);

  float aspectRatio() const;

  BufferResource* createBuffer(const BufferCreation& p_Creation);
  BufferResource* createBuffer(
      VkBufferUsageFlags p_Type,
      Graphics::ResourceUsageType::Enum p_Usage,
      uint32_t p_Size,
      void* p_Data,
      const char* p_Name);

  TextureResource* createTexture(const TextureCreation& p_Creation);

  GpuTechnique* createTechnique(const GpuTechniqueCreation& creation);
  Material* createMaterial(const MaterialCreation& creation);
  Material* createMaterial(GpuTechnique* technique, const char* name);

  SamplerResource* createSampler(const SamplerCreation& p_Creation);

  PipelineHandle getPipeline(Material* material, uint32_t passIndex);
  DescriptorSetHandle createDescriptorSet(
      CommandBuffer* p_CommandBuffer, Material* p_Material, DescriptorSetCreation& p_DsCreation);

  void destroyBuffer(BufferResource* p_Buffer);
  void destroyTexture(TextureResource* p_Texture);
  void destroySampler(SamplerResource* p_Sampler);
  void destroyTechnique(GpuTechnique* technique);
  void destroyMaterial(Material* material);

  void* mapBuffer(BufferResource* p_Buffer, uint32_t p_Offset = 0, uint32_t p_Size = 0);
  void unmapBuffer(BufferResource* p_Buffer);

  CommandBuffer*
  getCommandBuffer(uint32_t p_ThreadIndex, uint32_t p_CurrentFrameIndex, bool p_Begin)
  {
    // TODO: use queue type
    return m_GpuDevice->getCommandBuffer(p_ThreadIndex, p_CurrentFrameIndex, p_Begin);
  }
  void queueCommandBuffer(Graphics::CommandBuffer* p_CommandBuffer)
  {
    m_GpuDevice->queueCommandBuffer(p_CommandBuffer);
  }

  // Multithread friendly update to textures
  void addTextureToUpdate(Graphics::TextureHandle p_Texture);
  void addTextureUpdateCommands(uint32_t p_ThreadId);
  std::mutex m_TextureUpdateMutex;
  TextureHandle m_TexturesToUpdate[128];
  uint32_t m_NumTexturesToUpdate = 0;

  Framework::ResourcePoolTyped<TextureResource> m_Textures;
  Framework::ResourcePoolTyped<BufferResource> m_Buffers;
  Framework::ResourcePoolTyped<SamplerResource> m_Samplers;
  Framework::ResourcePoolTyped<GpuTechnique> m_Techniques;
  Framework::ResourcePoolTyped<Material> m_Materials;

  ResourceCache m_ResourceCache;

  Graphics::GpuDevice* m_GpuDevice;
  Framework::Allocator* m_ResidentAllocator;
  Framework::StackAllocator m_TemporaryAllocator;

  Framework::Array<VmaBudget> m_GpuHeapBudgets;

  uint16_t m_Width;
  uint16_t m_Height;

  static constexpr const char* kName = "Graphics rendering service";
};
//---------------------------------------------------------------------------//
} // namespace RendererUtil
} // namespace Graphics
