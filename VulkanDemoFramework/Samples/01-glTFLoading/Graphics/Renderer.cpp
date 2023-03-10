#include "Renderer.hpp"

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

  m_Textures.shutdown();
  m_Buffers.shutdown();
  m_Samplers.shutdown();
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
} // namespace RendererUtil
//---------------------------------------------------------------------------//
} // namespace Graphics
