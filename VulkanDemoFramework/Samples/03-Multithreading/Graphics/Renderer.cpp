#include "Renderer.hpp"

#include "Graphics/CommandBuffer.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "Externals/stb_image.h"

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

  it = m_Programs.iteratorBegin();
  while (it.isValid())
  {
    Graphics::RendererUtil::Program* program = m_Programs.get(it);
    p_Renderer->destroyProgram(program);

    m_Programs.iteratorAdvance(it);
  }

  m_Textures.shutdown();
  m_Buffers.shutdown();
  m_Samplers.shutdown();
  m_Materials.shutdown();
  m_Programs.shutdown();
}
//---------------------------------------------------------------------------//
// Internals:
//---------------------------------------------------------------------------//
struct TextureLoader : public Framework::ResourceLoader
{
  Framework::Resource* get(const char* p_Name) override
  {
    const uint64_t hashedName = Framework::hashCalculate(p_Name);
    return m_Renderer->m_ResourceCache.m_Textures.get(hashedName);
  }
  Framework::Resource* get(uint64_t p_HashedName) override
  {
    return m_Renderer->m_ResourceCache.m_Textures.get(p_HashedName);
  }

  Framework::Resource* unload(const char* p_Name) override
  {
    const uint64_t hashedName = Framework::hashCalculate(p_Name);
    TextureResource* texture = m_Renderer->m_ResourceCache.m_Textures.get(hashedName);
    if (texture)
    {
      m_Renderer->destroyTexture(texture);
    }
    return nullptr;
  }

  Framework::Resource* createFromFile(
      const char* p_Name,
      const char* p_Filename,
      Framework::ResourceManager* p_ResourceManager) override
  {
    return m_Renderer->createTexture(p_Name, p_Filename);
  }

  Renderer* m_Renderer;
};
//---------------------------------------------------------------------------//
struct BufferLoader : public Framework::ResourceLoader
{
  Framework::Resource* get(const char* p_Name) override
  {
    const uint64_t hashedName = Framework::hashCalculate(p_Name);
    return m_Renderer->m_ResourceCache.m_Buffers.get(hashedName);
  }
  Framework::Resource* get(uint64_t p_HashedName) override
  {
    return m_Renderer->m_ResourceCache.m_Buffers.get(p_HashedName);
  }

  Framework::Resource* unload(const char* p_Name) override
  {
    const uint64_t hashedName = Framework::hashCalculate(p_Name);
    BufferResource* buffer = m_Renderer->m_ResourceCache.m_Buffers.get(hashedName);
    if (buffer)
    {
      m_Renderer->destroyBuffer(buffer);
    }

    return nullptr;
  }

  Renderer* m_Renderer;
};
//---------------------------------------------------------------------------//
struct SamplerLoader : public Framework::ResourceLoader
{
  Framework::Resource* get(const char* p_Name) override
  {
    const uint64_t hashedName = Framework::hashCalculate(p_Name);
    return m_Renderer->m_ResourceCache.m_Samplers.get(hashedName);
  }
  Framework::Resource* get(uint64_t p_HashedName) override
  {
    return m_Renderer->m_ResourceCache.m_Samplers.get(p_HashedName);
  }

  Framework::Resource* unload(const char* p_Name) override
  {
    const uint64_t hashedName = Framework::hashCalculate(p_Name);
    SamplerResource* sampler = m_Renderer->m_ResourceCache.m_Samplers.get(hashedName);
    if (sampler)
    {
      m_Renderer->destroySampler(sampler);
    }
    return nullptr;
  }

  Renderer* m_Renderer;
};
//---------------------------------------------------------------------------//
MaterialCreation& MaterialCreation::reset()
{
  program = nullptr;
  name = nullptr;
  renderIndex = ~0u;
  return *this;
}
//---------------------------------------------------------------------------//
MaterialCreation& MaterialCreation::setProgram(Program* p_Program)
{
  program = p_Program;
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
static TextureHandle
_createTextureFromFile(GpuDevice& p_GpuDevice, const char* p_Filename, const char* p_Name)
{

  if (p_Filename)
  {
    int comp, width, height;
    uint8_t* imageData = stbi_load(p_Filename, &width, &height, &comp, 4);
    if (!imageData)
    {
      OutputDebugStringA("Error loading texture\n");
      return kInvalidTexture;
    }

    TextureCreation creation;
    creation.setData(imageData)
        .setFormatType(VK_FORMAT_R8G8B8A8_UNORM, TextureType::kTexture2D)
        .setFlags(1, 0)
        .setSize((uint16_t)width, (uint16_t)height, 1)
        .setName(p_Name);

    Graphics::TextureHandle newTexture = p_GpuDevice.createTexture(creation);

    // IMPORTANT:
    // Free memory loaded from file, it should not matter!
    free(imageData);

    return newTexture;
  }

  return kInvalidTexture;
}
//---------------------------------------------------------------------------//
static TextureLoader g_TextureLoader;
static BufferLoader g_BufferLoader;
static SamplerLoader g_SamplerLoader;

uint64_t RendererUtil::TextureResource::ms_TypeHash = 0;
uint64_t RendererUtil::BufferResource::ms_TypeHash = 0;
uint64_t RendererUtil::SamplerResource::ms_TypeHash = 0;

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
        p_CmdBuf->m_VulkanCmdBuffer,
        p_Texture->vkImage,
        RESOURCE_STATE_UNDEFINED,
        RESOURCE_STATE_SHADER_RESOURCE,
        0,
        p_Texture->mipmaps,
        false);
  }

  p_Texture->vkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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

  m_Width = m_GpuDevice->m_SwapchainWidth;
  m_Height = m_GpuDevice->m_SwapchainHeight;

  m_Textures.init(p_Creation.alloc, 512);
  m_Buffers.init(p_Creation.alloc, 512);
  m_Samplers.init(p_Creation.alloc, 128);
  m_Programs.init(p_Creation.alloc, 128);
  m_Materials.init(p_Creation.alloc, 128);

  m_ResourceCache.init(p_Creation.alloc);

  // Init resource hashes
  TextureResource::ms_TypeHash = Framework::hashCalculate(TextureResource::ms_TypeName);
  BufferResource::ms_TypeHash = Framework::hashCalculate(BufferResource::ms_TypeName);
  SamplerResource::ms_TypeHash = Framework::hashCalculate(SamplerResource::ms_TypeName);

  g_TextureLoader.m_Renderer = this;
  g_BufferLoader.m_Renderer = this;
  g_SamplerLoader.m_Renderer = this;
}
//---------------------------------------------------------------------------//
void Renderer::shutdown()
{
  m_ResourceCache.shutdown(this);

  m_Textures.shutdown();
  m_Buffers.shutdown();
  m_Samplers.shutdown();

  OutputDebugStringA("Renderer shutdown\n");

  m_GpuDevice->shutdown();
}
//---------------------------------------------------------------------------//
void Renderer::setLoaders(Framework::ResourceManager* p_Manager)
{
  p_Manager->setLoader(TextureResource::ms_TypeName, &g_TextureLoader);
  p_Manager->setLoader(BufferResource::ms_TypeName, &g_BufferLoader);
  p_Manager->setLoader(SamplerResource::ms_TypeName, &g_SamplerLoader);
}
//---------------------------------------------------------------------------//
void Renderer::beginFrame()
{
  // TODO:
  m_GpuDevice->newFrame();
}
//---------------------------------------------------------------------------//
void Renderer::endFrame()
{
  // Present
  m_GpuDevice->present();
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
  BufferCreation creation{p_Type, p_Usage, p_Size, p_Data, p_Name};
  return createBuffer(creation);
}
//---------------------------------------------------------------------------//
TextureResource* Renderer::createTexture(const TextureCreation& p_Creation)
{
  TextureResource* texture = m_Textures.obtain();

  if (texture)
  {
    TextureHandle handle = m_GpuDevice->createTexture(p_Creation);
    texture->m_Handle = handle;
    texture->m_Name = p_Creation.name;
    m_GpuDevice->queryTexture(handle, texture->m_Desc);

    if (p_Creation.name != nullptr)
    {
      m_ResourceCache.m_Textures.insert(Framework::hashCalculate(p_Creation.name), texture);
    }

    texture->m_References = 1;

    return texture;
  }
  return nullptr;
}
//---------------------------------------------------------------------------//
TextureResource* Renderer::createTexture(const char* p_Name, const char* p_Filename)
{
  TextureResource* texture = m_Textures.obtain();

  if (texture)
  {
    TextureHandle handle = _createTextureFromFile(*m_GpuDevice, p_Filename, p_Name);
    texture->m_Handle = handle;
    m_GpuDevice->queryTexture(handle, texture->m_Desc);
    texture->m_References = 1;
    texture->m_Name = p_Name;

    m_ResourceCache.m_Textures.insert(Framework::hashCalculate(p_Name), texture);

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
Program* Renderer::createProgram(const ProgramCreation& creation)
{
  Program* program = m_Programs.obtain();
  if (program)
  {
    const uint32_t numPasses = 1;
    // First create arrays
    program->m_Passes.init(m_GpuDevice->m_Allocator, numPasses, numPasses);

    program->m_Name = creation.pipelineCreation.name;

    Framework::StringBuffer pipelineCachePath;
    pipelineCachePath.init(1024, m_GpuDevice->m_Allocator);

    for (uint32_t i = 0; i < numPasses; ++i)
    {
      ProgramPass& pass = program->m_Passes[i];

      if (creation.pipelineCreation.name != nullptr)
      {
        char* cachePath = pipelineCachePath.appendUseFormatted(
            "%s%s%s.cache", m_GpuDevice->m_Cwd.path, SHADER_FOLDER, creation.pipelineCreation.name);

        pass.pipeline = m_GpuDevice->createPipeline(creation.pipelineCreation, cachePath);
      }
      else
      {
        pass.pipeline = m_GpuDevice->createPipeline(creation.pipelineCreation);
      }

      pass.descriptorSetLayout = m_GpuDevice->getDescriptorSetLayout(pass.pipeline, 0);
    }

    pipelineCachePath.shutdown();

    if (creation.pipelineCreation.name != nullptr)
    {
      m_ResourceCache.m_Programs.insert(
          Framework::hashCalculate(creation.pipelineCreation.name), program);
    }

    program->m_References = 1;

    return program;
  }
  return nullptr;
}
//---------------------------------------------------------------------------//
Material* Renderer::createMaterial(const MaterialCreation& creation)
{
  Material* material = m_Materials.obtain();
  if (material)
  {
    material->m_Program = creation.program;
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
Material* Renderer::createMaterial(Program* program, const char* name)
{
  MaterialCreation creation{program, name};
  return createMaterial(creation);
}
//---------------------------------------------------------------------------//
PipelineHandle Renderer::getPipeline(Material* material)
{
  assert(material != nullptr);

  return material->m_Program->m_Passes[0].pipeline;
}
//---------------------------------------------------------------------------//
DescriptorSetHandle Renderer::createDescriptorSet(
    CommandBuffer* commandBuffer, Material* material, DescriptorSetCreation& dsCreation)
{
  assert(material != nullptr);

  DescriptorSetLayoutHandle setLayout = material->m_Program->m_Passes[0].descriptorSetLayout;

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
void Renderer::destroyProgram(Program* p_Program)
{
  if (!p_Program)
  {
    return;
  }

  p_Program->removeReference();
  if (p_Program->m_References)
  {
    return;
  }

  m_ResourceCache.m_Programs.remove(Framework::hashCalculate(p_Program->m_Name));

  m_GpuDevice->destroyPipeline(p_Program->m_Passes[0].pipeline);
  p_Program->m_Passes.shutdown();

  m_Programs.release(p_Program);
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

  CommandBuffer* cmdBuf = m_GpuDevice->getCommandBuffer(p_ThreadId, false);
  cmdBuf->begin();

  for (uint32_t i = 0; i < m_NumTexturesToUpdate; ++i)
  {

    Texture* texture =
        (Texture*)m_GpuDevice->m_Textures.accessResource(m_TexturesToUpdate[i].index);

    texture->vkImageLayout = addImageBarrier2(
        cmdBuf->m_VulkanCmdBuffer,
        texture->vkImage,
        RESOURCE_STATE_COPY_DEST,
        RESOURCE_STATE_COPY_SOURCE,
        0,
        1,
        false,
        m_GpuDevice->m_VulkanTransferQueueFamily,
        m_GpuDevice->m_VulkanMainQueueFamily);

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
