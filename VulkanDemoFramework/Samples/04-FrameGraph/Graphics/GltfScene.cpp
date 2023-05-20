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
    const mat4s scale_matrix = glms_scale_make({p_GlobalScale, p_GlobalScale, -p_GlobalScale});
    p_GpuMeshData.world =
        glms_mat4_mul(scale_matrix, p_SceneGraph->worldMatrices[p_Mesh.sceneGraphNodeIndex]);

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
    Allocator* residentAllocator,
    StackAllocator* scratchAllocator)
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

  FrameGraphResource* depthTexture = frameGraph->getResource(depthTextureReference->name);
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
  size_t tempAllocatorInitialMarker = tempAllocator->get_marker();

  // Time statistics
  i64 start_scene_loading = time_now();

  gltf_scene = gltf_load_file(filename);

  i64 end_loading_file = time_now();

  // Load all textures
  images.init(resident_allocator, gltf_scene.images_count);

  Array<TextureCreation> tcs;
  tcs.init(tempAllocator, gltf_scene.images_count, gltf_scene.images_count);

  StringBuffer name_buffer;
  name_buffer.init(4096, tempAllocator);

  for (u32 image_index = 0; image_index < gltf_scene.images_count; ++image_index)
  {
    glTF::Image& image = gltf_scene.images[image_index];

    int comp, width, height;

    stbi_info(image.uri.data, &width, &height, &comp);

    u32 mip_levels = 1;
    if (true)
    {
      u32 w = width;
      u32 h = height;

      while (w > 1 && h > 1)
      {
        w /= 2;
        h /= 2;

        ++mip_levels;
      }
    }

    TextureCreation tc;
    tc.set_data(nullptr)
        .set_format_type(VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D)
        .set_flags(mip_levels, 0)
        .set_size((u16)width, (u16)height, 1)
        .set_name(image.uri.data);
    TextureResource* tr = renderer->create_texture(tc);
    RASSERT(tr != nullptr);

    images.push(*tr);

    // Reconstruct file path
    char* full_filename = name_buffer.append_use_f("%s%s", path, image.uri.data);
    asyncLoader->request_texture_data(full_filename, tr->handle);
    // Reset name buffer
    name_buffer.clear();
  }

  i64 end_loading_textures_files = time_now();

  i64 end_creating_textures = time_now();

  // Load all samplers
  samplers.init(resident_allocator, gltf_scene.samplers_count);

  for (u32 sampler_index = 0; sampler_index < gltf_scene.samplers_count; ++sampler_index)
  {
    glTF::Sampler& sampler = gltf_scene.samplers[sampler_index];

    char* sampler_name = name_buffer.append_use_f("sampler_%u", sampler_index);

    SamplerCreation creation;
    switch (sampler.min_filter)
    {
    case glTF::Sampler::NEAREST:
      creation.min_filter = VK_FILTER_NEAREST;
      break;
    case glTF::Sampler::LINEAR:
      creation.min_filter = VK_FILTER_LINEAR;
      break;
    case glTF::Sampler::LINEAR_MIPMAP_NEAREST:
      creation.min_filter = VK_FILTER_LINEAR;
      creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case glTF::Sampler::LINEAR_MIPMAP_LINEAR:
      creation.min_filter = VK_FILTER_LINEAR;
      creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    case glTF::Sampler::NEAREST_MIPMAP_NEAREST:
      creation.min_filter = VK_FILTER_NEAREST;
      creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case glTF::Sampler::NEAREST_MIPMAP_LINEAR:
      creation.min_filter = VK_FILTER_NEAREST;
      creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    }

    creation.mag_filter =
        sampler.mag_filter == glTF::Sampler::Filter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

    switch (sampler.wrap_s)
    {
    case glTF::Sampler::CLAMP_TO_EDGE:
      creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      break;
    case glTF::Sampler::MIRRORED_REPEAT:
      creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      break;
    case glTF::Sampler::REPEAT:
      creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      break;
    }

    switch (sampler.wrap_t)
    {
    case glTF::Sampler::CLAMP_TO_EDGE:
      creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      break;
    case glTF::Sampler::MIRRORED_REPEAT:
      creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      break;
    case glTF::Sampler::REPEAT:
      creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      break;
    }

    creation.name = sampler_name;

    SamplerResource* sr = renderer->create_sampler(creation);
    RASSERT(sr != nullptr);

    samplers.push(*sr);
  }

  i64 end_creating_samplers = time_now();

  // Temporary array of buffer data
  Array<void*> buffers_data;
  buffers_data.init(resident_allocator, gltf_scene.buffers_count);

  for (u32 buffer_index = 0; buffer_index < gltf_scene.buffers_count; ++buffer_index)
  {
    glTF::Buffer& buffer = gltf_scene.buffers[buffer_index];

    FileReadResult buffer_data = file_read_binary(buffer.uri.data, resident_allocator);
    buffers_data.push(buffer_data.data);
  }

  i64 end_reading_buffers_data = time_now();

  // Load all buffers and initialize them with buffer data
  buffers.init(resident_allocator, gltf_scene.buffer_views_count);

  for (u32 buffer_index = 0; buffer_index < gltf_scene.buffer_views_count; ++buffer_index)
  {
    glTF::BufferView& buffer = gltf_scene.buffer_views[buffer_index];

    i32 offset = buffer.byte_offset;
    if (offset == glTF::INVALID_INT_VALUE)
    {
      offset = 0;
    }

    u8* buffer_data = (u8*)buffers_data[buffer.buffer] + offset;

    // NOTE(marco): the target attribute of a BufferView is not mandatory, so we prepare for both
    // uses
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    char* buffer_name = buffer.name.data;
    if (buffer_name == nullptr)
    {
      buffer_name = name_buffer.append_use_f("buffer_%u", buffer_index);
    }

    BufferResource* br = renderer->create_buffer(
        flags, ResourceUsageType::Immutable, buffer.byte_length, buffer_data, buffer_name);
    RASSERT(br != nullptr);

    buffers.push(*br);
  }

  for (u32 buffer_index = 0; buffer_index < gltf_scene.buffers_count; ++buffer_index)
  {
    void* buffer = buffers_data[buffer_index];
    resident_allocator->deallocate(buffer);
  }
  buffers_data.shutdown();

  i64 end_creating_buffers = time_now();

  // This is not needed anymore, free all temp memory after.
  // resource_name_buffer.shutdown();
  tempAllocator->free_marker(tempAllocatorInitialMarker);

  // Init runtime meshes
  meshes.init(resident_allocator, gltf_scene.meshes_count);

  i64 end_loading = time_now();

  rprint(
      "Loaded scene %s in %f seconds.\nStats:\n\tReading GLTF file %f seconds\n\tTextures Creating "
      "%f seconds\n\tCreating Samplers %f seconds\n\tReading Buffers Data %f seconds\n\tCreating "
      "Buffers %f seconds\n",
      filename,
      time_delta_seconds(start_scene_loading, end_loading),
      time_delta_seconds(start_scene_loading, end_loading_file),
      time_delta_seconds(end_loading_file, end_creating_textures),
      time_delta_seconds(end_creating_textures, end_creating_samplers),
      time_delta_seconds(end_creating_samplers, end_reading_buffers_data),
      time_delta_seconds(end_reading_buffers_data, end_creating_buffers));
}
//---------------------------------------------------------------------------//
void glTFScene::shutdown(Renderer* renderer);
//---------------------------------------------------------------------------//
void glTFScene::registerRenderPasses(FrameGraph* frameGraph);
//---------------------------------------------------------------------------//
void glTFScene::prepareDraws(
    Renderer* renderer, StackAllocator* scratchAllocator, SceneGraph* sceneGraph);
//---------------------------------------------------------------------------//
void glTFScene::uploadMaterials();
//---------------------------------------------------------------------------//
void glTFScene::submitDrawTask(ImGuiService* imgui, enki::TaskScheduler* taskScheduler);
//---------------------------------------------------------------------------//
void glTFScene::drawMesh(CommandBuffer* gpuCommands, Mesh& mesh);
//---------------------------------------------------------------------------//
void glTFScene::getMeshVertexBuffer(
    int accessorIndex, BufferHandle& outBufferHandle, uint32_t& outBufferOffset);
//---------------------------------------------------------------------------//
uint16_t getMaterialTexture(GpuDevice& gpu, Framework::glTF::TextureInfo* textureInfo);
//---------------------------------------------------------------------------//
uint16_t getMaterialTexture(GpuDevice& gpu, int gltfTextureIndex);
//---------------------------------------------------------------------------//
void glTFScene::fillPbrMaterial(
    Renderer& renderer, Framework::glTF::Material& material, PBRMaterial& pbrMaterial);
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
