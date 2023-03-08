#pragma once

#include "Foundation/Prerequisites.hpp"
#include "Graphics/GpuEnum.hpp"

#include <vulkan/vulkan.h>
#include "Externals/vk_mem_alloc.h"

#define SHADER_FOLDER "\\Shaders\\"

namespace Graphics
{
// Forward delcare
namespace Spirv
{
struct ParseResult;
}

static const uint32_t kInvalidIndex = 0xffffffff;
typedef uint32_t ResourceHandle;

struct BufferHandle
{
  ResourceHandle index;
}; // struct BufferHandle

struct TextureHandle
{
  ResourceHandle index;
}; // struct TextureHandle

struct ShaderStateHandle
{
  ResourceHandle index;
}; // struct ShaderStateHandle

struct SamplerHandle
{
  ResourceHandle index;
}; // struct SamplerHandle

struct DescriptorSetLayoutHandle
{
  ResourceHandle index;
}; // struct DescriptorSetLayoutHandle

struct DescriptorSetHandle
{
  ResourceHandle index;
}; // struct DescriptorSetHandle

struct PipelineHandle
{
  ResourceHandle index;
}; // struct PipelineHandle

struct RenderPassHandle
{
  ResourceHandle index;
}; // struct RenderPassHandle
//---------------------------------------------------------------------------//
/// Invalid handles:
static BufferHandle kInvalidBuffer{kInvalidIndex};
static TextureHandle kInvalidTexture{kInvalidIndex};
static ShaderStateHandle kInvalidShader{kInvalidIndex};
static SamplerHandle kInvalidSampler{kInvalidIndex};
static DescriptorSetLayoutHandle kInvalidLayout{kInvalidIndex};
static DescriptorSetHandle kInvalidSet{kInvalidIndex};
static PipelineHandle kInvalidPipeline{kInvalidIndex};
static RenderPassHandle kInvalidPass{kInvalidIndex};
//---------------------------------------------------------------------------//
/// Consts:
// Maximum number of Images/RenderTargets/FBO attachments usable.
static const uint8_t kMaxImageOutputs = 8;
// Maximum number of layouts in the pipeline.
static const uint8_t kMaxDescriptorSetLayouts = 8;
// Maximum simultaneous shader stages. Applicable to all different type of pipelines.
static const uint8_t kMaxShaderStages = 5;
// Maximum list elements for both descriptor set layout and descriptor sets.
static const uint8_t kMaxDescriptorsPerSet = 16;
static const uint8_t kMaxVertexStreams = 16;
static const uint8_t kMaxVertexAttributes = 16;

static const uint32_t kSubmitHeaderSentinel = 0xfefeb7ba;
static const uint32_t kMaxResourceDeletions = 64;
//---------------------------------------------------------------------------//
// Resource creation structs:
//---------------------------------------------------------------------------//
struct Rect2D
{
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
}; // struct Rect2D

struct Rect2DInt
{
  int16_t x = 0;
  int16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
}; // struct Rect2D

struct Viewport
{
  Rect2DInt rect;
  float minDepth = 0.0f;
  float maxDepth = 0.0f;
}; // struct Viewport

struct ViewportState
{
  uint32_t numViewports = 0;
  uint32_t numScissors = 0;

  Viewport* viewport = nullptr;
  Rect2DInt* scissors = nullptr;
}; // struct ViewportState

struct StencilOperationState
{
  VkStencilOp fail = VK_STENCIL_OP_KEEP;
  VkStencilOp pass = VK_STENCIL_OP_KEEP;
  VkStencilOp depthFail = VK_STENCIL_OP_KEEP;
  VkCompareOp compare = VK_COMPARE_OP_ALWAYS;
  uint32_t compareMask = 0xff;
  uint32_t writeMask = 0xff;
  uint32_t reference = 0xff;

}; // struct StencilOperationState

struct DepthStencilCreation
{
  StencilOperationState front;
  StencilOperationState back;
  VkCompareOp depthComparison = VK_COMPARE_OP_ALWAYS;

  uint8_t depthEnable : 1;
  uint8_t depthWriteEnable : 1;
  uint8_t stencilEnable : 1;
  uint8_t pad : 5;

  // Default constructor
  DepthStencilCreation() : depthEnable(0), depthWriteEnable(0), stencilEnable(0) {}

  DepthStencilCreation& setDepth(bool write, VkCompareOp comparisonTest);

}; // struct DepthStencilCreation

struct BlendState
{
  VkBlendFactor sourceColor = VK_BLEND_FACTOR_ONE;
  VkBlendFactor destinationColor = VK_BLEND_FACTOR_ONE;
  VkBlendOp colorOperation = VK_BLEND_OP_ADD;

  VkBlendFactor sourceAlpha = VK_BLEND_FACTOR_ONE;
  VkBlendFactor destinationAlpha = VK_BLEND_FACTOR_ONE;
  VkBlendOp alphaOperation = VK_BLEND_OP_ADD;

  ColorWriteEnabled::Mask colorWriteMask = ColorWriteEnabled::kAllMask;

  uint8_t blendEnabled : 1;
  uint8_t separateBlend : 1;
  uint8_t pad : 6;

  BlendState() : blendEnabled(0), separateBlend(0) {}

  BlendState&
  setColor(VkBlendFactor sourceColor, VkBlendFactor destinationColor, VkBlendOp colorOperation);
  BlendState&
  setAlpha(VkBlendFactor sourceColor, VkBlendFactor destinationColor, VkBlendOp colorOperation);
  BlendState& setColorWriteMask(ColorWriteEnabled::Mask value);

}; // struct BlendState

struct BlendStateCreation
{
  BlendState blendStates[kMaxImageOutputs];
  uint32_t activeStates = 0;

  BlendStateCreation& reset();
  BlendState& addBlendState();

}; // BlendStateCreation

struct RasterizationCreation
{
  VkCullModeFlagBits cullMode = VK_CULL_MODE_NONE;
  VkFrontFace front = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  FillMode::Enum fill = FillMode::kSolid;
}; // struct RasterizationCreation

struct BufferCreation
{
  VkBufferUsageFlags typeFlags = 0;
  ResourceUsageType::Enum usage = ResourceUsageType::kImmutable;
  uint32_t size = 0;
  void* initialData = nullptr;

  const char* name = nullptr;

  BufferCreation& reset();
  BufferCreation& set(VkBufferUsageFlags flags, ResourceUsageType::Enum usage, uint32_t size);
  BufferCreation& setData(void* data);
  BufferCreation& setName(const char* name);

}; // struct BufferCreation

struct TextureCreation
{
  void* initialData = nullptr;
  uint16_t width = 1;
  uint16_t height = 1;
  uint16_t depth = 1;
  uint8_t mipmaps = 1;
  uint8_t flags = 0; // TextureFlags bitmasks

  VkFormat format = VK_FORMAT_UNDEFINED;
  TextureType::Enum type = TextureType::kTexture2D;

  const char* name = nullptr;

  TextureCreation& setSize(uint16_t width, uint16_t height, uint16_t depth);
  TextureCreation& setFlags(uint8_t mipmaps, uint8_t flags);
  TextureCreation& setFormatType(VkFormat format, TextureType::Enum type);
  TextureCreation& setName(const char* name);
  TextureCreation& setData(void* data);

}; // struct TextureCreation

struct SamplerCreation
{
  VkFilter minFilter = VK_FILTER_NEAREST;
  VkFilter magFilter = VK_FILTER_NEAREST;
  VkSamplerMipmapMode mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

  VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

  const char* name = nullptr;

  SamplerCreation& setMinMagMip(VkFilter min, VkFilter mag, VkSamplerMipmapMode mip);
  SamplerCreation& setAddressModeU(VkSamplerAddressMode u);
  SamplerCreation& setAddressModeUV(VkSamplerAddressMode u, VkSamplerAddressMode v);
  SamplerCreation&
  setAddressModeUVW(VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w);
  SamplerCreation& setName(const char* name);

}; // struct SamplerCreation

struct ShaderStage
{
  const char* code = nullptr;
  uint32_t codeSize = 0;
  VkShaderStageFlagBits type = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;

}; // struct ShaderStage

struct ShaderStateCreation
{
  ShaderStage stages[kMaxShaderStages];

  const char* name = nullptr;

  uint32_t stagesCount = 0;
  uint32_t spvInput = 0;

  // Building helpers
  ShaderStateCreation& reset();
  ShaderStateCreation& setName(const char* name);
  ShaderStateCreation& addStage(const char* code, uint32_t codeSize, VkShaderStageFlagBits type);
  ShaderStateCreation& setSpvInput(bool value);

}; // struct ShaderStateCreation

struct DescriptorSetLayoutCreation
{
  //
  // A single descriptor binding.
  // It can be relative to one or more resources of the same type.
  //
  struct Binding
  {

    VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    uint16_t start = 0;
    uint16_t count = 0;
    const char* name = nullptr; // Comes from external memory.
  };                            // struct Binding

  Binding bindings[kMaxDescriptorsPerSet];
  uint32_t numBindings = 0;
  uint32_t setIndex = 0;

  const char* name = nullptr;

  // Building helpers
  DescriptorSetLayoutCreation& reset();
  DescriptorSetLayoutCreation& addBinding(const Binding& binding);
  DescriptorSetLayoutCreation& addBindingAtIndex(const Binding& binding, int index);
  DescriptorSetLayoutCreation& setName(const char* name);
  DescriptorSetLayoutCreation& setSetIndex(uint32_t index);

}; // struct DescriptorSetLayoutCreation

struct DescriptorSetCreation
{
  ResourceHandle resources[kMaxDescriptorsPerSet];
  SamplerHandle samplers[kMaxDescriptorsPerSet];
  uint16_t bindings[kMaxDescriptorsPerSet];

  DescriptorSetLayoutHandle layout;
  uint32_t numResources = 0;

  const char* name = nullptr;

  // Building helpers
  DescriptorSetCreation& reset();
  DescriptorSetCreation& setLayout(DescriptorSetLayoutHandle layout);
  DescriptorSetCreation& texture(TextureHandle texture, uint16_t binding);
  DescriptorSetCreation& buffer(BufferHandle buffer, uint16_t binding);
  DescriptorSetCreation& textureSampler(
      TextureHandle texture,
      SamplerHandle sampler,
      uint16_t binding); // TODO: separate samplers from textures
  DescriptorSetCreation& setName(const char* name);

}; // struct DescriptorSetCreation

struct DescriptorSetUpdate
{
  DescriptorSetHandle descriptorSet;

  uint32_t frameIssued = 0;
}; // DescriptorSetUpdate

struct VertexAttribute
{
  uint16_t location = 0;
  uint16_t binding = 0;
  uint32_t offset = 0;
  VertexComponentFormat::Enum format = VertexComponentFormat::kCount;

}; // struct VertexAttribute

struct VertexStream
{
  uint16_t binding = 0;
  uint16_t stride = 0;
  VertexInputRate::Enum inputRate = VertexInputRate::kCount;

}; // struct VertexStream

struct VertexInputCreation
{
  uint32_t numVertexStreams = 0;
  uint32_t numVertexAttributes = 0;

  VertexStream vertexStreams[kMaxVertexStreams];
  VertexAttribute vertexAttributes[kMaxVertexAttributes];

  VertexInputCreation& reset();
  VertexInputCreation& addVertexStream(const VertexStream& stream);
  VertexInputCreation& addVertexAttribute(const VertexAttribute& attribute);
}; // struct VertexInputCreation

struct RenderPassOutput
{
  VkFormat colorFormats[kMaxImageOutputs];
  VkFormat depthStencilFormat;
  uint32_t numColorFormats;

  RenderPassOperation::Enum colorOperation = RenderPassOperation::kDontCare;
  RenderPassOperation::Enum depthOperation = RenderPassOperation::kDontCare;
  RenderPassOperation::Enum stencilOperation = RenderPassOperation::kDontCare;

  RenderPassOutput& reset();
  RenderPassOutput& color(VkFormat format);
  RenderPassOutput& depth(VkFormat format);
  RenderPassOutput& setOperations(
      RenderPassOperation::Enum color,
      RenderPassOperation::Enum depth,
      RenderPassOperation::Enum stencil);

}; // struct RenderPassOutput

struct RenderPassCreation
{
  uint16_t numRenderTargets = 0;
  RenderPassType::Enum type = RenderPassType::kGeometry;

  TextureHandle outputTextures[kMaxImageOutputs];
  TextureHandle depthStencilTexture;

  float scaleX = 1.f;
  float scaleY = 1.f;
  uint8_t resize = 1;

  RenderPassOperation::Enum colorOperation = RenderPassOperation::kDontCare;
  RenderPassOperation::Enum depthOperation = RenderPassOperation::kDontCare;
  RenderPassOperation::Enum stencilOperation = RenderPassOperation::kDontCare;

  const char* name = nullptr;

  RenderPassCreation& reset();
  RenderPassCreation& addRenderTexture(TextureHandle texture);
  RenderPassCreation& setScaling(float scaleX, float scaleY, uint8_t resize);
  RenderPassCreation& setDepthStencilTexture(TextureHandle texture);
  RenderPassCreation& setName(const char* name);
  RenderPassCreation& setType(RenderPassType::Enum type);
  RenderPassCreation& setOperations(
      RenderPassOperation::Enum color,
      RenderPassOperation::Enum depth,
      RenderPassOperation::Enum stencil);

}; // struct RenderPassCreation

struct PipelineCreation
{
  RasterizationCreation rasterization;
  DepthStencilCreation depthStencil;
  BlendStateCreation blendState;
  VertexInputCreation vertexInput;
  ShaderStateCreation shaders;

  RenderPassOutput renderPass;
  DescriptorSetLayoutHandle descriptorSetLayouts[kMaxDescriptorSetLayouts];
  const ViewportState* viewport = nullptr;

  uint32_t numActiveLayouts = 0;

  const char* name = nullptr;

  PipelineCreation& addDescriptorSetLayout(DescriptorSetLayoutHandle handle);
  RenderPassOutput& renderPassOutput();

}; // struct PipelineCreation
//---------------------------------------------------------------------------//
// Helper methods for texture formats
//---------------------------------------------------------------------------//
namespace TextureFormat
{

inline bool isDepthStencil(VkFormat value)
{
  return value == VK_FORMAT_D16_UNORM_S8_UINT || value == VK_FORMAT_D24_UNORM_S8_UINT ||
         value == VK_FORMAT_D32_SFLOAT_S8_UINT;
}
inline bool isDepthOnly(VkFormat value)
{
  return value >= VK_FORMAT_D16_UNORM && value < VK_FORMAT_D32_SFLOAT;
}
inline bool isStencilOnly(VkFormat value) { return value == VK_FORMAT_S8_UINT; }

inline bool hasDepth(VkFormat value)
{
  return (value >= VK_FORMAT_D16_UNORM && value < VK_FORMAT_S8_UINT) ||
         (value >= VK_FORMAT_D16_UNORM_S8_UINT && value <= VK_FORMAT_D32_SFLOAT_S8_UINT);
}
inline bool hasStencil(VkFormat value)
{
  return value >= VK_FORMAT_S8_UINT && value <= VK_FORMAT_D32_SFLOAT_S8_UINT;
}
inline bool hasDepthOrStencil(VkFormat value)
{
  return value >= VK_FORMAT_D16_UNORM && value <= VK_FORMAT_D32_SFLOAT_S8_UINT;
}

} // namespace TextureFormat

struct ResourceData
{
  void* data = nullptr;

}; // struct ResourceData

struct ResourceBinding
{
  uint16_t type = 0; // ResourceType
  uint16_t start = 0;
  uint16_t count = 0;
  uint16_t set = 0;

  const char* name = nullptr;
}; // struct ResourceBinding

/// Resources descriptions:

struct ShaderStateDescription
{
  void* nativeHandle = nullptr;
  const char* name = nullptr;

}; // struct ShaderStateDescription

struct BufferDescription
{
  void* nativeHandle = nullptr;
  const char* name = nullptr;

  VkBufferUsageFlags typeFlags = 0;
  ResourceUsageType::Enum usage = ResourceUsageType::kImmutable;
  uint32_t size = 0;
  BufferHandle parentHandle;

}; // struct BufferDescription

struct TextureDescription
{
  void* nativeHandle = nullptr;
  const char* name = nullptr;

  uint16_t width = 1;
  uint16_t height = 1;
  uint16_t depth = 1;
  uint8_t mipmaps = 1;
  uint8_t renderTarget = 0;
  uint8_t computeAccess = 0;

  VkFormat format = VK_FORMAT_UNDEFINED;
  TextureType::Enum type = TextureType::kTexture2D;

}; // struct Texture

struct SamplerDescription
{
  const char* name = nullptr;

  VkFilter minFilter = VK_FILTER_NEAREST;
  VkFilter magFilter = VK_FILTER_NEAREST;
  VkSamplerMipmapMode mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

  VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

}; // struct SamplerDescription

struct DescriptorSetLayoutDescription
{
  ResourceBinding bindings[kMaxDescriptorsPerSet];
  uint32_t numActiveBindings = 0;

}; // struct DescriptorSetLayoutDescription

struct DesciptorSetDescription
{
  ResourceData resources[kMaxDescriptorsPerSet];
  uint32_t numActiveResources = 0;

}; // struct DesciptorSetDescription

struct PipelineDescription
{
  ShaderStateHandle shader;

}; // struct PipelineDescription
//---------------------------------------------------------------------------//
struct MapBufferParameters
{
  BufferHandle buffer;
  uint32_t offset = 0;
  uint32_t size = 0;

}; // struct MapBufferParameters
//---------------------------------------------------------------------------//
// Synchronization resources
//---------------------------------------------------------------------------//
struct ImageBarrier
{
  TextureHandle texture;

}; // struct ImageBarrier

struct MemoryBarrier
{
  BufferHandle buffer;

}; // struct MemoryBarrier

struct ExecutionBarrier
{
  PipelineStage::Enum sourcePipelineStage;
  PipelineStage::Enum destinationPipelineStage;

  uint32_t newBarrierExperimental = UINT32_MAX;
  uint32_t loadOperation = 0;

  uint32_t numImageBarriers;
  uint32_t numMemoryBarriers;

  ImageBarrier imageBarriers[8];
  MemoryBarrier memoryBarriers[8];

  ExecutionBarrier& reset();
  ExecutionBarrier& set(PipelineStage::Enum source, PipelineStage::Enum destination);
  ExecutionBarrier& addImageBarrier(const ImageBarrier& imageBarrier);
  ExecutionBarrier& addMemoryBarrier(const MemoryBarrier& memoryBarrier);

}; // struct Barrier

struct ResourceUpdate
{
  ResourceDeletionType::Enum type;
  ResourceHandle handle;
  uint32_t currentFrame;
}; // struct ResourceUpdate
//---------------------------------------------------------------------------//
// Device Resources:
//---------------------------------------------------------------------------//
static const uint32_t kMaxSwapchainImages = 3;

struct DeviceStateVulkan;

struct Buffer
{
  VkBuffer vkBuffer;
  VmaAllocation vmaAllocation;
  VkDeviceMemory vkDeviceMemory;
  VkDeviceSize vkDeviceSize;

  VkBufferUsageFlags typeFlags = 0;
  ResourceUsageType::Enum usage = ResourceUsageType::kImmutable;
  uint32_t size = 0;
  uint32_t globalOffset = 0; // Offset into global constant, if dynamic

  BufferHandle handle;
  BufferHandle parentBuffer;

  const char* name = nullptr;

}; // struct BufferVulkan

struct Sampler
{
  VkSampler vkSampler;

  VkFilter minFilter = VK_FILTER_NEAREST;
  VkFilter magFilter = VK_FILTER_NEAREST;
  VkSamplerMipmapMode mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

  VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

  const char* name = nullptr;

}; // struct SamplerVulkan

struct Texture
{
  VkImage vkImage;
  VkImageView vkImageView;
  VkFormat vkFormat;
  VkImageLayout vkImageLayout;
  VmaAllocation vmaAllocation;

  uint16_t width = 1;
  uint16_t height = 1;
  uint16_t depth = 1;
  uint8_t mipmaps = 1;
  uint8_t flags = 0;

  TextureHandle handle;
  TextureType::Enum type = TextureType::kTexture2D;

  Sampler* sampler = nullptr;

  const char* name = nullptr;
}; // struct TextureVulkan

struct ShaderState
{
  VkPipelineShaderStageCreateInfo shaderStageInfo[kMaxShaderStages];

  const char* name = nullptr;

  uint32_t activeShaders = 0;
  bool graphicsPipeline = false;

  Spirv::ParseResult* parseResult;
}; // struct ShaderStateVulkan

struct DescriptorBinding
{
  VkDescriptorType type;
  uint16_t start = 0;
  uint16_t count = 0;
  uint16_t set = 0;

  const char* name = nullptr;
}; // struct ResourceBindingVulkan

struct DesciptorSetLayout
{
  VkDescriptorSetLayout vkDescriptorSetLayout;

  VkDescriptorSetLayoutBinding* vkBinding = nullptr;
  DescriptorBinding* bindings = nullptr;
  uint16_t numBindings = 0;
  uint16_t setIndex = 0;

  DescriptorSetLayoutHandle handle;

}; // struct DesciptorSetLayoutVulkan

struct DesciptorSet
{
  VkDescriptorSet vkDescriptorSet;

  ResourceHandle* resources = nullptr;
  SamplerHandle* samplers = nullptr;
  uint16_t* bindings = nullptr;

  const DesciptorSetLayout* layout = nullptr;
  uint32_t numResources = 0;
}; // struct DesciptorSetVulkan

struct Pipeline
{
  VkPipeline vkPipeline;
  VkPipelineLayout vkPipelineLayout;

  VkPipelineBindPoint vkBindPoint;

  ShaderStateHandle shaderState;

  const DesciptorSetLayout* descriptorSetLayout[kMaxDescriptorSetLayouts];
  DescriptorSetLayoutHandle descriptorSetLayoutHandle[kMaxDescriptorSetLayouts];
  uint32_t numActiveLayouts = 0;

  DepthStencilCreation depthStencil;
  BlendStateCreation blendState;
  RasterizationCreation rasterization;

  PipelineHandle handle;
  bool graphicsPipeline = true;

}; // struct PipelineVulkan

struct RenderPass
{
  VkRenderPass vkRenderPass;
  VkFramebuffer vkFrameBuffer;

  RenderPassOutput output;

  TextureHandle outputTextures[kMaxImageOutputs];
  TextureHandle outputDepth;

  RenderPassType::Enum type;

  float scaleX = 1.f;
  float scaleY = 1.f;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t dispatchX = 0;
  uint16_t dispatchY = 0;
  uint16_t dispatchZ = 0;

  uint8_t resize = 0;
  uint8_t numRenderTargets = 0;

  const char* name = nullptr;
}; // struct RenderPassVulkan
//---------------------------------------------------------------------------//
// Enum translations. Use tables or switches depending on the case.
//---------------------------------------------------------------------------//
static const char* toCompilerExtension(VkShaderStageFlagBits value)
{
  switch (value)
  {
  case VK_SHADER_STAGE_VERTEX_BIT:
    return "vert";
  case VK_SHADER_STAGE_FRAGMENT_BIT:
    return "frag";
  case VK_SHADER_STAGE_COMPUTE_BIT:
    return "comp";
  default:
    return "";
  }
}

//
static const char* toStageDefines(VkShaderStageFlagBits value)
{
  switch (value)
  {
  case VK_SHADER_STAGE_VERTEX_BIT:
    return "VERTEX";
  case VK_SHADER_STAGE_FRAGMENT_BIT:
    return "FRAGMENT";
  case VK_SHADER_STAGE_COMPUTE_BIT:
    return "COMPUTE";
  default:
    return "";
  }
}

static VkImageType toVkImageType(TextureType::Enum type)
{
  static VkImageType kVkTarget[TextureType::kCount] = {
      VK_IMAGE_TYPE_1D,
      VK_IMAGE_TYPE_2D,
      VK_IMAGE_TYPE_3D,
      VK_IMAGE_TYPE_1D,
      VK_IMAGE_TYPE_2D,
      VK_IMAGE_TYPE_3D};
  return kVkTarget[type];
}

static VkImageViewType toVkImageViewType(TextureType::Enum type)
{
  static VkImageViewType kVkData[] = {
      VK_IMAGE_VIEW_TYPE_1D,
      VK_IMAGE_VIEW_TYPE_2D,
      VK_IMAGE_VIEW_TYPE_3D,
      VK_IMAGE_VIEW_TYPE_1D_ARRAY,
      VK_IMAGE_VIEW_TYPE_2D_ARRAY,
      VK_IMAGE_VIEW_TYPE_CUBE_ARRAY};
  return kVkData[type];
}

static VkFormat toVkVertexFormat(VertexComponentFormat::Enum value)
{
  // Float, Float2, Float3, Float4, Mat4, Byte, Byte4N, UByte, UByte4N, Short2, Short2N, Short4,
  // Short4N, Uint, Uint2, Uint4, Count
  static VkFormat kVkVertexFormats[VertexComponentFormat::kCount] = {
      VK_FORMAT_R32_SFLOAT,
      VK_FORMAT_R32G32_SFLOAT,
      VK_FORMAT_R32G32B32_SFLOAT,
      VK_FORMAT_R32G32B32A32_SFLOAT,
      /*MAT4 TODO*/ VK_FORMAT_R32G32B32A32_SFLOAT,
      VK_FORMAT_R8_SINT,
      VK_FORMAT_R8G8B8A8_SNORM,
      VK_FORMAT_R8_UINT,
      VK_FORMAT_R8G8B8A8_UINT,
      VK_FORMAT_R16G16_SINT,
      VK_FORMAT_R16G16_SNORM,
      VK_FORMAT_R16G16B16A16_SINT,
      VK_FORMAT_R16G16B16A16_SNORM,
      VK_FORMAT_R32_UINT,
      VK_FORMAT_R32G32_UINT,
      VK_FORMAT_R32G32B32A32_UINT};

  return kVkVertexFormats[value];
}

static VkPipelineStageFlags toVkPipelineStage(PipelineStage::Enum value)
{
  static VkPipelineStageFlags kVkValues[] = {
      VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT};
  return kVkValues[value];
}

static VkAccessFlags utilToVkAccessFlags(ResourceState state)
{
  VkAccessFlags ret = 0;
  if (state & RESOURCE_STATE_COPY_SOURCE)
  {
    ret |= VK_ACCESS_TRANSFER_READ_BIT;
  }
  if (state & RESOURCE_STATE_COPY_DEST)
  {
    ret |= VK_ACCESS_TRANSFER_WRITE_BIT;
  }
  if (state & RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
  {
    ret |= VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
  }
  if (state & RESOURCE_STATE_INDEX_BUFFER)
  {
    ret |= VK_ACCESS_INDEX_READ_BIT;
  }
  if (state & RESOURCE_STATE_UNORDERED_ACCESS)
  {
    ret |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  }
  if (state & RESOURCE_STATE_INDIRECT_ARGUMENT)
  {
    ret |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
  }
  if (state & RESOURCE_STATE_RENDER_TARGET)
  {
    ret |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  }
  if (state & RESOURCE_STATE_DEPTH_WRITE)
  {
    ret |=
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  }
  if (state & RESOURCE_STATE_SHADER_RESOURCE)
  {
    ret |= VK_ACCESS_SHADER_READ_BIT;
  }
  if (state & RESOURCE_STATE_PRESENT)
  {
    ret |= VK_ACCESS_MEMORY_READ_BIT;
  }

  return ret;
}

static VkImageLayout utilToVkImageLayout(ResourceState usage)
{
  if (usage & RESOURCE_STATE_COPY_SOURCE)
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  if (usage & RESOURCE_STATE_COPY_DEST)
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

  if (usage & RESOURCE_STATE_RENDER_TARGET)
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  if (usage & RESOURCE_STATE_DEPTH_WRITE)
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  if (usage & RESOURCE_STATE_DEPTH_READ)
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

  if (usage & RESOURCE_STATE_UNORDERED_ACCESS)
    return VK_IMAGE_LAYOUT_GENERAL;

  if (usage & RESOURCE_STATE_SHADER_RESOURCE)
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  if (usage & RESOURCE_STATE_PRESENT)
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  if (usage == RESOURCE_STATE_COMMON)
    return VK_IMAGE_LAYOUT_GENERAL;

  return VK_IMAGE_LAYOUT_UNDEFINED;
}

// Determines pipeline stages involved for given accesses
static VkPipelineStageFlags
utilDeterminePipelineStageFlags(VkAccessFlags accessFlags, QueueType::Enum queueType)
{
  VkPipelineStageFlags flags = 0;

  switch (queueType)
  {
  case QueueType::kGraphics: {
    if ((accessFlags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0)
      flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;

    if ((accessFlags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                        VK_ACCESS_SHADER_WRITE_BIT)) != 0)
    {
      flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
      flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }

    if ((accessFlags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0)
      flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    if ((accessFlags &
         (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0)
      flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    if ((accessFlags & (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
      flags |=
          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

    break;
  }
  case QueueType::kCompute: {
    if ((accessFlags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0 ||
        (accessFlags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0 ||
        (accessFlags &
         (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0 ||
        (accessFlags & (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
      return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    if ((accessFlags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                        VK_ACCESS_SHADER_WRITE_BIT)) != 0)
      flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    break;
  }
  case QueueType::kCopyTransfer:
    return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  default:
    break;
  }

  // Compatible with both compute and graphics queues
  if ((accessFlags & VK_ACCESS_INDIRECT_COMMAND_READ_BIT) != 0)
    flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;

  if ((accessFlags & (VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)) != 0)
    flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;

  if ((accessFlags & (VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT)) != 0)
    flags |= VK_PIPELINE_STAGE_HOST_BIT;

  if (flags == 0)
    flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

  return flags;
}
//---------------------------------------------------------------------------//
} // namespace Graphics
