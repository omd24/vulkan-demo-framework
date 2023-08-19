#include "Renderer.hpp"

#include "Graphics/CommandBuffer.hpp"

#include "Externals/imgui/imgui.h"

namespace Graphics
{
//---------------------------------------------------------------------------//
namespace RendererUtil
{
//---------------------------------------------------------------------------//
void ResourceCache::shutdown(Renderer* p_Renderer)
{
  Framework::FlatHashMapIterator it = m_Textures.iteratorBegin();
  while (it.isValid())
  {
    Graphics::RendererUtil::TextureResource* texture = m_Textures.get(it);
    p_Renderer->destroyTexture(texture);

    m_Textures.iteratorAdvance(it);
  }

  it = m_Buffers.iteratorBegin();
  while (it.isValid())
  {
    Graphics::RendererUtil::BufferResource* buffer = m_Buffers.get(it);
    p_Renderer->destroyBuffer(buffer);

    m_Buffers.iteratorAdvance(it);
  }

  it = m_Samplers.iteratorBegin();
  while (it.isValid())
  {
    Graphics::RendererUtil::SamplerResource* sampler = m_Samplers.get(it);
    p_Renderer->destroySampler(sampler);

    m_Samplers.iteratorAdvance(it);
  }

  it = m_Materials.iteratorBegin();
  while (it.isValid())
  {
    Graphics::RendererUtil::Material* material = m_Materials.get(it);
    p_Renderer->destroyMaterial(material);

    m_Materials.iteratorAdvance(it);
  }

  it = m_Techniques.iteratorBegin();
  while (it.isValid())
  {
    Graphics::RendererUtil::GpuTechnique* tech = m_Techniques.get(it);
    p_Renderer->destroyTechnique(tech);

    m_Techniques.iteratorAdvance(it);
  }

  m_Textures.shutdown();
  m_Buffers.shutdown();
  m_Samplers.shutdown();
  m_Materials.shutdown();
  m_Techniques.shutdown();
}
//---------------------------------------------------------------------------//
GpuTechniqueCreation& GpuTechniqueCreation::reset()
{
  numCreations = 0;
  name = nullptr;
  return *this;
}
//---------------------------------------------------------------------------//
GpuTechniqueCreation& GpuTechniqueCreation::addPipeline(const PipelineCreation& pipeline)
{
  creations[numCreations++] = pipeline;
  return *this;
}
//---------------------------------------------------------------------------//
GpuTechniqueCreation& GpuTechniqueCreation::setName(const char* p_Name)
{
  name = p_Name;
  return *this;
}
//---------------------------------------------------------------------------//
MaterialCreation& MaterialCreation::reset()
{
  technique = nullptr;
  name = nullptr;
  renderIndex = ~0u;
  return *this;
}
//---------------------------------------------------------------------------//
MaterialCreation& MaterialCreation::setTechnique(GpuTechnique* p_technique)
{
  technique = p_technique;
  return *this;
}
//---------------------------------------------------------------------------//
MaterialCreation& MaterialCreation::setRenderIndex(uint32_t p_RenderIndex)
{
  renderIndex = p_RenderIndex;
  return *this;
}
//---------------------------------------------------------------------------//
MaterialCreation& MaterialCreation::setName(const char* p_Name)
{
  name = p_Name;
  return *this;
}
//---------------------------------------------------------------------------//

uint64_t RendererUtil::TextureResource::ms_TypeHash = 0;
uint64_t RendererUtil::BufferResource::ms_TypeHash = 0;
uint64_t RendererUtil::SamplerResource::ms_TypeHash = 0;
uint64_t RendererUtil::Material::ms_TypeHash = 0;
uint64_t RendererUtil::GpuTechnique::ms_TypeHash = 0;

static Renderer g_Renderer;
//---------------------------------------------------------------------------//
static VkImageLayout addImageBarrier2(
    VkCommandBuffer p_CmdBuf,
    VkImage p_Image,
    Graphics::ResourceState p_OldState,
    Graphics::ResourceState p_NewState,
    uint32_t p_BaseMipLevel,
    uint32_t p_MipCount,
    bool p_IsDepth,
    uint32_t p_SourceFamily,
    uint32_t p_DestinationFamily)
{
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.image = p_Image;
  barrier.srcQueueFamilyIndex = p_SourceFamily;
  barrier.dstQueueFamilyIndex = p_DestinationFamily;
  barrier.subresourceRange.aspectMask =
      p_IsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.subresourceRange.levelCount = p_MipCount;

  barrier.subresourceRange.baseMipLevel = p_BaseMipLevel;
  barrier.oldLayout = utilToVkImageLayout(p_OldState);
  barrier.newLayout = utilToVkImageLayout(p_NewState);
  barrier.srcAccessMask = utilToVkAccessFlags(p_OldState);
  barrier.dstAccessMask = utilToVkAccessFlags(p_NewState);

  const VkPipelineStageFlags sourceStageMask =
      utilDeterminePipelineStageFlags(barrier.srcAccessMask, QueueType::kGraphics);
  const VkPipelineStageFlags destinationStageMask =
      utilDeterminePipelineStageFlags(barrier.dstAccessMask, QueueType::kGraphics);

  vkCmdPipelineBarrier(
      p_CmdBuf, sourceStageMask, destinationStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);

  return barrier.newLayout;
}
//---------------------------------------------------------------------------//
static void generateMipmaps(
    Graphics::Texture* p_Texture, Graphics::CommandBuffer* p_CmdBuf, bool p_FromTransferQueue)
{
  using namespace Graphics;

  if (p_Texture->mipmaps > 1)
  {
    utilAddImageBarrier(
        p_CmdBuf->m_GpuDevice,
        p_CmdBuf->m_VulkanCmdBuffer,
        p_Texture->vkImage,
        p_FromTransferQueue ? RESOURCE_STATE_COPY_SOURCE : RESOURCE_STATE_COPY_SOURCE,
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
        p_CmdBuf->m_GpuDevice,
        p_CmdBuf->m_VulkanCmdBuffer,
        p_Texture->vkImage,
        RESOURCE_STATE_UNDEFINED,
        RESOURCE_STATE_COPY_DEST,
        mipIndex,
        1,
        false);

    VkImageBlit blit_region{};
    blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_region.srcSubresource.mipLevel = mipIndex - 1;
    blit_region.srcSubresource.baseArrayLayer = 0;
    blit_region.srcSubresource.layerCount = 1;

    blit_region.srcOffsets[0] = {0, 0, 0};
    blit_region.srcOffsets[1] = {w, h, 1};

    w /= 2;
    h /= 2;

    blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_region.dstSubresource.mipLevel = mipIndex;
    blit_region.dstSubresource.baseArrayLayer = 0;
    blit_region.dstSubresource.layerCount = 1;

    blit_region.dstOffsets[0] = {0, 0, 0};
    blit_region.dstOffsets[1] = {w, h, 1};

    vkCmdBlitImage(
        p_CmdBuf->m_VulkanCmdBuffer,
        p_Texture->vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        p_Texture->vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blit_region,
        VK_FILTER_LINEAR);

    // Prepare current mip for next level
    utilAddImageBarrier(
        p_CmdBuf->m_GpuDevice,
        p_CmdBuf->m_VulkanCmdBuffer,
        p_Texture->vkImage,
        RESOURCE_STATE_COPY_DEST,
        RESOURCE_STATE_COPY_SOURCE,
        mipIndex,
        1,
        false);
  }

  // Transition
  if (p_FromTransferQueue && false)
  {
    utilAddImageBarrier(
        p_CmdBuf->m_GpuDevice,
        p_CmdBuf->m_VulkanCmdBuffer,
        p_Texture->vkImage,
        (p_Texture->mipmaps > 1) ? RESOURCE_STATE_COPY_SOURCE : RESOURCE_STATE_COPY_DEST,
        RESOURCE_STATE_SHADER_RESOURCE,
        0,
        p_Texture->mipmaps,
        false);
  }
  else
  {
    utilAddImageBarrier(
        p_CmdBuf->m_GpuDevice,
        p_CmdBuf->m_VulkanCmdBuffer,
        p_Texture->vkImage,
        RESOURCE_STATE_UNDEFINED,
        RESOURCE_STATE_SHADER_RESOURCE,
        0,
        p_Texture->mipmaps,
        false);
  }
}
//---------------------------------------------------------------------------//
// Renderer:
//---------------------------------------------------------------------------//
Renderer* Renderer::instance()
{
  // TODO:
  return &g_Renderer;
}
//---------------------------------------------------------------------------//
void Renderer::init(const RendererCreation& p_Creation)
{
  OutputDebugStringA("Renderer init\n");

  m_GpuDevice = p_Creation.gpu;
  m_ResidentAllocator = p_Creation.alloc;
  m_TemporaryAllocator.init(FRAMEWORK_KILO(10));

  m_Width = m_GpuDevice->m_SwapchainWidth;
  m_Height = m_GpuDevice->m_SwapchainHeight;

  m_Textures.init(p_Creation.alloc, kTexturesPoolSize);
  m_Buffers.init(p_Creation.alloc, kBuffersPoolSize);
  m_Samplers.init(p_Creation.alloc, kSamplersPoolSize);
  m_Materials.init(p_Creation.alloc, 128);
  m_Techniques.init(p_Creation.alloc, 128);

  m_ResourceCache.init(p_Creation.alloc);

  // Init resource hashes
  TextureResource::ms_TypeHash = Framework::hashCalculate(TextureResource::ms_TypeName);
  BufferResource::ms_TypeHash = Framework::hashCalculate(BufferResource::ms_TypeName);
  SamplerResource::ms_TypeHash = Framework::hashCalculate(SamplerResource::ms_TypeName);
  Material::ms_TypeHash = Framework::hashCalculate(Material::ms_TypeName);
  GpuTechnique::ms_TypeHash = Framework::hashCalculate(GpuTechnique::ms_TypeName);

  const uint32_t gpuHeapCounts = m_GpuDevice->getMemoryHeapCount();
  m_GpuHeapBudgets.init(m_GpuDevice->m_Allocator, gpuHeapCounts, gpuHeapCounts);
}
//---------------------------------------------------------------------------//
void Renderer::shutdown()
{
  m_TemporaryAllocator.shutdown();

  m_ResourceCache.shutdown(this);
  m_GpuHeapBudgets.shutdown();

  m_Textures.shutdown();
  m_Buffers.shutdown();
  m_Samplers.shutdown();
  m_Materials.shutdown();
  m_Techniques.shutdown();

  OutputDebugStringA("Renderer shutdown\n");

  m_GpuDevice->shutdown();
}
//---------------------------------------------------------------------------//
void Renderer::setLoaders(Framework::ResourceManager* p_Manager)
{
  // Moved loaders to resources loader!
}
//---------------------------------------------------------------------------//
void Renderer::imguiDraw()
{
  // Print memory stats
  vmaGetHeapBudgets(m_GpuDevice->m_VmaAllocator, m_GpuHeapBudgets.m_Data);

  size_t totalMemoryUsed = 0;
  for (uint32_t i = 0; i < m_GpuDevice->getMemoryHeapCount(); ++i)
  {
    totalMemoryUsed += m_GpuHeapBudgets[i].usage;
  }

  ImGui::Text("GPU Memory Total: %lluMB", totalMemoryUsed / (1024 * 1024));
}
//---------------------------------------------------------------------------//
void Renderer::setPresentationMode(PresentMode::Enum value)
{
  m_GpuDevice->setPresentMode(value);
  m_GpuDevice->resizeSwapchain();
}
//---------------------------------------------------------------------------//
void Renderer::resizeSwapchain(uint32_t p_Width, uint32_t p_Height)
{
  m_GpuDevice->resize((uint16_t)p_Width, (uint16_t)p_Height);

  p_Width = m_GpuDevice->m_SwapchainWidth;
  p_Height = m_GpuDevice->m_SwapchainHeight;
}
//---------------------------------------------------------------------------//
float Renderer::aspectRatio() const
{
  return m_GpuDevice->m_SwapchainWidth * 1.f / m_GpuDevice->m_SwapchainHeight;
}
//---------------------------------------------------------------------------//
BufferResource* Renderer::createBuffer(const BufferCreation& p_Creation)
{
  BufferResource* buffer = m_Buffers.obtain();
  if (buffer)
  {
    BufferHandle handle = m_GpuDevice->createBuffer(p_Creation);
    buffer->m_Handle = handle;
    buffer->m_Name = p_Creation.name;
    m_GpuDevice->queryBuffer(handle, buffer->m_Desc);

    if (p_Creation.name != nullptr)
    {
      m_ResourceCache.m_Buffers.insert(Framework::hashCalculate(p_Creation.name), buffer);
    }

    buffer->m_References = 1;

    return buffer;
  }
  return nullptr;
}
//---------------------------------------------------------------------------//
BufferResource* Renderer::createBuffer(
    VkBufferUsageFlags p_Type,
    Graphics::ResourceUsageType::Enum p_Usage,
    uint32_t p_Size,
    void* p_Data,
    const char* p_Name)
{
  BufferCreation creation{p_Type, p_Usage, p_Size, 0, 0, p_Data, p_Name};
  return createBuffer(creation);
}
//---------------------------------------------------------------------------//
TextureResource* Renderer::createTexture(const TextureCreation& creation)
{
  TextureResource* texture = m_Textures.obtain();

  if (texture)
  {
    TextureHandle handle = m_GpuDevice->createTexture(creation);
    texture->m_Handle = handle;
    m_GpuDevice->queryTexture(handle, texture->m_Desc);
    texture->m_References = 1;
    texture->m_Name = creation.name;

    if (creation.name != nullptr)
      m_ResourceCache.m_Textures.insert(Framework::hashCalculate(creation.name), texture);

    texture->m_References = 1;
    return texture;
  }
  return nullptr;
}
//---------------------------------------------------------------------------//
SamplerResource* Renderer::createSampler(const SamplerCreation& p_Creation)
{
  SamplerResource* sampler = m_Samplers.obtain();
  if (sampler)
  {
    SamplerHandle handle = m_GpuDevice->createSampler(p_Creation);
    sampler->m_Handle = handle;
    sampler->m_Name = p_Creation.name;
    m_GpuDevice->querySampler(handle, sampler->m_Desc);

    if (p_Creation.name != nullptr)
    {
      m_ResourceCache.m_Samplers.insert(Framework::hashCalculate(p_Creation.name), sampler);
    }

    sampler->m_References = 1;

    return sampler;
  }
  return nullptr;
}
//---------------------------------------------------------------------------//
GpuTechnique* Renderer::createTechnique(const GpuTechniqueCreation& creation)
{
  GpuTechnique* technique = m_Techniques.obtain();
  if (technique)
  {
    technique->passes.init(m_ResidentAllocator, creation.numCreations, creation.numCreations);
    technique->nameHashToIndex.init(m_ResidentAllocator, creation.numCreations);
    technique->m_Name = creation.name;

    m_TemporaryAllocator.clear();

    Framework::StringBuffer pipelineCachePath;
    pipelineCachePath.init(2048, &m_TemporaryAllocator);

    for (uint32_t i = 0; i < creation.numCreations; ++i)
    {
      GpuTechniquePass& pass = technique->passes[i];
      const PipelineCreation& passCreation = creation.creations[i];
      if (passCreation.name != nullptr)
      {
        char* cachePath = pipelineCachePath.appendUseFormatted(
            "%s%s%s.cache", m_GpuDevice->m_Cwd.path, SHADER_FOLDER, passCreation.name);

        pass.pipeline = m_GpuDevice->createPipeline(passCreation, cachePath);
      }
      else
      {
        pass.pipeline = m_GpuDevice->createPipeline(passCreation);
      }
    }

    m_TemporaryAllocator.clear();

    if (creation.name != nullptr)
    {
      m_ResourceCache.m_Techniques.insert(Framework::hashCalculate(creation.name), technique);
    }

    technique->m_References = 1;
  }
  return technique;
}
//---------------------------------------------------------------------------//
Material* Renderer::createMaterial(const MaterialCreation& creation)
{
  Material* material = m_Materials.obtain();
  if (material)
  {
    material->m_Technique = creation.technique;
    material->m_Name = creation.name;
    material->m_RenderIndex = creation.renderIndex;

    if (creation.name != nullptr)
    {
      m_ResourceCache.m_Materials.insert(Framework::hashCalculate(creation.name), material);
    }

    material->m_References = 1;

    return material;
  }
  return nullptr;
}
//---------------------------------------------------------------------------//
Material* Renderer::createMaterial(GpuTechnique* technique, const char* name)
{
  MaterialCreation creation{technique, name};
  return createMaterial(creation);
}
//---------------------------------------------------------------------------//
PipelineHandle Renderer::getPipeline(Material* material, uint32_t passIndex)
{
  assert(material != nullptr);

  return material->m_Technique->passes[passIndex].pipeline;
}
//---------------------------------------------------------------------------//
DescriptorSetHandle Renderer::createDescriptorSet(
    CommandBuffer* commandBuffer, Material* material, DescriptorSetCreation& dsCreation)
{
  assert(material != nullptr);

  DescriptorSetLayoutHandle setLayout =
      m_GpuDevice->getDescriptorSetLayout(material->m_Technique->passes[0].pipeline, 1);

  dsCreation.setLayout(setLayout);

  return commandBuffer->createDescriptorSet(dsCreation);
}
//---------------------------------------------------------------------------//
void Renderer::destroyBuffer(BufferResource* p_Buffer)
{
  if (!p_Buffer)
  {
    return;
  }

  p_Buffer->removeReference();
  if (p_Buffer->m_References)
  {
    return;
  }

  m_ResourceCache.m_Buffers.remove(Framework::hashCalculate(p_Buffer->m_Desc.name));
  m_GpuDevice->destroyBuffer(p_Buffer->m_Handle);
  m_Buffers.release(p_Buffer);
}
//---------------------------------------------------------------------------//
void Renderer::destroyTexture(TextureResource* p_Texture)
{
  if (!p_Texture)
  {
    return;
  }

  p_Texture->removeReference();
  if (p_Texture->m_References)
  {
    return;
  }

  m_ResourceCache.m_Textures.remove(Framework::hashCalculate(p_Texture->m_Desc.name));
  m_GpuDevice->destroyTexture(p_Texture->m_Handle);
  m_Textures.release(p_Texture);
}
//---------------------------------------------------------------------------//
void Renderer::destroySampler(SamplerResource* p_Sampler)
{
  if (!p_Sampler)
  {
    return;
  }

  p_Sampler->removeReference();
  if (p_Sampler->m_References)
  {
    return;
  }

  m_ResourceCache.m_Samplers.remove(Framework::hashCalculate(p_Sampler->m_Desc.name));
  m_GpuDevice->destroySampler(p_Sampler->m_Handle);
  m_Samplers.release(p_Sampler);
}
//---------------------------------------------------------------------------//
void Renderer::destroyTechnique(GpuTechnique* p_Technique)
{
  if (!p_Technique)
  {
    return;
  }

  p_Technique->removeReference();
  if (p_Technique->m_References)
  {
    return;
  }

  for (uint32_t i = 0; i < p_Technique->passes.m_Size; ++i)
  {
    m_GpuDevice->destroyPipeline(p_Technique->passes[i].pipeline);
  }

  p_Technique->passes.shutdown();

  m_ResourceCache.m_Techniques.remove(Framework::hashCalculate(p_Technique->m_Name));
  m_Techniques.release(p_Technique);
}
//---------------------------------------------------------------------------//
void Renderer::destroyMaterial(Material* p_Material)
{
  if (!p_Material)
  {
    return;
  }

  p_Material->removeReference();
  if (p_Material->m_References)
  {
    return;
  }

  m_ResourceCache.m_Materials.remove(Framework::hashCalculate(p_Material->m_Name));
  m_Materials.release(p_Material);
}
//---------------------------------------------------------------------------//
void* Renderer::mapBuffer(BufferResource* p_Buffer, uint32_t p_Offset, uint32_t p_Size)
{
  MapBufferParameters mapParams = {p_Buffer->m_Handle, p_Offset, p_Size};
  return m_GpuDevice->mapBuffer(mapParams);
}
//---------------------------------------------------------------------------//
void Renderer::unmapBuffer(BufferResource* p_Buffer)
{
  if (p_Buffer->m_Desc.parentHandle.index == kInvalidIndex)
  {
    MapBufferParameters mapParams = {p_Buffer->m_Handle, 0, 0};
    m_GpuDevice->unmapBuffer(mapParams);
  }
}
//---------------------------------------------------------------------------//
void Renderer::addTextureToUpdate(Graphics::TextureHandle p_Texture)
{
  std::lock_guard<std::mutex> guard(m_TextureUpdateMutex);

  m_TexturesToUpdate[m_NumTexturesToUpdate++] = p_Texture;
}
//---------------------------------------------------------------------------//
void Renderer::addTextureUpdateCommands(uint32_t p_ThreadId)
{
  std::lock_guard<std::mutex> guard(m_TextureUpdateMutex);

  if (m_NumTexturesToUpdate == 0)
  {
    return;
  }

  CommandBuffer* cmdBuf =
      m_GpuDevice->getCommandBuffer(p_ThreadId, m_GpuDevice->m_CurrentFrameIndex, false);
  cmdBuf->begin();

  for (uint32_t i = 0; i < m_NumTexturesToUpdate; ++i)
  {

    Texture* texture =
        (Texture*)m_GpuDevice->m_Textures.accessResource(m_TexturesToUpdate[i].index);

    utilAddImageBarrierExt(
        cmdBuf->m_GpuDevice,
        cmdBuf->m_VulkanCmdBuffer,
        texture->vkImage,
        RESOURCE_STATE_COPY_DEST,
        RESOURCE_STATE_COPY_SOURCE,
        0,
        1,
        false,
        m_GpuDevice->m_VulkanTransferQueueFamily,
        m_GpuDevice->m_VulkanMainQueueFamily,
        QueueType::kCopyTransfer,
        QueueType::kGraphics);

    generateMipmaps(texture, cmdBuf, true);
  }

  // TODO: this is done before submitting to the queue in the device.
  m_GpuDevice->queueCommandBuffer(cmdBuf);

  m_NumTexturesToUpdate = 0;
}
//---------------------------------------------------------------------------//
} // namespace RendererUtil
//---------------------------------------------------------------------------//
} // namespace Graphics
