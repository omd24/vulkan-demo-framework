#pragma once

#include "Foundation/Gltf.hpp"

#include "Graphics/GpuResources.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/RenderSceneBase.hpp"
#include "Graphics/FrameGraph.hpp"
#include "Graphics/ImguiHelper.hpp"

#include "Externals/enkiTS/TaskScheduler.h"

namespace Graphics
{
struct glTFScene;
struct Material;

//---------------------------------------------------------------------------//
struct PBRMaterial
{
  RendererUtil::Material* material;

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
//---------------------------------------------------------------------------//
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
//---------------------------------------------------------------------------//
struct MeshInstance
{

  Mesh* mesh;
  uint32_t materialPassIndex;

}; // struct MeshInstance
//---------------------------------------------------------------------------//
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
//---------------------------------------------------------------------------//
// Render Passes //
//---------------------------------------------------------------------------//
struct DepthPrePass : public FrameGraphRenderPass
{
  void render(CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      glTFScene& scene,
      FrameGraph* frameGraph,
      Framework::Allocator* residentAllocator,
      Framework::StackAllocator* scratchAllocator);
  void freeGpuResources();

  Framework::Array<MeshInstance> meshInstances;
  RendererUtil::Renderer* renderer;
}; // struct DepthPrePass
//---------------------------------------------------------------------------//
struct GBufferPass : public FrameGraphRenderPass
{
  void render(CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      glTFScene& scene,
      FrameGraph* frameGraph,
      Framework::Allocator* residentAllocator,
      Framework::StackAllocator* scratchAllocator);
  void freeGpuResources();

  Framework::Array<MeshInstance> meshInstances;
  RendererUtil::Renderer* renderer;
}; // struct GBufferPass
//---------------------------------------------------------------------------//
struct LightPass : public FrameGraphRenderPass
{
  void render(CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      glTFScene& scene,
      FrameGraph* frameGraph,
      Framework::Allocator* residentAllocator,
      Framework::StackAllocator* scratchAllocator);
  void uploadMaterials();
  void freeGpuResources();

  Mesh mesh;
  RendererUtil::Renderer* renderer;
}; // struct LighPass
//---------------------------------------------------------------------------//
struct TransparentPass : public FrameGraphRenderPass
{
  void render(CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      glTFScene& scene,
      FrameGraph* frameGraph,
      Framework::Allocator* residentAllocator,
      Framework::StackAllocator* scratchAllocator);
  void freeGpuResources();

  Framework::Array<MeshInstance> meshInstances;
  RendererUtil::Renderer* renderer;
}; // struct TransparentPass
//---------------------------------------------------------------------------//
struct DoFPass : public FrameGraphRenderPass
{

  struct DoFData
  {
    uint32_t textures[4]; // diffuse, depth
    float znear;
    float zfar;
    float focalLength;
    float planeInFocus;
    float aperture;
  }; // struct DoFData

  void addUi() override;
  void preRender(CommandBuffer* gpuCommands, RenderScene* renderScene) override;
  void render(CommandBuffer* gpuCommands, RenderScene* renderScene) override;
  void onResize(GpuDevice& gpu, uint32_t newWidth, uint32_t newHeight) override;

  void prepareDraws(
      glTFScene& scene,
      FrameGraph* frameGraph,
      Framework::Allocator* residentAllocator,
      Framework::StackAllocator* scratchAllocator);
  void uploadMaterials();
  void freeGpuResources();

  Mesh mesh;
  RendererUtil::Renderer* renderer;

  RendererUtil::TextureResource* sceneMips;

  float znear;
  float zfar;
  float focalLength;
  float planeInFocus;
  float aperture;
}; // struct DoFPass
//---------------------------------------------------------------------------//
struct glTFScene : public RenderScene
{
  void init(
      const char* filename,
      const char* path,
      Framework::Allocator* residentAllocator,
      Framework::StackAllocator* tempAllocator,
      AsynchronousLoader* asyncLoader) override;
  void shutdown(RendererUtil::Renderer* renderer) override;

  void registerRenderPasses(FrameGraph* frameGraph) override;
  void prepareDraws(
      RendererUtil::Renderer* renderer,
      Framework::StackAllocator* scratchAllocator,
      SceneGraph* sceneGraph) override;
  void uploadMaterials() override;
  void submitDrawTask(ImguiUtil::ImguiService* imgui, enki::TaskScheduler* taskScheduler) override;

  void drawMesh(CommandBuffer* gpuCommands, Mesh& mesh);

  void
  getMeshVertexBuffer(int accessorIndex, BufferHandle& outBufferHandle, uint32_t& outBufferOffset);
  uint16_t getMaterialTexture(GpuDevice& gpu, Framework::glTF::TextureInfo* textureInfo);
  uint16_t getMaterialTexture(GpuDevice& gpu, int gltf_texture_index);

  void fillPbrMaterial(
      RendererUtil::Renderer& renderer,
      Framework::glTF::Material& material,
      PBRMaterial& pbrMaterial);

  Framework::Array<Mesh> meshes;

  DepthPrePass depthPrePass;
  GBufferPass gbufferPass;
  LightPass lightPass;
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

  RendererUtil::Renderer* renderer;
  FrameGraph* frameGraph;

}; // struct GltfScene
//---------------------------------------------------------------------------//
// glTFDrawTask
struct glTFDrawTask : public enki::ITaskSet
{

  GpuDevice* gpu = nullptr;
  FrameGraph* frameGraph = nullptr;
  RendererUtil::Renderer* renderer = nullptr;
  ImguiUtil::ImguiService* imgui = nullptr;
  glTFScene* scene = nullptr;
  uint32_t threadId = 0;

  void init(
      GpuDevice* p_Gpu,
      FrameGraph* p_FrameGraph,
      RendererUtil::Renderer* p_Renderer,
      ImguiUtil::ImguiService* p_Imgui,
      glTFScene* p_Scene);

  void ExecuteRange(enki::TaskSetPartition p_Range, uint32_t p_ThreadNum) override;
}; // struct glTFDrawTask
//---------------------------------------------------------------------------//
} // namespace Graphics
