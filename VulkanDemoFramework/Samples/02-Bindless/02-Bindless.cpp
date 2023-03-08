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
Graphics::BufferHandle sceneCb;

struct MeshDraw
{
  Graphics::RendererUtil::Material* material;

  Graphics::BufferHandle indexBuffer;
  Graphics::BufferHandle positionBuffer;
  Graphics::BufferHandle tangentBuffer;
  Graphics::BufferHandle normalBuffer;
  Graphics::BufferHandle texcoordBuffer;
  Graphics::BufferHandle materialBuffer;

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
};

enum DrawFlags
{
  DrawFlagsAlphaMask = 1 << 0, // two power by zero
};

struct UniformData
{
  mat4s viewProj;
  vec4s eye;
  vec4s light;
  float lightRange;
  float lightIntenstiy;
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

}; // struct GpuEffect

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
#if 0

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

  p_CommandBuffers->bind_vertex_buffer(p_MeshDraw.position_buffer, 0, p_MeshDraw.position_offset);
  p_CommandBuffers->bind_vertex_buffer(p_MeshDraw.tangent_buffer, 1, p_MeshDraw.tangent_offset);
  p_CommandBuffers->bind_vertex_buffer(p_MeshDraw.normal_buffer, 2, p_MeshDraw.normal_offset);
  p_CommandBuffers->bind_vertex_buffer(p_MeshDraw.texcoord_buffer, 3, p_MeshDraw.texcoord_offset);
  p_CommandBuffers->bind_index_buffer(p_MeshDraw.index_buffer, p_MeshDraw.index_offset);
  p_CommandBuffers->bind_local_descriptorSet(&descriptorSet, 1, nullptr, 0);

  p_CommandBuffers->draw_indexed(
      Graphics::TopologyType::Triangle, p_MeshDraw.primitive_count, 1, 0, 0, 0);
}
//---------------------------------------------------------------------------//
struct Scene
{
  Framework::Array<MeshDraw> m_MeshDraws;

  // All graphics resources used by the scene
  Framework::Array<Framework::TextureResource> images;
  Framework::Array<Framework::SamplerResource> samplers;
  Framework::Array<Framework::BufferResource> buffers;

  Framework::glTF::glTF gltf_scene; // Source gltf scene

}; // struct GltfScene

static void scene_load_from_gltf(
    cstring filename, Framework::Renderer& renderer, Framework::Allocator* allocator, Scene& scene)
{

  using namespace Framework;

  scene.gltf_scene = gltf_load_file(filename);

  // Load all textures
  scene.images.init(allocator, scene.gltf_scene.images_count);

  for (u32 image_index = 0; image_index < scene.gltf_scene.images_count; ++image_index)
  {
    glTF::Image& image = scene.gltf_scene.images[image_index];
    TextureResource* tr = renderer.create_texture(image.uri.data, image.uri.data, true);
    RASSERT(tr != nullptr);

    scene.images.push(*tr);
  }

  StringBuffer resource_name_buffer;
  resource_name_buffer.init(4096, allocator);

  // Load all samplers
  scene.samplers.init(allocator, scene.gltf_scene.samplers_count);

  for (u32 sampler_index = 0; sampler_index < scene.gltf_scene.samplers_count; ++sampler_index)
  {
    glTF::Sampler& sampler = scene.gltf_scene.samplers[sampler_index];

    char* sampler_name = resource_name_buffer.append_use_f("sampler_%u", sampler_index);

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

    SamplerResource* sr = renderer.create_sampler(creation);
    RASSERT(sr != nullptr);

    scene.samplers.push(*sr);
  }

  // Temporary array of buffer data
  Framework::Array<void*> buffers_data;
  buffers_data.init(allocator, scene.gltf_scene.buffers_count);

  for (u32 buffer_index = 0; buffer_index < scene.gltf_scene.buffers_count; ++buffer_index)
  {
    glTF::Buffer& buffer = scene.gltf_scene.buffers[buffer_index];

    FileReadResult buffer_data = file_read_binary(buffer.uri.data, allocator);
    buffers_data.push(buffer_data.data);
  }

  // Load all buffers and initialize them with buffer data
  scene.buffers.init(allocator, scene.gltf_scene.buffer_views_count);

  for (u32 buffer_index = 0; buffer_index < scene.gltf_scene.buffer_views_count; ++buffer_index)
  {
    glTF::BufferView& buffer = scene.gltf_scene.buffer_views[buffer_index];

    i32 offset = buffer.byte_offset;
    if (offset == glTF::INVALID_INT_VALUE)
    {
      offset = 0;
    }

    u8* data = (u8*)buffers_data[buffer.buffer] + offset;

    // NOTE(marco): the target attribute of a BufferView is not mandatory, so we prepare for both
    // uses
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    char* buffer_name = buffer.name.data;
    if (buffer_name == nullptr)
    {
      buffer_name = resource_name_buffer.append_use_f("buffer_%u", buffer_index);
    }

    BufferResource* br = renderer.create_buffer(
        flags, ResourceUsageType::Immutable, buffer.byte_length, data, buffer_name);
    RASSERT(br != nullptr);

    scene.buffers.push(*br);
  }

  for (u32 buffer_index = 0; buffer_index < scene.gltf_scene.buffers_count; ++buffer_index)
  {
    void* buffer = buffers_data[buffer_index];
    allocator->deallocate(buffer);
  }
  buffers_data.shutdown();

  resource_name_buffer.shutdown();

  // Init runtime meshes
  scene.mesh_draws.init(allocator, scene.gltf_scene.meshes_count);
}

static void scene_free_gpu_resources(Scene& scene, Framework::Renderer& renderer)
{
  Framework::GpuDevice& gpu = *renderer.gpu;

  for (u32 mesh_index = 0; mesh_index < scene.mesh_draws.size; ++mesh_index)
  {
    MeshDraw& mesh_draw = scene.mesh_draws[mesh_index];
    gpu.destroy_buffer(mesh_draw.material_buffer);
  }

  scene.mesh_draws.shutdown();
}

static void scene_unload(Scene& scene, Framework::Renderer& renderer)
{

  Framework::GpuDevice& gpu = *renderer.gpu;

  // Free scene buffers
  scene.samplers.shutdown();
  scene.images.shutdown();
  scene.buffers.shutdown();

  // NOTE(marco): we can't destroy this sooner as textures and buffers
  // hold a pointer to the names stored here
  Framework::gltf_free(scene.gltf_scene);
}

static int mesh_material_compare(const void* a, const void* b)
{
  const MeshDraw* mesh_a = (const MeshDraw*)a;
  const MeshDraw* mesh_b = (const MeshDraw*)b;

  if (mesh_a->material->render_index < mesh_b->material->render_index)
    return -1;
  if (mesh_a->material->render_index > mesh_b->material->render_index)
    return 1;
  return 0;
}

static void get_mesh_vertex_buffer(
    Scene& scene,
    i32 accessor_index,
    Framework::BufferHandle& out_buffer_handle,
    u32& out_buffer_offset)
{
  using namespace Framework;

  if (accessor_index != -1)
  {
    glTF::Accessor& buffer_accessor = scene.gltf_scene.accessors[accessor_index];
    glTF::BufferView& buffer_view = scene.gltf_scene.buffer_views[buffer_accessor.buffer_view];
    BufferResource& buffer_gpu = scene.buffers[buffer_accessor.buffer_view];

    out_buffer_handle = buffer_gpu.handle;
    out_buffer_offset =
        buffer_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : buffer_accessor.byte_offset;
  }
}

static bool get_mesh_material(
    Framework::Renderer& renderer,
    Scene& scene,
    Framework::glTF::Material& material,
    MeshDraw& mesh_draw)
{
  using namespace Framework;

  bool transparent = false;
  GpuDevice& gpu = *renderer.gpu;

  if (material.pbr_metallic_roughness != nullptr)
  {
    if (material.pbr_metallic_roughness->baseColorFactor_count != 0)
    {
      RASSERT(material.pbr_metallic_roughness->baseColorFactor_count == 4);

      mesh_draw.baseColorFactor = {
          material.pbr_metallic_roughness->baseColorFactor[0],
          material.pbr_metallic_roughness->baseColorFactor[1],
          material.pbr_metallic_roughness->baseColorFactor[2],
          material.pbr_metallic_roughness->baseColorFactor[3],
      };
    }
    else
    {
      mesh_draw.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    if (material.pbr_metallic_roughness->roughness_factor != glTF::INVALID_FLOAT_VALUE)
    {
      mesh_draw.metallicRoughnessOcclusionFactor.x =
          material.pbr_metallic_roughness->roughness_factor;
    }
    else
    {
      mesh_draw.metallicRoughnessOcclusionFactor.x = 1.0f;
    }

    if (material.alpha_mode.data != nullptr && strcmp(material.alpha_mode.data, "MASK") == 0)
    {
      mesh_draw.flags |= DrawFlags_AlphaMask;
      transparent = true;
    }

    if (material.alphaCutoff != glTF::INVALID_FLOAT_VALUE)
    {
      mesh_draw.alphaCutoff = material.alphaCutoff;
    }

    if (material.pbr_metallic_roughness->metallic_factor != glTF::INVALID_FLOAT_VALUE)
    {
      mesh_draw.metallicRoughnessOcclusionFactor.y =
          material.pbr_metallic_roughness->metallic_factor;
    }
    else
    {
      mesh_draw.metallicRoughnessOcclusionFactor.y = 1.0f;
    }

    if (material.pbr_metallic_roughness->base_color_texture != nullptr)
    {
      glTF::Texture& diffuse_texture =
          scene.gltf_scene.textures[material.pbr_metallic_roughness->base_color_texture->index];
      TextureResource& diffuse_texture_gpu = scene.images[diffuse_texture.source];
      SamplerResource& diffuse_sampler_gpu = scene.samplers[diffuse_texture.sampler];

      mesh_draw.diffuseTextureIndex = diffuse_texture_gpu.handle.index;

      gpu.link_texture_sampler(diffuse_texture_gpu.handle, diffuse_sampler_gpu.handle);
    }
    else
    {
      mesh_draw.diffuseTextureIndex = INVALID_TEXTURE_INDEX;
    }

    if (material.pbr_metallic_roughness->metallic_roughness_texture != nullptr)
    {
      glTF::Texture& roughness_texture =
          scene.gltf_scene
              .textures[material.pbr_metallic_roughness->metallic_roughness_texture->index];
      TextureResource& roughness_texture_gpu = scene.images[roughness_texture.source];
      SamplerResource& roughness_sampler_gpu = scene.samplers[roughness_texture.sampler];

      mesh_draw.roughnessTextureIndex = roughness_texture_gpu.handle.index;

      gpu.link_texture_sampler(roughness_texture_gpu.handle, roughness_sampler_gpu.handle);
    }
    else
    {
      mesh_draw.roughnessTextureIndex = INVALID_TEXTURE_INDEX;
    }
  }

  if (material.occlusion_texture != nullptr)
  {
    glTF::Texture& occlusion_texture = scene.gltf_scene.textures[material.occlusion_texture->index];

    TextureResource& occlusion_texture_gpu = scene.images[occlusion_texture.source];
    SamplerResource& occlusion_sampler_gpu = scene.samplers[occlusion_texture.sampler];

    mesh_draw.occlusionTextureIndex = occlusion_texture_gpu.handle.index;

    if (material.occlusion_texture->strength != glTF::INVALID_FLOAT_VALUE)
    {
      mesh_draw.metallicRoughnessOcclusionFactor.z = material.occlusion_texture->strength;
    }
    else
    {
      mesh_draw.metallicRoughnessOcclusionFactor.z = 1.0f;
    }

    gpu.link_texture_sampler(occlusion_texture_gpu.handle, occlusion_sampler_gpu.handle);
  }
  else
  {
    mesh_draw.occlusionTextureIndex = INVALID_TEXTURE_INDEX;
  }

  if (material.normal_texture != nullptr)
  {
    glTF::Texture& normal_texture = scene.gltf_scene.textures[material.normal_texture->index];
    TextureResource& normal_texture_gpu = scene.images[normal_texture.source];
    SamplerResource& normal_sampler_gpu = scene.samplers[normal_texture.sampler];

    gpu.link_texture_sampler(normal_texture_gpu.handle, normal_sampler_gpu.handle);

    mesh_draw.normalTextureIndex = normal_texture_gpu.handle.index;
  }
  else
  {
    mesh_draw.normalTextureIndex = INVALID_TEXTURE_INDEX;
  }

  // Create material buffer
  BufferCreation buffer_creation;
  buffer_creation.reset()
      .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof(MeshData))
      .set_name("Mesh Data");
  mesh_draw.material_buffer = gpu.create_buffer(buffer_creation);

  return transparent;
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

#  pragma region Window loop
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
#  pragma endregion End Window loop

#  pragma region Deinit, shutdown and cleanup

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
#  pragma endregion End Deinit, shutdown and cleanup

  return (0);
}
#endif
int main() { return (0); }
