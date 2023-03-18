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

Graphics::BufferHandle sceneCb;

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

  uint32_t indexOffset;
  uint32_t positionOffset;
  uint32_t tangentOffset;
  uint32_t normalOffset;
  uint32_t texcoordOffset;

  uint32_t primitive_count;

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
static void drawMesh(
    Graphics::RendererUtil::Renderer& p_Renderer,
    Graphics::CommandBuffer* p_CommandBuffers,
    MeshDraw& p_MeshDraw)
{
  // Descriptor Set
  Graphics::DescriptorSetCreation dsCreation{};
  dsCreation.buffer(sceneCb, 0).buffer(p_MeshDraw.materialBuffer, 1);
  Graphics::DescriptorSetHandle descriptorSet =
      p_Renderer.createDescriptorSet(p_CommandBuffers, p_MeshDraw.material, dsCreation);

  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.positionBuffer, 0, p_MeshDraw.positionOffset);
  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.tangentBuffer, 1, p_MeshDraw.tangentOffset);
  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.normalBuffer, 2, p_MeshDraw.normalOffset);
  p_CommandBuffers->bindVertexBuffer(p_MeshDraw.texcoordBuffer, 3, p_MeshDraw.texcoordOffset);
  p_CommandBuffers->bindIndexBuffer(p_MeshDraw.indexBuffer, p_MeshDraw.indexOffset);
  p_CommandBuffers->bindLocalDescriptorSet(&descriptorSet, 1, nullptr, 0);

  p_CommandBuffers->drawIndexed(
      Graphics::TopologyType::kTriangle, p_MeshDraw.primitive_count, 1, 0, 0, 0);
}
//---------------------------------------------------------------------------//
struct Scene
{
  Framework::Array<MeshDraw> meshDraws;

  // All graphics resources used by the scene
  Framework::Array<Graphics::RendererUtil::TextureResource> images;
  Framework::Array<Graphics::RendererUtil::SamplerResource> samplers;
  Framework::Array<Graphics::RendererUtil::BufferResource> buffers;

  Framework::glTF::glTF gltfScene; // Source gltf scene

}; // struct GltfScene

static void sceneLoadFromGltf(
    const char* filename,
    Graphics::RendererUtil::Renderer& renderer,
    Framework::Allocator* allocator,
    Scene& scene)
{

  using namespace Graphics::RendererUtil;

  scene.gltfScene = Framework::gltfLoadFile(filename);

  // Load all textures
  scene.images.init(allocator, scene.gltfScene.imagesCount);

  for (uint32_t i = 0; i < scene.gltfScene.imagesCount; ++i)
  {
    Framework::glTF::Image& image = scene.gltfScene.images[i];
    TextureResource* tr = renderer.createTexture(image.uri.m_Data, image.uri.m_Data);
    assert(tr != nullptr);

    scene.images.push(*tr);
  }

  Framework::StringBuffer resourceNameBuffer;
  resourceNameBuffer.init(4096, allocator);

  // Load all samplers
  scene.samplers.init(allocator, scene.gltfScene.samplersCount);

  for (uint32_t i = 0; i < scene.gltfScene.samplersCount; ++i)
  {
    Framework::glTF::Sampler& sampler = scene.gltfScene.samplers[i];

    char* samplerName = resourceNameBuffer.appendUseFormatted("sampler_%u", i);

    Graphics::SamplerCreation creation;
    switch (sampler.minFilter)
    {
    case Framework::glTF::Sampler::NEAREST:
      creation.minFilter = VK_FILTER_NEAREST;
      break;
    case Framework::glTF::Sampler::LINEAR:
      creation.minFilter = VK_FILTER_LINEAR;
      break;
    case Framework::glTF::Sampler::LINEAR_MIPMAP_NEAREST:
      creation.minFilter = VK_FILTER_LINEAR;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case Framework::glTF::Sampler::LINEAR_MIPMAP_LINEAR:
      creation.minFilter = VK_FILTER_LINEAR;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    case Framework::glTF::Sampler::NEAREST_MIPMAP_NEAREST:
      creation.minFilter = VK_FILTER_NEAREST;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case Framework::glTF::Sampler::NEAREST_MIPMAP_LINEAR:
      creation.minFilter = VK_FILTER_NEAREST;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    }

    creation.magFilter = sampler.magFilter == Framework::glTF::Sampler::Filter::LINEAR
                             ? VK_FILTER_LINEAR
                             : VK_FILTER_NEAREST;

    switch (sampler.wrapS)
    {
    case Framework::glTF::Sampler::CLAMP_TO_EDGE:
      creation.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      break;
    case Framework::glTF::Sampler::MIRRORED_REPEAT:
      creation.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      break;
    case Framework::glTF::Sampler::REPEAT:
      creation.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      break;
    }

    switch (sampler.wrapT)
    {
    case Framework::glTF::Sampler::CLAMP_TO_EDGE:
      creation.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      break;
    case Framework::glTF::Sampler::MIRRORED_REPEAT:
      creation.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      break;
    case Framework::glTF::Sampler::REPEAT:
      creation.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      break;
    }

    creation.name = samplerName;

    SamplerResource* sr = renderer.createSampler(creation);
    assert(sr != nullptr);

    scene.samplers.push(*sr);
  }

  // Temporary array of buffer data
  Framework::Array<void*> buffersData;
  buffersData.init(allocator, scene.gltfScene.buffersCount);

  for (uint32_t i = 0; i < scene.gltfScene.buffersCount; ++i)
  {
    Framework::glTF::Buffer& buffer = scene.gltfScene.buffers[i];

    Framework::FileReadResult bufferData = Framework::fileReadBinary(buffer.uri.m_Data, allocator);
    buffersData.push(bufferData.data);
  }

  // Load all buffers and initialize them with buffer data
  scene.buffers.init(allocator, scene.gltfScene.bufferViewsCount);

  for (uint32_t i = 0; i < scene.gltfScene.bufferViewsCount; ++i)
  {
    Framework::glTF::BufferView& buffer = scene.gltfScene.bufferViews[i];

    int offset = buffer.byteOffset;
    if (offset == Framework::glTF::INVALID_INT_VALUE)
    {
      offset = 0;
    }

    uint8_t* data = (uint8_t*)buffersData[buffer.buffer] + offset;

    // NOTE: the target attribute of a BufferView is not mandatory, so we prepare for both
    // uses
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    char* bufferName = buffer.name.m_Data;
    if (bufferName == nullptr)
    {
      bufferName = resourceNameBuffer.appendUseFormatted("buffer_%u", i);
    }

    BufferResource* br = renderer.createBuffer(
        flags, Graphics::ResourceUsageType::kImmutable, buffer.byteLength, data, bufferName);
    assert(br != nullptr);

    scene.buffers.push(*br);
  }

  for (uint32_t buffer_index = 0; buffer_index < scene.gltfScene.buffersCount; ++buffer_index)
  {
    void* buffer = buffersData[buffer_index];
    allocator->deallocate(buffer);
  }
  buffersData.shutdown();

  resourceNameBuffer.shutdown();

  // Init runtime meshes
  scene.meshDraws.init(allocator, scene.gltfScene.meshesCount);
}

static void sceneFreeGpuResources(Scene& scene, Graphics::RendererUtil::Renderer& renderer)
{
  Graphics::GpuDevice& gpu = *renderer.m_GpuDevice;

  for (uint32_t i = 0; i < scene.meshDraws.m_Size; ++i)
  {
    MeshDraw& meshDraw = scene.meshDraws[i];
    gpu.destroyBuffer(meshDraw.materialBuffer);
  }

  scene.meshDraws.shutdown();
}

static void sceneUnload(Scene& scene, Graphics::RendererUtil::Renderer& renderer)
{

  Graphics::GpuDevice& gpu = *renderer.m_GpuDevice;

  // Free scene buffers
  scene.samplers.shutdown();
  scene.images.shutdown();
  scene.buffers.shutdown();

  // NOTE: we can't destroy this sooner as textures and buffers
  // hold a pointer to the names stored here
  Framework::gltfFree(scene.gltfScene);
}

static int meshMaterialCompare(const void* a, const void* b)
{
  const MeshDraw* meshA = (const MeshDraw*)a;
  const MeshDraw* meshB = (const MeshDraw*)b;

  if (meshA->material->m_RenderIndex < meshB->material->m_RenderIndex)
    return -1;
  if (meshA->material->m_RenderIndex > meshB->material->m_RenderIndex)
    return 1;
  return 0;
}

static void getMeshVertexBuffer(
    Scene& scene,
    int accessorIndex,
    Graphics::BufferHandle& outBufferHandle,
    uint32_t& outBufferOffset)
{
  using namespace Framework;

  if (accessorIndex != -1)
  {
    glTF::Accessor& bufferAccessor = scene.gltfScene.accessors[accessorIndex];
    glTF::BufferView& bufferView = scene.gltfScene.bufferViews[bufferAccessor.bufferView];
    Graphics::RendererUtil::BufferResource& bufferGpu = scene.buffers[bufferAccessor.bufferView];

    outBufferHandle = bufferGpu.m_Handle;
    outBufferOffset =
        bufferAccessor.byteOffset == glTF::INVALID_INT_VALUE ? 0 : bufferAccessor.byteOffset;
  }
}

static bool getMeshMaterial(
    Graphics::RendererUtil::Renderer& renderer,
    Scene& scene,
    Framework::glTF::Material& material,
    MeshDraw& meshDraw)
{
  using namespace Graphics::RendererUtil;

  bool transparent = false;
  Graphics::GpuDevice& gpu = *renderer.m_GpuDevice;

  if (material.pbrMetallicRoughness != nullptr)
  {
    if (material.pbrMetallicRoughness->baseColorFactorCount != 0)
    {
      assert(material.pbrMetallicRoughness->baseColorFactorCount == 4);

      meshDraw.baseColorFactor = {
          material.pbrMetallicRoughness->baseColorFactor[0],
          material.pbrMetallicRoughness->baseColorFactor[1],
          material.pbrMetallicRoughness->baseColorFactor[2],
          material.pbrMetallicRoughness->baseColorFactor[3],
      };
    }
    else
    {
      meshDraw.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    if (material.pbrMetallicRoughness->roughnessFactor != Framework::glTF::INVALID_FLOAT_VALUE)
    {
      meshDraw.metallicRoughnessOcclusionFactor.x = material.pbrMetallicRoughness->roughnessFactor;
    }
    else
    {
      meshDraw.metallicRoughnessOcclusionFactor.x = 1.0f;
    }

    if (material.alphaMode.m_Data != nullptr && strcmp(material.alphaMode.m_Data, "MASK") == 0)
    {
      meshDraw.flags |= kDrawFlagsAlphaMask;
      transparent = true;
    }

    if (material.alphaCutoff != Framework::glTF::INVALID_FLOAT_VALUE)
    {
      meshDraw.alphaCutoff = material.alphaCutoff;
    }

    if (material.pbrMetallicRoughness->metallicFactor != Framework::glTF::INVALID_FLOAT_VALUE)
    {
      meshDraw.metallicRoughnessOcclusionFactor.y = material.pbrMetallicRoughness->metallicFactor;
    }
    else
    {
      meshDraw.metallicRoughnessOcclusionFactor.y = 1.0f;
    }

    if (material.pbrMetallicRoughness->baseColorTexture != nullptr)
    {
      Framework::glTF::Texture& diffuseTexture =
          scene.gltfScene.textures[material.pbrMetallicRoughness->baseColorTexture->index];
      TextureResource& diffuseTextureGpu = scene.images[diffuseTexture.source];
      SamplerResource& diffuseSamplerGpu = scene.samplers[diffuseTexture.sampler];

      meshDraw.diffuseTextureIndex = diffuseTextureGpu.m_Handle.index;

      gpu.linkTextureSampler(diffuseTextureGpu.m_Handle, diffuseSamplerGpu.m_Handle);
    }
    else
    {
      meshDraw.diffuseTextureIndex = INVALID_TEXTURE_INDEX;
    }

    if (material.pbrMetallicRoughness->metallicRoughnessTexture != nullptr)
    {
      Framework::glTF::Texture& roughnessTexture =
          scene.gltfScene.textures[material.pbrMetallicRoughness->metallicRoughnessTexture->index];
      TextureResource& roughnessTextureGpu = scene.images[roughnessTexture.source];
      SamplerResource& roughnessSamplerGpu = scene.samplers[roughnessTexture.sampler];

      meshDraw.roughnessTextureIndex = roughnessTextureGpu.m_Handle.index;

      gpu.linkTextureSampler(roughnessTextureGpu.m_Handle, roughnessSamplerGpu.m_Handle);
    }
    else
    {
      meshDraw.roughnessTextureIndex = INVALID_TEXTURE_INDEX;
    }
  }

  if (material.occlusionTexture != nullptr)
  {
    Framework::glTF::Texture& occlusionTexture =
        scene.gltfScene.textures[material.occlusionTexture->index];

    TextureResource& occlusionTextureGpu = scene.images[occlusionTexture.source];
    SamplerResource& occlusionSamplerGpu = scene.samplers[occlusionTexture.sampler];

    meshDraw.occlusionTextureIndex = occlusionTextureGpu.m_Handle.index;

    if (material.occlusionTexture->strength != Framework::glTF::INVALID_FLOAT_VALUE)
    {
      meshDraw.metallicRoughnessOcclusionFactor.z = material.occlusionTexture->strength;
    }
    else
    {
      meshDraw.metallicRoughnessOcclusionFactor.z = 1.0f;
    }

    gpu.linkTextureSampler(occlusionTextureGpu.m_Handle, occlusionSamplerGpu.m_Handle);
  }
  else
  {
    meshDraw.occlusionTextureIndex = INVALID_TEXTURE_INDEX;
  }

  if (material.normalTexture != nullptr)
  {
    Framework::glTF::Texture& normalTexture =
        scene.gltfScene.textures[material.normalTexture->index];
    TextureResource& normalTextureGpu = scene.images[normalTexture.source];
    SamplerResource& normalSamplerGpu = scene.samplers[normalTexture.sampler];

    gpu.linkTextureSampler(normalTextureGpu.m_Handle, normalSamplerGpu.m_Handle);

    meshDraw.normalTextureIndex = normalTextureGpu.m_Handle.index;
  }
  else
  {
    meshDraw.normalTextureIndex = INVALID_TEXTURE_INDEX;
  }

  // Create material buffer
  Graphics::BufferCreation bufferCreation;
  bufferCreation.reset()
      .set(
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          Graphics::ResourceUsageType::kDynamic,
          sizeof(MeshData))
      .setName("Mesh Data");
  meshDraw.materialBuffer = gpu.createBuffer(bufferCreation);

  return transparent;
}
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
  WindowConfiguration wconf{1280, 800, "Demo 02", &MemoryService::instance()->m_SystemAllocator};
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

  RendererUtil::Renderer renderer;
  renderer.init({&gpu, allocator});
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
  filenameFromPath(gltfFile);

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
        {0, 0, 0, VertexComponentFormat::kFloat3}); // position
    pipelineCreation.vertexInput.addVertexStream({0, 12, VertexInputRate::kPerVertex});

    pipelineCreation.vertexInput.addVertexAttribute(
        {1, 1, 0, VertexComponentFormat::kFloat4}); // tangent
    pipelineCreation.vertexInput.addVertexStream({1, 16, VertexInputRate::kPerVertex});

    pipelineCreation.vertexInput.addVertexAttribute(
        {2, 2, 0, VertexComponentFormat::kFloat3}); // normal
    pipelineCreation.vertexInput.addVertexStream({2, 12, VertexInputRate::kPerVertex});

    pipelineCreation.vertexInput.addVertexAttribute(
        {3, 3, 0, VertexComponentFormat::kFloat2}); // texcoord
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
        meshDraw.primitive_count = indicesAccessor.count;

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

        scene.meshDraws.push(meshDraw);
      }
    }
  }

  qsort(scene.meshDraws.m_Data, scene.meshDraws.m_Size, sizeof(MeshDraw), meshMaterialCompare);

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
      for (uint32_t meshIndex = 0; meshIndex < scene.meshDraws.m_Size; ++meshIndex)
      {
        MeshDraw& meshDraw = scene.meshDraws[meshIndex];

        cbMap.buffer = meshDraw.materialBuffer;
        MeshData* mesh_data = (MeshData*)gpu.mapBuffer(cbMap);
        if (mesh_data)
        {
          uploadMaterial(*mesh_data, meshDraw, modelScale);

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
      for (uint32_t meshIndex = 0; meshIndex < scene.meshDraws.m_Size; ++meshIndex)
      {
        MeshDraw& meshDraw = scene.meshDraws[meshIndex];

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
