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

  // Graphics:
  // TODO #1
  // 3. Renderer class
  // 4. Imgui helper

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

    // TODO! first set up imgui
    // window.handleOSMessages();

    if (window.m_Resized)
    {
      // gpuDevice.resize(window.m_Width, window.m_Height);
      window.m_Resized = false;
    }

    const int64_t currentTick = Framework::Time::getCurrentTime();
    float deltaTime = (float)Framework::Time::deltaSeconds(beginFrameTick, currentTick);
    beginFrameTick = currentTick;

    inputHandler.newFrame();
    inputHandler.update(deltaTime);

    if (!window.m_Minimized)
    {
      // TODO:
      // Draw!
    }
  }
#pragma endregion End Window loop
#pragma region Deinit, shutdown and cleanup

  resourceMgr.shutdown();

  // gpuDevice.shutdown(); // TODO: move this to renderer

  Framework::gltfFree(scene);

  inputHandler.shutdown();
  window.unregisterOSMessagesCallback(inputOSMessagesCallback);
  window.shutdown();

  Framework::MemoryService::instance()->shutdown();
#pragma endregion End Deinit, shutdown and cleanup

  return (0);
}
