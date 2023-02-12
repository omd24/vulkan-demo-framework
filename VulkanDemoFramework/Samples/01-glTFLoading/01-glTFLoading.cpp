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

//---------------------------------------------------------------------------//
// Demo specific utils
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

  Framework::ResourceManager resourceMgr = {};
  resourceMgr.init(allocator, nullptr);

  // Graphics:
  // TODO #1
  // 1. Gpu device creation
  // 2. Gpu profiler
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

#pragma endregion

  // TODO #2
  // 1. create textures
  // 2. create buffers
  // 3. prepare pipeline resources (VB, IB, etc)
  // 4. shader code (glsl)
  // 5. create pso
  // 6. descriptor sets
  // 7. uniforms
  // 8. determine draw arguments

  // TODO #3
  // 1. window loop
  // 2. update logic

#pragma region Deini, shutdown and cleanup

  resourceMgr.shutdown();

  Framework::gltfFree(scene);

  inputHandler.shutdown();
  window.unregisterOSMessagesCallback(inputOSMessagesCallback);
  window.shutdown();

  Framework::MemoryService::instance()->shutdown();
#pragma endregion

  return (0);
}
