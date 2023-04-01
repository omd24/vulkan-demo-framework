#include <Foundation/File.hpp>
#include <Foundation/Gltf.hpp>
#include <Foundation/Numerics.hpp>
#include <Foundation/ResourceManager.hpp>
#include <Foundation/Time.hpp>
#include <Foundation/String.hpp>

#include <Application/Window.hpp>
#include <Application/Input.hpp>
#include <Application/Keys.hpp>
#include <Application/GameCamera.hpp>

#include <Externals/cglm/struct/mat3.h>
#include <Externals/cglm/struct/mat4.h>
#include <Externals/cglm/struct/cam.h>
#include <Externals/cglm/struct/affine.h>

#include <Externals/imgui/imgui.h>
#include "Externals/stb_image.h"
#include <Externals/enkiTS/TaskScheduler.h>

#include <assimp/cimport.h>     // Plain-C interface
#include <assimp/scene.h>       // Output data structure
#include <assimp/postprocess.h> // Post processing flags

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
#define SHADER_FOLDER "\\Shaders\\"

Graphics::BufferHandle g_SceneCb;

static bool g_UseSecondaryCommandBuffers = false;

static const uint16_t INVALID_TEXTURE_INDEX = ~0;

struct MeshDraw
{
  Graphics::RendererUtil::Material* material;

  Graphics::BufferHandle indexBuffer;
  Graphics::BufferHandle positionBuffer;
  Graphics::BufferHandle tangentBuffer;
  Graphics::BufferHandle normalBuffer;
  Graphics::BufferHandle texcoordBuffer;
  Graphics::BufferHandle materialBuffer;

  VkIndexType indexType; // 32bit or 16bit type
  uint32_t indexOffset;
  uint32_t positionOffset;
  uint32_t tangentOffset;
  uint32_t normalOffset;
  uint32_t texcoordOffset;

  uint32_t primitiveCount;

  // Indices used for bindless textures.
  uint16_t diffuseTextureIndex;
  uint16_t roughnessTextureIndex;
  uint16_t normalTextureIndex;
  uint16_t occlusionTextureIndex;

  vec4s baseColorFactor;
  vec4s metallicRoughnessOcclusionFactor;
  vec3s scale;

  float alphaCutoff;
  uint32_t flags;

  Graphics::DescriptorSetHandle descriptorSet;
};

enum DrawFlags
{
  kDrawFlagsAlphaMask = 1 << 0, // two power by zero
};

struct UniformData
{
  mat4s viewProj;
  vec4s eye;
  vec4s light;
  float lightRange;
  float lightIntensity;
};

struct MeshData
{
  mat4s model;
  mat4s invModel;

  uint32_t textures[4]; // diffuse, roughness, normal, occlusion
  vec4s baseColorFactor;
  vec4s metallicRoughnessOcclusionFactor; // metallic, roughness, occlusion
  float alphaCutoff;
  float padding[3];
  uint32_t flags;
};

struct GpuEffect
{
  Graphics::PipelineHandle pipelineCull;
  Graphics::PipelineHandle pipelineNoCull;
};

struct ObjectMaterial
{
  vec4s diffuse;
  vec3s ambient;
  vec3s specular;
  float specularExp;

  float transparency;

  uint16_t diffuseTextureIndex = INVALID_TEXTURE_INDEX;
  uint16_t normalTextureIndex = INVALID_TEXTURE_INDEX;
};

struct ObjectDraw
{
  Graphics::BufferHandle geometryBufferCpu;
  Graphics::BufferHandle geometryBufferGpu;
  Graphics::BufferHandle meshBuffer;

  Graphics::DescriptorSetHandle descriptorSet;

  uint32_t indexOffset;
  uint32_t positionOffset;
  uint32_t tangentOffset;
  uint32_t normalOffset;
  uint32_t texcoordOffset;

  uint32_t primitiveCount;

  vec4s diffuse;
  vec3s ambient;
  vec3s specular;
  float specularExp;
  float transparency;

  uint16_t diffuseTextureIndex = INVALID_TEXTURE_INDEX;
  uint16_t normalTextureIndex = INVALID_TEXTURE_INDEX;

  uint32_t uploadsQueued = 0;
  // TODO: this should be an atomic value
  uint32_t uploadsCompleted = 0;

  Graphics::RendererUtil::Material* material;
};

struct ObjectGpuData
{
  mat4s model;
  mat4s invModel;

  uint32_t textures[4];
  vec4s diffuse;
  vec3s specular;
  float specularExp;
  vec3s ambient;
};

struct FileLoadRequest
{
  char path[512];
  Graphics::TextureHandle texture = Graphics::kInvalidTexture;
  Graphics::BufferHandle buffer = Graphics::kInvalidBuffer;
};

struct UploadRequest
{
  void* data = nullptr;
  uint32_t* completed = nullptr;
  Graphics::TextureHandle texture = Graphics::kInvalidTexture;
  Graphics::BufferHandle cpuBuffer = Graphics::kInvalidBuffer;
  Graphics::BufferHandle gpuBuffer = Graphics::kInvalidBuffer;
};

static bool g_RecreatePerThreadDescriptors = false;

//---------------------------------------------------------------------------//
// Async Loader Interface
//---------------------------------------------------------------------------//
struct AsynchronousLoader
{

  void init(
      Graphics::RendererUtil::Renderer* p_Renderer,
      enki::TaskScheduler* p_TaskScheduler,
      Framework::Allocator* p_ResidentAllocator);
  void update(Framework::Allocator* p_ScratchAllocator);
  void shutdown();

  void requestTextureData(const char* p_Filename, Graphics::TextureHandle p_Texture);
  void requestBufferUpload(void* p_Data, Graphics::BufferHandle p_Buffer);
  void requestBufferCopy(
      Graphics::BufferHandle p_Src, Graphics::BufferHandle p_Dst, uint32_t* p_Completed);

  Framework::Allocator* m_Allocator = nullptr;
  Graphics::RendererUtil::Renderer* m_Renderer = nullptr;
  enki::TaskScheduler* m_TaskScheduler = nullptr;

  Framework::Array<FileLoadRequest> m_FileLoadRequests;
  Framework::Array<UploadRequest> m_UploadRequests;

  Graphics::Buffer* m_StagingBuffer = nullptr;

  std::atomic_size_t m_StagingBufferOffset;
  Graphics::TextureHandle m_TextureReady;
  Graphics::BufferHandle m_CpuBufferReady;
  Graphics::BufferHandle m_GpuBufferReady;
  uint32_t* m_Completed;

  VkCommandPool m_CommandPools[Graphics::GpuDevice::kMaxFrames];
  Graphics::CommandBuffer m_CommandBuffers[Graphics::GpuDevice::kMaxFrames];
  VkSemaphore m_TransferCompleteSemaphore;
  VkFence m_TransferFence;
};

//---------------------------------------------------------------------------//
/// Window message loop callback
static void inputOSMessagesCallback(void* p_OSEvent, void* p_UserData)
{
  Framework::InputService* input = (Framework::InputService*)p_UserData;
  input->onEvent(p_OSEvent);
}
//---------------------------------------------------------------------------//
// Local helpers
//---------------------------------------------------------------------------//
static void uploadMaterial(MeshData& p_MeshData, const MeshDraw& p_MeshDraw, const float p_Scale)
{
  p_MeshData.textures[0] = p_MeshDraw.diffuseTextureIndex;
  p_MeshData.textures[1] = p_MeshDraw.roughnessTextureIndex;
  p_MeshData.textures[2] = p_MeshDraw.normalTextureIndex;
  p_MeshData.textures[3] = p_MeshDraw.occlusionTextureIndex;
  p_MeshData.baseColorFactor = p_MeshDraw.baseColorFactor;
  p_MeshData.metallicRoughnessOcclusionFactor = p_MeshDraw.metallicRoughnessOcclusionFactor;
  p_MeshData.alphaCutoff = p_MeshDraw.alphaCutoff;
  p_MeshData.flags = p_MeshDraw.flags;

  // NOTE: for left-handed systems (as defined in cglm) need to invert positive and negative Z.
  mat4s model = glms_scale_make(glms_vec3_mul(p_MeshDraw.scale, {p_Scale, p_Scale, -p_Scale}));
  p_MeshData.model = model;
  p_MeshData.invModel = glms_mat4_inv(glms_mat4_transpose(model));
}
//---------------------------------------------------------------------------//
static void
uploadMaterial(ObjectGpuData& p_MeshData, const ObjectDraw& p_MeshDraw, const float p_Scale)
{
  p_MeshData.textures[0] = p_MeshDraw.diffuseTextureIndex;
  p_MeshData.textures[1] = p_MeshDraw.normalTextureIndex;
  p_MeshData.textures[2] = 0;
  p_MeshData.textures[3] = 0;
  p_MeshData.diffuse = p_MeshDraw.diffuse;
  p_MeshData.specular = p_MeshDraw.specular;
  p_MeshData.specularExp = p_MeshDraw.specularExp;
  p_MeshData.ambient = p_MeshDraw.ambient;

  // NOTE: for left-handed systems (as defined in cglm) need to invert positive and negative Z.
  mat4s model = glms_scale_make(vec3s({p_Scale, p_Scale, -p_Scale}));
  p_MeshData.model = model;
  p_MeshData.invModel = glms_mat4_inv(glms_mat4_transpose(model));
}

//---------------------------------------------------------------------------//
static void drawMesh(
    Graphics::RendererUtil::Renderer& p_Renderer,
    Graphics::CommandBuffer* p_CommandBuffers,
    MeshDraw& p_MeshDraw)
{
  // Descriptor Set
  if (g_RecreatePerThreadDescriptors)
  {
    Graphics::DescriptorSetCreation dsCreation{};
    dsCreation.buffer(g_SceneCb, 0).buffer(p_MeshDraw.materialBuffer, 1);
    Graphics::DescriptorSetHandle descriptorSet =
        p_Renderer.createDescriptorSet(p_CommandBuffers, p_MeshDraw.material, dsCreation);
    p_CommandBuffers->bindLocalDescriptorSet(&descriptorSet, 1, nullptr, 0);
  }
  else
  {
    p_CommandBuffers->bindLocalDescriptorSet(&p_MeshDraw.descriptorSet, 1, nullptr, 0);
  }

  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.positionBuffer, 0, p_MeshDraw.positionOffset);
  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.tangentBuffer, 1, p_MeshDraw.tangentOffset);
  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.normalBuffer, 2, p_MeshDraw.normalOffset);
  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.texcoordBuffer, 3, p_MeshDraw.texcoordOffset);
  p_CommandBuffers->bindIndexBuffer(p_MeshDraw.indexBuffer, p_MeshDraw.indexOffset);

  p_CommandBuffers->drawIndexed(
      Graphics::TopologyType::kTriangle, p_MeshDraw.primitiveCount, 1, 0, 0, 0);
}
//---------------------------------------------------------------------------//
static void drawMesh(
    Graphics::RendererUtil::Renderer& p_Renderer,
    Graphics::CommandBuffer* p_CommandBuffers,
    ObjectDraw& p_MeshDraw)
{
  // Descriptor Set
  if (g_RecreatePerThreadDescriptors)
  {
    Graphics::DescriptorSetCreation dsCreation{};
    dsCreation.buffer(g_SceneCb, 0).buffer(p_MeshDraw.geometryBufferGpu, 1);
    Graphics::DescriptorSetHandle descriptorSet =
        p_Renderer.createDescriptorSet(p_CommandBuffers, p_MeshDraw.material, dsCreation);
    p_CommandBuffers->bindLocalDescriptorSet(&descriptorSet, 1, nullptr, 0);
  }
  else
  {
    p_CommandBuffers->bindLocalDescriptorSet(&p_MeshDraw.descriptorSet, 1, nullptr, 0);
  }

  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.geometryBufferGpu, 0, p_MeshDraw.positionOffset);
  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.geometryBufferGpu, 1, p_MeshDraw.tangentOffset);
  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.geometryBufferGpu, 2, p_MeshDraw.normalOffset);
  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.geometryBufferGpu, 3, p_MeshDraw.texcoordOffset);
  p_CommandBuffers->bindIndexBuffer(p_MeshDraw.geometryBufferGpu, p_MeshDraw.indexOffset);

  p_CommandBuffers->drawIndexed(
      Graphics::TopologyType::kTriangle, p_MeshDraw.primitiveCount, 1, 0, 0, 0);
}
//---------------------------------------------------------------------------//
struct Scene
{
  virtual void load(
      const char* p_Filename,
      const char* p_Path,
      Framework::Allocator* p_ResidentAllocator,
      Framework::StackAllocator* p_TempAllocator,
      AsynchronousLoader* p_AsyncLoader){};
  virtual void freeGpuResources(Graphics::RendererUtil::Renderer* p_Renderer){};
  virtual void unload(Graphics::RendererUtil::Renderer* p_Renderer){};

  virtual void prepareDraws(
      Graphics::RendererUtil::Renderer* p_Renderer,
      Framework::StackAllocator* p_ScratchAllocator){};

  virtual void uploadMaterials(float p_ModelScale){};
  virtual void submitDrawTask(
      Graphics::ImguiUtil::ImguiService* p_Imgui, enki::TaskScheduler* p_TaskScheduler){};
};
//---------------------------------------------------------------------------//
struct glTFScene : public Scene
{
  void load(
      const char* p_Filename,
      const char* p_Path,
      Framework::Allocator* p_ResidentAllocator,
      Framework::StackAllocator* p_TempAllocator,
      AsynchronousLoader* p_AsyncLoader){};
  void freeGpuResources(Graphics::RendererUtil::Renderer* p_Renderer);
  void unload(Graphics::RendererUtil::Renderer* p_Renderer);

  void prepareDraws(
      Graphics::RendererUtil::Renderer* p_Renderer, Framework::StackAllocator* p_ScratchAllocator);

  void uploadMaterials(float p_ModelScale);
  void
  submitDrawTask(Graphics::ImguiUtil::ImguiService* p_Imgui, enki::TaskScheduler* p_TaskScheduler);

  Framework::Array<MeshDraw> m_MeshDraws;

  // All graphics resources used by the scene
  Framework::Array<Graphics::RendererUtil::TextureResource> m_Images;
  Framework::Array<Graphics::RendererUtil::SamplerResource> m_Samplers;
  Framework::Array<Graphics::RendererUtil::BufferResource> m_Buffers;

  Framework::glTF::glTF m_GltfScene; // Source gltf scene

  Graphics::RendererUtil::Renderer* m_Renderer;
}; // struct gltfScene
//---------------------------------------------------------------------------//
struct ObjectScene : public Scene
{
  void load(
      const char* p_Filename,
      const char* p_Path,
      Framework::Allocator* p_ResidentAllocator,
      Framework::StackAllocator* p_TempAllocator,
      AsynchronousLoader* p_AsyncLoader){};
  void freeGpuResources(Graphics::RendererUtil::Renderer* p_Renderer);
  void unload(Graphics::RendererUtil::Renderer* p_Renderer);

  void prepareDraws(
      Graphics::RendererUtil::Renderer* p_Renderer, Framework::StackAllocator* p_ScratchAllocator);

  void uploadMaterials(float p_ModelScale);
  void
  submitDrawTask(Graphics::ImguiUtil::ImguiService* p_Imgui, enki::TaskScheduler* p_TaskScheduler);

  uint32_t loadTexture(
      const char* p_TexturePath, const char* p_Path, Framework::StackAllocator* p_TempAllocator);

  Framework::Array<ObjectDraw> m_MeshDraws;

  // All graphics resources used by the scene
  Framework::Array<ObjectMaterial> m_Materials;
  Framework::Array<Graphics::RendererUtil::TextureResource> m_Images;
  Graphics::RendererUtil::SamplerResource* m_Sampler;

  AsynchronousLoader* m_AsyncLoader;

  Graphics::RendererUtil::Renderer* m_Renderer;
}; // struct ObjectScene
//---------------------------------------------------------------------------//
// Draw Tasks:
//---------------------------------------------------------------------------//
struct glTFDrawTask : public enki::ITaskSet
{
  Graphics::GpuDevice* m_GpuDevice = nullptr;
  Graphics::RendererUtil::Renderer* m_Renderer = nullptr;
  Graphics::ImguiUtil::ImguiService* m_Imgui = nullptr;
  glTFScene* m_Scene = nullptr;
  uint32_t m_ThreadId = 0;

  void init(
      Graphics::GpuDevice* p_GpuDevice,
      Graphics::RendererUtil::Renderer* p_Renderer,
      Graphics::ImguiUtil::ImguiService* p_Imgui,
      glTFScene* p_Scene)
  {
    m_GpuDevice = p_GpuDevice;
    m_Renderer = p_Renderer;
    m_Imgui = p_Imgui;
    m_Scene = p_Scene;
  }

  void ExecuteRange(enki::TaskSetPartition p_Range, uint32_t p_ThreadNum) override
  {
    m_ThreadId = p_ThreadNum;

    // TODO: improve getting a command buffer/pool
    Graphics::CommandBuffer* cmdbuf = m_GpuDevice->getCommandBuffer(p_ThreadNum, true);

    cmdbuf->clear(0.3f, 0.3f, 0.3f, 1.f);
    cmdbuf->clearDepthStencil(1.0f, 0);
    cmdbuf->bindPass(m_GpuDevice->m_SwapchainPass, false);
    cmdbuf->setScissor(nullptr);
    cmdbuf->setViewport(nullptr);

    Graphics::RendererUtil::Material* lastMaterial = nullptr;
    // TODO: loop by material so that we can deal with multiple passes
    for (uint32_t meshIndex = 0; meshIndex < m_Scene->m_MeshDraws.m_Size; ++meshIndex)
    {
      MeshDraw& meshDraw = m_Scene->m_MeshDraws[meshIndex];

      if (meshDraw.material != lastMaterial)
      {
        Graphics::PipelineHandle pipeline = m_Renderer->getPipeline(meshDraw.material);

        cmdbuf->bindPipeline(pipeline);

        lastMaterial = meshDraw.material;
      }

      drawMesh(*m_Renderer, cmdbuf, meshDraw);
    }

    m_Imgui->render(*cmdbuf, false);

    // Send commands to GPU
    m_GpuDevice->queueCommandBuffer(cmdbuf);
  }

}; // struct DrawTask
//---------------------------------------------------------------------------//
struct SecondaryDrawTask : public enki::ITaskSet
{

  Graphics::RendererUtil::Renderer* m_Renderer = nullptr;
  ObjectScene* m_Scene = nullptr;
  Graphics::CommandBuffer* m_Parent = nullptr;
  Graphics::CommandBuffer* m_CmdBuf = nullptr;
  uint32_t m_Start = 0;
  uint32_t m_End = 0;

  void init(
      ObjectScene* p_Scene,
      Graphics::RendererUtil::Renderer* p_Renderer,
      Graphics::CommandBuffer* p_Parent,
      uint32_t p_Start,
      uint32_t p_End)
  {
    m_Renderer = p_Renderer;
    m_Scene = p_Scene;
    m_Parent = p_Parent;
    m_Start = p_Start;
    m_End = p_End;
  }

  void ExecuteRange(enki::TaskSetPartition p_Range, uint32_t p_ThreadNum) override
  {
    using namespace Framework;

    m_CmdBuf = m_Renderer->m_GpuDevice->getSecondaryCommandBuffer(p_ThreadNum);

    // TODO: loop by material so that we can deal with multiple passes
    m_CmdBuf->beginSecondary(m_Parent->m_CurrentRenderPass);

    m_CmdBuf->setScissor(nullptr);
    m_CmdBuf->setViewport(nullptr);

    Graphics::RendererUtil::Material* lastMaterial = nullptr;
    for (uint32_t meshIndex = m_Start; meshIndex < m_End; ++meshIndex)
    {
      ObjectDraw& meshDraw = m_Scene->m_MeshDraws[meshIndex];

      if (meshDraw.uploadsQueued != meshDraw.uploadsCompleted)
      {
        continue;
      }

      if (meshDraw.material != lastMaterial)
      {
        Graphics::PipelineHandle pipeline = m_Renderer->getPipeline(meshDraw.material);

        m_CmdBuf->bindPipeline(pipeline);

        lastMaterial = meshDraw.material;
      }

      drawMesh(*m_Renderer, m_CmdBuf, meshDraw);
    }

    m_CmdBuf->end();
  }
}; // SecondaryDrawTask
//---------------------------------------------------------------------------//
struct ObjectDrawTask : public enki::ITaskSet
{

  enki::TaskScheduler* m_TaskScheduler = nullptr;
  Graphics::GpuDevice* m_GpuDevice = nullptr;
  Graphics::RendererUtil::Renderer* m_Renderer = nullptr;
  Graphics::ImguiUtil::ImguiService* m_Imgui = nullptr;
  ObjectScene* m_Scene = nullptr;
  uint32_t m_ThreadId = 0;
  bool m_UseSecondary = false;

  void init(
      enki::TaskScheduler* p_TaskScheduler,
      Graphics::GpuDevice* p_GpuDevice,
      Graphics::RendererUtil::Renderer* p_Renderer,
      Graphics::ImguiUtil::ImguiService* p_Imgui,
      ObjectScene* p_Scene,
      bool p_UseSecondary)
  {
    m_TaskScheduler = p_TaskScheduler;
    m_GpuDevice = p_GpuDevice;
    m_Renderer = p_Renderer;
    m_Imgui = p_Imgui;
    m_Scene = p_Scene;
    m_UseSecondary = p_UseSecondary;
  }
  void ExecuteRange(enki::TaskSetPartition p_Range, uint32_t p_ThreadNum) override
  {
    using namespace Graphics;

    m_ThreadId = p_ThreadNum;

    // TODO: improve getting a command buffer/pool
    CommandBuffer* cmdBuf = m_GpuDevice->getCommandBuffer(p_ThreadNum, true);

    cmdBuf->clear(0.3f, 0.3f, 0.3f, 1.f);
    cmdBuf->clearDepthStencil(1.0f, 0);
    cmdBuf->setScissor(nullptr);
    cmdBuf->setViewport(nullptr);
    cmdBuf->bindPass(m_GpuDevice->m_SwapchainPass, m_UseSecondary);

    if (m_UseSecondary)
    {
      static const uint32_t parallelRecordings = 4;
      uint32_t drawsPerSecondary = m_Scene->m_MeshDraws.m_Size / parallelRecordings;
      uint32_t offset = drawsPerSecondary * parallelRecordings;

      SecondaryDrawTask secondaryTasks[parallelRecordings]{};

      uint32_t start = 0;
      for (uint32_t secondaryIndex = 0; secondaryIndex < parallelRecordings; ++secondaryIndex)
      {
        SecondaryDrawTask& task = secondaryTasks[secondaryIndex];

        task.init(m_Scene, m_Renderer, cmdBuf, start, start + drawsPerSecondary);
        start += drawsPerSecondary;

        m_TaskScheduler->AddTaskSetToPipe(&task);
      }

      CommandBuffer* secCmdBuf = m_Renderer->m_GpuDevice->getSecondaryCommandBuffer(p_ThreadNum);

      secCmdBuf->beginSecondary(cmdBuf->m_CurrentRenderPass);

      secCmdBuf->setScissor(nullptr);
      secCmdBuf->setViewport(nullptr);

      RendererUtil::Material* lastMaterial = nullptr;
      // TODO: loop by material so that we can deal with multiple passes
      for (uint32_t meshIndex = offset; meshIndex < m_Scene->m_MeshDraws.m_Size; ++meshIndex)
      {
        ObjectDraw& meshDraw = m_Scene->m_MeshDraws[meshIndex];

        if (meshDraw.uploadsQueued != meshDraw.uploadsCompleted)
        {
          continue;
        }

        if (meshDraw.material != lastMaterial)
        {
          PipelineHandle pipeline = m_Renderer->getPipeline(meshDraw.material);

          secCmdBuf->bindPipeline(pipeline);

          lastMaterial = meshDraw.material;
        }

        drawMesh(*m_Renderer, secCmdBuf, meshDraw);
      }

      for (uint32_t secondaryIndex = 0; secondaryIndex < parallelRecordings; ++secondaryIndex)
      {
        SecondaryDrawTask& task = secondaryTasks[secondaryIndex];
        m_TaskScheduler->WaitforTask(&task);

        vkCmdExecuteCommands(cmdBuf->m_VulkanCmdBuffer, 1, &task.m_CmdBuf->m_VulkanCmdBuffer);
      }

      // NOTE: ImGui also has to use a secondary command buffer, vkCmdExecuteCommands is
      // the only allowed command. We don't need this if we use a different render pass above
      m_Imgui->render(*secCmdBuf, true);

      secCmdBuf->end();

      vkCmdExecuteCommands(
          cmdBuf->m_VulkanCmdBuffer /*primary command buffer*/,
          1,
          &secCmdBuf->m_VulkanCmdBuffer /*chained command buffers*/);

      cmdBuf->endCurrentRenderPass();
    }
    else
    {
      RendererUtil::Material* lastMaterial = nullptr;
      // TODO: loop by material so that we can deal with multiple passes
      for (uint32_t meshIndex = 0; meshIndex < m_Scene->m_MeshDraws.m_Size; ++meshIndex)
      {
        ObjectDraw& meshDraw = m_Scene->m_MeshDraws[meshIndex];

        if (meshDraw.uploadsQueued != meshDraw.uploadsCompleted)
        {
          continue;
        }

        if (meshDraw.material != lastMaterial)
        {
          PipelineHandle pipeline = m_Renderer->getPipeline(meshDraw.material);

          cmdBuf->bindPipeline(pipeline);

          lastMaterial = meshDraw.material;
        }

        drawMesh(*m_Renderer, cmdBuf, meshDraw);
      }

      m_Imgui->render(*cmdBuf, false);
    }

    // Send commands to GPU
    m_GpuDevice->queueCommandBuffer(cmdBuf);
  }
}; // struct ObjectDrawTask
//---------------------------------------------------------------------------//
// Helper methods
//---------------------------------------------------------------------------//
void getMeshVertexBuffer(
    glTFScene& scene,
    int accessorIndex,
    Graphics::BufferHandle& outBufferHandle,
    uint32_t& outBufferOffset)
{
  using namespace Framework;

  if (accessorIndex != -1)
  {
    glTF::Accessor& bufferAccessor = scene.m_GltfScene.accessors[accessorIndex];
    glTF::BufferView& bufferView = scene.m_GltfScene.bufferViews[bufferAccessor.bufferView];
    Graphics::RendererUtil::BufferResource& bufferGpu = scene.m_Buffers[bufferAccessor.bufferView];

    outBufferHandle = bufferGpu.m_Handle;
    outBufferOffset =
        bufferAccessor.byteOffset == glTF::INVALID_INT_VALUE ? 0 : bufferAccessor.byteOffset;
  }
}
//---------------------------------------------------------------------------//
bool getMeshMaterial(
    Graphics::RendererUtil::Renderer& p_Renderer,
    glTFScene& p_Scene,
    Framework::glTF::Material& p_Material,
    MeshDraw& p_MeshDraw)
{
  using namespace Graphics::RendererUtil;

  bool transparent = false;
  Graphics::GpuDevice& gpu = *p_Renderer.m_GpuDevice;

  if (p_Material.pbrMetallicRoughness != nullptr)
  {
    if (p_Material.pbrMetallicRoughness->baseColorFactorCount != 0)
    {
      assert(p_Material.pbrMetallicRoughness->baseColorFactorCount == 4);

      p_MeshDraw.baseColorFactor = {
          p_Material.pbrMetallicRoughness->baseColorFactor[0],
          p_Material.pbrMetallicRoughness->baseColorFactor[1],
          p_Material.pbrMetallicRoughness->baseColorFactor[2],
          p_Material.pbrMetallicRoughness->baseColorFactor[3],
      };
    }
    else
    {
      p_MeshDraw.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    if (p_Material.pbrMetallicRoughness->roughnessFactor != Framework::glTF::INVALID_FLOAT_VALUE)
    {
      p_MeshDraw.metallicRoughnessOcclusionFactor.x =
          p_Material.pbrMetallicRoughness->roughnessFactor;
    }
    else
    {
      p_MeshDraw.metallicRoughnessOcclusionFactor.x = 1.0f;
    }

    if (p_Material.alphaMode.m_Data != nullptr && strcmp(p_Material.alphaMode.m_Data, "MASK") == 0)
    {
      p_MeshDraw.flags |= kDrawFlagsAlphaMask;
      transparent = true;
    }

    if (p_Material.alphaCutoff != Framework::glTF::INVALID_FLOAT_VALUE)
    {
      p_MeshDraw.alphaCutoff = p_Material.alphaCutoff;
    }

    if (p_Material.pbrMetallicRoughness->metallicFactor != Framework::glTF::INVALID_FLOAT_VALUE)
    {
      p_MeshDraw.metallicRoughnessOcclusionFactor.y =
          p_Material.pbrMetallicRoughness->metallicFactor;
    }
    else
    {
      p_MeshDraw.metallicRoughnessOcclusionFactor.y = 1.0f;
    }

    if (p_Material.pbrMetallicRoughness->baseColorTexture != nullptr)
    {
      Framework::glTF::Texture& diffuseTexture =
          p_Scene.m_GltfScene.textures[p_Material.pbrMetallicRoughness->baseColorTexture->index];
      TextureResource& diffuseTextureGpu = p_Scene.m_Images[diffuseTexture.source];
      SamplerResource& diffuseSamplerGpu = p_Scene.m_Samplers[diffuseTexture.sampler];

      p_MeshDraw.diffuseTextureIndex = diffuseTextureGpu.m_Handle.index;

      gpu.linkTextureSampler(diffuseTextureGpu.m_Handle, diffuseSamplerGpu.m_Handle);
    }
    else
    {
      p_MeshDraw.diffuseTextureIndex = INVALID_TEXTURE_INDEX;
    }

    if (p_Material.pbrMetallicRoughness->metallicRoughnessTexture != nullptr)
    {
      Framework::glTF::Texture& roughnessTexture =
          p_Scene.m_GltfScene
              .textures[p_Material.pbrMetallicRoughness->metallicRoughnessTexture->index];
      TextureResource& roughnessTextureGpu = p_Scene.m_Images[roughnessTexture.source];
      SamplerResource& roughnessSamplerGpu = p_Scene.m_Samplers[roughnessTexture.sampler];

      p_MeshDraw.roughnessTextureIndex = roughnessTextureGpu.m_Handle.index;

      gpu.linkTextureSampler(roughnessTextureGpu.m_Handle, roughnessSamplerGpu.m_Handle);
    }
    else
    {
      p_MeshDraw.roughnessTextureIndex = INVALID_TEXTURE_INDEX;
    }
  }

  if (p_Material.occlusionTexture != nullptr)
  {
    Framework::glTF::Texture& occlusionTexture =
        p_Scene.m_GltfScene.textures[p_Material.occlusionTexture->index];

    TextureResource& occlusionTextureGpu = p_Scene.m_Images[occlusionTexture.source];
    SamplerResource& occlusionSamplerGpu = p_Scene.m_Samplers[occlusionTexture.sampler];

    p_MeshDraw.occlusionTextureIndex = occlusionTextureGpu.m_Handle.index;

    if (p_Material.occlusionTexture->strength != Framework::glTF::INVALID_FLOAT_VALUE)
    {
      p_MeshDraw.metallicRoughnessOcclusionFactor.z = p_Material.occlusionTexture->strength;
    }
    else
    {
      p_MeshDraw.metallicRoughnessOcclusionFactor.z = 1.0f;
    }

    gpu.linkTextureSampler(occlusionTextureGpu.m_Handle, occlusionSamplerGpu.m_Handle);
  }
  else
  {
    p_MeshDraw.occlusionTextureIndex = INVALID_TEXTURE_INDEX;
  }

  if (p_Material.normalTexture != nullptr)
  {
    Framework::glTF::Texture& normalTexture =
        p_Scene.m_GltfScene.textures[p_Material.normalTexture->index];
    TextureResource& normalTextureGpu = p_Scene.m_Images[normalTexture.source];
    SamplerResource& normalSamplerGpu = p_Scene.m_Samplers[normalTexture.sampler];

    gpu.linkTextureSampler(normalTextureGpu.m_Handle, normalSamplerGpu.m_Handle);

    p_MeshDraw.normalTextureIndex = normalTextureGpu.m_Handle.index;
  }
  else
  {
    p_MeshDraw.normalTextureIndex = INVALID_TEXTURE_INDEX;
  }

  // Create material buffer
  Graphics::BufferCreation bufferCreation;
  bufferCreation.reset()
      .set(
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          Graphics::ResourceUsageType::kDynamic,
          sizeof(MeshData))
      .setName("Mesh Data");
  p_MeshDraw.materialBuffer = gpu.createBuffer(bufferCreation);

  return transparent;
}
int objectMeshMaterialCompare(const void* a, const void* b)
{
  const ObjectDraw* meshA = (const ObjectDraw*)a;
  const ObjectDraw* meshB = (const ObjectDraw*)b;

  if (meshA->material->m_RenderIndex < meshB->material->m_RenderIndex)
    return -1;
  if (meshA->material->m_RenderIndex > meshB->material->m_RenderIndex)
    return 1;
  return 0;
}
//---------------------------------------------------------------------------//
int gltfMeshMaterialCompare(const void* a, const void* b)
{
  const MeshDraw* meshA = (const MeshDraw*)a;
  const MeshDraw* meshB = (const MeshDraw*)b;

  if (meshA->material->m_RenderIndex < meshB->material->m_RenderIndex)
    return -1;
  if (meshA->material->m_RenderIndex > meshB->material->m_RenderIndex)
    return 1;
  return 0;
}
//---------------------------------------------------------------------------//
// Gltf scene impl
//---------------------------------------------------------------------------//
void glTFScene::load(
    const char* p_Filename,
    const char* p_Path,
    Framework::Allocator* p_ResidentAllocator,
    Framework::StackAllocator* p_TempAllocator,
    AsynchronousLoader* p_AsyncLoader)
{
  using namespace Framework;

  m_Renderer = p_AsyncLoader->m_Renderer;
  enki::TaskScheduler* taskScheduler = p_AsyncLoader->m_TaskScheduler;

  // Time statistics
  int64_t startSceneLoading = Time::getCurrentTime();

  m_GltfScene = Framework::gltfLoadFile(p_Filename);

  int64_t endLoadingFile = Time::getCurrentTime();

  // Load all textures
  m_Images.init(p_ResidentAllocator, m_GltfScene.imagesCount);

  Array<Graphics::TextureCreation> textures;
  textures.init(p_TempAllocator, m_GltfScene.imagesCount, m_GltfScene.imagesCount);

  StringBuffer nameBuffer;
  nameBuffer.init(4096, p_TempAllocator);

  for (uint32_t imageIndex = 0; imageIndex < m_GltfScene.imagesCount; ++imageIndex)
  {
    Framework::glTF::Image& image = m_GltfScene.images[imageIndex];

    int comp, width, height;

    stbi_info(image.uri.m_Data, &width, &height, &comp);

    uint32_t mipLevels = 1;
    if (true)
    {
      uint32_t w = width;
      uint32_t h = height;

      while (w > 1 && h > 1)
      {
        w /= 2;
        h /= 2;

        ++mipLevels;
      }
    }

    Graphics::TextureCreation texture;
    texture.setData(nullptr)
        .setFormatType(VK_FORMAT_R8G8B8A8_UNORM, Graphics::TextureType::kTexture2D)
        .setFlags(mipLevels, 0)
        .setSize((uint16_t)width, (uint16_t)height, 1)
        .setName(image.uri.m_Data);
    Graphics::RendererUtil::TextureResource* texRes = m_Renderer->createTexture(texture);
    assert(texRes != nullptr);

    m_Images.push(*texRes);

    // Reconstruct file path
    char* fullFilename = nameBuffer.appendUseFormatted("%s%s", p_Path, image.uri.m_Data);
    p_AsyncLoader->requestTextureData(fullFilename, texRes->m_Handle);
    // Reset name buffer
    nameBuffer.clear();
  }

  int64_t endCreatingTextures = Time::getCurrentTime();

  // Load all samplers
  m_Samplers.init(p_ResidentAllocator, m_GltfScene.samplersCount);

  for (uint32_t samplerIndex = 0; samplerIndex < m_GltfScene.samplersCount; ++samplerIndex)
  {
    glTF::Sampler& sampler = m_GltfScene.samplers[samplerIndex];

    char* samplerName = nameBuffer.appendUseFormatted("sampler_%u", samplerIndex);

    Graphics::SamplerCreation creation;
    switch (sampler.minFilter)
    {
    case glTF::Sampler::NEAREST:
      creation.minFilter = VK_FILTER_NEAREST;
      break;
    case glTF::Sampler::LINEAR:
      creation.minFilter = VK_FILTER_LINEAR;
      break;
    case glTF::Sampler::LINEAR_MIPMAP_NEAREST:
      creation.minFilter = VK_FILTER_LINEAR;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case glTF::Sampler::LINEAR_MIPMAP_LINEAR:
      creation.minFilter = VK_FILTER_LINEAR;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    case glTF::Sampler::NEAREST_MIPMAP_NEAREST:
      creation.minFilter = VK_FILTER_NEAREST;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case glTF::Sampler::NEAREST_MIPMAP_LINEAR:
      creation.minFilter = VK_FILTER_NEAREST;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    }

    creation.magFilter =
        sampler.magFilter == glTF::Sampler::Filter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

    switch (sampler.wrapS)
    {
    case glTF::Sampler::CLAMP_TO_EDGE:
      creation.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      break;
    case glTF::Sampler::MIRRORED_REPEAT:
      creation.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      break;
    case glTF::Sampler::REPEAT:
      creation.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      break;
    }

    switch (sampler.wrapT)
    {
    case glTF::Sampler::CLAMP_TO_EDGE:
      creation.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      break;
    case glTF::Sampler::MIRRORED_REPEAT:
      creation.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      break;
    case glTF::Sampler::REPEAT:
      creation.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      break;
    }

    creation.name = samplerName;

    Graphics::RendererUtil::SamplerResource* sampRes = m_Renderer->createSampler(creation);
    assert(sampRes != nullptr);

    m_Samplers.push(*sampRes);
  }

  int64_t endCreatingSamplers = Time::getCurrentTime();

  // Temporary array of buffer data
  Framework::Array<void*> buffersData;
  buffersData.init(p_ResidentAllocator, m_GltfScene.buffersCount);

  for (uint32_t bufferIndex = 0; bufferIndex < m_GltfScene.buffersCount; ++bufferIndex)
  {
    glTF::Buffer& buffer = m_GltfScene.buffers[bufferIndex];

    FileReadResult bufferData = fileReadBinary(buffer.uri.m_Data, p_ResidentAllocator);
    buffersData.push(bufferData.data);
  }

  int64_t endReadingBuffersData = Time::getCurrentTime();

  // Load all buffers and initialize them with buffer data
  m_Buffers.init(p_ResidentAllocator, m_GltfScene.bufferViewsCount);

  for (uint32_t bufferIndex = 0; bufferIndex < m_GltfScene.bufferViewsCount; ++bufferIndex)
  {
    glTF::BufferView& buffer = m_GltfScene.bufferViews[bufferIndex];

    int offset = buffer.byteOffset;
    if (offset == glTF::INVALID_INT_VALUE)
    {
      offset = 0;
    }

    uint8_t* bufferData = (uint8_t*)buffersData[buffer.buffer] + offset;

    // NOTE: the target attribute of a BufferView is not mandatory, so we prepare for both
    // uses
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    char* bufferName = buffer.name.m_Data;
    if (bufferName == nullptr)
    {
      bufferName = nameBuffer.appendUseFormatted("buffer_%u", bufferIndex);
    }

    Graphics::RendererUtil::BufferResource* bufRes = m_Renderer->createBuffer(
        flags, Graphics::ResourceUsageType::kImmutable, buffer.byteLength, bufferData, bufferName);
    assert(bufRes != nullptr);

    m_Buffers.push(*bufRes);
  }

  for (uint32_t bufferIndex = 0; bufferIndex < m_GltfScene.buffersCount; ++bufferIndex)
  {
    void* buffer = buffersData[bufferIndex];
    p_ResidentAllocator->deallocate(buffer);
  }
  buffersData.shutdown();

  int64_t endCreatingBuffers = Time::getCurrentTime();

  // Init runtime meshes
  m_MeshDraws.init(p_ResidentAllocator, m_GltfScene.meshesCount);

  int64_t endLoading = Time::getCurrentTime();

  char msg[512]{};
  sprintf(
      msg,
      "Loaded scene %s in %f seconds.\nStats:\n\tReading GLTF file %f seconds\n\tTextures Creating "
      "%f seconds\n\tCreating Samplers %f seconds\n\tReading Buffers Data %f seconds\n\tCreating "
      "Buffers %f seconds\n",
      p_Filename,
      Time::deltaSeconds(startSceneLoading, endLoading),
      Time::deltaSeconds(startSceneLoading, endLoadingFile),
      Time::deltaSeconds(endLoadingFile, endCreatingTextures),
      Time::deltaSeconds(endCreatingTextures, endCreatingSamplers),
      Time::deltaSeconds(endCreatingSamplers, endReadingBuffersData),
      Time::deltaSeconds(endReadingBuffersData, endCreatingBuffers));
  OutputDebugStringA(msg);
}

void glTFScene::freeGpuResources(Graphics::RendererUtil::Renderer* p_Renderer)
{
  Graphics::GpuDevice& gpuDev = *p_Renderer->m_GpuDevice;

  for (uint32_t meshIndex = 0; meshIndex < m_MeshDraws.m_Size; ++meshIndex)
  {
    MeshDraw& meshDraw = m_MeshDraws[meshIndex];
    gpuDev.destroyBuffer(meshDraw.materialBuffer);

    gpuDev.destroyDescriptorSet(meshDraw.descriptorSet);
  }

  m_MeshDraws.shutdown();
}

void glTFScene::unload(Graphics::RendererUtil::Renderer* p_Renderer)
{
  Graphics::GpuDevice& gpuDev = *p_Renderer->m_GpuDevice;

  // Free scene buffers
  m_Samplers.shutdown();
  m_Images.shutdown();
  m_Buffers.shutdown();

  // NOTE: we can't destroy this sooner as textures and buffers
  // hold a pointer to the names stored here
  Framework::gltfFree(m_GltfScene);
}

void glTFScene::prepareDraws(
    Graphics::RendererUtil::Renderer* p_Renderer, Framework::StackAllocator* p_ScratchAllocator)
{
  using namespace Framework;

  // Create pipeline state
  Graphics::PipelineCreation pipelineCreation;

  StringBuffer pathBuffer;
  pathBuffer.init(1024, p_ScratchAllocator);

  Directory cwd{};
  Framework::directoryCurrent(&cwd);

  const char* vertFile = "main.vert.glsl";
  char* vertPath = pathBuffer.appendUseFormatted("%s%s%s", cwd.path, SHADER_FOLDER, vertFile);
  FileReadResult vertCode = fileReadText(vertPath, p_ScratchAllocator);

  const char* fragFile = "main.frag.glsl";
  char* fragPath = pathBuffer.appendUseFormatted("%s%s%s", cwd.path, SHADER_FOLDER, fragFile);
  FileReadResult fragCode = fileReadText(fragPath, p_ScratchAllocator);

  // Vertex input
  // TODO: could these be inferred from SPIR-V?
  pipelineCreation.vertexInput.addVertexAttribute(
      {0, 0, 0, Graphics::VertexComponentFormat::kFloat3}); // position
  pipelineCreation.vertexInput.addVertexStream({0, 12, Graphics::VertexInputRate::kPerVertex});

  pipelineCreation.vertexInput.addVertexAttribute(
      {1, 1, 0, Graphics::VertexComponentFormat::kFloat4}); // tangent
  pipelineCreation.vertexInput.addVertexStream({1, 16, Graphics::VertexInputRate::kPerVertex});

  pipelineCreation.vertexInput.addVertexAttribute(
      {2, 2, 0, Graphics::VertexComponentFormat::kFloat3}); // normal
  pipelineCreation.vertexInput.addVertexStream({2, 12, Graphics::VertexInputRate::kPerVertex});

  pipelineCreation.vertexInput.addVertexAttribute(
      {3, 3, 0, Graphics::VertexComponentFormat::kFloat2}); // texcoord
  pipelineCreation.vertexInput.addVertexStream({3, 8, Graphics::VertexInputRate::kPerVertex});

  // Render pass
  pipelineCreation.renderPass = p_Renderer->m_GpuDevice->m_SwapchainOutput;
  // Depth
  pipelineCreation.depthStencil.setDepth(true, VK_COMPARE_OP_LESS_OR_EQUAL);

  // Blend
  pipelineCreation.blendState.addBlendState().setColor(
      VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD);

  pipelineCreation.shaders.setName("main")
      .addStage(vertCode.data, vertCode.size, VK_SHADER_STAGE_VERTEX_BIT)
      .addStage(fragCode.data, fragCode.size, VK_SHADER_STAGE_FRAGMENT_BIT);

  // Constant buffer
  Graphics::BufferCreation bufferCreation;
  bufferCreation.reset()
      .set(
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          Graphics::ResourceUsageType::kDynamic,
          sizeof(UniformData))
      .setName("scene_cb");
  g_SceneCb = p_Renderer->m_GpuDevice->createBuffer(bufferCreation);

  pipelineCreation.name = "main_no_cull";
  Graphics::RendererUtil::Program* programNoCull = p_Renderer->createProgram({pipelineCreation});

  pipelineCreation.rasterization.cullMode = VK_CULL_MODE_BACK_BIT;

  pipelineCreation.name = "main_cull";
  Graphics::RendererUtil::Program* programCull = p_Renderer->createProgram({pipelineCreation});

  Graphics::RendererUtil::MaterialCreation materialCreation;

  materialCreation.setName("materialNoCullOpaque").setProgram(programNoCull).setRenderIndex(0);
  Graphics::RendererUtil::Material* materialNoCullOpaque =
      p_Renderer->createMaterial(materialCreation);

  materialCreation.setName("material_cull_opaque").setProgram(programCull).setRenderIndex(1);
  Graphics::RendererUtil::Material* materialCullOpaque =
      p_Renderer->createMaterial(materialCreation);

  materialCreation.setName("materialNoCullTransparent").setProgram(programNoCull).setRenderIndex(2);
  Graphics::RendererUtil::Material* materialNoCullTransparent =
      p_Renderer->createMaterial(materialCreation);

  materialCreation.setName("material_cull_transparent").setProgram(programCull).setRenderIndex(3);
  Graphics::RendererUtil::Material* materialCullTransparent =
      p_Renderer->createMaterial(materialCreation);

  pathBuffer.shutdown();
  p_ScratchAllocator->deallocate(vertCode.data);
  p_ScratchAllocator->deallocate(fragCode.data);

  glTF::Scene& rootGltfScene = m_GltfScene.scenes[m_GltfScene.scene];

  for (uint32_t node_index = 0; node_index < rootGltfScene.nodesCount; ++node_index)
  {
    glTF::Node& node = m_GltfScene.nodes[rootGltfScene.nodes[node_index]];

    if (node.mesh == glTF::INVALID_INT_VALUE)
    {
      continue;
    }

    // TODO: children

    glTF::Mesh& mesh = m_GltfScene.meshes[node.mesh];

    vec3s nodeScale{1.0f, 1.0f, 1.0f};
    if (node.scaleCount != 0)
    {
      assert(node.scaleCount == 3);
      nodeScale = vec3s{node.scale[0], node.scale[1], node.scale[2]};
    }

    // Gltf primitives are conceptually submeshes.
    for (uint32_t primitive_index = 0; primitive_index < mesh.primitivesCount; ++primitive_index)
    {
      MeshDraw meshDraw{};

      meshDraw.scale = nodeScale;

      glTF::MeshPrimitive& meshPrimitive = mesh.primitives[primitive_index];

      const int positionAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "POSITION");
      const int tangentAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "TANGENT");
      const int normalAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "NORMAL");
      const int texcoordAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "TEXCOORD_0");

      getMeshVertexBuffer(
          *this, positionAccessorIndex, meshDraw.positionBuffer, meshDraw.positionOffset);
      getMeshVertexBuffer(
          *this, tangentAccessorIndex, meshDraw.tangentBuffer, meshDraw.tangentOffset);
      getMeshVertexBuffer(*this, normalAccessorIndex, meshDraw.normalBuffer, meshDraw.normalOffset);
      getMeshVertexBuffer(
          *this, texcoordAccessorIndex, meshDraw.texcoordBuffer, meshDraw.texcoordOffset);

      // Create index buffer
      glTF::Accessor& indicesAccessor = m_GltfScene.accessors[meshPrimitive.indices];
      assert(
          indicesAccessor.componentType == glTF::Accessor::ComponentType::UNSIGNED_SHORT ||
          indicesAccessor.componentType == glTF::Accessor::ComponentType::UNSIGNED_INT);
      meshDraw.indexType =
          (indicesAccessor.componentType == glTF::Accessor::ComponentType::UNSIGNED_SHORT)
              ? VK_INDEX_TYPE_UINT16
              : VK_INDEX_TYPE_UINT32;

      glTF::BufferView& indicesBufferView = m_GltfScene.bufferViews[indicesAccessor.bufferView];
      Graphics::RendererUtil::BufferResource& indicesBufferGpu =
          m_Buffers[indicesAccessor.bufferView];
      meshDraw.indexBuffer = indicesBufferGpu.m_Handle;
      meshDraw.indexOffset =
          indicesAccessor.byteOffset == glTF::INVALID_INT_VALUE ? 0 : indicesAccessor.byteOffset;
      meshDraw.primitiveCount = indicesAccessor.count;

      // Create material
      glTF::Material& material = m_GltfScene.materials[meshPrimitive.material];

      bool transparent = getMeshMaterial(*p_Renderer, *this, material, meshDraw);

      Graphics::DescriptorSetCreation dsCreation{};
      Graphics::DescriptorSetLayoutHandle layout =
          m_Renderer->m_GpuDevice->getDescriptorSetLayout(programCull->m_Passes[0].pipeline, 0);
      dsCreation.buffer(g_SceneCb, 0).buffer(meshDraw.materialBuffer, 1).setLayout(layout);
      meshDraw.descriptorSet = m_Renderer->m_GpuDevice->createDescriptorSet(dsCreation);

      if (transparent)
      {
        if (material.doubleSided)
        {
          meshDraw.material = materialNoCullTransparent;
        }
        else
        {
          meshDraw.material = materialCullTransparent;
        }
      }
      else
      {
        if (material.doubleSided)
        {
          meshDraw.material = materialNoCullOpaque;
        }
        else
        {
          meshDraw.material = materialCullOpaque;
        }
      }

      m_MeshDraws.push(meshDraw);
    }
  }

  qsort(m_MeshDraws.m_Data, m_MeshDraws.m_Size, sizeof(MeshDraw), gltfMeshMaterialCompare);
}

void glTFScene::uploadMaterials(float p_ModelScale)
{
  // Update per mesh material buffer
  for (uint32_t meshIndex = 0; meshIndex < m_MeshDraws.m_Size; ++meshIndex)
  {
    MeshDraw& meshDraw = m_MeshDraws[meshIndex];

    Graphics::MapBufferParameters cbMap = {meshDraw.materialBuffer, 0, 0};
    MeshData* meshData = (MeshData*)m_Renderer->m_GpuDevice->mapBuffer(cbMap);
    if (meshData)
    {
      uploadMaterial(*meshData, meshDraw, p_ModelScale);

      m_Renderer->m_GpuDevice->unmapBuffer(cbMap);
    }
  }
}

void glTFScene::submitDrawTask(
    Graphics::ImguiUtil::ImguiService* p_Imgui, enki::TaskScheduler* p_TaskScheduler)
{
  glTFDrawTask drawTask;
  drawTask.init(m_Renderer->m_GpuDevice, m_Renderer, p_Imgui, this);
  p_TaskScheduler->AddTaskSetToPipe(&drawTask);
  p_TaskScheduler->WaitforTaskSet(&drawTask);

  // Avoid using the same command buffer
  m_Renderer->addTextureUpdateCommands(
      (drawTask.m_ThreadId + 1) % p_TaskScheduler->GetNumTaskThreads());
}
//---------------------------------------------------------------------------//
// ObjectScene Impl:
//---------------------------------------------------------------------------//
void ObjectScene::load(
    const char* p_Filename,
    const char* p_Path,
    Framework::Allocator* p_ResidentAllocator,
    Framework::StackAllocator* p_TempAllocator,
    AsynchronousLoader* p_AsyncLoader)
{
  using namespace Framework;

  m_AsyncLoader = p_AsyncLoader;
  m_Renderer = m_AsyncLoader->m_Renderer;

  enki::TaskScheduler* taskScheduler = m_AsyncLoader->m_TaskScheduler;

  // Time statistics
  int64_t startSceneLoading = Time::getCurrentTime();

  const struct aiScene* scene = aiImportFile(
      p_Filename,
      aiProcess_CalcTangentSpace | aiProcess_GenNormals | aiProcess_Triangulate |
          aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);

  int64_t endLoadingFile = Time::getCurrentTime();

  // If the import failed, report it
  if (scene == nullptr)
  {
    assert(false);
    return;
  }

  Graphics::SamplerCreation samplerCreation{};
  samplerCreation.setAddressModeUV(VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT)
      .setMinMagMip(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR);
  m_Sampler = m_Renderer->createSampler(samplerCreation);

  m_Images.init(p_ResidentAllocator, 1024);
  m_Materials.init(p_ResidentAllocator, scene->mNumMaterials);
  for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex)
  {
    aiMaterial* material = scene->mMaterials[materialIndex];

    ObjectMaterial mat{};

    aiString texture_file;

    if (aiGetMaterialString(material, AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0), &texture_file) ==
        AI_SUCCESS)
    {
      mat.diffuseTextureIndex = loadTexture(texture_file.C_Str(), p_Path, p_TempAllocator);
    }

    if (aiGetMaterialString(material, AI_MATKEY_TEXTURE(aiTextureType_NORMALS, 0), &texture_file) ==
        AI_SUCCESS)
    {
      mat.normalTextureIndex = loadTexture(texture_file.C_Str(), p_Path, p_TempAllocator);
    }

    aiColor4D color;
    if (aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS)
    {
      mat.diffuse = {color.r, color.g, color.b, 1.0f};
    }

    if (aiGetMaterialColor(material, AI_MATKEY_COLOR_AMBIENT, &color) == AI_SUCCESS)
    {
      mat.ambient = {color.r, color.g, color.b};
    }

    if (aiGetMaterialColor(material, AI_MATKEY_COLOR_SPECULAR, &color) == AI_SUCCESS)
    {
      mat.specular = {color.r, color.g, color.b};
    }

    float f_value;
    if (aiGetMaterialFloat(material, AI_MATKEY_SHININESS, &f_value) == AI_SUCCESS)
    {
      mat.specularExp = f_value;
    }

    if (aiGetMaterialFloat(material, AI_MATKEY_OPACITY, &f_value) == AI_SUCCESS)
    {
      mat.transparency = f_value;
      mat.diffuse.w = f_value;
    }

    m_Materials.push(mat);
  }

  int64_t endCreatingTextures = Time::getCurrentTime();

  // Init runtime meshes
  m_MeshDraws.init(p_ResidentAllocator, scene->mNumMeshes);
  for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
  {
    aiMesh* mesh = scene->mMeshes[meshIndex];

    assert((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) != 0);

    Array<vec3s> positions;
    positions.init(p_ResidentAllocator, mesh->mNumVertices);
    Array<vec4s> tangents;
    tangents.init(p_ResidentAllocator, mesh->mNumVertices);
    Array<vec3s> normals;
    normals.init(p_ResidentAllocator, mesh->mNumVertices);
    Array<vec2s> uv_coords;
    uv_coords.init(p_ResidentAllocator, mesh->mNumVertices);
    for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
    {
      positions.push(vec3s{
          mesh->mVertices[vertexIndex].x,
          mesh->mVertices[vertexIndex].y,
          mesh->mVertices[vertexIndex].z});

      tangents.push(vec4s{
          mesh->mTangents[vertexIndex].x,
          mesh->mTangents[vertexIndex].y,
          mesh->mTangents[vertexIndex].z,
          1.0f});

      uv_coords.push(vec2s{
          mesh->mTextureCoords[0][vertexIndex].x,
          mesh->mTextureCoords[0][vertexIndex].y,
      });

      normals.push(vec3s{
          mesh->mNormals[vertexIndex].x,
          mesh->mNormals[vertexIndex].y,
          mesh->mNormals[vertexIndex].z});
    }

    Array<uint32_t> indices;
    indices.init(p_ResidentAllocator, mesh->mNumFaces * 3);
    for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
    {
      assert(mesh->mFaces[faceIndex].mNumIndices == 3);

      indices.push(mesh->mFaces[faceIndex].mIndices[0]);
      indices.push(mesh->mFaces[faceIndex].mIndices[1]);
      indices.push(mesh->mFaces[faceIndex].mIndices[2]);
    }

    size_t bufferSize = (indices.m_Size * sizeof(uint32_t)) + (positions.m_Size * sizeof(vec3s)) +
                        (normals.m_Size * sizeof(vec3s)) + (tangents.m_Size * sizeof(vec4s)) +
                        (uv_coords.m_Size * sizeof(vec2s));

    // NOTE: the target attribute of a BufferView is not mandatory, so we prepare for both
    // uses
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    Graphics::BufferCreation creation{};
    creation.set(flags, Graphics::ResourceUsageType::kImmutable, bufferSize)
        .setPersistent(true)
        .setName(nullptr);

    Graphics::BufferHandle buf = m_Renderer->m_GpuDevice->createBuffer(creation);

    Graphics::Buffer* buffer =
        (Graphics::Buffer*)m_Renderer->m_GpuDevice->m_Buffers.accessResource(buf.index);

    ObjectDraw& objMesh = m_MeshDraws.pushUse();
    memset(&objMesh, 0, sizeof(ObjectDraw));

    objMesh.geometryBufferCpu = buf;

    size_t offset = 0;

    memcpy(buffer->mappedData + offset, indices.m_Data, indices.m_Size * sizeof(uint32_t));
    objMesh.indexOffset = offset;
    offset += indices.m_Size * sizeof(uint32_t);

    memcpy(buffer->mappedData + offset, positions.m_Data, positions.m_Size * sizeof(vec3s));
    objMesh.positionOffset = offset;
    offset += positions.m_Size * sizeof(vec3s);

    memcpy(buffer->mappedData + offset, tangents.m_Data, tangents.m_Size * sizeof(vec4s));
    objMesh.tangentOffset = offset;
    offset += tangents.m_Size * sizeof(vec4s);

    memcpy(buffer->mappedData + offset, normals.m_Data, normals.m_Size * sizeof(vec3s));
    objMesh.normalOffset = offset;
    offset += normals.m_Size * sizeof(vec3s);

    memcpy(buffer->mappedData + offset, uv_coords.m_Data, uv_coords.m_Size * sizeof(vec2s));
    objMesh.texcoordOffset = offset;

    creation.reset()
        .set(flags, Graphics::ResourceUsageType::kImmutable, bufferSize)
        .setDeviceOnly(true)
        .setName(nullptr);
    buf = m_Renderer->m_GpuDevice->createBuffer(creation);
    objMesh.geometryBufferGpu = buf;

    // TODO: ideally the CPU buffer would be using staging memory and
    // freed after it has been copied!
    m_AsyncLoader->requestBufferCopy(
        objMesh.geometryBufferCpu, objMesh.geometryBufferGpu, &objMesh.uploadsCompleted);
    objMesh.uploadsQueued++;

    objMesh.primitiveCount = mesh->mNumFaces * 3;

    ObjectMaterial& material = m_Materials[mesh->mMaterialIndex];

    objMesh.diffuse = material.diffuse;
    objMesh.ambient = material.ambient;
    objMesh.specular = material.ambient;
    objMesh.specularExp = material.specularExp;

    objMesh.diffuseTextureIndex = material.diffuseTextureIndex;
    objMesh.normalTextureIndex = material.normalTextureIndex;

    objMesh.transparency = material.transparency;

    creation.reset();
    creation
        .set(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            Graphics::ResourceUsageType::kDynamic,
            sizeof(ObjectGpuData))
        .setName("meshData");

    objMesh.meshBuffer = m_Renderer->m_GpuDevice->createBuffer(creation);

    positions.shutdown();
    normals.shutdown();
    uv_coords.shutdown();
    tangents.shutdown();
    indices.shutdown();
  }

  int64_t endReadingBuffersData = Time::getCurrentTime();

  int64_t endCreatingBuffers = Time::getCurrentTime();

  int64_t endLoading = Time::getCurrentTime();

  char msg[512]{};
  sprintf(
      msg,
      "Loaded scene %s in %f seconds.\nStats:\n\tReading GLTF file %f seconds\n\tTextures Creating "
      "%f seconds\n\tReading Buffers Data %f seconds\n\tCreating Buffers %f seconds\n",
      p_Filename,
      Time::deltaSeconds(startSceneLoading, endLoading),
      Time::deltaSeconds(startSceneLoading, endLoadingFile),
      Time::deltaSeconds(endLoadingFile, endCreatingTextures),
      Time::deltaSeconds(endCreatingTextures, endReadingBuffersData),
      Time::deltaSeconds(endReadingBuffersData, endCreatingBuffers));
  OutputDebugStringA(msg);

  // We're done. Release all resources associated with this import
  aiReleaseImport(scene);
}

uint32_t ObjectScene::loadTexture(
    const char* p_TexturePath, const char* p_Path, Framework::StackAllocator* p_TempAllocator)
{
  using namespace Framework;

  int comp, width, height;

  stbi_info(p_TexturePath, &width, &height, &comp);

  uint32_t mip_levels = 1;
  if (true)
  {
    uint32_t w = width;
    uint32_t h = height;

    while (w > 1 && h > 1)
    {
      w /= 2;
      h /= 2;

      ++mip_levels;
    }
  }

  Graphics::TextureCreation tex;
  tex.setData(nullptr)
      .setFormatType(VK_FORMAT_R8G8B8A8_UNORM, Graphics::TextureType::kTexture2D)
      .setFlags(mip_levels, 0)
      .setSize((uint16_t)width, (uint16_t)height, 1)
      .setName(nullptr);
  Graphics::RendererUtil::TextureResource* texRes = m_Renderer->createTexture(tex);
  assert(texRes != nullptr);

  m_Images.push(*texRes);

  m_Renderer->m_GpuDevice->linkTextureSampler(texRes->m_Handle, m_Sampler->m_Handle);

  StringBuffer nameBuffer;
  nameBuffer.init(4096, p_TempAllocator);

  // Reconstruct file path
  char* fullFilename = nameBuffer.appendUseFormatted("%s%s", p_Path, p_TexturePath);
  m_AsyncLoader->requestTextureData(fullFilename, texRes->m_Handle);
  // Reset name buffer
  nameBuffer.clear();

  return texRes->m_Handle.index;
}

void ObjectScene::freeGpuResources(Graphics::RendererUtil::Renderer* p_Renderer)
{
  Graphics::GpuDevice& gpuDev = *m_Renderer->m_GpuDevice;

  for (uint32_t meshIndex = 0; meshIndex < m_MeshDraws.m_Size; ++meshIndex)
  {
    ObjectDraw& meshDraw = m_MeshDraws[meshIndex];
    gpuDev.destroyBuffer(meshDraw.geometryBufferCpu);
    gpuDev.destroyBuffer(meshDraw.geometryBufferGpu);
    gpuDev.destroyBuffer(meshDraw.meshBuffer);

    gpuDev.destroyDescriptorSet(meshDraw.descriptorSet);
  }

  for (uint32_t texture_index = 0; texture_index < m_Images.m_Size; ++texture_index)
  {
    m_Renderer->destroyTexture(m_Images.m_Data + texture_index);
  }

  m_Renderer->destroySampler(m_Sampler);

  m_MeshDraws.shutdown();
}

void ObjectScene::unload(Graphics::RendererUtil::Renderer* p_Renderer)
{
  // Free scene buffers
  m_Images.shutdown();
}

void ObjectScene::uploadMaterials(float p_ModelScale)
{
  // Update per mesh material buffer
  for (uint32_t meshIndex = 0; meshIndex < m_MeshDraws.m_Size; ++meshIndex)
  {
    ObjectDraw& meshDraw = m_MeshDraws[meshIndex];

    Graphics::MapBufferParameters cbMap = {meshDraw.meshBuffer, 0, 0};
    ObjectGpuData* meshData = (ObjectGpuData*)m_Renderer->m_GpuDevice->mapBuffer(cbMap);
    if (meshData)
    {
      uploadMaterial(*meshData, meshDraw, p_ModelScale);

      m_Renderer->m_GpuDevice->unmapBuffer(cbMap);
    }
  }
}

void ObjectScene::submitDrawTask(
    Graphics::ImguiUtil::ImguiService* p_Imgui, enki::TaskScheduler* p_TaskScheduler)
{
  ObjectDrawTask drawTask;
  drawTask.init(
      p_TaskScheduler,
      m_Renderer->m_GpuDevice,
      m_Renderer,
      p_Imgui,
      this,
      g_UseSecondaryCommandBuffers);
  p_TaskScheduler->AddTaskSetToPipe(&drawTask);
  p_TaskScheduler->WaitforTaskSet(&drawTask);

  // Avoid using the same command buffer
  m_Renderer->addTextureUpdateCommands(
      (drawTask.m_ThreadId + 1) % p_TaskScheduler->GetNumTaskThreads());
}

void ObjectScene::prepareDraws(
    Graphics::RendererUtil::Renderer* p_Renderer, Framework::StackAllocator* p_ScratchAllocator)
{
  using namespace Framework;

  // Create pipeline state
  Graphics::PipelineCreation pipelineCreation;

  StringBuffer pathBuffer;
  pathBuffer.init(1024, p_ScratchAllocator);

  Directory cwd{};
  Framework::directoryCurrent(&cwd);

  const char* vertFile = "phong.vert.glsl";
  char* vertPath = pathBuffer.appendUseFormatted("%s%s%s", cwd.path, SHADER_FOLDER, vertFile);
  FileReadResult vertCode = fileReadText(vertPath, p_ScratchAllocator);

  const char* fragFile = "phong.frag.glsl";
  char* fragPath = pathBuffer.appendUseFormatted("%s%s%s", cwd.path, SHADER_FOLDER, fragFile);
  FileReadResult fragCode = fileReadText(fragPath, p_ScratchAllocator);

  // Vertex input
  // TODO: could these be inferred from SPIR-V?
  pipelineCreation.vertexInput.addVertexAttribute(
      {0, 0, 0, Graphics::VertexComponentFormat::kFloat3}); // position
  pipelineCreation.vertexInput.addVertexStream({0, 12, Graphics::VertexInputRate::kPerVertex});

  pipelineCreation.vertexInput.addVertexAttribute(
      {1, 1, 0, Graphics::VertexComponentFormat::kFloat4}); // tangent
  pipelineCreation.vertexInput.addVertexStream({1, 16, Graphics::VertexInputRate::kPerVertex});

  pipelineCreation.vertexInput.addVertexAttribute(
      {2, 2, 0, Graphics::VertexComponentFormat::kFloat3}); // normal
  pipelineCreation.vertexInput.addVertexStream({2, 12, Graphics::VertexInputRate::kPerVertex});

  pipelineCreation.vertexInput.addVertexAttribute(
      {3, 3, 0, Graphics::VertexComponentFormat::kFloat2}); // texcoord
  pipelineCreation.vertexInput.addVertexStream({3, 8, Graphics::VertexInputRate::kPerVertex});

  // Render pass
  pipelineCreation.renderPass = m_Renderer->m_GpuDevice->m_SwapchainOutput;
  // Depth
  pipelineCreation.depthStencil.setDepth(true, VK_COMPARE_OP_LESS_OR_EQUAL);

  pipelineCreation.shaders.setName("main")
      .addStage(vertCode.data, vertCode.size, VK_SHADER_STAGE_VERTEX_BIT)
      .addStage(fragCode.data, fragCode.size, VK_SHADER_STAGE_FRAGMENT_BIT);

  pipelineCreation.rasterization.cullMode = VK_CULL_MODE_BACK_BIT;

  // Constant buffer
  Graphics::BufferCreation bufferCreation;
  bufferCreation.reset()
      .set(
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          Graphics::ResourceUsageType::kDynamic,
          sizeof(UniformData))
      .setName("scene_cb");
  g_SceneCb = m_Renderer->m_GpuDevice->createBuffer(bufferCreation);

  pipelineCreation.name = "phong_opaque";
  Graphics::RendererUtil::Program* programOpqaue = m_Renderer->createProgram({pipelineCreation});

  // Blend
  pipelineCreation.name = "phong_transparent";
  pipelineCreation.blendState.addBlendState().setColor(
      VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD);
  Graphics::RendererUtil::Program* programTransparent =
      m_Renderer->createProgram({pipelineCreation});

  Graphics::RendererUtil::MaterialCreation materialCreation;

  materialCreation.setName("material_phong_opaque").setProgram(programOpqaue).setRenderIndex(0);
  Graphics::RendererUtil::Material* phongMaterialOpaque =
      m_Renderer->createMaterial(materialCreation);

  materialCreation.setName("material_phong_transparent")
      .setProgram(programTransparent)
      .setRenderIndex(1);
  Graphics::RendererUtil::Material* phongMaterialTranparent =
      m_Renderer->createMaterial(materialCreation);

  for (uint32_t meshIndex = 0; meshIndex < m_MeshDraws.m_Size; ++meshIndex)
  {
    ObjectDraw& meshDraw = m_MeshDraws[meshIndex];

    if (meshDraw.transparency == 1.0f)
    {
      meshDraw.material = phongMaterialOpaque;
    }
    else
    {
      meshDraw.material = phongMaterialTranparent;
    }

    // Descriptor Set
    Graphics::DescriptorSetCreation dsCreation{};
    dsCreation.setLayout(meshDraw.material->m_Program->m_Passes[0].descriptorSetLayout);
    dsCreation.buffer(g_SceneCb, 0).buffer(meshDraw.meshBuffer, 1);
    meshDraw.descriptorSet = m_Renderer->m_GpuDevice->createDescriptorSet(dsCreation);
  }

  qsort(m_MeshDraws.m_Data, m_MeshDraws.m_Size, sizeof(ObjectDraw), objectMeshMaterialCompare);
}
//---------------------------------------------------------------------------//
// AsynchronousLoader impl:
//---------------------------------------------------------------------------//

//
//
//
//
//

//---------------------------------------------------------------------------//
int main(int argc, char** argv)
{

  // if (argc < 2)
  //{
  //  printf("No model specified, using the default model\n");
  //  exit(-1);
  //}

  const char* modelPath = "c:/gltf-models/Sponza/Sponza.gltf";

  using namespace Framework;
  using namespace Graphics;

  // Init services
  MemoryService::instance()->init(nullptr);
  Allocator* allocator = &MemoryService::instance()->m_SystemAllocator;

  StackAllocator scratchAllocator;
  scratchAllocator.init(FRAMEWORK_MEGA(8));

  // window
  WindowConfiguration wconf{1280, 800, "Demo 03", &MemoryService::instance()->m_SystemAllocator};
  Framework::Window window;
  window.init(&wconf);

  InputService input;
  input.init(allocator);

  // Callback register: input needs to react to OS messages.
  window.registerOSMessagesCallback(inputOSMessagesCallback, &input);

  // graphics
  DeviceCreation dc;
  dc.setWindow(window.m_Width, window.m_Height, window.m_PlatformHandle)
      .setAllocator(allocator)
      .setTemporaryAllocator(&scratchAllocator);
  GpuDevice gpu;
  gpu.init(dc);

  ResourceManager rm;
  rm.init(allocator, nullptr);

  RendererUtil::Renderer p_Renderer;
  p_Renderer.init({&gpu, allocator});
  renderer.setLoaders(&rm);

  ImguiUtil::ImguiService* imgui = ImguiUtil::ImguiService::instance();
  ImguiUtil::ImguiServiceConfiguration imguiConfig{&gpu, window.m_PlatformHandle};
  imgui->init(&imguiConfig);

  GameCamera gameCamera{};
  gameCamera.camera.initPerpective(0.1f, 4000.f, 60.f, wconf.m_Width * 1.f / wconf.m_Height);
  gameCamera.init(true, 20.f, 6.f, 0.1f);

  Time::serviceInit();

  Directory cwd{};
  directoryCurrent(&cwd);

  char gltfBasePath[512]{};
  // memcpy(gltfBasePath, argv[1], strlen(argv[1]));
  memcpy(gltfBasePath, modelPath, strlen(modelPath));
  fileDirectoryFromPath(gltfBasePath);

  directoryChange(gltfBasePath);

  char gltfFile[512]{};
  // memcpy(gltfFile, argv[1], strlen(argv[1]));
  memcpy(gltfFile, modelPath, strlen(modelPath));
  p_FilenameFromPath(gltfFile);

  Scene scene;
  sceneLoadFromGltf(gltfFile, renderer, allocator, scene);

  // NOTE: restore working directory
  directoryChange(cwd.path);

  {
    // Create pipeline state
    PipelineCreation pipelineCreation;

    StringBuffer pathBuffer;
    pathBuffer.init(1024, allocator);

    const char* vertFile = "main.vert.glsl";
    char* vertPath = pathBuffer.appendUseFormatted("%s%s%s", cwd.path, SHADER_FOLDER, vertFile);
    FileReadResult vertCode = fileReadText(vertPath, allocator);

    const char* fragFile = "main.frag.glsl";
    char* fragPath = pathBuffer.appendUseFormatted("%s%s%s", cwd.path, SHADER_FOLDER, fragFile);
    FileReadResult fragCode = fileReadText(fragPath, allocator);

    // Vertex input
    // TODO: could these be inferred from SPIR-V?
    pipelineCreation.vertexInput.addVertexAttribute(
        {0, 0, 0, Graphics::VertexComponentFormat::kFloat3}); // position
    pipelineCreation.vertexInput.addVertexStream({0, 12, VertexInputRate::kPerVertex});

    pipelineCreation.vertexInput.addVertexAttribute(
        {1, 1, 0, Graphics::VertexComponentFormat::kFloat4}); // tangent
    pipelineCreation.vertexInput.addVertexStream({1, 16, VertexInputRate::kPerVertex});

    pipelineCreation.vertexInput.addVertexAttribute(
        {2, 2, 0, Graphics::VertexComponentFormat::kFloat3}); // normal
    pipelineCreation.vertexInput.addVertexStream({2, 12, VertexInputRate::kPerVertex});

    pipelineCreation.vertexInput.addVertexAttribute(
        {3, 3, 0, Graphics::VertexComponentFormat::kFloat2}); // texcoord
    pipelineCreation.vertexInput.addVertexStream({3, 8, VertexInputRate::kPerVertex});

    // Render pass
    pipelineCreation.renderPass = gpu.m_SwapchainOutput;
    // Depth
    pipelineCreation.depthStencil.setDepth(true, VK_COMPARE_OP_LESS_OR_EQUAL);

    // Blend
    pipelineCreation.blendState.addBlendState().setColor(
        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD);

    pipelineCreation.shaders.setName("main")
        .addStage(vertCode.data, (uint32_t)vertCode.size, VK_SHADER_STAGE_VERTEX_BIT)
        .addStage(fragCode.data, (uint32_t)fragCode.size, VK_SHADER_STAGE_FRAGMENT_BIT);

    // Constant buffer
    BufferCreation bufferCreation;
    bufferCreation.reset()
        .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, sizeof(UniformData))
        .setName("sceneCb");
    sceneCb = gpu.createBuffer(bufferCreation);

    pipelineCreation.name = "main_no_cull";
    RendererUtil::Program* programNoCull = renderer.createProgram({pipelineCreation});

    pipelineCreation.rasterization.cullMode = VK_CULL_MODE_BACK_BIT;

    pipelineCreation.name = "main_cull";
    RendererUtil::Program* programCull = renderer.createProgram({pipelineCreation});

    RendererUtil::MaterialCreation materialCreation;

    materialCreation.setName("materialNoCullOpaque").setProgram(programNoCull).setRenderIndex(0);
    RendererUtil::Material* materialNoCullOpaque = renderer.createMaterial(materialCreation);

    materialCreation.setName("material_cull_opaque").setProgram(programCull).setRenderIndex(1);
    RendererUtil::Material* material_cull_opaque = renderer.createMaterial(materialCreation);

    materialCreation.setName("materialNoCullTransparent")
        .setProgram(programNoCull)
        .setRenderIndex(2);
    RendererUtil::Material* materialNoCullTransparent = renderer.createMaterial(materialCreation);

    materialCreation.setName("material_cull_transparent").setProgram(programCull).setRenderIndex(3);
    RendererUtil::Material* material_cull_transparent = renderer.createMaterial(materialCreation);

    pathBuffer.shutdown();
    allocator->deallocate(vertCode.data);
    allocator->deallocate(fragCode.data);

    glTF::Scene& rootGltfScene = scene.gltfScene.scenes[scene.gltfScene.scene];

    for (uint32_t i = 0; i < rootGltfScene.nodesCount; ++i)
    {
      glTF::Node& node = scene.gltfScene.nodes[rootGltfScene.nodes[i]];

      if (node.mesh == glTF::INVALID_INT_VALUE)
      {
        continue;
      }

      // TODO: children

      glTF::Mesh& mesh = scene.gltfScene.meshes[node.mesh];

      vec3s nodeScale{1.0f, 1.0f, 1.0f};
      if (node.scaleCount != 0)
      {
        assert(node.scaleCount == 3);
        nodeScale = vec3s{node.scale[0], node.scale[1], node.scale[2]};
      }

      // Gltf primitives are conceptually submeshes.
      for (uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitivesCount; ++primitiveIndex)
      {
        MeshDraw meshDraw{};

        meshDraw.scale = nodeScale;

        glTF::MeshPrimitive& meshPrimitive = mesh.primitives[primitiveIndex];

        const int positionAccessorIndex = gltfGetAttributeAccessorIndex(
            meshPrimitive.attributes, meshPrimitive.attributeCount, "POSITION");
        const int tangentAccessorIndex = gltfGetAttributeAccessorIndex(
            meshPrimitive.attributes, meshPrimitive.attributeCount, "TANGENT");
        const int normalAccessorIndex = gltfGetAttributeAccessorIndex(
            meshPrimitive.attributes, meshPrimitive.attributeCount, "NORMAL");
        const int texcoordAccessorIndex = gltfGetAttributeAccessorIndex(
            meshPrimitive.attributes, meshPrimitive.attributeCount, "TEXCOORD_0");

        getMeshVertexBuffer(
            scene, positionAccessorIndex, meshDraw.positionBuffer, meshDraw.positionOffset);
        getMeshVertexBuffer(
            scene, tangentAccessorIndex, meshDraw.tangentBuffer, meshDraw.tangentOffset);
        getMeshVertexBuffer(
            scene, normalAccessorIndex, meshDraw.normalBuffer, meshDraw.normalOffset);
        getMeshVertexBuffer(
            scene, texcoordAccessorIndex, meshDraw.texcoordBuffer, meshDraw.texcoordOffset);

        // Create index buffer
        glTF::Accessor& indicesAccessor = scene.gltfScene.accessors[meshPrimitive.indices];
        glTF::BufferView& indicesBufferView =
            scene.gltfScene.bufferViews[indicesAccessor.bufferView];
        RendererUtil::BufferResource& indicesBufferGpu = scene.buffers[indicesAccessor.bufferView];
        meshDraw.indexBuffer = indicesBufferGpu.m_Handle;
        meshDraw.indexOffset =
            indicesAccessor.byteOffset == glTF::INVALID_INT_VALUE ? 0 : indicesAccessor.byteOffset;
        meshDraw.primitiveCount = indicesAccessor.count;

        // Create material
        glTF::Material& material = scene.gltfScene.materials[meshPrimitive.material];

        bool transparent = getMeshMaterial(renderer, scene, material, meshDraw);

        if (transparent)
        {
          if (material.doubleSided)
          {
            meshDraw.material = materialNoCullTransparent;
          }
          else
          {
            meshDraw.material = material_cull_transparent;
          }
        }
        else
        {
          if (material.doubleSided)
          {
            meshDraw.material = materialNoCullOpaque;
          }
          else
          {
            meshDraw.material = material_cull_opaque;
          }
        }

        scene.m_MeshDraws.push(meshDraw);
      }
    }
  }

  qsort(scene.m_MeshDraws.m_Data, scene.m_MeshDraws.m_Size, sizeof(MeshDraw), meshMaterialCompare);

  int64_t beginFrameTick = Time::getCurrentTime();

  vec3s light = vec3s{0.0f, 4.0f, 0.0f};

  float modelScale = 1.0f;
  float lightRange = 20.0f;
  float lightIntensity = 80.0f;

  while (!window.m_RequestedExit)
  {
    // New frame
    if (!window.m_Minimized)
    {
      gpu.newFrame();
    }

    window.handleOSMessages();
    input.newFrame();

    if (window.m_Resized)
    {
      gpu.resize(window.m_Width, window.m_Height);
      window.m_Resized = false;

      gameCamera.camera.setAspectRatio(window.m_Width * 1.f / window.m_Height);
    }
    // This MUST be AFTER os messages!
    imgui->newFrame();

    const int64_t currentTick = Time::getCurrentTime();
    float deltaTime = (float)Time::deltaSeconds(beginFrameTick, currentTick);
    beginFrameTick = currentTick;

    input.update(deltaTime);
    gameCamera.update(&input, window.m_Width, window.m_Height, deltaTime);
    window.centerMouse(gameCamera.mouseDragging);

    if (ImGui::Begin("Framework ImGui"))
    {
      ImGui::InputFloat("Model scale", &modelScale, 0.001f);
      ImGui::InputFloat3("Light position", light.raw);
      ImGui::InputFloat("Light range", &lightRange);
      ImGui::InputFloat("Light intensity", &lightIntensity);
      ImGui::InputFloat3("Camera position", gameCamera.camera.position.raw);
      ImGui::InputFloat3("Camera target movement", gameCamera.targetMovement.raw);
    }
    ImGui::End();

    MemoryService::instance()->imguiDraw();

    {
      // Update common constant buffer
      MapBufferParameters cbMap = {sceneCb, 0, 0};
      float* cbData = (float*)gpu.mapBuffer(cbMap);
      if (cbData)
      {

        UniformData uniform_data{};
        uniform_data.viewProj = gameCamera.camera.viewProjection;
        uniform_data.eye = vec4s{
            gameCamera.camera.position.x,
            gameCamera.camera.position.y,
            gameCamera.camera.position.z,
            1.0f};
        uniform_data.light = vec4s{light.x, light.y, light.z, 1.0f};
        uniform_data.lightRange = lightRange;
        uniform_data.lightIntensity = lightIntensity;

        memcpy(cbData, &uniform_data, sizeof(UniformData));

        gpu.unmapBuffer(cbMap);
      }

      // Update per mesh material buffer
      for (uint32_t meshIndex = 0; meshIndex < scene.m_MeshDraws.m_Size; ++meshIndex)
      {
        MeshDraw& meshDraw = scene.m_MeshDraws[meshIndex];

        cbMap.buffer = meshDraw.materialBuffer;
        MeshData* meshData = (MeshData*)gpu.mapBuffer(cbMap);
        if (meshData)
        {
          uploadMaterial(*meshData, meshDraw, modelScale);

          gpu.unmapBuffer(cbMap);
        }
      }
    }

    if (!window.m_Minimized)
    {
      CommandBuffer* cmdBuf = gpu.getCommandBuffer(true);

      cmdBuf->clear(0.3f, 0.3f, 0.3f, 1.0f);
      cmdBuf->clearDepthStencil(1.0f, 0);
      cmdBuf->bindPass(gpu.m_SwapchainPass);
      cmdBuf->setScissor(nullptr);
      cmdBuf->setViewport(nullptr);

      RendererUtil::Material* lastMaterial = nullptr;
      // TODO: loop by material so that we can deal with multiple passes
      for (uint32_t meshIndex = 0; meshIndex < scene.m_MeshDraws.m_Size; ++meshIndex)
      {
        MeshDraw& meshDraw = scene.m_MeshDraws[meshIndex];

        if (meshDraw.material != lastMaterial)
        {
          PipelineHandle pipeline = renderer.getPipeline(meshDraw.material);

          cmdBuf->bindPipeline(pipeline);

          lastMaterial = meshDraw.material;
        }

        drawMesh(renderer, cmdBuf, meshDraw);
      }

      imgui->render(*cmdBuf);

      // Send commands to GPU
      gpu.queueCommandBuffer(cmdBuf);
      gpu.present();
    }
    else
    {
      ImGui::Render();
    }
  }

  gpu.destroyBuffer(sceneCb);

  imgui->shutdown();

  sceneFreeGpuResources(scene, renderer);

  rm.shutdown();
  renderer.shutdown();

  sceneUnload(scene, renderer);

  input.shutdown();
  window.unregisterOSMessagesCallback(inputOSMessagesCallback);
  window.shutdown();

  MemoryService::instance()->shutdown();

  return 0;
}
