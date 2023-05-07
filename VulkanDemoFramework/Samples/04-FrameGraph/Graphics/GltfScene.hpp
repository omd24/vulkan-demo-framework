#pragma once

#include "Foundation/Gltf.hpp"

#include "Graphics/GpuResources.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/RenderSceneBase.hpp"
#include "Graphics/FrameGraph.hpp"

#include "Externals/enkiTS/TaskScheduler.h"

namespace Graphics
{
struct glTFScene;
struct Material;

//
//
struct PBRMaterial
{

  Material* material;

  BufferHandle materialBuffer;
  DescriptorSetHandle descriptorSet;

  // Indices used for bindless textures.
  uint16_t diffuseTextureIndex;
  uint16_t roughnessTextureIndex;
  uint16_t normalTextureIndex;
  uint16_t occlusionTextureIndex;

  vec4s baseColorFactor;
  vec4s metallicRoughnessOcclusionFactor;

  float alphaCutoff;
  uint32_t flags;
}; // struct PBRMaterial

//
//
struct Mesh
{

  PBRMaterial pbrMaterial;

  BufferHandle indexBuffer;
  BufferHandle positionBuffer;
  BufferHandle tangentBuffer;
  BufferHandle normalBuffer;
  BufferHandle texcoordBuffer;

  uint32_t positionOffset;
  uint32_t tangentOffset;
  uint32_t normalOffset;
  uint32_t texcoordOffset;

  VkIndexType indexType;
  uint32_t indexOffset;

  uint32_t primitiveCount;
  uint32_t sceneGraphNodeIndex = UINT32_MAX;

  bool isTransparent() const
  {
    return (pbrMaterial.flags & (kDrawFlagsAlphaMask | kDrawFlagsTransparent)) != 0;
  }
  bool isDoubleSided() const
  {
    return (pbrMaterial.flags & kDrawFlagsDoubleSided) == kDrawFlagsDoubleSided;
  }
}; // struct Mesh

//
//
struct MeshInstance
{

  Mesh* mesh;
  uint32_t materialPassIndex;

}; // struct MeshInstance

//
//
struct GpuMeshData
{
  mat4s world;
  mat4s inverseWorld;

  uint32_t textures[4]; // diffuse, roughness, normal, occlusion
  vec4s baseColorFactor;
  vec4s metallicRoughnessOcclusionFactor; // metallic, roughness, occlusion
  float alphaCutoff;
  float padding0[3];

  uint32_t flags;
  uint32_t padding1[3];
}; // struct GpuMeshData

// Render Passes //

//
//
struct DepthPrePass : public FrameGraphRenderPass
{
  void render(CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      glTFScene& scene,
      FrameGraph* frameGraph,
      Allocator* residentAllocator,
      StackAllocator* scratchAllocator);
  void freeGpuResources();

  Framework::Array<MeshInstance> meshInstances;
  Renderer* renderer;
}; // struct DepthPrePass

//
//
struct GBufferPass : public FrameGraphRenderPass
{
  void render(CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      glTFScene& scene,
      FrameGraph* frameGraph,
      Allocator* residentAllocator,
      StackAllocator* scratchAllocator);
  void freeGpuResources();

  Framework::Array<MeshInstance> meshInstances;
  Renderer* renderer;
}; // struct GBufferPass

//
//
struct LighPass : public FrameGraphRenderPass
{
  void render(CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      glTFScene& scene,
      FrameGraph* frameGraph,
      Allocator* residentAllocator,
      StackAllocator* scratchAllocator);
  void uploadMaterials();
  void freeGpuResources();

  Mesh mesh;
  Renderer* renderer;
}; // struct LighPass

//
//
struct TransparentPass : public FrameGraphRenderPass
{
  void render(CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      glTFScene& scene,
      FrameGraph* frameGraph,
      Allocator* residentAllocator,
      StackAllocator* scratchAllocator);
  void freeGpuResources();

  Framework::Array<MeshInstance> meshInstances;
  Renderer* renderer;
}; // struct TransparentPass

//
//
struct DoFPass : public FrameGraphRenderPass
{

  struct DoFData
  {
    uint32_t textures[4]; // diffuse, depth
    float znear;
    float zfar;
    float focal_length;
    float plane_in_focus;
    float aperture;
  }; // struct DoFData

  void addUi() override;
  void preRender(CommandBuffer* gpuCommands, RenderScene* renderScene) override;
  void render(CommandBuffer* gpuCommands, RenderScene* renderScene) override;
  void onResize(GpuDevice& gpu, uint32_t new_width, uint32_t new_height) override;

  void prepareDraws(
      glTFScene& scene,
      FrameGraph* frameGraph,
      Allocator* residentAllocator,
      StackAllocator* scratchAllocator);
  void uploadMaterials();
  void freeGpuResources();

  Mesh mesh;
  Renderer* renderer;

  RendererUtil::TextureResource* scene_mips;

  float znear;
  float zfar;
  float focal_length;
  float plane_in_focus;
  float aperture;
}; // struct DoFPass

//
//
struct glTFScene : public RenderScene
{

  void init(
      const char* filename,
      const char* path,
      Allocator* residentAllocator,
      StackAllocator* tempAllocator,
      AsynchronousLoader* asyncLoader) override;
  void shutdown(Renderer* renderer) override;

  void registerRenderPasses(FrameGraph* frameGraph) override;
  void prepareDraws(
      Renderer* renderer, StackAllocator* scratchAllocator, SceneGraph* sceneGraph) override;
  void uploadMaterials() override;
  void submitDrawTask(ImGuiService* imgui, enki::TaskScheduler* taskScheduler) override;

  void drawMesh(CommandBuffer* gpuCommands, Mesh& mesh);

  void
  getMeshVertexBuffer(int accessorIndex, BufferHandle& outBufferHandle, uint32_t& outBufferOffset);
  uint16_t getMaterialTexture(GpuDevice& gpu, Framework::glTF::TextureInfo* textureInfo);
  uint16_t getMaterialTexture(GpuDevice& gpu, int gltf_texture_index);

  void fillPbrMaterial(
      Renderer& renderer, Framework::glTF::Material& material, PBRMaterial& pbrMaterial);

  Framework::Array<Mesh> meshes;

  DepthPrePass depthPrePass;
  GBufferPass gbufferPass;
  LighPass lightPass;
  TransparentPass transparentPass;
  DoFPass dofPass;

  // Fullscreen data
  RendererUtil::GpuTechnique* fullscreenTech = nullptr;
  DescriptorSetHandle fullscreenDS;
  uint32_t fullscreenInputRT = UINT32_MAX;

  // All graphics resources used by the scene
  Framework::Array<RendererUtil::TextureResource> images;
  Framework::Array<RendererUtil::SamplerResource> samplers;
  Framework::Array<RendererUtil::BufferResource> buffers;

  Framework::glTF::glTF gltfScene; // Source gltf scene

  Renderer* renderer;
  FrameGraph* frameGraph;

}; // struct GltfScene

// glTFDrawTask

//
//
struct glTFDrawTask : public enki::ITaskSet
{

  GpuDevice* gpu = nullptr;
  FrameGraph* frameGraph = nullptr;
  Renderer* renderer = nullptr;
  ImGuiService* imgui = nullptr;
  GPUProfiler* gpu_profiler = nullptr;
  glTFScene* scene = nullptr;
  uint32_t thread_id = 0;

  void init(
      GpuDevice* gpu_,
      FrameGraph* frameGraph_,
      Renderer* renderer_,
      ImGuiService* imgui_,
      GPUProfiler* gpu_profiler_,
      glTFScene* scene_);

  void ExecuteRange(enki::TaskSetPartition range_, uint32_t threadnum_) override;

}; // struct glTFDrawTask
} // namespace Graphics
