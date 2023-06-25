#include "Graphics/GltfScene.hpp"

#include "Foundation/Time.hpp"

#include "Graphics/SceneGraph.hpp"
#include "Graphics/ImguiHelper.hpp"
#include "Graphics/AsynchronousLoader.hpp"

#include "Externals/cglm/struct/affine.h"
#include "Externals/cglm/struct/mat4.h"
#include "Externals/cglm/struct/vec3.h"
#include "Externals/cglm/struct/quat.h"

#include "Externals/imgui/imgui.h"
#include "Externals/stb_image.h"

namespace Graphics
{
//---------------------------------------------------------------------------//
static TextureCreation g_DofSceneTextureCreation;
//---------------------------------------------------------------------------//
// Internal methods:
//---------------------------------------------------------------------------//
static int gltfMeshMaterialCompare(const void* a, const void* b)
{
  const Mesh* meshA = (const Mesh*)a;
  const Mesh* meshB = (const Mesh*)b;

  if (meshA->pbrMaterial.material->m_RenderIndex < meshB->pbrMaterial.material->m_RenderIndex)
    return -1;
  if (meshA->pbrMaterial.material->m_RenderIndex > meshB->pbrMaterial.material->m_RenderIndex)
    return 1;
  return 0;
}
//---------------------------------------------------------------------------//
static void copyGpuMaterialData(GpuMeshData& p_GpuMeshData, const Mesh& p_Mesh)
{
  p_GpuMeshData.textures[0] = p_Mesh.pbrMaterial.diffuseTextureIndex;
  p_GpuMeshData.textures[1] = p_Mesh.pbrMaterial.roughnessTextureIndex;
  p_GpuMeshData.textures[2] = p_Mesh.pbrMaterial.normalTextureIndex;
  p_GpuMeshData.textures[3] = p_Mesh.pbrMaterial.occlusionTextureIndex;
  p_GpuMeshData.baseColorFactor = p_Mesh.pbrMaterial.baseColorFactor;
  p_GpuMeshData.metallicRoughnessOcclusionFactor =
      p_Mesh.pbrMaterial.metallicRoughnessOcclusionFactor;
  p_GpuMeshData.alphaCutoff = p_Mesh.pbrMaterial.alphaCutoff;
  p_GpuMeshData.flags = p_Mesh.pbrMaterial.flags;
}
//---------------------------------------------------------------------------//
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
//---------------------------------------------------------------------------//
// Depth pre pass:
//---------------------------------------------------------------------------//
void DepthPrePass::render(CommandBuffer* gpuCommands, RenderScene* renderScene)
{
  glTFScene* scene = (glTFScene*)renderScene;

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

    scene->drawMesh(gpuCommands, mesh);
  }
}
//---------------------------------------------------------------------------//
void DepthPrePass::prepareDraws(
    glTFScene& p_Scene,
    FrameGraph* p_FrameGraph,
    Framework::Allocator* residentAllocator,
    Framework::StackAllocator* scratchAllocator)
{
  using namespace RendererUtil;

  renderer = p_Scene.renderer;

  FrameGraphNode* node = p_FrameGraph->getNode("depth_pre_pass");
  if (node == nullptr)
  {
    assert(false);
    return;
  }

  // Create pipeline state
  PipelineCreation pipelineCreation;

  const uint64_t hashedName = Framework::hashCalculate("main");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;

  materialCreation.setName("material_depth_pre_pass").setTechnique(mainTechnique).setRenderIndex(0);
  RendererUtil::Material* materialDepthPrePass = renderer->createMaterial(materialCreation);

  glTF::glTF& gltfScene = p_Scene.gltfScene;

  meshInstances.init(residentAllocator, 16);

  // Copy all mesh draws and change only material.
  for (uint32_t i = 0; i < p_Scene.meshes.m_Size; ++i)
  {

    Mesh* mesh = &p_Scene.meshes[i];
    if (mesh->isTransparent())
    {
      continue;
    }

    MeshInstance meshInstance{};
    meshInstance.mesh = mesh;
    // TODO: pass 0 of main material is depth prepass.
    meshInstance.materialPassIndex = 0;

    meshInstances.push(meshInstance);
  }
}
//---------------------------------------------------------------------------//
void DepthPrePass::freeGpuResources()
{
  GpuDevice& gpu = *renderer->m_GpuDevice;

  meshInstances.shutdown();
}
//---------------------------------------------------------------------------//
// Gbuffer pass:
//---------------------------------------------------------------------------//
void GBufferPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene)
{
  glTFScene* scene = (glTFScene*)renderScene;

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

    scene->drawMesh(gpuCommands, mesh);
  }
}
//---------------------------------------------------------------------------//
void GBufferPass::prepareDraws(
    glTFScene& p_Scene,
    FrameGraph* p_FrameGraph,
    Framework::Allocator* p_ResidentAllocator,
    Framework::StackAllocator* p_ScratchAllocator)
{
  using namespace RendererUtil;

  renderer = p_Scene.renderer;

  FrameGraphNode* node = p_FrameGraph->getNode("gbuffer_pass");
  if (node == nullptr)
  {
    assert(false);
    return;
  }

  const uint64_t hashedName = Framework::hashCalculate("main");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;

  materialCreation.setName("material_no_cull").setTechnique(mainTechnique).setRenderIndex(0);
  RendererUtil::Material* material = renderer->createMaterial(materialCreation);

  glTF::glTF& gltfScene = p_Scene.gltfScene;

  meshInstances.init(p_ResidentAllocator, 16);

  // Copy all mesh draws and change only material.
  for (uint32_t i = 0; i < p_Scene.meshes.m_Size; ++i)
  {

    // Skip transparent meshes
    Mesh* mesh = &p_Scene.meshes[i];
    if (mesh->isTransparent())
    {
      continue;
    }

    MeshInstance meshInstance{};
    meshInstance.mesh = mesh;
    meshInstance.materialPassIndex = 1;

    meshInstances.push(meshInstance);
  }

  // qsort( meshDraws.m_Data, meshDraws.m_Size, sizeof( MeshDraw ), gltfMeshMaterialCompare );
}
//---------------------------------------------------------------------------//
void GBufferPass::freeGpuResources()
{
  GpuDevice& gpu = *renderer->m_GpuDevice;

  meshInstances.shutdown();
}
//---------------------------------------------------------------------------//
// Light pass:
void LightPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene)
{
  glTFScene* scene = (glTFScene*)renderScene;

  PipelineHandle pipeline = renderer->getPipeline(mesh.pbrMaterial.material, 0);

  gpuCommands->bindPipeline(pipeline);
  gpuCommands->bindVertexBuffer(mesh.positionBuffer, 0, 0);
  gpuCommands->bindDescriptorSet(&mesh.pbrMaterial.descriptorSet, 1, nullptr, 0);

  gpuCommands->draw(TopologyType::kTriangle, 0, 3, 0, 1);
}
//---------------------------------------------------------------------------//
void LightPass::prepareDraws(
    glTFScene& scene,
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

  const uint64_t hashedName = Framework::hashCalculate("pbr_lighting");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;

  materialCreation.setName("material_pbr").setTechnique(mainTechnique).setRenderIndex(0);
  RendererUtil::Material* materialPbr = renderer->createMaterial(materialCreation);

  BufferCreation bufferCreation;
  bufferCreation.reset()
      .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, sizeof(GpuMeshData))
      .setName("meshData");
  mesh.pbrMaterial.materialBuffer = renderer->m_GpuDevice->createBuffer(bufferCreation);

  DescriptorSetCreation dsCreation{};
  DescriptorSetLayoutHandle layout = renderer->m_GpuDevice->getDescriptorSetLayout(
      mainTechnique->passes[0].pipeline, kMaterialDescriptorSetIndex);
  dsCreation.buffer(scene.sceneCb, 0).buffer(mesh.pbrMaterial.materialBuffer, 1).setLayout(layout);
  mesh.pbrMaterial.descriptorSet = renderer->m_GpuDevice->createDescriptorSet(dsCreation);

  BufferHandle vb = renderer->m_GpuDevice->m_FullscreenVertexBuffer;
  mesh.positionBuffer = vb;

  FrameGraphResource* colorTexture = frameGraph->accessResource(node->inputs[0]);
  FrameGraphResource* normalTexture = frameGraph->accessResource(node->inputs[1]);
  FrameGraphResource* roughnessTexture = frameGraph->accessResource(node->inputs[2]);
  FrameGraphResource* positionTexture = frameGraph->accessResource(node->inputs[3]);

  mesh.pbrMaterial.diffuseTextureIndex = colorTexture->resourceInfo.texture.texture.index;
  mesh.pbrMaterial.normalTextureIndex = normalTexture->resourceInfo.texture.texture.index;
  mesh.pbrMaterial.roughnessTextureIndex = roughnessTexture->resourceInfo.texture.texture.index;
  mesh.pbrMaterial.occlusionTextureIndex = positionTexture->resourceInfo.texture.texture.index;
  mesh.pbrMaterial.material = materialPbr;
}
//---------------------------------------------------------------------------//
void LightPass::uploadMaterials()
{
  MapBufferParameters cbMap = {mesh.pbrMaterial.materialBuffer, 0, 0};
  GpuMeshData* meshData = (GpuMeshData*)renderer->m_GpuDevice->mapBuffer(cbMap);
  if (meshData)
  {
    copyGpuMaterialData(*meshData, mesh);

    renderer->m_GpuDevice->unmapBuffer(cbMap);
  }
}
//---------------------------------------------------------------------------//
void LightPass::freeGpuResources()
{
  GpuDevice& gpu = *renderer->m_GpuDevice;

  gpu.destroyBuffer(mesh.pbrMaterial.materialBuffer);
  gpu.destroyDescriptorSet(mesh.pbrMaterial.descriptorSet);
}
//---------------------------------------------------------------------------//
// Transparent pass:
//---------------------------------------------------------------------------//
void TransparentPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene)
{
  glTFScene* scene = (glTFScene*)renderScene;

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

    scene->drawMesh(gpuCommands, mesh);
  }
}
//---------------------------------------------------------------------------//
void TransparentPass::prepareDraws(
    glTFScene& scene,
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

  const uint64_t hashedName = Framework::hashCalculate("main");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;

  materialCreation.setName("material_transparent").setTechnique(mainTechnique).setRenderIndex(0);
  RendererUtil::Material* materialDepthPrePass = renderer->createMaterial(materialCreation);

  glTF::glTF& gltfScene = scene.gltfScene;

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
    meshInstance.materialPassIndex = 4;

    meshInstances.push(meshInstance);
  }
}
//---------------------------------------------------------------------------//
void TransparentPass::freeGpuResources()
{
  GpuDevice& gpu = *renderer->m_GpuDevice;

  meshInstances.shutdown();
}
//---------------------------------------------------------------------------//
// DoF pass:
//---------------------------------------------------------------------------//
void DoFPass::addUi()
{
  ImGui::InputFloat("Focal Length", &focalLength);
  ImGui::InputFloat("Plane in Focus", &planeInFocus);
  ImGui::InputFloat("Aperture", &aperture);
}
//---------------------------------------------------------------------------//
void DoFPass::preRender(CommandBuffer* gpuCommands, RenderScene* renderScene)
{
  glTFScene* scene = (glTFScene*)renderScene;

  FrameGraphResource* texture = (FrameGraphResource*)scene->frameGraph->getResource("lighting");
  assert(texture != nullptr);

  gpuCommands->copyTexture(
      texture->resourceInfo.texture.texture,
      RESOURCE_STATE_RENDER_TARGET,
      sceneMips->m_Handle,
      RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
//---------------------------------------------------------------------------//
void DoFPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene)
{
  glTFScene* scene = (glTFScene*)renderScene;

  PipelineHandle pipeline = renderer->getPipeline(mesh.pbrMaterial.material, 0);

  gpuCommands->bindPipeline(pipeline);
  gpuCommands->bindVertexBuffer(mesh.positionBuffer, 0, 0);
  gpuCommands->bindDescriptorSet(&mesh.pbrMaterial.descriptorSet, 1, nullptr, 0);

  gpuCommands->draw(TopologyType::kTriangle, 0, 3, 0, 1);
}
//---------------------------------------------------------------------------//
void DoFPass::onResize(GpuDevice& gpu, uint32_t newWidth, uint32_t newHeight)
{
  uint32_t w = newWidth;
  uint32_t h = newHeight;

  uint32_t mips = 1;
  while (w > 1 && h > 1)
  {
    w /= 2;
    h /= 2;
    mips++;
  }

  // Destroy scene mips
  renderer->destroyTexture(sceneMips);

  // Reuse cached texture creation and create new scene mips.
  g_DofSceneTextureCreation.setFlags(mips, 0).setSize(newWidth, newHeight, 1);
  sceneMips = renderer->createTexture(g_DofSceneTextureCreation);

  mesh.pbrMaterial.diffuseTextureIndex = sceneMips->m_Handle.index;
}
//---------------------------------------------------------------------------//
void DoFPass::prepareDraws(
    glTFScene& scene,
    FrameGraph* frameGraph,
    Framework::Allocator* residentAllocator,
    Framework::StackAllocator* scratchAllocator)
{
  using namespace RendererUtil;

  renderer = scene.renderer;

  FrameGraphNode* node = frameGraph->getNode("depth_of_field_pass");
  if (node == nullptr)
  {
    assert(false);
    return;
  }

  const uint64_t hashedName = Framework::hashCalculate("depth_of_field");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;

  materialCreation.setName("material_dof").setTechnique(mainTechnique).setRenderIndex(0);
  RendererUtil::Material* materialDof = renderer->createMaterial(materialCreation);

  BufferCreation bufferCreation;
  bufferCreation.reset()
      .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, sizeof(DoFData))
      .setName("dof_data");
  mesh.pbrMaterial.materialBuffer = renderer->m_GpuDevice->createBuffer(bufferCreation);

  DescriptorSetCreation dsCreation{};
  DescriptorSetLayoutHandle layout = renderer->m_GpuDevice->getDescriptorSetLayout(
      mainTechnique->passes[0].pipeline, kMaterialDescriptorSetIndex);
  dsCreation.buffer(mesh.pbrMaterial.materialBuffer, 0).setLayout(layout);
  mesh.pbrMaterial.descriptorSet = renderer->m_GpuDevice->createDescriptorSet(dsCreation);

  BufferHandle vb = renderer->m_GpuDevice->m_FullscreenVertexBuffer;
  mesh.positionBuffer = vb;

  FrameGraphResource* colorTexture = frameGraph->accessResource(node->inputs[0]);
  FrameGraphResource* depthTextureReference = frameGraph->accessResource(node->inputs[1]);

  FrameGraphResource* depthTexture = frameGraph->getResource(depthTextureReference->m_Name);
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

  g_DofSceneTextureCreation.setData(nullptr)
      .setFormatType(info.texture.format, TextureType::kTexture2D)
      .setFlags(mips, 0)
      .setSize((uint16_t)info.texture.width, (uint16_t)info.texture.height, 1)
      .setName("scene_mips");
  sceneMips = renderer->createTexture(g_DofSceneTextureCreation);

  mesh.pbrMaterial.diffuseTextureIndex = sceneMips->m_Handle.index;
  mesh.pbrMaterial.roughnessTextureIndex = depthTexture->resourceInfo.texture.texture.index;
  mesh.pbrMaterial.material = materialDof;

  znear = 0.1f;
  zfar = 1000.0f;
  focalLength = 5.0f;
  planeInFocus = 1.0f;
  aperture = 8.0f;
}
//---------------------------------------------------------------------------//
void DoFPass::uploadMaterials()
{
  MapBufferParameters cbMap = {mesh.pbrMaterial.materialBuffer, 0, 0};
  DoFData* dofData = (DoFData*)renderer->m_GpuDevice->mapBuffer(cbMap);
  if (dofData)
  {
    dofData->textures[0] = mesh.pbrMaterial.diffuseTextureIndex;
    dofData->textures[1] = mesh.pbrMaterial.roughnessTextureIndex;

    dofData->znear = znear;
    dofData->zfar = zfar;
    dofData->focalLength = focalLength;
    dofData->planeInFocus = planeInFocus;
    dofData->aperture = aperture;

    renderer->m_GpuDevice->unmapBuffer(cbMap);
  }
}
//---------------------------------------------------------------------------//
void DoFPass::freeGpuResources()
{
  GpuDevice& gpu = *renderer->m_GpuDevice;

  renderer->destroyTexture(sceneMips);
  gpu.destroyBuffer(mesh.pbrMaterial.materialBuffer);
  gpu.destroyDescriptorSet(mesh.pbrMaterial.descriptorSet);
}
//---------------------------------------------------------------------------//
// glTF scene:
//---------------------------------------------------------------------------//
void glTFScene::init(
    const char* filename,
    const char* path,
    Framework::Allocator* residentAllocator,
    Framework::StackAllocator* tempAllocator,
    AsynchronousLoader* asyncLoader)
{
  renderer = asyncLoader->renderer;
  enki::TaskScheduler* taskScheduler = asyncLoader->taskScheduler;
  size_t tempAllocatorInitialMarker = tempAllocator->getMarker();

  // Time statistics
  int64_t startSceneLoading = Time::getCurrentTime();

  gltfScene = gltfLoadFile(filename);

  int64_t endLoadingFile = Time::getCurrentTime();

  // Load all textures
  images.init(residentAllocator, gltfScene.imagesCount);

  Array<TextureCreation> tcs;
  tcs.init(tempAllocator, gltfScene.imagesCount, gltfScene.imagesCount);

  StringBuffer nameBuffer;
  nameBuffer.init(4096, tempAllocator);

  for (uint32_t imageIndex = 0; imageIndex < gltfScene.imagesCount; ++imageIndex)
  {
    glTF::Image& image = gltfScene.images[imageIndex];

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

    TextureCreation tc;
    tc.setData(nullptr)
        .setFormatType(VK_FORMAT_R8G8B8A8_UNORM, TextureType::kTexture2D)
        .setFlags(mipLevels, 0)
        .setSize((uint16_t)width, (uint16_t)height, 1)
        .setName(image.uri.m_Data);
    RendererUtil::TextureResource* tr = renderer->createTexture(tc);
    assert(tr != nullptr);

    images.push(*tr);

    // Reconstruct file path
    char* fullFilename = nameBuffer.appendUseFormatted("%s%s", path, image.uri.m_Data);
    asyncLoader->requestTextureData(fullFilename, tr->m_Handle);
    // Reset name buffer
    nameBuffer.clear();
  }

  int64_t endLoadingTexturesFiles = Time::getCurrentTime();

  int64_t endCreatingTextures = Time::getCurrentTime();

  // Load all samplers
  samplers.init(residentAllocator, gltfScene.samplersCount);

  for (uint32_t samplerIndex = 0; samplerIndex < gltfScene.samplersCount; ++samplerIndex)
  {
    glTF::Sampler& sampler = gltfScene.samplers[samplerIndex];

    char* samplerName = nameBuffer.appendUseFormatted("sampler_%u", samplerIndex);

    SamplerCreation creation;
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

    RendererUtil::SamplerResource* sr = renderer->createSampler(creation);
    assert(sr != nullptr);

    samplers.push(*sr);
  }

  int64_t endCreatingSamplers = Time::getCurrentTime();

  // Temporary array of buffer data
  Array<void*> buffersData;
  buffersData.init(residentAllocator, gltfScene.buffersCount);

  for (uint32_t bufferIndex = 0; bufferIndex < gltfScene.buffersCount; ++bufferIndex)
  {
    glTF::Buffer& buffer = gltfScene.buffers[bufferIndex];

    FileReadResult bufferData = fileReadBinary(buffer.uri.m_Data, residentAllocator);
    buffersData.push(bufferData.data);
  }

  int64_t endReadingBuffersData = Time::getCurrentTime();

  // Load all buffers and initialize them with buffer data
  buffers.init(residentAllocator, gltfScene.bufferViewsCount);

  for (uint32_t bufferIndex = 0; bufferIndex < gltfScene.bufferViewsCount; ++bufferIndex)
  {
    glTF::BufferView& buffer = gltfScene.bufferViews[bufferIndex];

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

    RendererUtil::BufferResource* br = renderer->createBuffer(
        flags, ResourceUsageType::kImmutable, buffer.byteLength, bufferData, bufferName);
    assert(br != nullptr);

    buffers.push(*br);
  }

  for (uint32_t bufferIndex = 0; bufferIndex < gltfScene.buffersCount; ++bufferIndex)
  {
    void* buffer = buffersData[bufferIndex];
    residentAllocator->deallocate(buffer);
  }
  buffersData.shutdown();

  int64_t endCreatingBuffers = Time::getCurrentTime();

  // This is not needed anymore, free all temp memory after.
  // resourceNameBuffer.shutdown();
  tempAllocator->freeMarker(tempAllocatorInitialMarker);

  // Init runtime meshes
  meshes.init(residentAllocator, gltfScene.meshesCount);

  int64_t endLoading = Time::getCurrentTime();

  printf(
      "Loaded scene %s in %f seconds.\nStats:\n\tReading GLTF file %f seconds\n\tTextures Creating "
      "%f seconds\n\tCreating Samplers %f seconds\n\tReading Buffers Data %f seconds\n\tCreating "
      "Buffers %f seconds\n",
      filename,
      Time::deltaSeconds(startSceneLoading, endLoading),
      Time::deltaSeconds(startSceneLoading, endLoadingFile),
      Time::deltaSeconds(endLoadingFile, endCreatingTextures),
      Time::deltaSeconds(endCreatingTextures, endCreatingSamplers),
      Time::deltaSeconds(endCreatingSamplers, endReadingBuffersData),
      Time::deltaSeconds(endReadingBuffersData, endCreatingBuffers));
}
//---------------------------------------------------------------------------//
void glTFScene::shutdown(RendererUtil::Renderer* p_Renderer)
{
  GpuDevice& gpu = *p_Renderer->m_GpuDevice;

  for (uint32_t meshIndex = 0; meshIndex < meshes.m_Size; ++meshIndex)
  {
    Mesh& mesh = meshes[meshIndex];

    gpu.destroyBuffer(mesh.pbrMaterial.materialBuffer);
    gpu.destroyDescriptorSet(mesh.pbrMaterial.descriptorSet);
  }

  gpu.destroyDescriptorSet(fullscreenDS);
  gpu.destroyBuffer(sceneCb);

  for (uint32_t i = 0; i < images.m_Size; ++i)
  {
    p_Renderer->destroyTexture(&images[i]);
  }

  for (uint32_t i = 0; i < samplers.m_Size; ++i)
  {
    p_Renderer->destroySampler(&samplers[i]);
  }

  for (uint32_t i = 0; i < buffers.m_Size; ++i)
  {
    p_Renderer->destroyBuffer(&buffers[i]);
  }

  meshes.shutdown();

  depthPrePass.freeGpuResources();
  gbufferPass.freeGpuResources();
  lightPass.freeGpuResources();
  transparentPass.freeGpuResources();
  dofPass.freeGpuResources();

  // Free scene buffers
  samplers.shutdown();
  images.shutdown();
  buffers.shutdown();

  // NOTE: we can't destroy this sooner as textures and buffers
  // hold a pointer to the names stored here
  gltfFree(gltfScene);
}
//---------------------------------------------------------------------------//
void glTFScene::registerRenderPasses(FrameGraph* p_FrameGraph)
{
  frameGraph = p_FrameGraph;

  frameGraph->builder->registerRenderPass("depth_pre_pass", &depthPrePass);
  frameGraph->builder->registerRenderPass("gbuffer_pass", &gbufferPass);
  frameGraph->builder->registerRenderPass("lighting_pass", &lightPass);
  frameGraph->builder->registerRenderPass("transparent_pass", &transparentPass);
  frameGraph->builder->registerRenderPass("depth_of_field_pass", &dofPass);
}
//---------------------------------------------------------------------------//
void glTFScene::prepareDraws(
    RendererUtil::Renderer* p_Renderer,
    Framework::StackAllocator* p_ScratchAllocator,
    SceneGraph* p_SceneGraph)
{
  using namespace RendererUtil;

  sceneGraph = p_SceneGraph;

  size_t cachedScratchSize = p_ScratchAllocator->getMarker();

  // Scene constant buffer
  BufferCreation bufferCreation;
  bufferCreation.reset()
      .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, sizeof(GpuSceneData))
      .setName("scene_cb");
  sceneCb = p_Renderer->m_GpuDevice->createBuffer(bufferCreation);

  // Create material
  const uint64_t hashedName = Framework::hashCalculate("main");
  GpuTechnique* mainTechnique = p_Renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;
  materialCreation.setName("material_no_cull_opaque").setTechnique(mainTechnique).setRenderIndex(0);

  RendererUtil::Material* pbrMaterial = p_Renderer->createMaterial(materialCreation);

  glTF::Scene& rootGltfScene = gltfScene.scenes[gltfScene.scene];

  //
  Array<int> nodesToVisit;
  nodesToVisit.init(p_ScratchAllocator, 4);

  // Calculate total node count: add first the root nodes.
  uint32_t totalNodeCount = rootGltfScene.nodesCount;

  // Add initial nodes
  for (uint32_t nodeIndex = 0; nodeIndex < rootGltfScene.nodesCount; ++nodeIndex)
  {
    const int node = rootGltfScene.nodes[nodeIndex];
    nodesToVisit.push(node);
  }
  // Visit nodes
  while (nodesToVisit.m_Size)
  {
    int nodeIndex = nodesToVisit.front();
    nodesToVisit.deleteSwap(0);

    glTF::Node& node = gltfScene.nodes[nodeIndex];
    for (uint32_t ch = 0; ch < node.childrenCount; ++ch)
    {
      const int childrenIndex = node.children[ch];
      nodesToVisit.push(childrenIndex);
    }

    // Add only children nodes to the count, as the current node is
    // already calculated when inserting it.
    totalNodeCount += node.childrenCount;
  }

  sceneGraph->resize(totalNodeCount);

  // Populate scene graph: visit again
  nodesToVisit.clear();
  // Add initial nodes
  for (uint32_t nodeIndex = 0; nodeIndex < rootGltfScene.nodesCount; ++nodeIndex)
  {
    const int node = rootGltfScene.nodes[nodeIndex];
    nodesToVisit.push(node);
  }

  while (nodesToVisit.m_Size)
  {
    int nodeIndex = nodesToVisit.front();
    nodesToVisit.deleteSwap(0);

    glTF::Node& node = gltfScene.nodes[nodeIndex];

    // Compute local transform: read either raw matrix or individual Scale/Rotation/Translation
    // components
    if (node.matrixCount)
    {
      // CGLM and glTF have the same matrix layout, just memcopy it
      memcpy(&sceneGraph->localMatrices[nodeIndex], node.matrix, sizeof(mat4s));
      sceneGraph->updatedNodes.setBit(nodeIndex);
    }
    else
    {
      // Handle individual transform components: SRT (scale, rotation, translation)
      vec3s nodeScale{1.0f, 1.0f, 1.0f};
      if (node.scaleCount)
      {
        assert(node.scaleCount == 3);
        nodeScale = vec3s{node.scale[0], node.scale[1], node.scale[2]};
      }
      mat4s scaleMatrix = glms_scale_make(nodeScale);

      vec3s translation{0.f, 0.f, 0.f};
      if (node.translationCount)
      {
        assert(node.translationCount == 3);
        translation = vec3s{node.translation[0], node.translation[1], node.translation[2]};
      }
      mat4s translation_matrix = glms_translate_make(translation);
      // Rotation is written as a plain quaternion
      versors rotation = glms_quat_identity();
      if (node.rotationCount)
      {
        assert(node.rotationCount == 4);
        rotation =
            glms_quat_init(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]);
      }
      // Final SRT composition
      const mat4s localMatrix =
          glms_mat4_mul(glms_mat4_mul(scaleMatrix, glms_quat_mat4(rotation)), translation_matrix);
      sceneGraph->setLocalMatrix(nodeIndex, localMatrix);
    }

    // Handle parent-relationship
    if (node.childrenCount)
    {
      const Hierarchy& nodeHierarchy = sceneGraph->nodesHierarchy[nodeIndex];

      for (uint32_t ch = 0; ch < node.childrenCount; ++ch)
      {
        const int childrenIndex = node.children[ch];
        Hierarchy& childrenHierarchy = sceneGraph->nodesHierarchy[childrenIndex];
        sceneGraph->setHierarchy(childrenIndex, nodeIndex, nodeHierarchy.level + 1);

        nodesToVisit.push(childrenIndex);
      }
    }

    if (node.mesh == glTF::INVALID_INT_VALUE)
    {
      continue;
    }

    glTF::Mesh& gltfMesh = gltfScene.meshes[node.mesh];

    // Gltf primitives are conceptually submeshes.
    for (uint32_t primitive_index = 0; primitive_index < gltfMesh.primitivesCount;
         ++primitive_index)
    {
      Mesh mesh{};
      // Assign scene graph node index
      mesh.sceneGraphNodeIndex = nodeIndex;

      glTF::MeshPrimitive& meshPrimitive = gltfMesh.primitives[primitive_index];

      const int positionAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "POSITION");
      const int tangentAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "TANGENT");
      const int normalAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "NORMAL");
      const int texcoordAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "TEXCOORD_0");

      getMeshVertexBuffer(positionAccessorIndex, mesh.positionBuffer, mesh.positionOffset);
      getMeshVertexBuffer(tangentAccessorIndex, mesh.tangentBuffer, mesh.tangentOffset);
      getMeshVertexBuffer(normalAccessorIndex, mesh.normalBuffer, mesh.normalOffset);
      getMeshVertexBuffer(texcoordAccessorIndex, mesh.texcoordBuffer, mesh.texcoordOffset);

      // Create index buffer
      glTF::Accessor& indicesAccessor = gltfScene.accessors[meshPrimitive.indices];
      assert(
          indicesAccessor.componentType == glTF::Accessor::ComponentType::UNSIGNED_SHORT ||
          indicesAccessor.componentType == glTF::Accessor::ComponentType::UNSIGNED_INT);
      mesh.indexType =
          (indicesAccessor.componentType == glTF::Accessor::ComponentType::UNSIGNED_SHORT)
              ? VK_INDEX_TYPE_UINT16
              : VK_INDEX_TYPE_UINT32;

      glTF::BufferView& indicesBufferView = gltfScene.bufferViews[indicesAccessor.bufferView];
      RendererUtil::BufferResource& indicesBufferGpu = buffers[indicesAccessor.bufferView];
      mesh.indexBuffer = indicesBufferGpu.m_Handle;
      mesh.indexOffset =
          indicesAccessor.byteOffset == glTF::INVALID_INT_VALUE ? 0 : indicesAccessor.byteOffset;
      mesh.primitiveCount = indicesAccessor.count;

      // Read pbr material data
      glTF::Material& material = gltfScene.materials[meshPrimitive.material];
      fillPbrMaterial(*p_Renderer, material, mesh.pbrMaterial);

      // Create material buffer
      BufferCreation bufferCreation;
      bufferCreation.reset()
          .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, sizeof(GpuMeshData))
          .setName("mesh_data");
      mesh.pbrMaterial.materialBuffer = p_Renderer->m_GpuDevice->createBuffer(bufferCreation);

      DescriptorSetCreation dsCreation{};
      DescriptorSetLayoutHandle layout = p_Renderer->m_GpuDevice->getDescriptorSetLayout(
          mainTechnique->passes[3].pipeline, kMaterialDescriptorSetIndex);
      dsCreation.buffer(sceneCb, 0).buffer(mesh.pbrMaterial.materialBuffer, 1).setLayout(layout);
      mesh.pbrMaterial.descriptorSet = p_Renderer->m_GpuDevice->createDescriptorSet(dsCreation);

      mesh.pbrMaterial.material = pbrMaterial;

      meshes.push(mesh);
    }
  }

  qsort(meshes.m_Data, meshes.m_Size, sizeof(Mesh), gltfMeshMaterialCompare);

  p_ScratchAllocator->freeMarker(cachedScratchSize);

  depthPrePass.prepareDraws(
      *this, frameGraph, p_Renderer->m_GpuDevice->m_Allocator, p_ScratchAllocator);
  gbufferPass.prepareDraws(
      *this, frameGraph, p_Renderer->m_GpuDevice->m_Allocator, p_ScratchAllocator);
  lightPass.prepareDraws(
      *this, frameGraph, p_Renderer->m_GpuDevice->m_Allocator, p_ScratchAllocator);
  transparentPass.prepareDraws(
      *this, frameGraph, p_Renderer->m_GpuDevice->m_Allocator, p_ScratchAllocator);
  dofPass.prepareDraws(*this, frameGraph, p_Renderer->m_GpuDevice->m_Allocator, p_ScratchAllocator);

  // Handle fullscreen pass.
  fullscreenTech =
      p_Renderer->m_ResourceCache.m_Techniques.get(Framework::hashCalculate("fullscreen"));

  DescriptorSetCreation dsc;
  DescriptorSetLayoutHandle descriptorSetLayout = p_Renderer->m_GpuDevice->getDescriptorSetLayout(
      fullscreenTech->passes[0].pipeline, kMaterialDescriptorSetIndex);
  dsc.reset().buffer(sceneCb, 0).setLayout(descriptorSetLayout);
  fullscreenDS = p_Renderer->m_GpuDevice->createDescriptorSet(dsc);

  FrameGraphResource* texture = frameGraph->getResource("final");
  if (texture != nullptr)
  {
    fullscreenInputRT = texture->resourceInfo.texture.texture.index;
  }
}
//---------------------------------------------------------------------------//
void glTFScene::uploadMaterials()
{
  // Update per mesh material buffer
  for (uint32_t meshIndex = 0; meshIndex < meshes.m_Size; ++meshIndex)
  {
    Mesh& mesh = meshes[meshIndex];

    MapBufferParameters cbMap = {mesh.pbrMaterial.materialBuffer, 0, 0};
    GpuMeshData* meshData = (GpuMeshData*)renderer->m_GpuDevice->mapBuffer(cbMap);
    if (meshData)
    {
      copyGpuMaterialData(*meshData, mesh);
      copyGpuMeshMatrix(*meshData, mesh, globalScale, sceneGraph);

      renderer->m_GpuDevice->unmapBuffer(cbMap);
    }
  }

  lightPass.uploadMaterials();
  dofPass.uploadMaterials();
}
//---------------------------------------------------------------------------//
void glTFScene::submitDrawTask(ImguiUtil::ImguiService* imgui, enki::TaskScheduler* taskScheduler)
{
  glTFDrawTask drawTask;
  drawTask.init(renderer->m_GpuDevice, frameGraph, renderer, imgui, this);
  taskScheduler->AddTaskSetToPipe(&drawTask);
  taskScheduler->WaitforTaskSet(&drawTask);

  // Avoid using the same command buffer
  renderer->addTextureUpdateCommands((drawTask.threadId + 1) % taskScheduler->GetNumTaskThreads());
}
//---------------------------------------------------------------------------//
void glTFScene::drawMesh(CommandBuffer* gpuCommands, Mesh& mesh)
{
  gpuCommands->bindVertexBuffer(mesh.positionBuffer, 0, mesh.positionOffset);
  gpuCommands->bindVertexBuffer(mesh.tangentBuffer, 1, mesh.tangentOffset);
  gpuCommands->bindVertexBuffer(mesh.normalBuffer, 2, mesh.normalOffset);
  gpuCommands->bindVertexBuffer(mesh.texcoordBuffer, 3, mesh.texcoordOffset);
  gpuCommands->bindIndexBuffer(mesh.indexBuffer, mesh.indexOffset, mesh.indexType);

  if (g_RecreatePerThreadDescriptors)
  {
    DescriptorSetCreation dsCreation{};
    dsCreation.buffer(sceneCb, 0).buffer(mesh.pbrMaterial.materialBuffer, 1);
    DescriptorSetHandle descriptorSet =
        renderer->createDescriptorSet(gpuCommands, mesh.pbrMaterial.material, dsCreation);

    gpuCommands->bindLocalDescriptorSet(&descriptorSet, 1, nullptr, 0);
  }
  else
  {
    gpuCommands->bindDescriptorSet(&mesh.pbrMaterial.descriptorSet, 1, nullptr, 0);
  }

  gpuCommands->drawIndexed(TopologyType::kTriangle, mesh.primitiveCount, 1, 0, 0, 0);
}
//---------------------------------------------------------------------------//
void glTFScene::getMeshVertexBuffer(
    int accessorIndex, BufferHandle& outBufferHandle, uint32_t& outBufferOffset)
{
  if (accessorIndex != -1)
  {
    glTF::Accessor& bufferAccessor = gltfScene.accessors[accessorIndex];
    glTF::BufferView& bufferView = gltfScene.bufferViews[bufferAccessor.bufferView];
    RendererUtil::BufferResource& bufferGpu = buffers[bufferAccessor.bufferView];

    outBufferHandle = bufferGpu.m_Handle;
    outBufferOffset =
        bufferAccessor.byteOffset == glTF::INVALID_INT_VALUE ? 0 : bufferAccessor.byteOffset;
  }
}
//---------------------------------------------------------------------------//
uint16_t glTFScene::getMaterialTexture(GpuDevice& gpu, Framework::glTF::TextureInfo* textureInfo)
{
  if (textureInfo != nullptr)
  {
    glTF::Texture& gltfTexture = gltfScene.textures[textureInfo->index];
    RendererUtil::TextureResource& textureGpu = images[gltfTexture.source];
    RendererUtil::SamplerResource& samplerGpu = samplers[gltfTexture.sampler];

    gpu.linkTextureSampler(textureGpu.m_Handle, samplerGpu.m_Handle);

    return textureGpu.m_Handle.index;
  }
  else
  {
    return kInvalidSceneTextureIndex;
  }
}
//---------------------------------------------------------------------------//
uint16_t glTFScene::getMaterialTexture(GpuDevice& gpu, int gltfTextureIndex)
{
  if (gltfTextureIndex >= 0)
  {
    glTF::Texture& gltfTexture = gltfScene.textures[gltfTextureIndex];
    RendererUtil::TextureResource& textureGpu = images[gltfTexture.source];
    RendererUtil::SamplerResource& samplerGpu = samplers[gltfTexture.sampler];

    gpu.linkTextureSampler(textureGpu.m_Handle, samplerGpu.m_Handle);

    return textureGpu.m_Handle.index;
  }
  else
  {
    return kInvalidSceneTextureIndex;
  }
}
//---------------------------------------------------------------------------//
void glTFScene::fillPbrMaterial(
    RendererUtil::Renderer& p_Renderer,
    Framework::glTF::Material& p_Material,
    PBRMaterial& p_PbrMaterial)
{
  GpuDevice& gpu = *p_Renderer.m_GpuDevice;

  // Handle flags
  if (p_Material.alphaMode.m_Data != nullptr && strcmp(p_Material.alphaMode.m_Data, "MASK") == 0)
  {
    p_PbrMaterial.flags |= kDrawFlagsAlphaMask;
  }
  else if (
      p_Material.alphaMode.m_Data != nullptr && strcmp(p_Material.alphaMode.m_Data, "BLEND") == 0)
  {
    p_PbrMaterial.flags |= kDrawFlagsTransparent;
  }

  p_PbrMaterial.flags |= p_Material.doubleSided ? kDrawFlagsDoubleSided : 0;
  // Alpha cutoff
  p_PbrMaterial.alphaCutoff =
      p_Material.alphaCutoff != glTF::INVALID_FLOAT_VALUE ? p_Material.alphaCutoff : 1.f;

  if (p_Material.pbrMetallicRoughness != nullptr)
  {
    if (p_Material.pbrMetallicRoughness->baseColorFactorCount != 0)
    {
      assert(p_Material.pbrMetallicRoughness->baseColorFactorCount == 4);

      memcpy(
          p_PbrMaterial.baseColorFactor.raw,
          p_Material.pbrMetallicRoughness->baseColorFactor,
          sizeof(vec4s));
    }
    else
    {
      p_PbrMaterial.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    p_PbrMaterial.metallicRoughnessOcclusionFactor.x =
        p_Material.pbrMetallicRoughness->roughnessFactor != glTF::INVALID_FLOAT_VALUE
            ? p_Material.pbrMetallicRoughness->roughnessFactor
            : 1.f;
    p_PbrMaterial.metallicRoughnessOcclusionFactor.y =
        p_Material.pbrMetallicRoughness->metallicFactor != glTF::INVALID_FLOAT_VALUE
            ? p_Material.pbrMetallicRoughness->metallicFactor
            : 1.f;

    p_PbrMaterial.diffuseTextureIndex =
        getMaterialTexture(gpu, p_Material.pbrMetallicRoughness->baseColorTexture);
    p_PbrMaterial.roughnessTextureIndex =
        getMaterialTexture(gpu, p_Material.pbrMetallicRoughness->metallicRoughnessTexture);
  }

  p_PbrMaterial.occlusionTextureIndex = getMaterialTexture(
      gpu, (p_Material.occlusionTexture != nullptr) ? p_Material.occlusionTexture->index : -1);
  p_PbrMaterial.normalTextureIndex = getMaterialTexture(
      gpu, (p_Material.normalTexture != nullptr) ? p_Material.normalTexture->index : -1);

  if (p_Material.occlusionTexture != nullptr)
  {
    if (p_Material.occlusionTexture->strength != glTF::INVALID_FLOAT_VALUE)
    {
      p_PbrMaterial.metallicRoughnessOcclusionFactor.z = p_Material.occlusionTexture->strength;
    }
    else
    {
      p_PbrMaterial.metallicRoughnessOcclusionFactor.z = 1.0f;
    }
  }
}
//---------------------------------------------------------------------------//
// glTF draw task:
//---------------------------------------------------------------------------//
void glTFDrawTask::init(
    GpuDevice* p_Gpu,
    FrameGraph* p_FrameGraph,
    RendererUtil::Renderer* p_Renderer,
    ImguiUtil::ImguiService* p_Imgui,
    glTFScene* p_Scene)
{
  gpu = p_Gpu;
  frameGraph = p_FrameGraph;
  renderer = p_Renderer;
  imgui = p_Imgui;
  scene = p_Scene;
}
//---------------------------------------------------------------------------//
void glTFDrawTask::ExecuteRange(enki::TaskSetPartition p_Range, uint32_t p_ThreadNum)
{
  using namespace Framework;

  threadId = p_ThreadNum;

  // printf( "Executing draw task from thread %u\n", p_ThreadNum );
  // TODO: improve getting a command buffer/pool
  CommandBuffer* gpuCommands = gpu->getCommandBuffer(p_ThreadNum, true);

  frameGraph->render(gpuCommands, scene);

  gpuCommands->clear(0.3f, 0.3f, 0.3f, 1.f);
  gpuCommands->clearDepthStencil(1.0f, 0);
  gpuCommands->bindPass(gpu->m_SwapchainRenderPass, gpu->getCurrentFramebuffer(), false);
  gpuCommands->setScissor(nullptr);
  gpuCommands->setViewport(nullptr);

  // TODO: add global switch
  if (false)
  {
    RendererUtil::Material* lastMaterial = nullptr;
    // TODO: loop by material so that we can deal with multiple passes
    for (uint32_t meshIndex = 0; meshIndex < scene->meshes.m_Size; ++meshIndex)
    {
      Mesh& mesh = scene->meshes[meshIndex];

      if (mesh.pbrMaterial.material != lastMaterial)
      {
        PipelineHandle pipeline = renderer->getPipeline(mesh.pbrMaterial.material, 3);

        gpuCommands->bindPipeline(pipeline);

        lastMaterial = mesh.pbrMaterial.material;
      }

      scene->drawMesh(gpuCommands, mesh);
    }
  }
  else
  {
    // Apply fullscreen material
    gpuCommands->bindPipeline(scene->fullscreenTech->passes[0].pipeline);
    gpuCommands->bindDescriptorSet(&scene->fullscreenDS, 1, nullptr, 0);
    gpuCommands->draw(TopologyType::kTriangle, 0, 3, scene->fullscreenInputRT, 1);
  }

  imgui->render(*gpuCommands, false);

  // Send commands to GPU
  gpu->queueCommandBuffer(gpuCommands);
}
//---------------------------------------------------------------------------//
} // namespace Graphics
