#include "GpuResources.hpp"
#include "Graphics/GpuDevice.hpp"

namespace Graphics
{
//---------------------------------------------------------------------------//
/// DepthStencilCreation

DepthStencilCreation& DepthStencilCreation::setDepth(bool write, VkCompareOp comparisonTest)
{
  depthWriteEnable = write;
  depthComparison = comparisonTest;
  // Setting depth like this means we want to use the depth test.
  depthEnable = 1;

  return *this;
}
//---------------------------------------------------------------------------//
/// BlendState
BlendState&
BlendState::setColor(VkBlendFactor source, VkBlendFactor destination, VkBlendOp operation)
{
  sourceColor = source;
  destinationColor = destination;
  colorOperation = operation;
  blendEnabled = 1;

  return *this;
}

BlendState&
BlendState::setAlpha(VkBlendFactor source, VkBlendFactor destination, VkBlendOp operation)
{
  sourceAlpha = source;
  destinationAlpha = destination;
  alphaOperation = operation;
  separateBlend = 1;

  return *this;
}

BlendState& BlendState::setColorWriteMask(ColorWriteEnabled::Mask value)
{
  colorWriteMask = value;

  return *this;
}
//---------------------------------------------------------------------------//
/// BlendStateCreation
BlendStateCreation& BlendStateCreation::reset()
{
  activeStates = 0;

  return *this;
}

BlendState& BlendStateCreation::addBlendState() { return blendStates[activeStates++]; }
//---------------------------------------------------------------------------//
/// BufferCreation
BufferCreation& BufferCreation::reset()
{
  size = 0;
  initialData = nullptr;

  return *this;
}

BufferCreation&
BufferCreation::set(VkBufferUsageFlags p_Flags, ResourceUsageType::Enum p_Usage, uint32_t p_Size)
{
  typeFlags = p_Flags;
  usage = p_Usage;
  size = p_Size;

  return *this;
}

BufferCreation& BufferCreation::setData(void* p_Data)
{
  initialData = p_Data;

  return *this;
}

BufferCreation& BufferCreation::setName(const char* p_Name)
{
  name = p_Name;

  return *this;
}

BufferCreation& BufferCreation::setPersistent(bool value)
{
  persistent = value ? 1 : 0;
  return *this;
}

BufferCreation& BufferCreation::setDeviceOnly(bool value)
{
  deviceOnly = value ? 1 : 0;
  return *this;
}

//---------------------------------------------------------------------------//
/// TextureCreation
TextureCreation& TextureCreation::setSize(uint16_t p_Width, uint16_t p_Height, uint16_t p_Depth)
{
  width = p_Width;
  height = p_Height;
  depth = p_Depth;

  return *this;
}

TextureCreation& TextureCreation::setFlags(uint8_t p_Mipmaps, uint8_t p_Flags)
{
  mipmaps = p_Mipmaps;
  flags = p_Flags;

  return *this;
}

TextureCreation& TextureCreation::setFormatType(VkFormat p_Format, TextureType::Enum p_Type)
{
  format = p_Format;
  type = p_Type;

  return *this;
}

TextureCreation& TextureCreation::setName(const char* p_Name)
{
  name = p_Name;
  return *this;
}

TextureCreation& TextureCreation::setData(void* p_Data)
{
  initialData = p_Data;
  return *this;
}

TextureCreation& TextureCreation::setAlias(TextureHandle p_Alias)
{
  alias = p_Alias;
  return *this;
}
//---------------------------------------------------------------------------//
/// SamplerCreation
SamplerCreation& SamplerCreation::setMinMagMip(VkFilter min, VkFilter mag, VkSamplerMipmapMode mip)
{
  minFilter = min;
  magFilter = mag;
  mipFilter = mip;

  return *this;
}

SamplerCreation& SamplerCreation::setAddressModeU(VkSamplerAddressMode u)
{
  addressModeU = u;

  return *this;
}

SamplerCreation& SamplerCreation::setAddressModeUV(VkSamplerAddressMode u, VkSamplerAddressMode v)
{
  addressModeU = u;
  addressModeV = v;

  return *this;
}

SamplerCreation& SamplerCreation::setAddressModeUVW(
    VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w)
{
  addressModeU = u;
  addressModeV = v;
  addressModeW = w;

  return *this;
}

SamplerCreation& SamplerCreation::setName(const char* name_)
{
  name = name_;

  return *this;
}
//---------------------------------------------------------------------------//
/// ShaderStateCreation
ShaderStateCreation& ShaderStateCreation::reset()
{
  stagesCount = 0;

  return *this;
}

ShaderStateCreation& ShaderStateCreation::setName(const char* name_)
{
  name = name_;

  return *this;
}

ShaderStateCreation&
ShaderStateCreation::addStage(const char* code, uint32_t codeSize, VkShaderStageFlagBits type)
{
  stages[stagesCount].code = code;
  stages[stagesCount].codeSize = codeSize;
  stages[stagesCount].type = type;
  ++stagesCount;

  return *this;
}

ShaderStateCreation& ShaderStateCreation::setSpvInput(bool value)
{
  spvInput = value;
  return *this;
}
//---------------------------------------------------------------------------//
/// DescriptorSetLayoutCreation
DescriptorSetLayoutCreation& DescriptorSetLayoutCreation::reset()
{
  numBindings = 0;
  setIndex = 0;
  return *this;
}

DescriptorSetLayoutCreation& DescriptorSetLayoutCreation::addBinding(const Binding& binding)
{
  bindings[numBindings++] = binding;
  return *this;
}
DescriptorSetLayoutCreation& DescriptorSetLayoutCreation::addBinding(
    VkDescriptorType type, uint32_t index, uint32_t count, const char* name)
{
  bindings[numBindings++] = {type, (uint16_t)index, (uint16_t)count, name};
  return *this;
}

DescriptorSetLayoutCreation&
DescriptorSetLayoutCreation::addBindingAtIndex(const Binding& binding, int index)
{
  bindings[index] = binding;
  numBindings = (index + 1) > numBindings ? (index + 1) : numBindings;
  return *this;
}
DescriptorSetLayoutCreation& DescriptorSetLayoutCreation::setName(const char* name_)
{
  name = name_;
  return *this;
}

DescriptorSetLayoutCreation& DescriptorSetLayoutCreation::setSetIndex(uint32_t index)
{
  setIndex = index;
  return *this;
}
//---------------------------------------------------------------------------//
/// DescriptorSetCreation
DescriptorSetCreation& DescriptorSetCreation::reset()
{
  numResources = 0;
  return *this;
}

DescriptorSetCreation& DescriptorSetCreation::setLayout(DescriptorSetLayoutHandle layout_)
{
  layout = layout_;
  return *this;
}

DescriptorSetCreation& DescriptorSetCreation::texture(TextureHandle texture, uint16_t binding)
{
  // Set a default sampler
  samplers[numResources] = kInvalidSampler;
  bindings[numResources] = binding;
  resources[numResources++] = texture.index;
  return *this;
}

DescriptorSetCreation& DescriptorSetCreation::buffer(BufferHandle buffer, uint16_t binding)
{
  samplers[numResources] = kInvalidSampler;
  bindings[numResources] = binding;
  resources[numResources++] = buffer.index;
  return *this;
}

DescriptorSetCreation& DescriptorSetCreation::textureSampler(
    TextureHandle texture, SamplerHandle sampler, uint16_t binding)
{
  bindings[numResources] = binding;
  resources[numResources] = texture.index;
  samplers[numResources++] = sampler;
  return *this;
}

DescriptorSetCreation& DescriptorSetCreation::setName(const char* name_)
{
  name = name_;
  return *this;
}
//---------------------------------------------------------------------------//
/// VertexInputCreation
VertexInputCreation& VertexInputCreation::reset()
{
  numVertexStreams = numVertexAttributes = 0;
  return *this;
}

VertexInputCreation& VertexInputCreation::addVertexStream(const VertexStream& stream)
{
  vertexStreams[numVertexStreams++] = stream;
  return *this;
}

VertexInputCreation& VertexInputCreation::addVertexAttribute(const VertexAttribute& attribute)
{
  vertexAttributes[numVertexAttributes++] = attribute;
  return *this;
}
//---------------------------------------------------------------------------//
/// RenderPassOutput
RenderPassOutput& RenderPassOutput::reset()
{
  numColorFormats = 0;
  for (uint32_t i = 0; i < kMaxImageOutputs; ++i)
  {
    colorFormats[i] = VK_FORMAT_UNDEFINED;
    colorFinalLayouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    colorOperations[i] = RenderPassOperation::kDontCare;
  }
  depthStencilFormat = VK_FORMAT_UNDEFINED;
  depthOperation = stencilOperation = RenderPassOperation::kDontCare;
  return *this;
}

RenderPassOutput&
RenderPassOutput::color(VkFormat format, VkImageLayout layout, RenderPassOperation::Enum loadOp)
{
  colorFormats[numColorFormats] = format;
  colorOperations[numColorFormats] = loadOp;
  colorFinalLayouts[numColorFormats++] = layout;
  return *this;
}

RenderPassOutput& RenderPassOutput::depth(VkFormat format, VkImageLayout layout)
{
  depthStencilFormat = format;
  depthStencilFinalLayout = layout;
  return *this;
}

RenderPassOutput& RenderPassOutput::setDepthStencilOperations(
    RenderPassOperation::Enum p_Depth, RenderPassOperation::Enum p_Stencil)
{
  depthOperation = p_Depth;
  stencilOperation = p_Stencil;

  return *this;
}
//---------------------------------------------------------------------------//
/// PipelineCreation
PipelineCreation& PipelineCreation::addDescriptorSetLayout(DescriptorSetLayoutHandle handle)
{
  descriptorSetLayouts[numActiveLayouts++] = handle;
  return *this;
}
//---------------------------------------------------------------------------//
RenderPassOutput& PipelineCreation::renderPassOutput() { return renderPass; }
//---------------------------------------------------------------------------//
FramebufferCreation& FramebufferCreation::reset()
{
  numRenderTargets = 0;
  name = nullptr;
  depthStencilTexture.index = kInvalidIndex;

  resize = 0;
  scaleX = 1.f;
  scaleY = 1.f;

  return *this;
}
//---------------------------------------------------------------------------//
FramebufferCreation& FramebufferCreation::addRenderTexture(TextureHandle p_Texture)
{
  outputTextures[numRenderTargets++] = p_Texture;

  return *this;
}
//---------------------------------------------------------------------------//
FramebufferCreation& FramebufferCreation::setDepthStencilTexture(TextureHandle p_Texture)
{
  depthStencilTexture = p_Texture;

  return *this;
}
//---------------------------------------------------------------------------//
FramebufferCreation&
FramebufferCreation::setScaling(float p_ScaleX, float p_ScaleY, uint8_t p_Resize)
{
  scaleX = p_ScaleX;
  scaleY = p_ScaleY;
  resize = p_Resize;

  return *this;
}
//---------------------------------------------------------------------------//
FramebufferCreation& FramebufferCreation::setName(const char* p_Name)
{
  name = p_Name;

  return *this;
}
//---------------------------------------------------------------------------//
/// RenderPassCreation
RenderPassCreation& RenderPassCreation::reset()
{
  numRenderTargets = 0;
  depthStencilFormat = VK_FORMAT_UNDEFINED;
  for (uint32_t i = 0; i < kMaxImageOutputs; ++i)
  {
    colorOperations[i] = RenderPassOperation::kDontCare;
  }
  depthOperation = stencilOperation = RenderPassOperation::kDontCare;

  return *this;
}
RenderPassCreation& RenderPassCreation::addAttachment(
    VkFormat format, VkImageLayout layout, RenderPassOperation::Enum loadOp)
{
  colorFormats[numRenderTargets] = format;
  colorOperations[numRenderTargets] = loadOp;
  colorFinalLayouts[numRenderTargets++] = layout;

  return *this;
}
RenderPassCreation&
RenderPassCreation::setDepthStencilTexture(VkFormat format, VkImageLayout layout)
{
  depthStencilFormat = format;
  depthStencilFinalLayout = layout;

  return *this;
}
RenderPassCreation& RenderPassCreation::setName(const char* p_Name)
{
  name = p_Name;

  return *this;
}
RenderPassCreation& RenderPassCreation::setDepthStencilOperations(
    RenderPassOperation::Enum depth, RenderPassOperation::Enum stencil)
{
  depthOperation = depth;
  stencilOperation = stencil;

  return *this;
}
//---------------------------------------------------------------------------//
/// ExecutionBarrier
ExecutionBarrier& ExecutionBarrier::reset()
{
  numImageBarriers = numMemoryBarriers = 0;
  sourcePipelineStage = PipelineStage::kDrawIndirect;
  destinationPipelineStage = PipelineStage::kDrawIndirect;
  return *this;
}

ExecutionBarrier& ExecutionBarrier::set(PipelineStage::Enum source, PipelineStage::Enum destination)
{
  sourcePipelineStage = source;
  destinationPipelineStage = destination;

  return *this;
}

ExecutionBarrier& ExecutionBarrier::addImageBarrier(const ImageBarrier& imageBarrier)
{
  imageBarriers[numImageBarriers++] = imageBarrier;

  return *this;
}

ExecutionBarrier& ExecutionBarrier::addMemoryBarrier(const MemoryBarrier& memoryBarrier)
{
  memoryBarriers[numMemoryBarriers++] = memoryBarrier;

  return *this;
}
//---------------------------------------------------------------------------//
VkFormat utilStringToVkFormat(const char* p_Format)
{
  if (strcmp(p_Format, "VK_FORMAT_R4G4_UNORM_PACK8") == 0)
  {
    return VK_FORMAT_R4G4_UNORM_PACK8;
  }
  if (strcmp(p_Format, "VK_FORMAT_R4G4B4A4_UNORM_PACK16") == 0)
  {
    return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_B4G4R4A4_UNORM_PACK16") == 0)
  {
    return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_R5G6B5_UNORM_PACK16") == 0)
  {
    return VK_FORMAT_R5G6B5_UNORM_PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_B5G6R5_UNORM_PACK16") == 0)
  {
    return VK_FORMAT_B5G6R5_UNORM_PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_R5G5B5A1_UNORM_PACK16") == 0)
  {
    return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_B5G5R5A1_UNORM_PACK16") == 0)
  {
    return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_A1R5G5B5_UNORM_PACK16") == 0)
  {
    return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8_UNORM") == 0)
  {
    return VK_FORMAT_R8_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8_SNORM") == 0)
  {
    return VK_FORMAT_R8_SNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8_USCALED") == 0)
  {
    return VK_FORMAT_R8_USCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8_SSCALED") == 0)
  {
    return VK_FORMAT_R8_SSCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8_UINT") == 0)
  {
    return VK_FORMAT_R8_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8_SINT") == 0)
  {
    return VK_FORMAT_R8_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8_SRGB") == 0)
  {
    return VK_FORMAT_R8_SRGB;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8_UNORM") == 0)
  {
    return VK_FORMAT_R8G8_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8_SNORM") == 0)
  {
    return VK_FORMAT_R8G8_SNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8_USCALED") == 0)
  {
    return VK_FORMAT_R8G8_USCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8_SSCALED") == 0)
  {
    return VK_FORMAT_R8G8_SSCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8_UINT") == 0)
  {
    return VK_FORMAT_R8G8_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8_SINT") == 0)
  {
    return VK_FORMAT_R8G8_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8_SRGB") == 0)
  {
    return VK_FORMAT_R8G8_SRGB;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8_UNORM") == 0)
  {
    return VK_FORMAT_R8G8B8_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8_SNORM") == 0)
  {
    return VK_FORMAT_R8G8B8_SNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8_USCALED") == 0)
  {
    return VK_FORMAT_R8G8B8_USCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8_SSCALED") == 0)
  {
    return VK_FORMAT_R8G8B8_SSCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8_UINT") == 0)
  {
    return VK_FORMAT_R8G8B8_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8_SINT") == 0)
  {
    return VK_FORMAT_R8G8B8_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8_SRGB") == 0)
  {
    return VK_FORMAT_R8G8B8_SRGB;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8_UNORM") == 0)
  {
    return VK_FORMAT_B8G8R8_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8_SNORM") == 0)
  {
    return VK_FORMAT_B8G8R8_SNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8_USCALED") == 0)
  {
    return VK_FORMAT_B8G8R8_USCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8_SSCALED") == 0)
  {
    return VK_FORMAT_B8G8R8_SSCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8_UINT") == 0)
  {
    return VK_FORMAT_B8G8R8_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8_SINT") == 0)
  {
    return VK_FORMAT_B8G8R8_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8_SRGB") == 0)
  {
    return VK_FORMAT_B8G8R8_SRGB;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8A8_UNORM") == 0)
  {
    return VK_FORMAT_R8G8B8A8_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8A8_SNORM") == 0)
  {
    return VK_FORMAT_R8G8B8A8_SNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8A8_USCALED") == 0)
  {
    return VK_FORMAT_R8G8B8A8_USCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8A8_SSCALED") == 0)
  {
    return VK_FORMAT_R8G8B8A8_SSCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8A8_UINT") == 0)
  {
    return VK_FORMAT_R8G8B8A8_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8A8_SINT") == 0)
  {
    return VK_FORMAT_R8G8B8A8_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R8G8B8A8_SRGB") == 0)
  {
    return VK_FORMAT_R8G8B8A8_SRGB;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8A8_UNORM") == 0)
  {
    return VK_FORMAT_B8G8R8A8_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8A8_SNORM") == 0)
  {
    return VK_FORMAT_B8G8R8A8_SNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8A8_USCALED") == 0)
  {
    return VK_FORMAT_B8G8R8A8_USCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8A8_SSCALED") == 0)
  {
    return VK_FORMAT_B8G8R8A8_SSCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8A8_UINT") == 0)
  {
    return VK_FORMAT_B8G8R8A8_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8A8_SINT") == 0)
  {
    return VK_FORMAT_B8G8R8A8_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8A8_SRGB") == 0)
  {
    return VK_FORMAT_B8G8R8A8_SRGB;
  }
  if (strcmp(p_Format, "VK_FORMAT_A8B8G8R8_UNORM_PACK32") == 0)
  {
    return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A8B8G8R8_SNORM_PACK32") == 0)
  {
    return VK_FORMAT_A8B8G8R8_SNORM_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A8B8G8R8_USCALED_PACK32") == 0)
  {
    return VK_FORMAT_A8B8G8R8_USCALED_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A8B8G8R8_SSCALED_PACK32") == 0)
  {
    return VK_FORMAT_A8B8G8R8_SSCALED_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A8B8G8R8_UINT_PACK32") == 0)
  {
    return VK_FORMAT_A8B8G8R8_UINT_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A8B8G8R8_SINT_PACK32") == 0)
  {
    return VK_FORMAT_A8B8G8R8_SINT_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A8B8G8R8_SRGB_PACK32") == 0)
  {
    return VK_FORMAT_A8B8G8R8_SRGB_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2R10G10B10_UNORM_PACK32") == 0)
  {
    return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2R10G10B10_SNORM_PACK32") == 0)
  {
    return VK_FORMAT_A2R10G10B10_SNORM_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2R10G10B10_USCALED_PACK32") == 0)
  {
    return VK_FORMAT_A2R10G10B10_USCALED_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2R10G10B10_SSCALED_PACK32") == 0)
  {
    return VK_FORMAT_A2R10G10B10_SSCALED_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2R10G10B10_UINT_PACK32") == 0)
  {
    return VK_FORMAT_A2R10G10B10_UINT_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2R10G10B10_SINT_PACK32") == 0)
  {
    return VK_FORMAT_A2R10G10B10_SINT_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2B10G10R10_UNORM_PACK32") == 0)
  {
    return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2B10G10R10_SNORM_PACK32") == 0)
  {
    return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2B10G10R10_USCALED_PACK32") == 0)
  {
    return VK_FORMAT_A2B10G10R10_USCALED_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2B10G10R10_SSCALED_PACK32") == 0)
  {
    return VK_FORMAT_A2B10G10R10_SSCALED_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2B10G10R10_UINT_PACK32") == 0)
  {
    return VK_FORMAT_A2B10G10R10_UINT_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_A2B10G10R10_SINT_PACK32") == 0)
  {
    return VK_FORMAT_A2B10G10R10_SINT_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16_UNORM") == 0)
  {
    return VK_FORMAT_R16_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16_SNORM") == 0)
  {
    return VK_FORMAT_R16_SNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16_USCALED") == 0)
  {
    return VK_FORMAT_R16_USCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16_SSCALED") == 0)
  {
    return VK_FORMAT_R16_SSCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16_UINT") == 0)
  {
    return VK_FORMAT_R16_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16_SINT") == 0)
  {
    return VK_FORMAT_R16_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16_SFLOAT") == 0)
  {
    return VK_FORMAT_R16_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16_UNORM") == 0)
  {
    return VK_FORMAT_R16G16_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16_SNORM") == 0)
  {
    return VK_FORMAT_R16G16_SNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16_USCALED") == 0)
  {
    return VK_FORMAT_R16G16_USCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16_SSCALED") == 0)
  {
    return VK_FORMAT_R16G16_SSCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16_UINT") == 0)
  {
    return VK_FORMAT_R16G16_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16_SINT") == 0)
  {
    return VK_FORMAT_R16G16_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16_SFLOAT") == 0)
  {
    return VK_FORMAT_R16G16_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16_UNORM") == 0)
  {
    return VK_FORMAT_R16G16B16_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16_SNORM") == 0)
  {
    return VK_FORMAT_R16G16B16_SNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16_USCALED") == 0)
  {
    return VK_FORMAT_R16G16B16_USCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16_SSCALED") == 0)
  {
    return VK_FORMAT_R16G16B16_SSCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16_UINT") == 0)
  {
    return VK_FORMAT_R16G16B16_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16_SINT") == 0)
  {
    return VK_FORMAT_R16G16B16_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16_SFLOAT") == 0)
  {
    return VK_FORMAT_R16G16B16_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16A16_UNORM") == 0)
  {
    return VK_FORMAT_R16G16B16A16_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16A16_SNORM") == 0)
  {
    return VK_FORMAT_R16G16B16A16_SNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16A16_USCALED") == 0)
  {
    return VK_FORMAT_R16G16B16A16_USCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16A16_SSCALED") == 0)
  {
    return VK_FORMAT_R16G16B16A16_SSCALED;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16A16_UINT") == 0)
  {
    return VK_FORMAT_R16G16B16A16_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16A16_SINT") == 0)
  {
    return VK_FORMAT_R16G16B16A16_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R16G16B16A16_SFLOAT") == 0)
  {
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32_UINT") == 0)
  {
    return VK_FORMAT_R32_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32_SINT") == 0)
  {
    return VK_FORMAT_R32_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32_SFLOAT") == 0)
  {
    return VK_FORMAT_R32_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32G32_UINT") == 0)
  {
    return VK_FORMAT_R32G32_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32G32_SINT") == 0)
  {
    return VK_FORMAT_R32G32_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32G32_SFLOAT") == 0)
  {
    return VK_FORMAT_R32G32_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32G32B32_UINT") == 0)
  {
    return VK_FORMAT_R32G32B32_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32G32B32_SINT") == 0)
  {
    return VK_FORMAT_R32G32B32_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32G32B32_SFLOAT") == 0)
  {
    return VK_FORMAT_R32G32B32_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32G32B32A32_UINT") == 0)
  {
    return VK_FORMAT_R32G32B32A32_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32G32B32A32_SINT") == 0)
  {
    return VK_FORMAT_R32G32B32A32_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R32G32B32A32_SFLOAT") == 0)
  {
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64_UINT") == 0)
  {
    return VK_FORMAT_R64_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64_SINT") == 0)
  {
    return VK_FORMAT_R64_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64_SFLOAT") == 0)
  {
    return VK_FORMAT_R64_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64G64_UINT") == 0)
  {
    return VK_FORMAT_R64G64_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64G64_SINT") == 0)
  {
    return VK_FORMAT_R64G64_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64G64_SFLOAT") == 0)
  {
    return VK_FORMAT_R64G64_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64G64B64_UINT") == 0)
  {
    return VK_FORMAT_R64G64B64_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64G64B64_SINT") == 0)
  {
    return VK_FORMAT_R64G64B64_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64G64B64_SFLOAT") == 0)
  {
    return VK_FORMAT_R64G64B64_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64G64B64A64_UINT") == 0)
  {
    return VK_FORMAT_R64G64B64A64_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64G64B64A64_SINT") == 0)
  {
    return VK_FORMAT_R64G64B64A64_SINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_R64G64B64A64_SFLOAT") == 0)
  {
    return VK_FORMAT_R64G64B64A64_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_B10G11R11_UFLOAT_PACK32") == 0)
  {
    return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_E5B9G9R9_UFLOAT_PACK32") == 0)
  {
    return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_D16_UNORM") == 0)
  {
    return VK_FORMAT_D16_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_X8_D24_UNORM_PACK32") == 0)
  {
    return VK_FORMAT_X8_D24_UNORM_PACK32;
  }
  if (strcmp(p_Format, "VK_FORMAT_D32_SFLOAT") == 0)
  {
    return VK_FORMAT_D32_SFLOAT;
  }
  if (strcmp(p_Format, "VK_FORMAT_S8_UINT") == 0)
  {
    return VK_FORMAT_S8_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_D16_UNORM_S8_UINT") == 0)
  {
    return VK_FORMAT_D16_UNORM_S8_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_D24_UNORM_S8_UINT") == 0)
  {
    return VK_FORMAT_D24_UNORM_S8_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_D32_SFLOAT_S8_UINT") == 0)
  {
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC1_RGB_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC1_RGB_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC1_RGBA_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC1_RGBA_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC2_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_BC2_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC2_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_BC2_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC3_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_BC3_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC3_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_BC3_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC4_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_BC4_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC4_SNORM_BLOCK") == 0)
  {
    return VK_FORMAT_BC4_SNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC5_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_BC5_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC5_SNORM_BLOCK") == 0)
  {
    return VK_FORMAT_BC5_SNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC6H_UFLOAT_BLOCK") == 0)
  {
    return VK_FORMAT_BC6H_UFLOAT_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC6H_SFLOAT_BLOCK") == 0)
  {
    return VK_FORMAT_BC6H_SFLOAT_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC7_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_BC7_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_BC7_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_BC7_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_EAC_R11_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_EAC_R11_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_EAC_R11_SNORM_BLOCK") == 0)
  {
    return VK_FORMAT_EAC_R11_SNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_EAC_R11G11_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_EAC_R11G11_SNORM_BLOCK") == 0)
  {
    return VK_FORMAT_EAC_R11G11_SNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_4x4_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_4x4_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_5x4_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_5x4_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_5x4_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_5x4_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_5x5_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_5x5_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_5x5_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_6x5_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_6x5_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_6x5_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_6x5_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_6x6_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_6x6_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_8x5_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_8x5_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_8x5_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_8x5_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_8x6_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_8x6_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_8x6_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_8x6_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_8x8_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_8x8_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x5_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_10x5_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x5_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_10x5_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x6_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_10x6_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x6_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_10x6_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x8_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_10x8_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x8_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_10x8_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x10_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_10x10_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x10_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_10x10_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_12x10_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_12x10_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_12x10_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_12x10_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_12x12_UNORM_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_12x12_UNORM_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_12x12_SRGB_BLOCK") == 0)
  {
    return VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
  }
  if (strcmp(p_Format, "VK_FORMAT_G8B8G8R8_422_UNORM") == 0)
  {
    return VK_FORMAT_G8B8G8R8_422_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_B8G8R8G8_422_UNORM") == 0)
  {
    return VK_FORMAT_B8G8R8G8_422_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM") == 0)
  {
    return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_G8_B8R8_2PLANE_420_UNORM") == 0)
  {
    return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM") == 0)
  {
    return VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_G8_B8R8_2PLANE_422_UNORM") == 0)
  {
    return VK_FORMAT_G8_B8R8_2PLANE_422_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM") == 0)
  {
    return VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_R10X6_UNORM_PACK16") == 0)
  {
    return VK_FORMAT_R10X6_UNORM_PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_R10X6G10X6_UNORM_2PACK16") == 0)
  {
    return VK_FORMAT_R10X6G10X6_UNORM_2PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16") == 0)
  {
    return VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16") == 0)
  {
    return VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16") == 0)
  {
    return VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16") == 0)
  {
    return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16") == 0)
  {
    return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16") == 0)
  {
    return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16") == 0)
  {
    return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16") == 0)
  {
    return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_R12X4_UNORM_PACK16") == 0)
  {
    return VK_FORMAT_R12X4_UNORM_PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_R12X4G12X4_UNORM_2PACK16") == 0)
  {
    return VK_FORMAT_R12X4G12X4_UNORM_2PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16") == 0)
  {
    return VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16") == 0)
  {
    return VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16") == 0)
  {
    return VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16") == 0)
  {
    return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16") == 0)
  {
    return VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16") == 0)
  {
    return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16") == 0)
  {
    return VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16") == 0)
  {
    return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16;
  }
  if (strcmp(p_Format, "VK_FORMAT_G16B16G16R16_422_UNORM") == 0)
  {
    return VK_FORMAT_G16B16G16R16_422_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_B16G16R16G16_422_UNORM") == 0)
  {
    return VK_FORMAT_B16G16R16G16_422_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM") == 0)
  {
    return VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_G16_B16R16_2PLANE_420_UNORM") == 0)
  {
    return VK_FORMAT_G16_B16R16_2PLANE_420_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM") == 0)
  {
    return VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_G16_B16R16_2PLANE_422_UNORM") == 0)
  {
    return VK_FORMAT_G16_B16R16_2PLANE_422_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM") == 0)
  {
    return VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM;
  }
  if (strcmp(p_Format, "VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG") == 0)
  {
    return VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG;
  }
  if (strcmp(p_Format, "VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG") == 0)
  {
    return VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG;
  }
  if (strcmp(p_Format, "VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG") == 0)
  {
    return VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG;
  }
  if (strcmp(p_Format, "VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG") == 0)
  {
    return VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG;
  }
  if (strcmp(p_Format, "VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG") == 0)
  {
    return VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG;
  }
  if (strcmp(p_Format, "VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG") == 0)
  {
    return VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG;
  }
  if (strcmp(p_Format, "VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG") == 0)
  {
    return VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG;
  }
  if (strcmp(p_Format, "VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG") == 0)
  {
    return VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT") == 0)
  {
    return VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_G8_B8R8_2PLANE_444_UNORM_EXT") == 0)
  {
    return VK_FORMAT_G8_B8R8_2PLANE_444_UNORM_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT") == 0)
  {
    return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT") == 0)
  {
    return VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_G16_B16R16_2PLANE_444_UNORM_EXT") == 0)
  {
    return VK_FORMAT_G16_B16R16_2PLANE_444_UNORM_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT") == 0)
  {
    return VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT;
  }
  if (strcmp(p_Format, "VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT") == 0)
  {
    return VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT;
  }

  assert(false);
  return VK_FORMAT_UNDEFINED;
}
//---------------------------------------------------------------------------//
void utilAddImageBarrier(
    GpuDevice* p_GpuDevice,
    VkCommandBuffer p_Cmdbuf,
    VkImage p_Image,
    ResourceState p_OldState,
    ResourceState p_NewState,
    uint32_t p_BaseMipLevel,
    uint32_t p_MipCount,
    bool p_IsDepth)
{
  if (p_GpuDevice->m_Synchronization2ExtensionPresent)
  {
    VkImageMemoryBarrier2KHR barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR};
    barrier.srcAccessMask = utilToVkAccessFlags2(p_OldState);
    barrier.srcStageMask =
        utilDeterminePipelineStageFlags2(barrier.srcAccessMask, QueueType::kGraphics);
    barrier.dstAccessMask = utilToVkAccessFlags2(p_NewState);
    barrier.dstStageMask =
        utilDeterminePipelineStageFlags2(barrier.dstAccessMask, QueueType::kGraphics);
    barrier.oldLayout = utilToVkImageLayout2(p_OldState);
    barrier.newLayout = utilToVkImageLayout2(p_NewState);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = p_Image;
    barrier.subresourceRange.aspectMask =
        p_IsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.baseMipLevel = p_BaseMipLevel;
    barrier.subresourceRange.levelCount = p_MipCount;

    VkDependencyInfoKHR dependency_info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR};
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &barrier;

    p_GpuDevice->m_CmdPipelineBarrier2(p_Cmdbuf, &dependency_info);
  }
  else
  {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image = p_Image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask =
        p_IsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = p_MipCount;

    barrier.subresourceRange.baseMipLevel = p_BaseMipLevel;
    barrier.oldLayout = utilToVkImageLayout(p_OldState);
    barrier.newLayout = utilToVkImageLayout(p_NewState);
    barrier.srcAccessMask = utilToVkAccessFlags(p_OldState);
    barrier.dstAccessMask = utilToVkAccessFlags(p_NewState);

    const VkPipelineStageFlags sourceStageMask =
        utilDeterminePipelineStageFlags(barrier.srcAccessMask, QueueType::kGraphics);
    const VkPipelineStageFlags destinationStageMask =
        utilDeterminePipelineStageFlags(barrier.dstAccessMask, QueueType::kGraphics);

    vkCmdPipelineBarrier(
        p_Cmdbuf, sourceStageMask, destinationStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  }
}
//---------------------------------------------------------------------------//
void utilAddImageBarrier(
    GpuDevice* gpu,
    VkCommandBuffer cmdbuf,
    Texture* texture,
    ResourceState newState,
    uint32_t baseMipLevel,
    uint32_t mipCount,
    bool isDepth)
{
  utilAddImageBarrier(
      gpu, cmdbuf, texture->vkImage, texture->state, newState, baseMipLevel, mipCount, isDepth);
  texture->state = newState;
}

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
    QueueType::Enum destinationQueueType)
{
  if (gpu->m_Synchronization2ExtensionPresent)
  {
    VkImageMemoryBarrier2KHR barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR};
    barrier.srcAccessMask = utilToVkAccessFlags2(oldState);
    barrier.srcStageMask = utilDeterminePipelineStageFlags2(barrier.srcAccessMask, sourceQueueType);
    barrier.dstAccessMask = utilToVkAccessFlags2(newState);
    barrier.dstStageMask =
        utilDeterminePipelineStageFlags2(barrier.dstAccessMask, destinationQueueType);
    barrier.oldLayout = utilToVkImageLayout2(oldState);
    barrier.newLayout = utilToVkImageLayout2(newState);
    barrier.srcQueueFamilyIndex = sourceFamily;
    barrier.dstQueueFamilyIndex = destinationFamily;
    barrier.image = image;
    barrier.subresourceRange.aspectMask =
        isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = mipCount;

    VkDependencyInfoKHR dependency_info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR};
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &barrier;

    gpu->m_CmdPipelineBarrier2(cmdbuf, &dependency_info);
  }
  else
  {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image = image;
    barrier.srcQueueFamilyIndex = sourceFamily;
    barrier.dstQueueFamilyIndex = destinationFamily;
    barrier.subresourceRange.aspectMask =
        isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = mipCount;

    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.oldLayout = utilToVkImageLayout(oldState);
    barrier.newLayout = utilToVkImageLayout(newState);
    barrier.srcAccessMask = utilToVkAccessFlags(oldState);
    barrier.dstAccessMask = utilToVkAccessFlags(newState);

    const VkPipelineStageFlags sourceStageMask =
        utilDeterminePipelineStageFlags(barrier.srcAccessMask, sourceQueueType);
    const VkPipelineStageFlags destinationStageMask =
        utilDeterminePipelineStageFlags(barrier.dstAccessMask, destinationQueueType);

    vkCmdPipelineBarrier(
        cmdbuf, sourceStageMask, destinationStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  }
}
//---------------------------------------------------------------------------//
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
    QueueType::Enum destinationQueueType)
{

  utilAddImageBarrierExt(
      gpu,
      cmdbuf,
      texture->vkImage,
      texture->state,
      newState,
      baseMipLevel,
      mipCount,
      isDepth,
      sourceFamily,
      destinationFamily,
      sourceQueueType,
      destinationQueueType);
  texture->state = newState;
}
//---------------------------------------------------------------------------//
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
    QueueType::Enum destinationQueueType)
{

  if (gpu->m_Synchronization2ExtensionPresent)
  {
    VkBufferMemoryBarrier2KHR barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR};
    barrier.srcAccessMask = utilToVkAccessFlags2(oldState);
    barrier.srcStageMask = utilDeterminePipelineStageFlags2(barrier.srcAccessMask, sourceQueueType);
    barrier.dstAccessMask = utilToVkAccessFlags2(newState);
    barrier.dstStageMask =
        utilDeterminePipelineStageFlags2(barrier.dstAccessMask, destinationQueueType);
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = buffer_size;

    VkDependencyInfoKHR dependency_info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR};
    dependency_info.bufferMemoryBarrierCount = 1;
    dependency_info.pBufferMemoryBarriers = &barrier;

    gpu->m_CmdPipelineBarrier2(cmdbuf, &dependency_info);
  }
  else
  {
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.buffer = buffer;
    barrier.srcQueueFamilyIndex = sourceFamily;
    barrier.dstQueueFamilyIndex = destinationFamily;
    barrier.offset = 0;
    barrier.size = buffer_size;
    barrier.srcAccessMask = utilToVkAccessFlags(oldState);
    barrier.dstAccessMask = utilToVkAccessFlags(newState);

    const VkPipelineStageFlags sourceStageMask =
        utilDeterminePipelineStageFlags(barrier.srcAccessMask, sourceQueueType);
    const VkPipelineStageFlags destinationStageMask =
        utilDeterminePipelineStageFlags(barrier.dstAccessMask, destinationQueueType);

    vkCmdPipelineBarrier(
        cmdbuf, sourceStageMask, destinationStageMask, 0, 0, nullptr, 1, &barrier, 0, nullptr);
  }
}
//---------------------------------------------------------------------------//
// Determines pipeline stages involved for given accesses
VkPipelineStageFlags
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
VkPipelineStageFlags2KHR
utilDeterminePipelineStageFlags2(VkAccessFlags2KHR p_AccessFlags, QueueType::Enum p_QueueType)
{
  VkPipelineStageFlags2KHR flags = 0;

  switch (p_QueueType)
  {
  case QueueType::kGraphics: {
    if ((p_AccessFlags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0)
      flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT_KHR;

    if ((p_AccessFlags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                          VK_ACCESS_SHADER_WRITE_BIT)) != 0)
    {
      flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR;
      flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
      /*if ( pRenderer->pActiveGpuSettings->mGeometryShaderSupported ) {
          flags |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
      }
      if ( pRenderer->pActiveGpuSettings->mTessellationSupported ) {
          flags |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
          flags |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
      }*/
      flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
#ifdef ENABLE_RAYTRACING
      if (pRenderer->mVulkan.mRaytracingExtension)
      {
        flags |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;
      }
#endif
    }

    if ((p_AccessFlags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0)
      flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;

    if ((p_AccessFlags &
         (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0)
      flags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;

    if ((p_AccessFlags & (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
      flags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR |
               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT_KHR;

    break;
  }
  case QueueType::kCompute: {
    if ((p_AccessFlags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0 ||
        (p_AccessFlags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0 ||
        (p_AccessFlags &
         (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0 ||
        (p_AccessFlags & (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
      return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;

    if ((p_AccessFlags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                          VK_ACCESS_SHADER_WRITE_BIT)) != 0)
      flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;

    break;
  }
  case QueueType::kCopyTransfer:
    return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
  default:
    break;
  }

  // Compatible with both compute and graphics queues
  if ((p_AccessFlags & VK_ACCESS_INDIRECT_COMMAND_READ_BIT) != 0)
    flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR;

  if ((p_AccessFlags & (VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)) != 0)
    flags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;

  if ((p_AccessFlags & (VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT)) != 0)
    flags |= VK_PIPELINE_STAGE_2_HOST_BIT_KHR;

  if (flags == 0)
    flags = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR;

  return flags;
}
//---------------------------------------------------------------------------//
const char* toCompilerExtension(VkShaderStageFlagBits value)
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
//---------------------------------------------------------------------------//
const char* toStageDefines(VkShaderStageFlagBits value)
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
//---------------------------------------------------------------------------//
VkImageType toVkImageType(TextureType::Enum type)
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
//---------------------------------------------------------------------------//
VkImageViewType toVkImageViewType(TextureType::Enum type)
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
//---------------------------------------------------------------------------//
VkFormat toVkVertexFormat(VertexComponentFormat::Enum value)
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
//---------------------------------------------------------------------------//
VkPipelineStageFlags toVkPipelineStage(PipelineStage::Enum value)
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
//---------------------------------------------------------------------------//
VkAccessFlags utilToVkAccessFlags(ResourceState state)
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
//---------------------------------------------------------------------------//
VkAccessFlags utilToVkAccessFlags2(ResourceState state)
{
  VkAccessFlags ret = 0;
  if (state & RESOURCE_STATE_COPY_SOURCE)
  {
    ret |= VK_ACCESS_2_TRANSFER_READ_BIT_KHR;
  }
  if (state & RESOURCE_STATE_COPY_DEST)
  {
    ret |= VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
  }
  if (state & RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
  {
    ret |= VK_ACCESS_2_UNIFORM_READ_BIT_KHR | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT_KHR;
  }
  if (state & RESOURCE_STATE_INDEX_BUFFER)
  {
    ret |= VK_ACCESS_2_INDEX_READ_BIT_KHR;
  }
  if (state & RESOURCE_STATE_UNORDERED_ACCESS)
  {
    ret |= VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR;
  }
  if (state & RESOURCE_STATE_INDIRECT_ARGUMENT)
  {
    ret |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR;
  }
  if (state & RESOURCE_STATE_RENDER_TARGET)
  {
    ret |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT_KHR | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR;
  }
  if (state & RESOURCE_STATE_DEPTH_WRITE)
  {
    ret |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT_KHR |
           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT_KHR;
  }
  if (state & RESOURCE_STATE_SHADER_RESOURCE)
  {
    ret |= VK_ACCESS_2_SHADER_READ_BIT_KHR;
  }
  if (state & RESOURCE_STATE_PRESENT)
  {
    ret |= VK_ACCESS_2_MEMORY_READ_BIT_KHR;
  }
#ifdef ENABLE_RAYTRACING
  if (state & RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)
  {
    ret |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV |
           VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV;
  }
#endif

  return ret;
}
//---------------------------------------------------------------------------//
VkImageLayout utilToVkImageLayout(ResourceState usage)
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
//---------------------------------------------------------------------------//
VkImageLayout utilToVkImageLayout2(ResourceState usage)
{
  if (usage & RESOURCE_STATE_COPY_SOURCE)
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  if (usage & RESOURCE_STATE_COPY_DEST)
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

  if (usage & RESOURCE_STATE_RENDER_TARGET)
    return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;

  if (usage & RESOURCE_STATE_DEPTH_WRITE)
    return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;

  if (usage & RESOURCE_STATE_DEPTH_READ)
    return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR;

  if (usage & RESOURCE_STATE_UNORDERED_ACCESS)
    return VK_IMAGE_LAYOUT_GENERAL;

  if (usage & RESOURCE_STATE_SHADER_RESOURCE)
    return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR;

  if (usage & RESOURCE_STATE_PRESENT)
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  if (usage == RESOURCE_STATE_COMMON)
    return VK_IMAGE_LAYOUT_GENERAL;

  return VK_IMAGE_LAYOUT_UNDEFINED;
}
//---------------------------------------------------------------------------//
} // namespace Graphics
