#pragma once

... Move this to a seperate file "RenderScene.hpp"

#include "Foundation/Array.hpp"
#include "Foundation/Prerequisites.hpp"

#include "Graphics/CommandBuffer.hpp"
#include "Graphics/GpuDevice.hpp"
#include "Graphics/GpuResources.hpp"
#include "Graphics/ImguiHelper.hpp"

#include "Externals/cglm/types-struct.h"

#include <atomic>

    namespace enki
{
  class TaskScheduler;
}

namespace Graphics
{

struct Allocator;
struct AsynchronousLoader;
struct FrameGraph;
struct ImguiService;
struct Renderer;
struct SceneGraph;
struct StackAllocator;

static const uint16_t kInvalidSceneTextureIndex = UINT16_MAX;
static const uint32_t kMaterialDescriptorSetIndex = 1;

static bool g_RecreatePerThreadDescriptors = false;
static bool g_UseSecondaryCommandBuffers = false;

//
//
enum DrawFlags
{
  kDrawFlagsAlphaMask = 1 << 0,
  kDrawFlagsDoubleSided = 1 << 1,
  kDrawFlagsTransparent = 1 << 2,
}; // enum DrawFlags

//
//
struct GpuSceneData
{
  mat4s viewProj;
  vec4s eye;
  vec4s lightPosition;
  float lightRange;
  float lightIntensity;
  float padding[2];
}; // struct GpuSceneData

//
//
struct RenderScene
{
  virtual ~RenderScene(){};

  virtual void init(
      const char* filename,
      const char* path,
      Framework::Allocator* residentAllocator,
      Framework::StackAllocator* tempAllocator,
      AsynchronousLoader* asyncLoader){};
  virtual void shutdown(RendererUtil::Renderer* renderer){};

  virtual void registerRenderPasses(FrameGraph* frameGraph){};
  virtual void prepareDraws(
      RendererUtil::Renderer* renderer,
      Framework::StackAllocator* scratchAllocator,
      SceneGraph* sceneGraph){};

  virtual void uploadMaterials(){};
  virtual void submitDrawTask(ImguiUtil::ImguiService* imgui, enki::TaskScheduler* taskScheduler){};

  SceneGraph* sceneGraph;
  BufferHandle sceneCb;

  float globalScale = 1.f;
}; // struct RenderScene

} // namespace Graphics