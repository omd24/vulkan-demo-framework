#include <Foundation/File.hpp>
#include <Foundation/Gltf.hpp>
#include <Foundation/Numerics.hpp>
#include <Foundation/ResourceManager.hpp>
#include <Foundation/Time.hpp>
#include <Foundation/String.hpp>

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
#include "Graphics/Renderer.hpp"
#include "Graphics/ImguiHelper.hpp"

//---------------------------------------------------------------------------//
// Demo specific utils:
//---------------------------------------------------------------------------//
// Demo objects
Graphics::PipelineHandle demoPso;
Graphics::BufferHandle demoCb;
Graphics::DescriptorSetHandle demoDs;
Graphics::DescriptorSetLayoutHandle demoDsl;

float rx, ry;
struct MaterialData
{
  vec4s baseColorFactor;
};

struct MeshDraw
{
  Graphics::BufferHandle indexBuffer;
  Graphics::BufferHandle positionBuffer;
  Graphics::BufferHandle tangentBuffer;
  Graphics::BufferHandle normalBuffer;
  Graphics::BufferHandle texcoordBuffer;

  Graphics::BufferHandle materialBuffer;
  MaterialData materialData;

  uint32_t indexOffset;
  uint32_t positionOffset;
  uint32_t tangentOffset;
  uint32_t normalOffset;
  uint32_t texcoordOffset;

  uint32_t count;

  Graphics::DescriptorSetHandle descriptorSet;
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
void _loadGltfScene(
    Framework::StringBuffer& modelPath,
    Framework::Allocator* allocator,
    Framework::glTF::glTF& scene,
    Framework::Array<Graphics::RendererUtil::TextureResource>& images,
    Graphics::RendererUtil::Renderer& renderer,
    Framework::Array<Graphics::RendererUtil::SamplerResource>& samplers,
    Framework::Array<Graphics::RendererUtil::BufferResource>& buffers,
    Graphics::GpuDevice& gpuDevice,
    Framework::Array<MeshDraw>& meshDraws)
{
  // Store currect working dir to restore later
  Framework::Directory cwd = gpuDevice.m_Cwd;

  // Change directory:
  char gltfBasePath[512] = {};
  ::memcpy(gltfBasePath, modelPath.m_Data, modelPath.m_CurrentSize);
  Framework::fileDirectoryFromPath(gltfBasePath);
  Framework::directoryChange(gltfBasePath);

  // Determine filename:
  char gltfFile[512] = {};
  ::memcpy(gltfFile, modelPath.m_Data, modelPath.m_CurrentSize);
  Framework::filenameFromPath(gltfFile);

  // Load scene:
  scene = Framework::gltfLoadFile(gltfFile);

  // Create textures:
  images.init(allocator, scene.imagesCount);
  for (uint32_t imageIndex = 0; imageIndex < scene.imagesCount; ++imageIndex)
  {
    Framework::glTF::Image& image = scene.images[imageIndex];
    Graphics::RendererUtil::TextureResource* tr =
        renderer.createTexture(image.uri.m_Data, image.uri.m_Data);
    assert(tr != nullptr);
    images.push(*tr);
  }

  // Create samplers
  Framework::StringBuffer resourceNameBuffer;
  resourceNameBuffer.init(4096, allocator);

  samplers.init(allocator, scene.samplersCount);
  for (uint32_t samplerIndex = 0; samplerIndex < scene.samplersCount; ++samplerIndex)
  {
    Framework::glTF::Sampler& sampler = scene.samplers[samplerIndex];

    char* samplerName = resourceNameBuffer.appendUseFormatted("sampler %u", samplerIndex);

    Graphics::SamplerCreation creation;
    creation.minFilter = sampler.minFilter == Framework::glTF::Sampler::Filter::LINEAR
                             ? VK_FILTER_LINEAR
                             : VK_FILTER_NEAREST;
    creation.magFilter = sampler.magFilter == Framework::glTF::Sampler::Filter::LINEAR
                             ? VK_FILTER_LINEAR
                             : VK_FILTER_NEAREST;
    creation.name = samplerName;

    Graphics::RendererUtil::SamplerResource* sr = renderer.createSampler(creation);
    assert(sr != nullptr);
    samplers.push(*sr);
  }

  // Create buffers:
  Framework::Array<void*> buffersData;
  buffersData.init(allocator, scene.buffersCount);
  for (uint32_t bufferIndex = 0; bufferIndex < scene.buffersCount; ++bufferIndex)
  {
    Framework::glTF::Buffer& buffer = scene.buffers[bufferIndex];

    Framework::FileReadResult bufferData = Framework::fileReadBinary(buffer.uri.m_Data, allocator);
    buffersData.push(bufferData.data);
  }

  buffers.init(allocator, scene.bufferViewsCount);
  for (uint32_t bufferIndex = 0; bufferIndex < scene.bufferViewsCount; ++bufferIndex)
  {
    Framework::glTF::BufferView& buffer = scene.bufferViews[bufferIndex];

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
      bufferName = resourceNameBuffer.appendUseFormatted("buffer %u", bufferIndex);
    }

    Graphics::RendererUtil::BufferResource* br = renderer.createBuffer(
        flags, Graphics::ResourceUsageType::kImmutable, buffer.byteLength, data, bufferName);
    assert(br != nullptr);

    buffers.push(*br);
  }

  for (uint32_t bufferIndex = 0; bufferIndex < scene.buffersCount; ++bufferIndex)
  {
    void* buffer = buffersData[bufferIndex];
    allocator->deallocate(buffer);
  }
  buffersData.shutdown();
  resourceNameBuffer.shutdown();

  // Create pipeline state
  {
    Graphics::PipelineCreation pipelineCreation;

    // Vertex input
    // TODO: component format should be based on buffer view type
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
    pipelineCreation.renderPass = gpuDevice.m_SwapchainOutput;
    // Depth
    pipelineCreation.depthStencil.setDepth(true, VK_COMPARE_OP_LESS_OR_EQUAL);

    // Shader state
    {
      Framework::StringBuffer shaderPath;
      shaderPath.init(MAX_PATH, allocator);

      // Reading vertex shader:
      shaderPath.append(cwd.path);
      shaderPath.append(R"FOO(\Shaders\default.vert.glsl)FOO");
      const char* vsCode =
          Framework::fileReadText(shaderPath.m_Data, gpuDevice.m_TemporaryAllocator, nullptr);
      assert(vsCode != nullptr && "Error reading vertex shader");

      // Reset string buffer
      shaderPath.clear();

      // Reading fragment shader
      shaderPath.append(cwd.path);
      shaderPath.append(R"FOO(\Shaders\simple_pbr.frag.glsl)FOO");
      const char* fsCode =
          Framework::fileReadText(shaderPath.m_Data, gpuDevice.m_TemporaryAllocator, nullptr);
      assert(fsCode != nullptr && "Error reading fragment shader");

      // Release string buffer
      shaderPath.shutdown();

      pipelineCreation.shaders.setName("Demo")
          .addStage(vsCode, (uint32_t)strlen(vsCode), VK_SHADER_STAGE_VERTEX_BIT)
          .addStage(fsCode, (uint32_t)strlen(fsCode), VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    // Descriptor set layout
    Graphics::DescriptorSetLayoutCreation demoDslCreation{};
    demoDslCreation.addBinding({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 1, "LocalConstants"});
    demoDslCreation.addBinding({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, 1, "diffuseTexture"});
    demoDslCreation.addBinding(
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, 1, "occlusionRoughnessMetalnessTexture"});
    demoDslCreation.addBinding({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, 1, "normalTexture"});
    demoDslCreation.addBinding({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, 1, "MaterialConstant"});
    // Setting it into pipeline
    demoDsl = gpuDevice.createDescriptorSetLayout(demoDslCreation);
    pipelineCreation.addDescriptorSetLayout(demoDsl);

    demoPso = gpuDevice.createPipeline(pipelineCreation);
  } // end Create pipeline

  // Restore directory:
  Framework::directoryChange(cwd.path);

  // Create drawable objects (mesh draws)
  meshDraws.init(allocator, scene.meshesCount);
  {
    // Constant buffer
    Graphics::BufferCreation cbCreation;
    cbCreation.reset()
        .set(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            Graphics::ResourceUsageType::kDynamic,
            sizeof(UniformData))
        .setName("demoCb");
    demoCb = gpuDevice.createBuffer(cbCreation);

    for (uint32_t meshIndex = 0; meshIndex < scene.meshesCount; ++meshIndex)
    {
      MeshDraw meshDraw{};

      Framework::glTF::Mesh& mesh = scene.meshes[meshIndex];

      for (uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitivesCount; ++primitiveIndex)
      {
        Framework::glTF::MeshPrimitive& meshPrimitive = mesh.primitives[primitiveIndex];

        int positionAccessorIndex = Framework::gltfGetAttributeAccessorIndex(
            meshPrimitive.attributes, meshPrimitive.attributeCount, "POSITION");
        int tangentAccessorIndex = Framework::gltfGetAttributeAccessorIndex(
            meshPrimitive.attributes, meshPrimitive.attributeCount, "TANGENT");
        int normalAccessorIndex = Framework::gltfGetAttributeAccessorIndex(
            meshPrimitive.attributes, meshPrimitive.attributeCount, "NORMAL");
        int texcoordAccessorIndex = Framework::gltfGetAttributeAccessorIndex(
            meshPrimitive.attributes, meshPrimitive.attributeCount, "TEXCOORD_0");

        if (positionAccessorIndex != -1)
        {
          Framework::glTF::Accessor& positionAccessor = scene.accessors[positionAccessorIndex];
          Framework::glTF::BufferView& positionBufferView =
              scene.bufferViews[positionAccessor.bufferView];
          Graphics::RendererUtil::BufferResource& positionBufferGpu =
              buffers[positionAccessor.bufferView];

          meshDraw.positionBuffer = positionBufferGpu.m_Handle;
          meshDraw.positionOffset =
              positionAccessor.byteOffset == Framework::glTF::INVALID_INT_VALUE
                  ? 0
                  : positionAccessor.byteOffset;
        }

        if (tangentAccessorIndex != -1)
        {
          Framework::glTF::Accessor& tangentAccessor = scene.accessors[tangentAccessorIndex];
          Framework::glTF::BufferView& tangentBufferView =
              scene.bufferViews[tangentAccessor.bufferView];
          Graphics::RendererUtil::BufferResource& tangentBufferGpu =
              buffers[tangentAccessor.bufferView];

          meshDraw.tangentBuffer = tangentBufferGpu.m_Handle;
          meshDraw.tangentOffset = tangentAccessor.byteOffset == Framework::glTF::INVALID_INT_VALUE
                                       ? 0
                                       : tangentAccessor.byteOffset;
        }

        if (normalAccessorIndex != -1)
        {
          Framework::glTF::Accessor& normalAccessor = scene.accessors[normalAccessorIndex];
          Framework::glTF::BufferView& normalBufferView =
              scene.bufferViews[normalAccessor.bufferView];
          Graphics::RendererUtil::BufferResource& normalBufferGpu =
              buffers[normalAccessor.bufferView];

          meshDraw.normalBuffer = normalBufferGpu.m_Handle;
          meshDraw.normalOffset = normalAccessor.byteOffset == Framework::glTF::INVALID_INT_VALUE
                                      ? 0
                                      : normalAccessor.byteOffset;
        }

        if (texcoordAccessorIndex != -1)
        {
          Framework::glTF::Accessor& texcoordAccessor = scene.accessors[texcoordAccessorIndex];
          Framework::glTF::BufferView& texcoordBufferView =
              scene.bufferViews[texcoordAccessor.bufferView];
          Graphics::RendererUtil::BufferResource& texcoordBufferGpu =
              buffers[texcoordAccessor.bufferView];

          meshDraw.texcoordBuffer = texcoordBufferGpu.m_Handle;
          meshDraw.texcoordOffset =
              texcoordAccessor.byteOffset == Framework::glTF::INVALID_INT_VALUE
                  ? 0
                  : texcoordAccessor.byteOffset;
        }

        Framework::glTF::Accessor& indicesAccessor = scene.accessors[meshPrimitive.indices];
        Framework::glTF::BufferView& indicesBufferView =
            scene.bufferViews[indicesAccessor.bufferView];
        Graphics::RendererUtil::BufferResource& indicesBufferGpu =
            buffers[indicesAccessor.bufferView];
        meshDraw.indexBuffer = indicesBufferGpu.m_Handle;
        meshDraw.indexOffset = indicesAccessor.byteOffset == Framework::glTF::INVALID_INT_VALUE
                                   ? 0
                                   : indicesAccessor.byteOffset;

        Framework::glTF::Material& material = scene.materials[meshPrimitive.material];

        // Descriptor Set
        Graphics::DescriptorSetCreation dsCreation{};
        dsCreation.setLayout(demoDsl).buffer(demoCb, 0);

        // NOTE: for now we expect all three textures to be defined. In the next chapter
        // we'll relax this constraint thanks to bindless rendering!

        if (material.pbrMetallicRoughness != nullptr)
        {
          if (material.pbrMetallicRoughness->baseColorFactorCount != 0)
          {
            assert(material.pbrMetallicRoughness->baseColorFactorCount == 4);

            meshDraw.materialData.baseColorFactor = {
                material.pbrMetallicRoughness->baseColorFactor[0],
                material.pbrMetallicRoughness->baseColorFactor[1],
                material.pbrMetallicRoughness->baseColorFactor[2],
                material.pbrMetallicRoughness->baseColorFactor[3],
            };
          }
          else
          {
            meshDraw.materialData.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
          }

          if (material.pbrMetallicRoughness->baseColorTexture != nullptr)
          {
            Framework::glTF::Texture& diffuseTexture =
                scene.textures[material.pbrMetallicRoughness->baseColorTexture->index];
            Graphics::RendererUtil::TextureResource& diffuseTextureGpu =
                images[diffuseTexture.source];
            Graphics::RendererUtil::SamplerResource& diffuseSamplerGpu =
                samplers[diffuseTexture.sampler];

            dsCreation.textureSampler(diffuseTextureGpu.m_Handle, diffuseSamplerGpu.m_Handle, 1);
          }
          else
          {
            continue;
          }

          if (material.pbrMetallicRoughness->metallicRoughnessTexture != nullptr)
          {
            Framework::glTF::Texture& roughness_texture =
                scene.textures[material.pbrMetallicRoughness->metallicRoughnessTexture->index];
            Graphics::RendererUtil::TextureResource& roughnessTextureGpu =
                images[roughness_texture.source];
            Graphics::RendererUtil::SamplerResource& roughnessSamplerGpu =
                samplers[roughness_texture.sampler];

            dsCreation.textureSampler(
                roughnessTextureGpu.m_Handle, roughnessSamplerGpu.m_Handle, 2);
          }
          else if (material.occlusionTexture != nullptr)
          {
            Framework::glTF::Texture& occlusionTexture =
                scene.textures[material.occlusionTexture->index];

            Graphics::RendererUtil::TextureResource& occlusionTextureGpu =
                images[occlusionTexture.source];
            Graphics::RendererUtil::SamplerResource& occlusionSamplerGpu =
                samplers[occlusionTexture.sampler];

            dsCreation.textureSampler(
                occlusionTextureGpu.m_Handle, occlusionSamplerGpu.m_Handle, 2);
          }
          else
          {
            continue;
          }
        }
        else
        {
          continue;
        }

        if (material.normalTexture != nullptr)
        {
          Framework::glTF::Texture& normalTexture = scene.textures[material.normalTexture->index];
          Graphics::RendererUtil::TextureResource& normalTextureGpu = images[normalTexture.source];
          Graphics::RendererUtil::SamplerResource& normalSamplerGpu =
              samplers[normalTexture.sampler];

          dsCreation.textureSampler(normalTextureGpu.m_Handle, normalSamplerGpu.m_Handle, 3);
        }
        else
        {
          continue;
        }

        cbCreation.reset()
            .set(
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                Graphics::ResourceUsageType::kDynamic,
                sizeof(MaterialData))
            .setName("material");
        meshDraw.materialBuffer = gpuDevice.createBuffer(cbCreation);
        dsCreation.buffer(meshDraw.materialBuffer, 4);

        meshDraw.count = indicesAccessor.count;

        meshDraw.descriptorSet = gpuDevice.createDescriptorSet(dsCreation);

        meshDraws.push(meshDraw);
      }
    }
  }
}
void _unloadGltfScene(
    Framework::Array<MeshDraw>& meshDraws,
    Graphics::GpuDevice& gpuDevice,
    Framework::StringBuffer& modelPath)
{
  for (uint32_t meshIndex = 0; meshIndex < meshDraws.m_Size; ++meshIndex)
  {
    MeshDraw& meshDraw = meshDraws[meshIndex];
    gpuDevice.destroyDescriptorSet(meshDraw.descriptorSet);
    gpuDevice.destroyBuffer(meshDraw.materialBuffer);
  }

  meshDraws.shutdown();

  gpuDevice.destroyBuffer(demoCb);
  gpuDevice.destroyPipeline(demoPso);
  gpuDevice.destroyDescriptorSetLayout(demoDsl);
}
//---------------------------------------------------------------------------//
int main(int argc, char** argv)
{

  if (argc < 2)
  {
    printf("No model specified, using the default model\n");
    exit(-1);
  }

  // main variable:
  Graphics::GpuDevice gpuDevice;
  Framework::ResourceManager resourceMgr = {};
  Graphics::RendererUtil::Renderer renderer;
  Graphics::ImguiUtil::ImguiService* imgui = nullptr;
  Framework::glTF::glTF scene;
  Framework::Array<Graphics::RendererUtil::TextureResource> images;
  Framework::Array<Graphics::RendererUtil::BufferResource> buffers;
  Framework::Array<Graphics::RendererUtil::SamplerResource> samplers;
  Framework::Array<MeshDraw> meshDraws;
  Framework::StringBuffer modelPath;
  int modelPathIdx = 0;

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
  winCfg.m_Name = "Demo 02";
  winCfg.m_Allocator = allocator;
  Framework::Window window = {};
  window.init(&winCfg);

  window.registerOSMessagesCallback(inputOSMessagesCallback, &inputHandler);

  // graphics
  Graphics::DeviceCreation deviceCreation;
  deviceCreation.setWindow(window.m_Width, window.m_Height, window.m_PlatformHandle)
      .setAllocator(allocator)
      .setTemporaryAllocator(&scratchAllocator);

  gpuDevice.init(deviceCreation);
  resourceMgr.init(allocator, nullptr);
  renderer.init({&gpuDevice, allocator});
  renderer.setLoaders(&resourceMgr);

  imgui = Graphics::ImguiUtil::ImguiService::instance();
  Graphics::ImguiUtil::ImguiServiceConfiguration imguiConfig{&gpuDevice, window.m_PlatformHandle};
  imgui->init(&imguiConfig);

  // Load glTF scene
  {
    modelPath.init(MAX_PATH, allocator);
    modelPath.append(argv[1]);
    _loadGltfScene(
        modelPath, allocator, scene, images, renderer, samplers, buffers, gpuDevice, meshDraws);
  }

  int64_t beginFrameTick = Framework::Time::getCurrentTime();

  vec3s eye = vec3s{0.0f, 2.5f, 2.0f};
  vec3s look = vec3s{0.0f, 0.0, -1.0f};
  vec3s right = vec3s{1.0f, 0.0, 0.0f};

  float yaw = 0.0f;
  float pitch = 0.0f;

  float modelScale = 0.008f;
  bool reloadModel = false;

#pragma region Window loop
  while (!window.m_RequestedExit)
  {
    if (reloadModel)
    {
      _unloadGltfScene(meshDraws, gpuDevice, modelPath);

      imgui->shutdown(); // This should be freed bc it uses gpuDevice (in the renderer object)

      // TODO: just reset these instead of full shutdown/init
      {
        resourceMgr.shutdown();
        renderer.shutdown();
      }
      Framework::gltfFree(scene); // should be after renderer shutdown

      modelPath.clear();
      switch (modelPathIdx)
      {
      case 0:
        modelPath.append("c:/gltf-models/FlightHelmet/FlightHelmet.gltf");
        modelScale = 4.0f;
        break;
      case 1:
        modelPath.append("c:/gltf-models/Sponza/Sponza.gltf");
        modelScale = 0.008f;
        break;
      }

      // TODO: just reset these instead of full shutdown/init
      {
        gpuDevice.init(deviceCreation);
        resourceMgr.init(allocator, nullptr);
        renderer.init({&gpuDevice, allocator});
        renderer.setLoaders(&resourceMgr);
        imgui->init(&imguiConfig);
      }

      _loadGltfScene(
          modelPath, allocator, scene, images, renderer, samplers, buffers, gpuDevice, meshDraws);

      reloadModel = false;
    }

    // New frame
    if (!window.m_Minimized)
    {
      gpuDevice.newFrame();
    }

    window.handleOSMessages();

    if (window.m_Resized)
    {
      // gpuDevice.resize(window.m_Width, window.m_Height);
      window.m_Resized = false;
    }

    imgui->newFrame();

    const int64_t currentTick = Framework::Time::getCurrentTime();
    float deltaTime = (float)Framework::Time::deltaSeconds(beginFrameTick, currentTick);
    beginFrameTick = currentTick;

    inputHandler.newFrame();
    inputHandler.update(deltaTime);

    // Imgui control
    if (ImGui::Begin("Framework ImGui"))
    {
      ImGui::InputFloat("Model scale", &modelScale, 0.01f);
      modelScale = Framework::max(modelScale, 0.005f);

      ImGui::Combo(
          "glTF Model",
          &modelPathIdx,
          "Flight Helmet\0"
          "Sponza\0"
          /*"Triangle\0"
          "Dragon\0"
          "Suzanne\0"*/
      );
      // Buttons return true when clicked (most widgets return true
      // when edited/activated)
      if (ImGui::Button("Load model"))
      {
        reloadModel = true;
      }
    }
    ImGui::End();

    // Update rotating demo gpu data
    {
      Graphics::MapBufferParameters cbMap = {demoCb, 0, 0};
      float* cbData = (float*)gpuDevice.mapBuffer(cbMap);
      if (cbData)
      {
        if (inputHandler.isMouseDown(Framework::MouseButtons::MOUSE_BUTTONS_LEFT))
        {
          pitch += (inputHandler.m_MousePosition.y - inputHandler.m_PreviousMousePosition.y) * 0.1f;
          yaw += (inputHandler.m_MousePosition.x - inputHandler.m_PreviousMousePosition.x) * 0.3f;

          pitch = clamp(pitch, -60.0f, 60.0f);

          if (yaw > 360.0f)
          {
            yaw -= 360.0f;
          }

          mat3s rxm = glms_mat4_pick3(glms_rotate_make(glm_rad(-pitch), vec3s{1.0f, 0.0f, 0.0f}));
          mat3s rym = glms_mat4_pick3(glms_rotate_make(glm_rad(-yaw), vec3s{0.0f, 1.0f, 0.0f}));

          look = glms_mat3_mulv(rxm, vec3s{0.0f, 0.0f, -1.0f});
          look = glms_mat3_mulv(rym, look);

          right = glms_cross(look, vec3s{0.0f, 1.0f, 0.0f});
        }

        if (inputHandler.isKeyDown(Framework::Keys::KEY_W))
        {
          eye = glms_vec3_add(eye, glms_vec3_scale(look, 5.0f * deltaTime));
        }
        else if (inputHandler.isKeyDown(Framework::Keys::KEY_S))
        {
          eye = glms_vec3_sub(eye, glms_vec3_scale(look, 5.0f * deltaTime));
        }

        if (inputHandler.isKeyDown(Framework::Keys::KEY_D))
        {
          eye = glms_vec3_add(eye, glms_vec3_scale(right, 5.0f * deltaTime));
        }
        else if (inputHandler.isKeyDown(Framework::Keys::KEY_A))
        {
          eye = glms_vec3_sub(eye, glms_vec3_scale(right, 5.0f * deltaTime));
        }

        mat4s view = glms_lookat(eye, glms_vec3_add(eye, look), vec3s{0.0f, 1.0f, 0.0f});
        mat4s projection = glms_perspective(
            glm_rad(60.0f),
            gpuDevice.m_SwapchainWidth * 1.0f / gpuDevice.m_SwapchainHeight,
            0.01f,
            1000.0f);

        // Calculate view projection matrix
        mat4s view_projection = glms_mat4_mul(projection, view);

        // Rotate:
        rx += 1.0f * deltaTime;
        ry += 2.0f * deltaTime;

        mat4s rxm = glms_rotate_make(rx, vec3s{1.0f, 0.0f, 0.0f});
        mat4s rym = glms_rotate_make(glm_rad(45.0f), vec3s{0.0f, 1.0f, 0.0f});

        mat4s sm = glms_scale_make(vec3s{modelScale, modelScale, modelScale});
        mat4s model = glms_mat4_mul(rym, sm);

        UniformData uniform_data{};
        uniform_data.viewProj = view_projection, model;
        uniform_data.model = model;
        uniform_data.invModel = glms_mat4_inv(glms_mat4_transpose(model));
        uniform_data.eye = vec4s{eye.x, eye.y, eye.z, 1.0f};
        uniform_data.light = vec4s{2.0f, 2.0f, 0.0f, 1.0f};

        memcpy(cbData, &uniform_data, sizeof(UniformData));

        gpuDevice.unmapBuffer(cbMap);
      }
    }

    if (!window.m_Minimized)
    {
      Graphics::CommandBuffer* gpuCommands = gpuDevice.getCommandBuffer(true);

      gpuCommands->clear(0.3f, 0.9f, 0.3f, 1.0f);
      gpuCommands->clearDepthStencil(1.0f, 0);
      gpuCommands->bindPass(gpuDevice.m_SwapchainPass);
      gpuCommands->bindPipeline(demoPso);
      gpuCommands->setScissor(nullptr);
      gpuCommands->setViewport(nullptr);

      for (uint32_t meshIndex = 0; meshIndex < meshDraws.m_Size; ++meshIndex)
      {
        MeshDraw meshDraw = meshDraws[meshIndex];

        Graphics::MapBufferParameters material_map = {meshDraw.materialBuffer, 0, 0};
        MaterialData* material_bufferData = (MaterialData*)gpuDevice.mapBuffer(material_map);

        memcpy(material_bufferData, &meshDraw.materialData, sizeof(MaterialData));

        gpuDevice.unmapBuffer(material_map);

        gpuCommands->bindVertexBuffer(meshDraw.positionBuffer, 0, meshDraw.positionOffset);
        gpuCommands->bindVertexBuffer(meshDraw.tangentBuffer, 1, meshDraw.tangentOffset);
        gpuCommands->bindVertexBuffer(meshDraw.normalBuffer, 2, meshDraw.normalOffset);
        gpuCommands->bindVertexBuffer(meshDraw.texcoordBuffer, 3, meshDraw.texcoordOffset);
        gpuCommands->bindIndexBuffer(meshDraw.indexBuffer, meshDraw.indexOffset);
        gpuCommands->bindDescriptorSet(&meshDraw.descriptorSet, 1, nullptr, 0);

        gpuCommands->drawIndexed(Graphics::TopologyType::kTriangle, meshDraw.count, 1, 0, 0, 0);
      }

      imgui->render(*gpuCommands);

      // Send commands to GPU
      gpuDevice.queueCommandBuffer(gpuCommands);
      gpuDevice.present();
    }
  }
#pragma endregion End Window loop

#pragma region Deinit, shutdown and cleanup

  _unloadGltfScene(meshDraws, gpuDevice, modelPath);

  modelPath.shutdown();

  imgui->shutdown();

  resourceMgr.shutdown();
  renderer.shutdown();
  Framework::gltfFree(scene); // should be after renderer shutdown

  inputHandler.shutdown();
  window.unregisterOSMessagesCallback(inputOSMessagesCallback);
  window.shutdown();

  Framework::MemoryService::instance()->shutdown();
#pragma endregion End Deinit, shutdown and cleanup

  return (0);
}
