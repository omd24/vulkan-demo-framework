#include <Foundation/File.hpp>
#include <Foundation/Gltf.hpp>
#include <Foundation/Numerics.hpp>
#include <Foundation/ResourceManager.hpp>
#include <Foundation/Time.hpp>

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
// Rotating cube test
Graphics::BufferHandle cubeVb;
Graphics::BufferHandle cubeIb;
Graphics::PipelineHandle cubePso;
Graphics::BufferHandle cubeCb;
Graphics::DescriptorSetHandle cubeDs;
Graphics::DescriptorSetLayoutHandle cubeDsl;

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
int main(int argc, char** argv)
{
  if (argc < 2)
  {
    printf("No model specified, using the default model\n");
    exit(-1);
    // argv[1] = const_cast<char*>("C:\\gltf-models\\FlightHelmet\\FlightHelmet.gltf");
  }

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
  winCfg.m_Name = "Demo 01";
  winCfg.m_Allocator = allocator;
  Framework::Window window = {};
  window.init(&winCfg);

  window.registerOSMessagesCallback(inputOSMessagesCallback, &inputHandler);

  // graphics
  Graphics::DeviceCreation deviceCreation;
  deviceCreation.setWindow(window.m_Width, window.m_Height, window.m_PlatformHandle)
      .setAllocator(allocator)
      .setTemporaryAllocator(&scratchAllocator);
  Graphics::GpuDevice gpuDevice;
  gpuDevice.init(deviceCreation);

  Framework::ResourceManager resourceMgr = {};
  resourceMgr.init(allocator, nullptr);

  Graphics::RendererUtil::Renderer renderer;
  renderer.init({&gpuDevice, allocator});
  renderer.setLoaders(&resourceMgr);

  Graphics::ImguiUtil::ImguiService* imgui = Graphics::ImguiUtil::ImguiService::instance();
  Graphics::ImguiUtil::ImguiServiceConfiguration imguiConfig{&gpuDevice, window.m_PlatformHandle};
  imgui->init(&imguiConfig);

  // Load glTF scene
#pragma region Load glTF scene

  // Store currect working dir to restore later
  Framework::Directory cwd = {};
  Framework::directoryCurrent(&cwd);

  // Change directory:
  char gltfBasePath[512] = {};
  ::memcpy(gltfBasePath, argv[1], strlen(argv[1]));
  Framework::fileDirectoryFromPath(gltfBasePath);
  Framework::directoryChange(gltfBasePath);

  // Determine filename:
  char gltfFile[512] = {};
  ::memcpy(gltfFile, argv[1], strlen(argv[1]));
  Framework::filenameFromPath(gltfFile);

  // Load scene:
  Framework::glTF::glTF scene = Framework::gltfLoadFile(gltfFile);

  // Create textures:
  Framework::Array<Graphics::RendererUtil::TextureResource> images;
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

  Framework::Array<Graphics::RendererUtil::SamplerResource> samplers;
  samplers.init(allocator, scene.samplersCount);
  for (uint32_t samplerIndex = 0; samplerIndex < scene.samplersCount; ++samplerIndex)
  {
    Framework::glTF::Sampler& sampler = scene.samplers[samplerIndex];

    char* sampler_name = resourceNameBuffer.appendUseFormatted("sampler %u", samplerIndex);

    Graphics::SamplerCreation creation;
    creation.minFilter = sampler.minFilter == Framework::glTF::Sampler::Filter::LINEAR
                             ? VK_FILTER_LINEAR
                             : VK_FILTER_NEAREST;
    creation.magFilter = sampler.magFilter == Framework::glTF::Sampler::Filter::LINEAR
                             ? VK_FILTER_LINEAR
                             : VK_FILTER_NEAREST;
    creation.name = sampler_name;

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

  Framework::Array<Graphics::RendererUtil::BufferResource> buffers;
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

    // NOTE(marco): the target attribute of a BufferView is not mandatory, so we prepare for both
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

  // Create pipeline state
  {
    Graphics::PipelineCreation pipelineCreation;

    // Vertex input
    // TODO(marco): component format should be based on buffer view type
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
    const char* vsCode = R"FOO(#version 450
layout(std140, binding = 0) uniform LocalConstants {
    mat4 m;
    mat4 vp;
    mat4 mInverse;
    vec4 eye;
    vec4 light;
};

layout(location=0) in vec3 position;
layout(location=1) in vec4 tangent;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 texCoord0;

layout (location = 0) out vec2 vTexcoord0;
layout (location = 1) out vec3 vNormal;
layout (location = 2) out vec4 vTangent;
layout (location = 3) out vec4 vPosition;

void main() {
    gl_Position = vp * m * vec4(position, 1);
    vPosition = m * vec4(position, 1.0);
    vTexcoord0 = texCoord0;
    vNormal = mat3(mInverse) * normal;
    vTangent = tangent;
}
)FOO";

    const char* fsCode = R"FOO(#version 450
layout(std140, binding = 0) uniform LocalConstants {
    mat4 m;
    mat4 vp;
    mat4 mInverse;
    vec4 eye;
    vec4 light;
};

layout(std140, binding = 4) uniform MaterialConstant {
    vec4 base_color_factor;
};

layout (binding = 1) uniform sampler2D diffuseTexture;
layout (binding = 2) uniform sampler2D occlusionRoughnessMetalnessTexture;
layout (binding = 3) uniform sampler2D normalTexture;

layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec4 vTangent;
layout (location = 3) in vec4 vPosition;

layout (location = 0) out vec4 frag_color;

#define PI 3.1415926538

vec3 decode_srgb( vec3 c ) {
    vec3 result;
    if ( c.r <= 0.04045) {
        result.r = c.r / 12.92;
    } else {
        result.r = pow( ( c.r + 0.055 ) / 1.055, 2.4 );
    }

    if ( c.g <= 0.04045) {
        result.g = c.g / 12.92;
    } else {
        result.g = pow( ( c.g + 0.055 ) / 1.055, 2.4 );
    }

    if ( c.b <= 0.04045) {
        result.b = c.b / 12.92;
    } else {
        result.b = pow( ( c.b + 0.055 ) / 1.055, 2.4 );
    }

    return clamp( result, 0.0, 1.0 );
}

vec3 encode_srgb( vec3 c ) {
    vec3 result;
    if ( c.r <= 0.0031308) {
        result.r = c.r * 12.92;
    } else {
        result.r = 1.055 * pow( c.r, 1.0 / 2.4 ) - 0.055;
    }

    if ( c.g <= 0.0031308) {
        result.g = c.g * 12.92;
    } else {
        result.g = 1.055 * pow( c.g, 1.0 / 2.4 ) - 0.055;
    }

    if ( c.b <= 0.0031308) {
        result.b = c.b * 12.92;
    } else {
        result.b = 1.055 * pow( c.b, 1.0 / 2.4 ) - 0.055;
    }

    return clamp( result, 0.0, 1.0 );
}

float heaviside( float v ) {
    if ( v > 0.0 ) return 1.0;
    else return 0.0;
}

void main() {
    // NOTE(marco): normal textures are encoded to [0, 1] but need to be mapped to [-1, 1] value
    vec3 bump_normal = normalize( texture(normalTexture, vTexcoord0).rgb * 2.0 - 1.0 );
    vec3 tangent = normalize( vTangent.xyz );
    vec3 bitangent = cross( normalize( vNormal ), tangent ) * vTangent.w;

    mat3 TBN = transpose(mat3(
        tangent,
        bitangent,
        normalize( vNormal )
    ));

    // vec3 V = normalize(eye.xyz - vPosition.xyz);
    // vec3 L = normalize(light.xyz - vPosition.xyz);
    // vec3 N = normalize(vNormal);
    // vec3 H = normalize(L + V);

    vec3 V = normalize( TBN * ( eye.xyz - vPosition.xyz ) );
    vec3 L = normalize( TBN * ( light.xyz - vPosition.xyz ) );
    vec3 N = bump_normal;
    vec3 H = normalize( L + V );

    vec4 rmo = texture(occlusionRoughnessMetalnessTexture, vTexcoord0);

    // Green channel contains roughness values
    float roughness = rmo.g;
    float alpha = pow(roughness, 2.0);

    // Blue channel contains metalness
    float metalness = rmo.b;

    // Red channel for occlusion value

    vec4 base_colour = texture(diffuseTexture, vTexcoord0) * base_color_factor;
    base_colour.rgb = decode_srgb( base_colour.rgb );

    // https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#specular-brdf
    float NdotH = dot(N, H);
    float alpha_squared = alpha * alpha;
    float d_denom = ( NdotH * NdotH ) * ( alpha_squared - 1.0 ) + 1.0;
    float distribution = ( alpha_squared * heaviside( NdotH ) ) / ( PI * d_denom * d_denom );

    float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    float HdotL = dot(H, L);
    float HdotV = dot(H, V);

    float visibility = ( heaviside( HdotL ) / ( abs( NdotL ) + sqrt( alpha_squared + ( 1.0 - alpha_squared ) * ( NdotL * NdotL ) ) ) ) * ( heaviside( HdotV ) / ( abs( NdotV ) + sqrt( alpha_squared + ( 1.0 - alpha_squared ) * ( NdotV * NdotV ) ) ) );

    float specular_brdf = visibility * distribution;

    vec3 diffuse_brdf = (1 / PI) * base_colour.rgb;

    // NOTE(marco): f0 in the formula notation refers to the base colour here
    vec3 conductor_fresnel = specular_brdf * ( base_colour.rgb + ( 1.0 - base_colour.rgb ) * pow( 1.0 - abs( HdotV ), 5 ) );

    // NOTE(marco): f0 in the formula notation refers to the value derived from ior = 1.5
    float f0 = 0.04; // pow( ( 1 - ior ) / ( 1 + ior ), 2 )
    float fr = f0 + ( 1 - f0 ) * pow(1 - abs( HdotV ), 5 );
    vec3 fresnel_mix = mix( diffuse_brdf, vec3( specular_brdf ), fr );

    vec3 material_colour = mix( fresnel_mix, conductor_fresnel, metalness );

    frag_color = vec4( encode_srgb( material_colour ), base_colour.a );
}
)FOO";

    pipelineCreation.shaders.setName("Cube")
        .addStage(vsCode, (uint32_t)strlen(vsCode), VK_SHADER_STAGE_VERTEX_BIT)
        .addStage(fsCode, (uint32_t)strlen(fsCode), VK_SHADER_STAGE_FRAGMENT_BIT);

    // Descriptor set layout
    Graphics::DescriptorSetLayoutCreation cubeDslCreation{};
    cubeDslCreation.addBinding({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 1, "LocalConstants"});
    cubeDslCreation.addBinding({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, 1, "diffuseTexture"});
    cubeDslCreation.addBinding(
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, 1, "occlusionRoughnessMetalnessTexture"});
    cubeDslCreation.addBinding({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, 1, "normalTexture"});
    cubeDslCreation.addBinding({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, 1, "MaterialConstant"});
    // Setting it into pipeline
    cubeDsl = gpuDevice.createDescriptorSetLayout(cubeDslCreation);
    pipelineCreation.addDescriptorSetLayout(cubeDsl);

    cubePso = gpuDevice.createPipeline(pipelineCreation);
  } // end Create pipeline

  // Create drawable objects (mesh draws)
  Framework::Array<MeshDraw> meshDraws;
  meshDraws.init(allocator, scene.meshesCount);
  {
    // Constant buffer
    Graphics::BufferCreation cbCreation;
    cbCreation.reset()
        .set(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            Graphics::ResourceUsageType::kDynamic,
            sizeof(UniformData))
        .setName("cubeCb");
    cubeCb = gpuDevice.createBuffer(cbCreation);

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
          Graphics::RendererUtil::BufferResource& texcoord_buffer_gpu =
              buffers[texcoordAccessor.bufferView];

          meshDraw.texcoordBuffer = texcoord_buffer_gpu.m_Handle;
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
        dsCreation.setLayout(cubeDsl).buffer(cubeCb, 0);

        // NOTE(marco): for now we expect all three textures to be defined. In the next chapter
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

#pragma endregion End Load glTF scene

  int64_t beginFrameTick = Framework::Time::getCurrentTime();
  float modelScale = 0.008f;

#pragma region Window loop
  while (!window.m_RequestedExit)
  {
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

    if (!window.m_Minimized)
    {
      Graphics::CommandBuffer* gpuCommands = gpuDevice.getCommandBuffer(true);

      gpuCommands->clear(0.3f, 0.9f, 0.3f, 1.0f);
      gpuCommands->clearDepthStencil(1.0f, 0);
      gpuCommands->bindPass(gpuDevice.m_SwapchainPass);
      gpuCommands->bindPipeline(cubePso);
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

  for (uint32_t meshIndex = 0; meshIndex < meshDraws.m_Size; ++meshIndex)
  {
    MeshDraw& meshDraw = meshDraws[meshIndex];
    gpuDevice.destroyDescriptorSet(meshDraw.descriptorSet);
    gpuDevice.destroyBuffer(meshDraw.materialBuffer);
  }

  meshDraws.shutdown();

  gpuDevice.destroyBuffer(cubeCb);
  gpuDevice.destroyPipeline(cubePso);
  gpuDevice.destroyDescriptorSetLayout(cubeDsl);

  imgui->shutdown();

  resourceMgr.shutdown();
  renderer.shutdown();

  Framework::gltfFree(scene);

  inputHandler.shutdown();
  window.unregisterOSMessagesCallback(inputOSMessagesCallback);
  window.shutdown();

  Framework::MemoryService::instance()->shutdown();
#pragma endregion End Deinit, shutdown and cleanup

  return (0);
}
