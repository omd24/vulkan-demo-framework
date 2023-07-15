#include "RenderScene.hpp"

#include "Graphics/Renderer.hpp"
#include "Graphics/SceneGraph.hpp"
#include "Graphics/AsynchronousLoader.hpp"
#include "Graphics/ImguiHelper.hpp"

#include "Foundation/Time.hpp"
#include "Foundation/Numerics.hpp"

#include "Externals/imgui/imgui.h"
#include "Externals/stb_image.h"

#include "Externals/cglm/struct/affine.h"
#include "Externals/cglm/struct/mat4.h"
#include "Externals/cglm/struct/vec3.h"
#include "Externals/cglm/struct/quat.h"

#include "Externals/stb_image.h"

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <math.h>

#if !defined(DATA_FOLDER)
#  define DATA_FOLDER "\\Data\\"
#endif

namespace Graphics
{
static int meshMaterialCompare(const void* a, const void* b)
{
  const Mesh* meshA = (const Mesh*)a;
  const Mesh* meshB = (const Mesh*)b;

  if (meshA->pbrMaterial.material->m_RenderIndex < meshB->pbrMaterial.material->m_RenderIndex)
    return -1;
  if (meshA->pbrMaterial.material->m_RenderIndex > meshB->pbrMaterial.material->m_RenderIndex)
    return 1;
  return 0;
}

//
//
static void copyGpuMaterialData(GpuMeshData& p_GpuMeshData, const Mesh& p_Mesh)
{
  p_GpuMeshData.textures[0] = p_Mesh.pbrMaterial.diffuseTextureIndex;
  p_GpuMeshData.textures[1] = p_Mesh.pbrMaterial.roughnessTextureIndex;
  p_GpuMeshData.textures[2] = p_Mesh.pbrMaterial.normalTextureIndex;
  p_GpuMeshData.textures[3] = p_Mesh.pbrMaterial.occlusionTextureIndex;

  p_GpuMeshData.emissive = {
      p_Mesh.pbrMaterial.emissiveFactor.x,
      p_Mesh.pbrMaterial.emissiveFactor.y,
      p_Mesh.pbrMaterial.emissiveFactor.z,
      (float)p_Mesh.pbrMaterial.emissiveTextureIndex};

  p_GpuMeshData.baseColorFactor = p_Mesh.pbrMaterial.baseColorFactor;
  p_GpuMeshData.metallicRoughnessOcclusionFactor =
      p_Mesh.pbrMaterial.metallicRoughnessOcclusionFactor;
  p_GpuMeshData.alphaCutoff = p_Mesh.pbrMaterial.alphaCutoff;

  p_GpuMeshData.diffuseColour = p_Mesh.pbrMaterial.diffuseColour;
  p_GpuMeshData.specularColour = p_Mesh.pbrMaterial.specularColour;
  p_GpuMeshData.specularExp = p_Mesh.pbrMaterial.specularExp;
  p_GpuMeshData.ambientColour = p_Mesh.pbrMaterial.ambientColour;

  p_GpuMeshData.flags = p_Mesh.pbrMaterial.flags;
}

//
//
static void copyGpuMeshMatrix(
    GpuMeshData& p_GpuMeshData,
    const Mesh& p_Mesh,
    const float p_GlobalScale,
    const SceneGraph* p_SceneGraph)
{
  if (p_SceneGraph)
  {
    // Apply global scale matrix
    // NOTE: for left-handed systems (as defined in cglm) need to invert positive and negative Z.
    const mat4s scaleMatrix = glms_scale_make({p_GlobalScale, p_GlobalScale, -p_GlobalScale});
    p_GpuMeshData.world =
        glms_mat4_mul(scaleMatrix, p_SceneGraph->worldMatrices[p_Mesh.sceneGraphNodeIndex]);

    p_GpuMeshData.inverseWorld = glms_mat4_inv(glms_mat4_transpose(p_GpuMeshData.world));
  }
  else
  {
    p_GpuMeshData.world = glms_mat4_identity();
    p_GpuMeshData.inverseWorld = glms_mat4_identity();
  }
}

//
// PhysicsVertex ///////////////////////////////////////////////////////
void PhysicsVertex::addJoint(uint32_t p_VertexIndex)
{
  for (uint32_t j = 0; j < jointCount; ++j)
  {
    if (joints[j].vertexIndex == p_VertexIndex)
    {
      return;
    }
  }

  assert(jointCount < kMaxJointCount);
  joints[jointCount++].vertexIndex = p_VertexIndex;
}

//
// DepthPrePass ///////////////////////////////////////////////////////
void DepthPrePass::render(CommandBuffer* gpuCommands, RenderScene* renderScene)
{

  RendererUtil::Material* lastMaterial = nullptr;
  for (uint32_t meshIndex = 0; meshIndex < meshInstances.m_Size; ++meshIndex)
  {
    MeshInstance& meshInstance = meshInstances[meshIndex];
    Mesh& mesh = *meshInstance.mesh;

    if (mesh.pbrMaterial.material != lastMaterial)
    {
      PipelineHandle pipeline =
          renderer->getPipeline(mesh.pbrMaterial.material, meshInstance.materialPassIndex);

      gpuCommands->bindPipeline(pipeline);

      lastMaterial = mesh.pbrMaterial.material;
    }

    renderScene->drawMesh(gpuCommands, mesh);
  }
}

void DepthPrePass::prepareDraws(
    RenderScene& scene,
    FrameGraph* frameGraph,
    Framework::Allocator* residentAllocator,
    Framework::StackAllocator* scratchAllocator)
{
  using namespace RendererUtil;
  renderer = scene.renderer;

  FrameGraphNode* node = frameGraph->getNode("depth_pre_pass");
  if (node == nullptr)
  {
    assert(false);
    return;
  }

  // Create pipeline state
  PipelineCreation pipelineCreation;

  const uint64_t hashedName = hashCalculate("main");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;

  materialCreation.setName("material_depth_pre_pass").setTechnique(mainTechnique).setRenderIndex(0);
  RendererUtil::Material* materialDepthPrePass = renderer->createMaterial(materialCreation);

  meshInstances.init(residentAllocator, 16);

  // Copy all mesh draws and change only material.
  for (uint32_t i = 0; i < scene.meshes.m_Size; ++i)
  {

    Mesh* mesh = &scene.meshes[i];
    if (mesh->isTransparent())
    {
      continue;
    }

    MeshInstance meshInstance{};
    meshInstance.mesh = mesh;
    meshInstance.materialPassIndex =
        mesh->hasSkinning()
            ? mainTechnique->nameHashToIndex.get(hashCalculate("depth_pre_skinning"))
            : mainTechnique->nameHashToIndex.get(hashCalculate("depth_pre"));

    meshInstances.push(meshInstance);
  }
}

void DepthPrePass::freeGpuResources()
{
  GpuDevice& gpu = *renderer->m_GpuDevice;

  meshInstances.shutdown();
}

//
// GBufferPass ////////////////////////////////////////////////////////
void GBufferPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene)
{

  RendererUtil::Material* lastMaterial = nullptr;
  for (uint32_t meshIndex = 0; meshIndex < meshInstances.m_Size; ++meshIndex)
  {
    MeshInstance& meshInstance = meshInstances[meshIndex];
    Mesh& mesh = *meshInstance.mesh;

    if (mesh.pbrMaterial.material != lastMaterial)
    {
      PipelineHandle pipeline =
          renderer->getPipeline(mesh.pbrMaterial.material, meshInstance.materialPassIndex);

      gpuCommands->bindPipeline(pipeline);

      lastMaterial = mesh.pbrMaterial.material;
    }

    renderScene->drawMesh(gpuCommands, mesh);
  }
}

void GBufferPass::prepareDraws(
    RenderScene& scene,
    FrameGraph* frameGraph,
    Framework::Allocator* residentAllocator,
    Framework::StackAllocator* scratchAllocator)
{
  using namespace RendererUtil;

  renderer = scene.renderer;

  FrameGraphNode* node = frameGraph->getNode("gbuffer_pass");
  if (node == nullptr)
  {
    assert(false);
    return;
  }

  const uint64_t hashedName = hashCalculate("main");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;

  materialCreation.setName("material_no_cull").setTechnique(mainTechnique).setRenderIndex(0);
  RendererUtil::Material* material = renderer->createMaterial(materialCreation);

  meshInstances.init(residentAllocator, 16);

  // Copy all mesh draws and change only material.
  for (uint32_t i = 0; i < scene.meshes.m_Size; ++i)
  {
    // Skip transparent meshes
    Mesh* mesh = &scene.meshes[i];
    if (mesh->isTransparent())
    {
      continue;
    }

    MeshInstance meshInstance{};
    meshInstance.mesh = mesh;
    meshInstance.materialPassIndex =
        mesh->hasSkinning() ? mainTechnique->nameHashToIndex.get(hashCalculate("gbuffer_skinning"))
                            : mainTechnique->nameHashToIndex.get(hashCalculate("gbuffer_cull"));

    meshInstances.push(meshInstance);
  }

  // qsort( meshDraws.data, meshDraws.m_Size, sizeof( MeshDraw ), gltfMeshMaterialCompare );
}

void GBufferPass::freeGpuResources()
{
  GpuDevice& gpu = *renderer->m_GpuDevice;

  meshInstances.shutdown();
}

//
// LightPass //////////////////////////////////////////////////////////////

//
//
struct LightingConstants
{
  uint32_t albedoIndex;
  uint32_t rmoIndex;
  uint32_t normalIndex;
  uint32_t depthIndex;

  uint32_t outputIndex;
  uint32_t output_width;
  uint32_t output_height;
  uint32_t emissive;
}; // struct LightingConstants

void LighPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene)
{

  if (useCompute)
  {
    PipelineHandle pipeline = renderer->getPipeline(mesh.pbrMaterial.material, 1);
    gpuCommands->bindPipeline(pipeline);
    gpuCommands->bindDescriptorSet(&mesh.pbrMaterial.descriptorSet, 1, nullptr, 0);

    gpuCommands->dispatch(
        ceil(renderer->m_GpuDevice->m_SwapchainWidth * 1.f / 8),
        ceil(renderer->m_GpuDevice->m_SwapchainHeight * 1.f / 8),
        1);
  }
  else
  {
    PipelineHandle pipeline = renderer->getPipeline(mesh.pbrMaterial.material, 0);

    gpuCommands->bindPipeline(pipeline);
    gpuCommands->bindVertexBuffer(mesh.positionBuffer, 0, 0);
    gpuCommands->bindDescriptorSet(&mesh.pbrMaterial.descriptorSet, 1, nullptr, 0);

    gpuCommands->draw(TopologyType::kTriangle, 0, 3, 0, 1);
  }
}

void LighPass::prepareDraws(
    RenderScene& scene,
    FrameGraph* frameGraph,
    Framework::Allocator* residentAllocator,
    Framework::StackAllocator* scratchAllocator)
{
  using namespace RendererUtil;
  renderer = scene.renderer;

  FrameGraphNode* node = frameGraph->getNode("lighting_pass");
  if (node == nullptr)
  {
    assert(false);
    return;
  }

  useCompute = node->compute;

  const uint64_t hashedName = hashCalculate("pbr_lighting");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;

  materialCreation.setName("material_pbr").setTechnique(mainTechnique).setRenderIndex(0);
  RendererUtil::Material* materialPbr = renderer->createMaterial(materialCreation);

  BufferCreation bufferCreation;
  bufferCreation.reset()
      .set(
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          ResourceUsageType::kDynamic,
          sizeof(LightingConstants))
      .setName("lighting_constants");
  mesh.pbrMaterial.materialBuffer = renderer->m_GpuDevice->createBuffer(bufferCreation);

  const uint32_t passIndex = useCompute ? 1 : 0;
  DescriptorSetCreation dsCreation{};
  DescriptorSetLayoutHandle layout = renderer->m_GpuDevice->getDescriptorSetLayout(
      mainTechnique->passes[passIndex].pipeline, kMaterialDescriptorSetIndex);
  dsCreation.buffer(scene.sceneCb, 0).buffer(mesh.pbrMaterial.materialBuffer, 1).setLayout(layout);
  mesh.pbrMaterial.descriptorSet = renderer->m_GpuDevice->createDescriptorSet(dsCreation);

  BufferHandle fs_vb = renderer->m_GpuDevice->m_FullscreenVertexBuffer;
  mesh.positionBuffer = fs_vb;

  colorTexture = frameGraph->accessResource(node->inputs[0]);
  normalTexture = frameGraph->accessResource(node->inputs[1]);
  roughnessTexture = frameGraph->accessResource(node->inputs[2]);
  emissiveTexture = frameGraph->accessResource(node->inputs[3]);
  depthTexture = frameGraph->accessResource(node->inputs[4]);

  outputTexture = frameGraph->accessResource(node->outputs[0]);

  mesh.pbrMaterial.material = materialPbr;
}

void LighPass::uploadGpuData()
{

  uint32_t currentFrameIndex = renderer->m_GpuDevice->m_CurrentFrameIndex;

  MapBufferParameters cbMap = {mesh.pbrMaterial.materialBuffer, 0, 0};
  LightingConstants* lighting_data = (LightingConstants*)renderer->m_GpuDevice->mapBuffer(cbMap);
  if (lighting_data)
  {
    lighting_data->albedoIndex = colorTexture->resourceInfo.texture.handle[currentFrameIndex].index;
    ;
    lighting_data->rmoIndex =
        roughnessTexture->resourceInfo.texture.handle[currentFrameIndex].index;
    lighting_data->normalIndex =
        normalTexture->resourceInfo.texture.handle[currentFrameIndex].index;
    lighting_data->depthIndex = depthTexture->resourceInfo.texture.handle[currentFrameIndex].index;
    lighting_data->outputIndex =
        outputTexture->resourceInfo.texture.handle[currentFrameIndex].index;
    lighting_data->output_width = renderer->m_Width;
    lighting_data->output_height = renderer->m_Height;
    lighting_data->emissive = emissiveTexture->resourceInfo.texture.handle[currentFrameIndex].index;

    renderer->m_GpuDevice->unmapBuffer(cbMap);
  }
}

void LighPass::freeGpuResources()
{
  GpuDevice& gpu = *renderer->m_GpuDevice;

  gpu.destroyBuffer(mesh.pbrMaterial.materialBuffer);
  gpu.destroyDescriptorSet(mesh.pbrMaterial.descriptorSet);
}

//
// TransparentPass ////////////////////////////////////////////////////////
void TransparentPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene)
{

  RendererUtil::Material* lastMaterial = nullptr;
  for (uint32_t meshIndex = 0; meshIndex < meshInstances.m_Size; ++meshIndex)
  {
    MeshInstance& meshInstance = meshInstances[meshIndex];
    Mesh& mesh = *meshInstance.mesh;

    if (mesh.pbrMaterial.material != lastMaterial)
    {
      PipelineHandle pipeline =
          renderer->getPipeline(mesh.pbrMaterial.material, meshInstance.materialPassIndex);

      gpuCommands->bindPipeline(pipeline);

      lastMaterial = mesh.pbrMaterial.material;
    }

    renderScene->drawMesh(gpuCommands, mesh);
  }
}

void TransparentPass::prepareDraws(
    RenderScene& scene,
    FrameGraph* frameGraph,
    Framework::Allocator* residentAllocator,
    Framework::StackAllocator* scratchAllocator)
{
  using namespace RendererUtil;
  renderer = scene.renderer;

  FrameGraphNode* node = frameGraph->getNode("transparent_pass");
  if (node == nullptr)
  {
    assert(false);
    return;
  }

  // Create pipeline state
  PipelineCreation pipelineCreation;

  const uint64_t hashedName = hashCalculate("main");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;

  materialCreation.setName("material_transparent").setTechnique(mainTechnique).setRenderIndex(0);
  RendererUtil::Material* materialDepthPrePass = renderer->createMaterial(materialCreation);

  meshInstances.init(residentAllocator, 16);

  // Copy all mesh draws and change only material.
  for (uint32_t i = 0; i < scene.meshes.m_Size; ++i)
  {

    // Skip transparent meshes
    Mesh* mesh = &scene.meshes[i];
    if (!mesh->isTransparent())
    {
      continue;
    }

    MeshInstance meshInstance{};
    meshInstance.mesh = mesh;
    meshInstance.materialPassIndex =
        mesh->hasSkinning()
            ? mainTechnique->nameHashToIndex.get(hashCalculate("transparent_skinning_no_cull"))
            : mainTechnique->nameHashToIndex.get(hashCalculate("transparent_no_cull"));

    meshInstances.push(meshInstance);
  }
}

void TransparentPass::freeGpuResources() { meshInstances.shutdown(); }

//
// DebugPass ////////////////////////////////////////////////////////
void DebugPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene)
{

  PipelineHandle pipeline = renderer->getPipeline(debugMaterial, 0);

  gpuCommands->bindPipeline(pipeline);

  for (uint32_t meshIndex = 0; meshIndex < meshInstances.m_Size; ++meshIndex)
  {
    MeshInstance& meshInstance = meshInstances[meshIndex];
    Mesh& mesh = *meshInstance.mesh;

    if (mesh.physicsMesh != nullptr)
    {
      PhysicsMesh* physicsMesh = mesh.physicsMesh;

      gpuCommands->bindVertexBuffer(sphereMeshBuffer->m_Handle, 0, 0);
      gpuCommands->bindIndexBuffer(sphereMeshIndices->m_Handle, 0, VK_INDEX_TYPE_UINT32);

      gpuCommands->bindDescriptorSet(&physicsMesh->debugMeshDescriptorSet, 1, nullptr, 0);

      gpuCommands->drawIndexed(
          TopologyType::kTriangle, sphereIndexCount, physicsMesh->vertices.m_Size, 0, 0, 0);
    }
  }

  pipeline = renderer->getPipeline(debugMaterial, 1);

  gpuCommands->bindPipeline(pipeline);

  for (uint32_t meshIndex = 0; meshIndex < meshInstances.m_Size; ++meshIndex)
  {
    MeshInstance& meshInstance = meshInstances[meshIndex];
    Mesh& mesh = *meshInstance.mesh;

    if (mesh.physicsMesh != nullptr)
    {
      PhysicsMesh* physicsMesh = mesh.physicsMesh;

      gpuCommands->bindDescriptorSet(&physicsMesh->debugMeshDescriptorSet, 1, nullptr, 0);

      gpuCommands->drawIndirect(
          physicsMesh->drawIndirectBuffer,
          physicsMesh->vertices.m_Size,
          0,
          sizeof(VkDrawIndirectCommand));
    }
  }
}

void DebugPass::prepareDraws(
    RenderScene& scene,
    FrameGraph* frameGraph,
    Framework::Allocator* residentAllocator,
    Framework::StackAllocator* scratchAllocator)
{
  using namespace RendererUtil;

  renderer = scene.renderer;
  sceneGraph = scene.sceneGraph;

  FrameGraphNode* node = frameGraph->getNode("debug_pass");
  if (node == nullptr)
  {
    assert(false);
    return;
  }

  // Create pipeline state
  PipelineCreation pipelineCreation;

  const uint64_t hashedName = hashCalculate("debug");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;

  materialCreation.setName("material_debug").setTechnique(mainTechnique).setRenderIndex(0);
  debugMaterial = renderer->createMaterial(materialCreation);

  size_t marker = scratchAllocator->getMarker();

  StringBuffer mesh_name;
  mesh_name.init(1024, scratchAllocator);
  cstring filename = mesh_name.appendUseFormatted("%s/sphere.obj", DATA_FOLDER);

  const aiScene* sphere_mesh = aiImportFile(
      filename,
      aiProcess_CalcTangentSpace | aiProcess_GenNormals | aiProcess_Triangulate |
          aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);

  scratchAllocator->freeMarker(marker);

  Array<vec3s> positions;
  positions.init(residentAllocator, FRAMEWORK_KILO(64));

  Array<uint32_t> indices;
  indices.init(residentAllocator, FRAMEWORK_KILO(64));

  sphereIndexCount = 0;

  for (uint32_t meshIndex = 0; meshIndex < sphere_mesh->mNumMeshes; ++meshIndex)
  {
    aiMesh* mesh = sphere_mesh->mMeshes[meshIndex];

    assert((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) != 0);

    for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
    {
      vec3s position{
          mesh->mVertices[vertexIndex].x,
          mesh->mVertices[vertexIndex].y,
          mesh->mVertices[vertexIndex].z};

      positions.push(position);
    }

    for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
    {
      assert(mesh->mFaces[faceIndex].mNumIndices == 3);

      uint32_t index_a = mesh->mFaces[faceIndex].mIndices[0];
      uint32_t index_b = mesh->mFaces[faceIndex].mIndices[1];
      uint32_t index_c = mesh->mFaces[faceIndex].mIndices[2];

      indices.push(index_a);
      indices.push(index_b);
      indices.push(index_c);
    }

    sphereIndexCount = indices.m_Size;
  }

  {
    BufferCreation creation{};
    size_t bufferSize = positions.m_Size * sizeof(vec3s);
    creation.set(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ResourceUsageType::kImmutable, bufferSize)
        .setData(positions.m_Data)
        .setName("debug_sphere_pos");

    sphereMeshBuffer = renderer->createBuffer(creation);
  }

  {
    BufferCreation creation{};
    size_t bufferSize = indices.m_Size * sizeof(uint32_t);
    creation.set(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ResourceUsageType::kImmutable, bufferSize)
        .setData(indices.m_Data)
        .setName("debug_sphere_indices");

    sphereMeshIndices = renderer->createBuffer(creation);
  }

  positions.shutdown();
  indices.shutdown();

  meshInstances.init(residentAllocator, 16);

  // Copy all mesh draws
  for (uint32_t i = 0; i < scene.meshes.m_Size; ++i)
  {
    Mesh& mesh = scene.meshes[i];

    MeshInstance newInstance{};
    newInstance.mesh = &mesh;

    meshInstances.push(newInstance);
  }
}

void DebugPass::freeGpuResources()
{

  renderer->destroyBuffer(sphereMeshIndices);
  renderer->destroyBuffer(sphereMeshBuffer);

  meshInstances.shutdown();
}

//
// DoFPass ////////////////////////////////////////////////////////////////
void DoFPass::addUi()
{
  ImGui::InputFloat("Focal Length", &focalLength);
  ImGui::InputFloat("Plane in Focus", &planeInFocus);
  ImGui::InputFloat("Aperture", &aperture);
}

void DoFPass::preRender(
    uint32_t currentFrameIndex, CommandBuffer* gpuCommands, FrameGraph* frameGraph)
{

  FrameGraphResource* texture = (FrameGraphResource*)frameGraph->getResource("lighting");
  assert(texture != nullptr);

  gpuCommands->copyTexture(
      texture->resourceInfo.texture.handle[currentFrameIndex],
      sceneMips[currentFrameIndex]->m_Handle,
      RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void DoFPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene)
{

  PipelineHandle pipeline = renderer->getPipeline(mesh.pbrMaterial.material, 0);

  gpuCommands->bindPipeline(pipeline);
  gpuCommands->bindVertexBuffer(mesh.positionBuffer, 0, 0);
  gpuCommands->bindDescriptorSet(&mesh.pbrMaterial.descriptorSet, 1, nullptr, 0);

  gpuCommands->draw(TopologyType::kTriangle, 0, 3, 0, 1);
}

// TODO:
static TextureCreation dof_scene_tc;

void DoFPass::on_resize(GpuDevice& gpu, uint32_t new_width, uint32_t new_height)
{

  uint32_t w = new_width;
  uint32_t h = new_height;

  uint32_t mips = 1;
  while (w > 1 && h > 1)
  {
    w /= 2;
    h /= 2;
    mips++;
  }

  // Destroy scene mips
  for (uint32_t i = 0; i < k_max_frames; ++i)
  {
    renderer->destroyTexture(sceneMips[i]);

    // Reuse cached texture creation and create new scene mips.
    dof_scene_tc.set_flags(mips, 0).setSize(new_width, new_height, 1);
    sceneMips[i] = renderer->createTexture(dof_scene_tc);
  }
}

void DoFPass::prepareDraws(
    RenderScene& scene,
    FrameGraph* frameGraph,
    Framework::Allocator* residentAllocator,
    Framework::StackAllocator* scratchAllocator)
{
  renderer = scene.renderer;

  FrameGraphNode* node = frameGraph->getNode("depth_of_field_pass");
  if (node == nullptr)
  {
    assert(false);
    return;
  }

  const uint64_t hashedName = hashCalculate("depth_of_field");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;

  materialCreation.setName("material_dof").setTechnique(mainTechnique).setRenderIndex(0);
  Material* material_dof = renderer->createMaterial(materialCreation);

  BufferCreation bufferCreation;
  bufferCreation.reset()
      .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof(DoFData))
      .setName("dof_data");
  mesh.pbrMaterial.materialBuffer = renderer->m_GpuDevice->createBuffer(bufferCreation);

  DescriptorSetCreation dsCreation{};
  DescriptorSetLayoutHandle layout = renderer->m_GpuDevice->getDescriptorSetLayout(
      mainTechnique->passes[0].pipeline, kMaterialDescriptorSetIndex);
  dsCreation.buffer(mesh.pbrMaterial.materialBuffer, 0).setLayout(layout);
  mesh.pbrMaterial.descriptorSet = renderer->m_GpuDevice->createDescriptorSet(dsCreation);

  BufferHandle fs_vb = renderer->m_GpuDevice->getFullscreenVertexBuffer();
  mesh.positionBuffer = fs_vb;

  FrameGraphResource* colorTexture = frameGraph->accessResource(node->inputs[0]);
  FrameGraphResource* depthTexture_reference = frameGraph->accessResource(node->inputs[1]);

  depthTexture = frameGraph->getResource(depthTexture_reference->name);
  assert(depthTexture != nullptr);

  FrameGraphResourceInfo& info = colorTexture->resourceInfo;
  uint32_t w = info.texture.width;
  uint32_t h = info.texture.height;

  uint32_t mips = 1;
  while (w > 1 && h > 1)
  {
    w /= 2;
    h /= 2;
    mips++;
  }

  dof_scene_tc.setData(nullptr)
      .set_format_type(info.texture.format, TextureType::Texture2D)
      .set_flags(mips, 0)
      .setSize((u16)info.texture.width, (u16)info.texture.height, 1)
      .setName("sceneMips");
  for (uint32_t i = 0; i < k_max_frames; ++i)
  {
    sceneMips[i] = renderer->createTexture(dof_scene_tc);
  }
  mesh.pbrMaterial.material = material_dof;

  znear = 0.1f;
  zfar = 1000.0f;
  focalLength = 5.0f;
  planeInFocus = 1.0f;
  aperture = 8.0f;
}

void DoFPass::uploadGpuData()
{

  uint32_t currentFrameIndex = renderer->m_GpuDevice->currentFrame;

  MapBufferParameters cbMap = {mesh.pbrMaterial.materialBuffer, 0, 0};
  DoFData* dof_data = (DoFData*)renderer->m_GpuDevice->mapBuffer(cbMap);
  if (dof_data)
  {
    dof_data->textures[0] = sceneMips[currentFrameIndex]->handle.index;
    dof_data->textures[1] = depthTexture->resourceInfo.texture.handle[currentFrameIndex].index;

    dof_data->znear = znear;
    dof_data->zfar = zfar;
    dof_data->focalLength = focalLength;
    dof_data->planeInFocus = planeInFocus;
    dof_data->aperture = aperture;

    renderer->m_GpuDevice->unmapBuffer(cbMap);
  }
}

void DoFPass::freeGpuResources()
{
  GpuDevice& gpu = *renderer->m_GpuDevice;

  for (uint32_t i = 0; i < k_max_frames; ++i)
  {
    renderer->destroyTexture(sceneMips[i]);
  }
  gpu.destroyBuffer(mesh.pbrMaterial.materialBuffer);
  gpu.destroyDescriptorSet(mesh.pbrMaterial.descriptorSet);
}

CommandBuffer* RenderScene::update_physics(
    float delta_time,
    float air_density,
    float spring_stiffness,
    float spring_damping,
    vec3s wind_direction,
    bool reset_simulation)
{
  // Based on http://graphics.stanford.edu/courses/cs468-02-winter/Papers/Rigidcloth.pdf

#if 0
    // NOTE: left for reference
    const uint32_t sim_steps = 10;
    const float dt_multiplier = 1.0f / sim_steps;
    delta_time *= dt_multiplier;

    const vec3s g{ 0.0f, -9.8f, 0.0f };

    for ( uint32_t m = 0; m < meshes.m_Size; ++m ) {
        Mesh& mesh = meshes[ m ];

        PhysicsMesh* physicsMesh = mesh.physicsMesh;

        if ( physicsMesh != nullptr ) {
            const vec3s fixedVertex_1{ 0.0f,  1.0f, -1.0f };
            const vec3s fixedVertex_2{ 0.0f,  1.0f,  1.0f };
            const vec3s fixedVertex_3{ 0.0f, -1.0f,  1.0f };
            const vec3s fixedVertex_4{ 0.0f, -1.0f, -1.0f };

            if ( reset_simulation ) {
                for ( uint32_t v = 0; v < physicsMesh->vertices.m_Size; ++v ) {
                    PhysicsVertex& vertex = physicsMesh->vertices[ v ];
                    vertex.position = vertex.start_position;
                    vertex.previous_position = vertex.start_position;
                    vertex.velocity = vec3s{ };
                    vertex.force = vec3s{ };
                }
            }

            for ( uint32_t s = 0; s < sim_steps; ++s ) {
                // First calculate the force to apply to each vertex
                for ( uint32_t v = 0; v < physicsMesh->vertices.m_Size; ++v ) {
                    PhysicsVertex& vertex = physicsMesh->vertices[ v ];

                    if ( glms_vec3_eqv( vertex.start_position, fixedVertex_1 ) || glms_vec3_eqv( vertex.start_position, fixedVertex_2 ) ||
                        glms_vec3_eqv( vertex.start_position, fixedVertex_3 ) || glms_vec3_eqv( vertex.start_position, fixedVertex_4 )) {
                        continue;
                    }

                    float m = vertex.mass;

                    vec3s spring_force{ };

                    for ( uint32_t j = 0; j < vertex.jointCount; ++j ) {
                        PhysicsVertex& otherVertex = physicsMesh->vertices[ vertex.joints[ j ].vertexIndex ];

                        float spring_rest_length =  glms_vec3_distance( vertex.start_position, otherVertex.start_position );

                        vec3s pull_direction = glms_vec3_sub( vertex.position, otherVertex.position );
                        vec3s relative_pull_direction = glms_vec3_sub( pull_direction, glms_vec3_scale( glms_vec3_normalize( pull_direction ), spring_rest_length ) );
                        pull_direction = glms_vec3_scale( relative_pull_direction, spring_stiffness );
                        spring_force = glms_vec3_add( spring_force, pull_direction );
                    }

                    vec3s viscous_damping = glms_vec3_scale( vertex.velocity, -spring_damping );

                    vec3s viscous_velocity = glms_vec3_sub( wind_direction, vertex.velocity );
                    viscous_velocity = glms_vec3_scale( vertex.normal, glms_vec3_dot( vertex.normal, viscous_velocity ) );
                    viscous_velocity = glms_vec3_scale( viscous_velocity, air_density );

                    vertex.force = glms_vec3_scale( g, m );
                    vertex.force = glms_vec3_sub( vertex.force, spring_force );
                    vertex.force = glms_vec3_add( vertex.force, viscous_damping );
                    vertex.force = glms_vec3_add( vertex.force, viscous_velocity );
                }

                // Then update their position
                for ( uint32_t v = 0; v < physicsMesh->vertices.m_Size; ++v ) {
                    PhysicsVertex& vertex = physicsMesh->vertices[ v ];

                    vec3s previous_position = vertex.previous_position;
                    vec3s current_position = vertex.position;

                    // Verlet integration
                    vertex.position = glms_vec3_scale( current_position, 2.0f );
                    vertex.position = glms_vec3_sub( vertex.position, previous_position );
                    vertex.position = glms_vec3_add( vertex.position, glms_vec3_scale( vertex.force, delta_time * delta_time ) );

                    vertex.previous_position = current_position;

                    vertex.velocity = glms_vec3_sub( vertex.position, current_position );
                }
            }

            Buffer* positionBuffer = renderer->m_GpuDevice->accessBuffer( mesh.positionBuffer );
            vec3s* positions = ( vec3s* )( positionBuffer->mapped_data + mesh.position_offset );

            Buffer* normalBuffer = renderer->m_GpuDevice->accessBuffer( mesh.normalBuffer );
            vec3s* normals = ( vec3s* )( normalBuffer->mapped_data + mesh.normal_offset );

            Buffer* tangentBuffer = renderer->m_GpuDevice->accessBuffer( mesh.tangentBuffer );
            vec3s* tangents = ( vec3s* )( tangentBuffer->mapped_data + mesh.tangent_offset );

            Buffer* indexBuffer = renderer->m_GpuDevice->accessBuffer( mesh.indexBuffer );
            uint32_t* indices = ( uint32_t* )( indexBuffer->mapped_data + mesh.index_offset );

            for ( uint32_t v = 0; v < physicsMesh->vertices.m_Size; ++v ) {
                positions[ v ] = physicsMesh->vertices[ v ].position;
            }

            for ( uint32_t i = 0; i < mesh.primitive_count; i += 3 ) {
                uint32_t i0 = indices[ i + 0 ];
                uint32_t i1 = indices[ i + 1 ];
                uint32_t i2 = indices[ i + 2 ];

                vec3s p0 = physicsMesh->vertices[ i0 ].position;
                vec3s p1 = physicsMesh->vertices[ i1 ].position;
                vec3s p2 = physicsMesh->vertices[ i2 ].position;

                // TODO: better normal compuation, also update tangents
                vec3s edge1 = glms_vec3_sub( p1, p0 );
                vec3s edge2 = glms_vec3_sub( p2, p0 );

                vec3s n = glms_cross( edge1, edge2 );

                physicsMesh->vertices[ i0 ].normal = glms_normalize( glms_vec3_add( normals[ i0 ], n ) );
                physicsMesh->vertices[ i1 ].normal = glms_normalize( glms_vec3_add( normals[ i1 ], n ) );
                physicsMesh->vertices[ i2 ].normal = glms_normalize( glms_vec3_add( normals[ i2 ], n ) );

                normals[ i0 ] = physicsMesh->vertices[ i0 ].normal;
                normals[ i1 ] = physicsMesh->vertices[ i1 ].normal;
                normals[ i2 ] = physicsMesh->vertices[ i2 ].normal;
            }
        }
    }
#else
  if (physicsCb.index == k_invalidBuffer.index)
    return nullptr;

  GpuDevice& gpu = *renderer->m_GpuDevice;

  MapBufferParameters physicsCbMap = {physicsCb, 0, 0};
  PhysicsSceneData* gpu_physics_data = (PhysicsSceneData*)gpu.mapBuffer(physicsCbMap);
  if (gpu_physics_data)
  {
    gpu_physics_data->wind_direction = wind_direction;
    gpu_physics_data->reset_simulation = reset_simulation ? 1 : 0;
    gpu_physics_data->air_density = air_density;
    gpu_physics_data->spring_stiffness = spring_stiffness;
    gpu_physics_data->spring_damping = spring_damping;

    gpu.unmapBuffer(physicsCbMap);
  }

  CommandBuffer* cb = nullptr;

  for (uint32_t m = 0; m < meshes.m_Size; ++m)
  {
    Mesh& mesh = meshes[m];

    PhysicsMesh* physicsMesh = mesh.physicsMesh;

    if (physicsMesh != nullptr)
    {
      if (!gpu.buffer_ready(mesh.positionBuffer) || !gpu.buffer_ready(mesh.normalBuffer) ||
          !gpu.buffer_ready(mesh.tangentBuffer) || !gpu.buffer_ready(mesh.indexBuffer) ||
          !gpu.buffer_ready(physicsMesh->m_GpuDeviceBuffer) ||
          !gpu.buffer_ready(physicsMesh->drawIndirectBuffer))
      {
        continue;
      }

      if (cb == nullptr)
      {
        cb = gpu.get_commandBuffer(0, gpu.currentFrame, true, true /*compute*/);

        cb->push_marker("Frame");
        cb->push_marker("async");

        const uint64_t cloth_hashedName = hashCalculate("cloth");
        GpuTechnique* cloth_technique =
            renderer->m_ResourceCache.m_Techniques.get(cloth_hashedName);

        cb->bindPipeline(cloth_technique->passes[0].pipeline);
      }

      cb->bindDescriptorSet(&physicsMesh->descriptorSet, 1, nullptr, 0);

      // TODO: submit all meshes at once
      cb->dispatch(1, 1, 1);
    }
  }

  if (cb != nullptr)
  {
    cb->pop_marker();
    cb->pop_marker();

    // Graphics queries not available in compute only queues.

    cb->end();
  }

  return cb;
#endif
}

// TODO: refactor
Transform animated_transforms[256];

void RenderScene::update_animations(float delta_time)
{

  if (animations.m_Size == 0)
  {
    return;
  }

  // TODO: update the first animation as test
  Animation& animation = animations[0];
  static float current_time = 0.f;

  current_time += delta_time;
  if (current_time > animation.time_end)
  {
    current_time -= animation.time_end;
  }

  // TODO: fix skeleton/scene graph relationship
  for (uint32_t i = 0; i < 256; ++i)
  {

    Transform& transform = animated_transforms[i];
    transform.reset();
  }
  // Accumulate transformations

  u8 changed[256];
  memset(changed, 0, 256);

  // For each animation channel
  for (uint32_t ac = 0; ac < animation.channels.m_Size; ++ac)
  {
    AnimationChannel& channel = animation.channels[ac];
    AnimationSampler& sampler = animation.samplers[channel.sampler];

    if (sampler.interpolation_type != AnimationSampler::Linear)
    {
      rprint("Interpolation %s still not supported.\n", sampler.interpolation_type);
      continue;
    }

    // Scroll through all key frames
    for (uint32_t ki = 0; ki < sampler.key_frames.m_Size - 1; ++ki)
    {
      const float keyframe = sampler.key_frames[ki];
      const float next_keyframe = sampler.key_frames[ki + 1];
      if (current_time >= keyframe && current_time <= next_keyframe)
      {

        const float interpolation = (current_time - keyframe) / (next_keyframe - keyframe);

        assert(channel.targetNode < 256);
        changed[channel.targetNode] = 1;
        Transform& transform = animated_transforms[channel.targetNode];
        switch (channel.target_type)
        {
        case AnimationChannel::TargetType::Translation: {
          const vec3s current_data{sampler.data[ki].x, sampler.data[ki].y, sampler.data[ki].z};
          const vec3s next_data{
              sampler.data[ki + 1].x, sampler.data[ki + 1].y, sampler.data[ki + 1].z};
          transform.translation = glms_vec3_lerp(current_data, next_data, interpolation);

          break;
        }
        case AnimationChannel::TargetType::Rotation: {
          const vec4s current_data = sampler.data[ki];
          const versors current_rotation =
              glms_quat_init(current_data.x, current_data.y, current_data.z, current_data.w);

          const vec4s next_data = sampler.data[ki + 1];
          const versors next_rotation =
              glms_quat_init(next_data.x, next_data.y, next_data.z, next_data.w);

          transform.rotation =
              glms_quat_normalize(glms_quat_slerp(current_rotation, next_rotation, interpolation));

          break;
        }
        case AnimationChannel::TargetType::Scale: {
          const vec3s current_data{sampler.data[ki].x, sampler.data[ki].y, sampler.data[ki].z};
          const vec3s next_data{
              sampler.data[ki + 1].x, sampler.data[ki + 1].y, sampler.data[ki + 1].z};
          transform.scale = glms_vec3_lerp(current_data, next_data, interpolation);

          break;
        }
        default:
          break;
        }

        break;
      }
    }
  }
}

// TODO: remove, improve
mat4s get_local_matrix(SceneGraph* sceneGraph, uint32_t nodeIndex)
{
  const mat4s& a = animated_transforms[nodeIndex].calculate_matrix();
  // NOTE: according to the spec (3.7.3.2)
  // Only the joint transforms are applied to the skinned mesh; the transform of the skinned mesh
  // node MUST be ignored
  return a;
}

mat4s getNode_transform(SceneGraph* sceneGraph, uint32_t nodeIndex)
{
  mat4s node_transform = get_local_matrix(sceneGraph, nodeIndex);

  i32 parent = sceneGraph->nodes_hierarchy[nodeIndex].parent;
  while (parent >= 0)
  {
    node_transform = glms_mat4_mul(get_local_matrix(sceneGraph, parent), node_transform);

    parent = sceneGraph->nodes_hierarchy[parent].parent;
  }

  return node_transform;
}

void RenderScene::update_joints()
{

  for (uint32_t i = 0; i < skins.m_Size; i++)
  {
    Skin& skin = skins[i];

    // Calculate joint transforms and upload to GPU
    MapBufferParameters cbMap{skin.joint_transforms, 0, 0};
    mat4s* joint_transforms = (mat4s*)renderer->m_GpuDevice->mapBuffer(cbMap);

    if (joint_transforms)
    {
      for (uint32_t ji = 0; ji < skin.joints.m_Size; ji++)
      {
        uint32_t joint = skin.joints[ji];

        mat4s& joint_transform = joint_transforms[ji];

        joint_transform =
            glms_mat4_mul(getNode_transform(sceneGraph, joint), skin.inverse_bind_matrices[ji]);
      }

      renderer->m_GpuDevice->unmapBuffer(cbMap);
    }
  }
}

// RenderScene ////////////////////////////////////////////////////////////
void RenderScene::uploadGpuData()
{

  // uint32_t currentFrameIndex = renderer->m_GpuDevice->absolute_frame;

  // Update per mesh material buffer
  for (uint32_t meshIndex = 0; meshIndex < meshes.m_Size; ++meshIndex)
  {
    Mesh& mesh = meshes[meshIndex];

    MapBufferParameters cbMap = {mesh.pbrMaterial.materialBuffer, 0, 0};
    GpuMeshData* mesh_data = (GpuMeshData*)renderer->m_GpuDevice->mapBuffer(cbMap);
    if (mesh_data)
    {
      copyGpuMaterialData(*mesh_data, mesh);
      copyGpuMeshMatrix(*mesh_data, mesh, p_GlobalScale, sceneGraph);

      renderer->m_GpuDevice->unmapBuffer(cbMap);
    }
  }
}

void RenderScene::drawMesh(CommandBuffer* gpuCommands, Mesh& mesh)
{

  BufferHandle buffers[]{
      mesh.positionBuffer,
      mesh.tangentBuffer,
      mesh.normalBuffer,
      mesh.texcoordBuffer,
      mesh.jointsBuffer,
      mesh.weightsBuffer};
  uint32_t offsets[]{
      mesh.position_offset,
      mesh.tangent_offset,
      mesh.normal_offset,
      mesh.texcoord_offset,
      mesh.joints_offset,
      mesh.weights_offset};
  gpuCommands->bindVertexBuffers(buffers, 0, mesh.skinIndex != i32_max ? 6 : 4, offsets);

  gpuCommands->bindIndexBuffer(mesh.indexBuffer, mesh.index_offset, mesh.index_type);

  if (recreate_per_thread_descriptors)
  {
    DescriptorSetCreation dsCreation{};
    dsCreation.buffer(sceneCb, 0).buffer(mesh.pbrMaterial.materialBuffer, 1);
    DescriptorSetHandle descriptorSet =
        renderer->createDescriptorSet(gpuCommands, mesh.pbrMaterial.material, dsCreation);

    gpuCommands->bind_local_descriptorSet(&descriptorSet, 1, nullptr, 0);
  }
  else
  {
    gpuCommands->bindDescriptorSet(&mesh.pbrMaterial.descriptorSet, 1, nullptr, 0);
  }

  gpuCommands->drawIndexed(TopologyType::kTriangle, mesh.primitive_count, 1, 0, 0, 0);
}

// DrawTask ///////////////////////////////////////////////////////////////
void DrawTask::init(
    GpuDevice* gpu_,
    FrameGraph* frameGraph_,
    Renderer* renderer_,
    ImGuiService* imgui_,
    GpuVisualProfiler* gpu_profiler_,
    RenderScene* scene_,
    FrameRenderer* frame_renderer_)
{
  gpu = gpu_;
  frameGraph = frameGraph_;
  renderer = renderer_;
  imgui = imgui_;
  gpu_profiler = gpu_profiler_;
  scene = scene_;
  frame_renderer = frame_renderer_;

  currentFrameIndex = gpu->currentFrame;
  currentFramebuffer = gpu->get_currentFramebuffer();
}

void DrawTask::ExecuteRange(enki::TaskSetPartition range_, uint32_t threadnum_)
{
  ZoneScoped;

  using namespace Graphics;

  thread_id = threadnum_;

  // rprint( "Executing draw task from thread %u\n", threadnum_ );
  // TODO: improve getting a command buffer/pool
  CommandBuffer* gpuCommands = gpu->get_commandBuffer(threadnum_, currentFrameIndex, true);
  gpuCommands->push_marker("Frame");

  frameGraph->render(currentFrameIndex, gpuCommands, scene);

  gpuCommands->push_marker("Fullscreen");
  gpuCommands->clear(0.3f, 0.3f, 0.3f, 1.f, 0);
  gpuCommands->clear_depth_stencil(1.0f, 0);
  gpuCommands->bind_pass(gpu->get_swapchain_pass(), currentFramebuffer, false);
  gpuCommands->set_scissor(nullptr);
  gpuCommands->set_viewport(nullptr);

  // Apply fullscreen material
  FrameGraphResource* texture = frameGraph->getResource("final");
  assert(texture != nullptr);

  gpuCommands->bindPipeline(frame_renderer->fullscreen_tech->passes[0].pipeline);
  gpuCommands->bindDescriptorSet(&frame_renderer->fullscreen_ds, 1, nullptr, 0);
  gpuCommands->draw(
      TopologyType::kTriangle,
      0,
      3,
      texture->resourceInfo.texture.handle[currentFrameIndex].index,
      1);

  imgui->render(*gpuCommands, false);

  gpuCommands->pop_marker(); // Fullscreen marker
  gpuCommands->pop_marker(); // Frame marker

  gpu_profiler->update(*gpu);

  // Send commands to GPU
  gpu->queue_commandBuffer(gpuCommands);
}

// FrameRenderer //////////////////////////////////////////////////////////
void FrameRenderer::init(
    Allocator* residentAllocator_,
    Renderer* renderer_,
    FrameGraph* frameGraph_,
    SceneGraph* sceneGraph_,
    RenderScene* scene_)
{
  residentAllocator = residentAllocator_;
  renderer = renderer_;
  frameGraph = frameGraph_;
  sceneGraph = sceneGraph_;
  scene = scene_;

  frameGraph->builder->register_render_pass("depth_pre_pass", &depth_pre_pass);
  frameGraph->builder->register_render_pass("gbuffer_pass", &gbuffer_pass);
  frameGraph->builder->register_render_pass("lighting_pass", &light_pass);
  frameGraph->builder->register_render_pass("transparent_pass", &transparent_pass);
  frameGraph->builder->register_render_pass("depth_of_field_pass", &dof_pass);
  frameGraph->builder->register_render_pass("debug_pass", &debug_pass);
}

void FrameRenderer::shutdown()
{
  depth_pre_pass.freeGpuResources();
  gbuffer_pass.freeGpuResources();
  light_pass.freeGpuResources();
  transparent_pass.freeGpuResources();
  // TODO: check that node is enabled before calling
  // dof_pass.freeGpuResources();
  debug_pass.freeGpuResources();

  renderer->m_GpuDevice->destroyDescriptorSet(fullscreen_ds);
}

void FrameRenderer::uploadGpuData()
{
  light_pass.uploadGpuData();
  // dof_pass.uploadGpuData();

  scene->uploadGpuData();
}

void FrameRenderer::render(CommandBuffer* gpuCommands, RenderScene* renderScene) {}

void FrameRenderer::prepareDraws(Framework::StackAllocator* scratchAllocator)
{

  scene->prepareDraws(renderer, scratchAllocator, sceneGraph);

  depth_pre_pass.prepareDraws(
      *scene, frameGraph, renderer->m_GpuDevice->allocator, scratchAllocator);
  gbuffer_pass.prepareDraws(*scene, frameGraph, renderer->m_GpuDevice->allocator, scratchAllocator);
  light_pass.prepareDraws(*scene, frameGraph, renderer->m_GpuDevice->allocator, scratchAllocator);
  transparent_pass.prepareDraws(
      *scene, frameGraph, renderer->m_GpuDevice->allocator, scratchAllocator);
  // dof_pass.prepareDraws( *scene, frameGraph, renderer->m_GpuDevice->allocator, scratchAllocator
  // );
  debug_pass.prepareDraws(*scene, frameGraph, renderer->m_GpuDevice->allocator, scratchAllocator);

  // Handle fullscreen pass.
  fullscreen_tech = renderer->m_ResourceCache.m_Techniques.get(hashCalculate("fullscreen"));

  DescriptorSetCreation dsc;
  DescriptorSetLayoutHandle descriptorSet_layout = renderer->m_GpuDevice->getDescriptorSetLayout(
      fullscreen_tech->passes[0].pipeline, kMaterialDescriptorSetIndex);
  dsc.reset().buffer(scene->sceneCb, 0).setLayout(descriptorSet_layout);
  fullscreen_ds = renderer->m_GpuDevice->createDescriptorSet(dsc);
}

// Transform ////////////////////////////////////////////////////

void Transform::reset()
{
  translation = {0.f, 0.f, 0.f};
  scale = {1.f, 1.f, 1.f};
  rotation = glms_quat_identity();
}

mat4s Transform::calculate_matrix() const
{

  const mat4s translation_matrix = glms_translate_make(translation);
  const mat4s scaleMatrix = glms_scale_make(scale);
  const mat4s local_matrix =
      glms_mat4_mul(glms_mat4_mul(translation_matrix, glms_quat_mat4(rotation)), scaleMatrix);
  return local_matrix;
}

} // namespace Graphics
