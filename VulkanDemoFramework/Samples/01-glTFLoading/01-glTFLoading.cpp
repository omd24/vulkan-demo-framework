#include <Foundation/File.hpp>
#include <Foundation/Gltf.hpp>
#include <Foundation/Numerics.hpp>
#include <Foundation/ResourceManager.hpp>
#include <Foundation/Time.hpp>

#include <Application/Window.hpp>
#include <Application/Input.hpp>
#include <Application/Keys.hpp>

#include <Externals/cglm/struct/mat3.h>
#include <Externals/cglm/struct/mat4.h>
#include <Externals/cglm/struct/cam.h>
#include <Externals/cglm/struct/affine.h>

#include <Externals/imgui/imgui.h>

#include <stdlib.h> // for exit()

//---------------------------------------------------------------------------//
// Graphics includes:
#include "Graphics/GpuEnum.hpp"
#include "Graphics/GpuResources.hpp"
#include "Graphics/GpuDevice.hpp"
#include "Graphics/CommandBuffer.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/ImguiHelper.hpp"

//---------------------------------------------------------------------------//
// Demo specific utils:
//---------------------------------------------------------------------------//

float rx, ry;
struct MaterialData
{
  vec4s baseColorFactor;
};

struct MeshDraw
{
};

struct UniformData
{
  mat4s model;
  mat4s viewProj;
  mat4s invModel;
  vec4s eye;
  vec4s light;
};
//---------------------------------------------------------------------------//
/// Window message loop callback
static void inputOSMessagesCallback(void* p_OSEvent, void* p_UserData)
{
  Framework::InputService* input = (Framework::InputService*)p_UserData;
  input->onEvent(p_OSEvent);
}
//---------------------------------------------------------------------------//
int main(int argc, char** argv)
{
  if (argc < 2)
  {
    printf("No model specified, using the default model\n");
    // exit(-1);
    argv[1] = const_cast<char*>("C:\\gltf-models\\FlightHelmet\\FlightHelmet.gltf");
  }

  // Init services
  Framework::MemoryService::instance()->init(nullptr);
  Framework::Time::serviceInit();

  Framework::Allocator* allocator = &Framework::MemoryService::instance()->m_SystemAllocator;
  Framework::StackAllocator scratchAllocator;
  scratchAllocator.init(FRAMEWORK_MEGA(8));

  Framework::InputService inputHandler = {};
  inputHandler.init(allocator);

  // Init window
  Framework::WindowConfiguration winCfg = {};
  winCfg.m_Width = 1280;
  winCfg.m_Height = 800;
  winCfg.m_Name = "Demo 01";
  winCfg.m_Allocator = allocator;
  Framework::Window window = {};
  window.init(&winCfg);

  window.registerOSMessagesCallback(inputOSMessagesCallback, &inputHandler);

  // graphics
  Graphics::DeviceCreation deviceCreation;
  deviceCreation.setWindow(window.m_Width, window.m_Height, window.m_PlatformHandle)
      .setAllocator(allocator)
      .setTemporaryAllocator(&scratchAllocator);
  Graphics::GpuDevice gpuDevice;
  gpuDevice.init(deviceCreation);

  Framework::ResourceManager resourceMgr = {};
  resourceMgr.init(allocator, nullptr);

  Graphics::RendererUtil::Renderer renderer;
  renderer.init({&gpuDevice, allocator});
  renderer.setLoaders(&resourceMgr);

  Graphics::ImguiUtil::ImguiService* imgui = Graphics::ImguiUtil::ImguiService::instance();
  Graphics::ImguiUtil::ImguiServiceConfiguration imguiConfig{&gpuDevice, window.m_PlatformHandle};
  imgui->init(&imguiConfig);

  // Load glTF scene
#pragma region Load glTF scene

  // Store currect working dir to restore later
  Framework::Directory cwd = {};
  Framework::directoryCurrent(&cwd);

  // Change directory:
  char gltfBasePath[512] = {};
  ::memcpy(gltfBasePath, argv[1], strlen(argv[1]));
  Framework::fileDirectoryFromPath(gltfBasePath);
  Framework::directoryChange(gltfBasePath);

  // Determine filename:
  char gltfFile[512] = {};
  ::memcpy(gltfFile, argv[1], strlen(argv[1]));
  Framework::filenameFromPath(gltfFile);

  // Load scene:
  Framework::glTF::glTF scene = Framework::gltfLoadFile(gltfFile);

  // Create textures:
  Framework::Array<Graphics::RendererUtil::TextureResource> images;
  images.init(allocator, scene.imagesCount);
  for (uint32_t imageIndex = 0; imageIndex < scene.imagesCount; ++imageIndex)
  {
    Framework::glTF::Image& image = scene.images[imageIndex];
    Graphics::RendererUtil::TextureResource* tr =
        renderer.createTexture(image.uri.m_Data, image.uri.m_Data);
    assert(tr != nullptr);
    images.push(*tr);
  }

  // Create samplers
  Framework::StringBuffer resourceNameBuffer;
  resourceNameBuffer.init(4096, allocator);

  Framework::Array<Graphics::RendererUtil::SamplerResource> samplers;
  samplers.init(allocator, scene.samplersCount);
  for (uint32_t samplerIndex = 0; samplerIndex < scene.samplersCount; ++samplerIndex)
  {
    Framework::glTF::Sampler& sampler = scene.samplers[samplerIndex];

    char* sampler_name = resourceNameBuffer.appendUseFormatted("sampler %u", samplerIndex);

    Graphics::SamplerCreation creation;
    creation.minFilter = sampler.minFilter == Framework::glTF::Sampler::Filter::LINEAR
                             ? VK_FILTER_LINEAR
                             : VK_FILTER_NEAREST;
    creation.magFilter = sampler.magFilter == Framework::glTF::Sampler::Filter::LINEAR
                             ? VK_FILTER_LINEAR
                             : VK_FILTER_NEAREST;
    creation.name = sampler_name;

    Graphics::RendererUtil::SamplerResource* sr = renderer.createSampler(creation);
    assert(sr != nullptr);
    samplers.push(*sr);
  }

  // Create buffers:
  Framework::Array<void*> buffersData;
  buffersData.init(allocator, scene.buffersCount);

  for (uint32_t bufferIndex = 0; bufferIndex < scene.buffersCount; ++bufferIndex)
  {
    Framework::glTF::Buffer& buffer = scene.buffers[bufferIndex];

    Framework::FileReadResult buffer_data = Framework::fileReadBinary(buffer.uri.m_Data, allocator);
    buffersData.push(buffer_data.data);
  }

  Framework::Array<Graphics::RendererUtil::BufferResource> buffers;
  buffers.init(allocator, scene.bufferViewsCount);

  for (uint32_t bufferIndex = 0; bufferIndex < scene.bufferViewsCount; ++bufferIndex)
  {
    Framework::glTF::BufferView& buffer = scene.bufferViews[bufferIndex];

    int offset = buffer.byteOffset;
    if (offset == Framework::glTF::INVALID_INT_VALUE)
    {
      offset = 0;
    }

    uint8_t* data = (uint8_t*)buffersData[buffer.buffer] + offset;

    // NOTE(marco): the target attribute of a BufferView is not mandatory, so we prepare for both
    // uses
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    char* bufferName = buffer.name.m_Data;
    if (bufferName == nullptr)
    {
      bufferName = resourceNameBuffer.appendUseFormatted("buffer %u", bufferIndex);
    }

    Graphics::RendererUtil::BufferResource* br = renderer.createBuffer(
        flags, Graphics::ResourceUsageType::kImmutable, buffer.byteLength, data, bufferName);
    assert(br != nullptr);

    buffers.push(*br);
  }

  for (uint32_t bufferIndex = 0; bufferIndex < scene.buffersCount; ++bufferIndex)
  {
    void* buffer = buffersData[bufferIndex];
    allocator->deallocate(buffer);
  }
  buffersData.shutdown();

#pragma endregion End Load glTF scene

  int64_t beginFrameTick = Framework::Time::getCurrentTime();
  float modelScale = 0.008f;

#pragma region Window loop
  while (!window.m_RequestedExit)
  {
    // New frame
    if (!window.m_Minimized)
    {
      gpuDevice.newFrame();
    }

    window.handleOSMessages();

    if (window.m_Resized)
    {
      // gpuDevice.resize(window.m_Width, window.m_Height);
      window.m_Resized = false;
    }

    imgui->newFrame();

    const int64_t currentTick = Framework::Time::getCurrentTime();
    float deltaTime = (float)Framework::Time::deltaSeconds(beginFrameTick, currentTick);
    beginFrameTick = currentTick;

    inputHandler.newFrame();
    inputHandler.update(deltaTime);

    // if (!window.m_Minimized)
    //{
    //  Graphics::CommandBuffer* gpuCommands = gpuDevice.getCommandBuffer(true);

    //  // TODO
    //  // draw

    //  imgui->render(*gpuCommands);

    //  // Submit commands
    //  gpuDevice.queueCommandBuffer(gpuCommands);
    //  gpuDevice.present();
    //}
  }
#pragma endregion End Window loop
#pragma region Deinit, shutdown and cleanup

  imgui->shutdown();

  resourceMgr.shutdown();
  renderer.shutdown();

  Framework::gltfFree(scene);

  inputHandler.shutdown();
  window.unregisterOSMessagesCallback(inputOSMessagesCallback);
  window.shutdown();

  Framework::MemoryService::instance()->shutdown();
#pragma endregion End Deinit, shutdown and cleanup

  return (0);
}
