#pragma once

#include "Foundation/Prerequisites.hpp"
#include "Graphics/GpuEnum.hpp"

// DON'T!!! ENDS UP WITH A CIRCULAR INCLUDE
// with following error msg: "unknown override specifier"
//#include "Graphics/GpuDevice.hpp"

#include <vulkan/vulkan.h>
#include "Externals/vk_mem_alloc.h"

#if !defined(SHADER_FOLDER)
#  define SHADER_FOLDER "\\Shaders\\"
#endif

namespace Graphics
{
// Forward declare
struct GpuDevice;
namespace Spirv
{
struct ParseResult;
}

static const uint32_t kInvalidIndex = 0xffffffff;

static const uint32_t kBuffersPoolSize = 16384;
static const uint32_t kTexturesPoolSize = 512;
static const uint32_t kRenderPassesPoolSize = 256;
static const uint32_t kDescriptorSetLayoutsPoolSize = 128;
static const uint32_t kPipelinesPoolSize = 128;
static const uint32_t kShadersPoolSize = 128;
static const uint32_t kDescriptorSetsPoolSize = 4096;
static const uint32_t kSamplersPoolSize = 32;

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

struct FramebufferHandle
{
  ResourceHandle index;
};
struct PagePoolHandle
{
  ResourceHandle index;
}; // struct FramebufferHandle
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
static FramebufferHandle kInvalidFramebuffer{kInvalidIndex};
static PagePoolHandle k_invalid_page_pool{kInvalidIndex};
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
  uint32_t persistent = 0;
  uint32_t deviceOnly = 0;
  void* initialData = nullptr;

  const char* name = nullptr;

  BufferCreation& reset();
  BufferCreation& set(VkBufferUsageFlags flags, ResourceUsageType::Enum usage, uint32_t size);
  BufferCreation& setData(void* data);
  BufferCreation& setName(const char* name);
  BufferCreation& setPersistent(bool value);
  BufferCreation& setDeviceOnly(bool value);

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

  TextureHandle alias = kInvalidTexture;

  const char* name = nullptr;

  TextureCreation& setSize(uint16_t width, uint16_t height, uint16_t depth);
  TextureCreation& setFlags(uint8_t flags);
  TextureCreation& setFormatType(VkFormat format, TextureType::Enum type);
  TextureCreation& setName(const char* name);
  TextureCreation& setData(void* data);
  TextureCreation& setAlias(TextureHandle alias);

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
    uint16_t index = 0;
    uint16_t count = 0;
    const char* name = nullptr; // Comes from external memory.
  };                            // struct Binding

  Binding bindings[kMaxDescriptorsPerSet];
  uint32_t numBindings = 0;
  uint32_t setIndex = 0;
  bool bindless = false;
  bool dynamic = false;

  const char* name = nullptr;

  // Building helpers
  DescriptorSetLayoutCreation& reset();
  DescriptorSetLayoutCreation& addBinding(const Binding& binding);
  DescriptorSetLayoutCreation&
  addBinding(VkDescriptorType type, uint32_t index, uint32_t count, const char* name);
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
  VkImageLayout colorFinalLayouts[kMaxImageOutputs];
  RenderPassOperation::Enum colorOperations[kMaxImageOutputs];

  VkFormat depthStencilFormat;
  VkImageLayout depthStencilFinalLayout;

  uint32_t numColorFormats;

  RenderPassOperation::Enum depthOperation = RenderPassOperation::kDontCare;
  RenderPassOperation::Enum stencilOperation = RenderPassOperation::kDontCare;

  RenderPassOutput& reset();
  RenderPassOutput& color(VkFormat format, VkImageLayout layout, RenderPassOperation::Enum loadOp);
  RenderPassOutput& depth(VkFormat format, VkImageLayout layout);
  RenderPassOutput&
  setDepthStencilOperations(RenderPassOperation::Enum depth, RenderPassOperation::Enum stencil);

}; // struct RenderPassOutput

struct RenderPassCreation
{
  uint16_t numRenderTargets = 0;

  VkFormat colorFormats[kMaxImageOutputs];
  VkImageLayout colorFinalLayouts[kMaxImageOutputs];
  RenderPassOperation::Enum colorOperations[kMaxImageOutputs];

  VkFormat depthStencilFormat = VK_FORMAT_UNDEFINED;
  VkImageLayout depthStencilFinalLayout;

  uint32_t shading_rate_image_index = kInvalidIndex;

  RenderPassOperation::Enum depthOperation = RenderPassOperation::kDontCare;
  RenderPassOperation::Enum stencilOperation = RenderPassOperation::kDontCare;

  const char* name = nullptr;

  RenderPassCreation& reset();
  RenderPassCreation&
  addAttachment(VkFormat format, VkImageLayout layout, RenderPassOperation::Enum loadOp);
  RenderPassCreation& setDepthStencilTexture(VkFormat format, VkImageLayout layout);
  RenderPassCreation& setName(const char* name);
  RenderPassCreation&
  setDepthStencilOperations(RenderPassOperation::Enum depth, RenderPassOperation::Enum stencil);

}; // struct RenderPassCreation

struct PipelineCreation
{
  RasterizationCreation rasterization;
  DepthStencilCreation depthStencil;
  BlendStateCreation blendState;
  VertexInputCreation vertexInput;
  ShaderStateCreation shaders;

  VkPrimitiveTopology topology;

  RenderPassOutput renderPass;
  DescriptorSetLayoutHandle descriptorSetLayouts[kMaxDescriptorSetLayouts];
  const ViewportState* viewport = nullptr;

  uint32_t numActiveLayouts = 0;

  const char* name = nullptr;

  PipelineCreation& addDescriptorSetLayout(DescriptorSetLayoutHandle handle);
  RenderPassOutput& renderPassOutput();

}; // struct PipelineCreation

struct FramebufferCreation
{

  RenderPassHandle renderPass;

  uint16_t numRenderTargets = 0;

  TextureHandle outputTextures[kMaxImageOutputs];
  TextureHandle depthStencilTexture = {kInvalidIndex};

  uint16_t width = 0;
  uint16_t height = 0;

  float scaleX = 1.f;
  float scaleY = 1.f;
  uint8_t resize = 1;

  const char* name = nullptr;

  FramebufferCreation& reset();
  FramebufferCreation& addRenderTexture(TextureHandle texture);
  FramebufferCreation& setDepthStencilTexture(TextureHandle texture);
  FramebufferCreation& setScaling(float scaleX, float scaleY, uint8_t resize);
  FramebufferCreation& setName(const char* name);

}; // struct FramebufferCreation

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
  ResourceUpdateType::Enum type;
  ResourceHandle handle;
  uint32_t currentFrame;
  uint32_t deleting;
}; // struct ResourceUpdate
//---------------------------------------------------------------------------//
// Device Resources:
//---------------------------------------------------------------------------//
static const uint32_t k_max_swapchain_images = 3;
static const uint32_t k_max_frames = 1;

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

  bool ready = true;

  uint8_t* mappedData = nullptr;
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
  VmaAllocation vmaAllocation;
  ResourceState state = RESOURCE_STATE_UNDEFINED;

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
  uint16_t index = 0;
  uint16_t count = 0;
  uint16_t set = 0;

  const char* name = nullptr;
}; // struct ResourceBindingVulkan

struct DescriptorSetLayout
{
  VkDescriptorSetLayout vkDescriptorSetLayout;

  VkDescriptorSetLayoutBinding* vkBinding = nullptr;
  DescriptorBinding* bindings = nullptr;
  uint8_t* indexToBinding = nullptr; // Mapping between binding point and binding data.
  uint16_t numBindings = 0;
  uint16_t setIndex = 0;

  uint8_t bindless = 0;
  uint8_t dynamic = 0;

  DescriptorSetLayoutHandle handle;

}; // struct DesciptorSetLayoutVulkan

struct DescriptorSet
{
  VkDescriptorSet vkDescriptorSet;

  ResourceHandle* resources = nullptr;
  SamplerHandle* samplers = nullptr;
  uint16_t* bindings = nullptr;

  const DescriptorSetLayout* layout = nullptr;
  uint32_t numResources = 0;
}; // struct DesciptorSetVulkan

struct Pipeline
{
  VkPipeline vkPipeline;
  VkPipelineLayout vkPipelineLayout;

  VkPipelineBindPoint vkBindPoint;

  ShaderStateHandle shaderState;

  const DescriptorSetLayout* descriptorSetLayout[kMaxDescriptorSetLayouts];
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

  RenderPassOutput output;

  uint16_t dispatchX = 0;
  uint16_t dispatchY = 0;
  uint16_t dispatchZ = 0;

  uint8_t numRenderTargets = 0;

  const char* name = nullptr;
}; // struct RenderPassVulkan

struct Framebuffer
{

  // NOTE: this will be a null handle if dynamic rendering is available
  VkFramebuffer vkFramebuffer;

  // NOTE: cache render pass handle
  RenderPassHandle renderPass;

  float scaleX = 1.f;
  float scaleY = 1.f;
  uint16_t width = 0;
  uint16_t height = 0;

  TextureHandle colorAttachments[kMaxImageOutputs];
  TextureHandle depthStencilAttachment;
  uint32_t numColorAttachments;

  uint8_t resize = 0;

  const char* name = nullptr;
}; // struct Framebuffer

struct ComputeLocalSize
{
  uint32_t x : 10;
  uint32_t y : 10;
  uint32_t z : 10;
  uint32_t pad : 2;
}; // struct ComputeLocalSize

//---------------------------------------------------------------------------//
// Enum translations. Use tables or switches depending on the case.
//---------------------------------------------------------------------------//
const char* toCompilerExtension(VkShaderStageFlagBits value);

//
const char* toStageDefines(VkShaderStageFlagBits value);

VkImageType toVkImageType(TextureType::Enum type);

VkImageViewType toVkImageViewType(TextureType::Enum type);

VkFormat toVkVertexFormat(VertexComponentFormat::Enum value);

VkPipelineStageFlags toVkPipelineStage(PipelineStage::Enum value);

VkAccessFlags utilToVkAccessFlags(ResourceState state);

VkAccessFlags utilToVkAccessFlags2(ResourceState state);

VkImageLayout utilToVkImageLayout(ResourceState usage);

VkImageLayout utilToVkImageLayout2(ResourceState usage);
// Determines pipeline stages involved for given accesses
VkPipelineStageFlags
utilDeterminePipelineStageFlags(VkAccessFlags accessFlags, QueueType::Enum queueType);

VkPipelineStageFlags2KHR
utilDeterminePipelineStageFlags2(VkAccessFlags2KHR p_AccessFlags, QueueType::Enum p_QueueType);
//---------------------------------------------------------------------------//
void utilAddImageBarrier(
    GpuDevice* p_GpuDevice,
    VkCommandBuffer p_Cmdbuf,
    VkImage p_Image,
    ResourceState p_OldState,
    ResourceState p_NewState,
    uint32_t p_BaseMipLevel,
    uint32_t p_MipCount,
    bool p_IsDepth);

void utilAddImageBarrier(
    GpuDevice* gpu,
    VkCommandBuffer cmdbuf,
    Texture* texture,
    ResourceState newState,
    uint32_t baseMipLevel,
    uint32_t mipCount,
    bool isDepth);

void utilAddImageBarrierExt(
    GpuDevice* gpu,
    VkCommandBuffer cmdbuf,
    VkImage image,
    ResourceState oldState,
    ResourceState newState,
    uint32_t baseMipLevel,
    uint32_t mipCount,
    bool isDepth,
    uint32_t sourceFamily,
    uint32_t destinationFamily,
    QueueType::Enum sourceQueueType,
    QueueType::Enum destinationQueueType);

void utilAddImageBarrierExt(
    GpuDevice* gpu,
    VkCommandBuffer cmdbuf,
    Texture* texture,
    ResourceState newState,
    uint32_t baseMipLevel,
    uint32_t mipCount,
    bool isDepth,
    uint32_t sourceFamily,
    uint32_t destinationFamily,
    QueueType::Enum sourceQueueType,
    QueueType::Enum destinationQueueType);

void utilAddBufferBarrierExt(
    GpuDevice* gpu,
    VkCommandBuffer cmdbuf,
    VkBuffer buffer,
    ResourceState oldState,
    ResourceState newState,
    uint32_t buffer_size,
    uint32_t sourceFamily,
    uint32_t destinationFamily,
    QueueType::Enum sourceQueueType,
    QueueType::Enum destinationQueueType);

//---------------------------------------------------------------------------//
VkFormat utilStringToVkFormat(const char* p_Format);
//---------------------------------------------------------------------------//
} // namespace Graphics