#include "GpuResources.hpp"

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
TextureCreation& TextureCreation::setSize(uint16_t width_, uint16_t height_, uint16_t depth_)
{
  width = width_;
  height = height_;
  depth = depth_;

  return *this;
}

TextureCreation& TextureCreation::setFlags(uint8_t mipmaps_, uint8_t flags_)
{
  mipmaps = mipmaps_;
  flags = flags_;

  return *this;
}

TextureCreation& TextureCreation::setFormatType(VkFormat format_, TextureType::Enum type_)
{
  format = format_;
  type = type_;

  return *this;
}

TextureCreation& TextureCreation::setName(const char* name_)
{
  name = name_;

  return *this;
}

TextureCreation& TextureCreation::setData(void* data_)
{
  initialData = data_;

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
  }
  depthStencilFormat = VK_FORMAT_UNDEFINED;
  colorOperation = depthOperation = stencilOperation = RenderPassOperation::kDontCare;
  return *this;
}

RenderPassOutput& RenderPassOutput::color(VkFormat format)
{
  colorFormats[numColorFormats++] = format;
  return *this;
}

RenderPassOutput& RenderPassOutput::depth(VkFormat format)
{
  depthStencilFormat = format;
  return *this;
}

RenderPassOutput& RenderPassOutput::setOperations(
    RenderPassOperation::Enum color_,
    RenderPassOperation::Enum depth_,
    RenderPassOperation::Enum stencil_)
{
  colorOperation = color_;
  depthOperation = depth_;
  stencilOperation = stencil_;

  return *this;
}
//---------------------------------------------------------------------------//
/// PipelineCreation
PipelineCreation& PipelineCreation::addDescriptorSetLayout(DescriptorSetLayoutHandle handle)
{
  descriptorSetLayouts[numActiveLayouts++] = handle;
  return *this;
}

RenderPassOutput& PipelineCreation::renderPassOutput() { return renderPass; }
//---------------------------------------------------------------------------//
/// RenderPassCreation
RenderPassCreation& RenderPassCreation::reset()
{
  numRenderTargets = 0;
  depthStencilTexture = kInvalidTexture;
  resize = 0;
  scaleX = 1.f;
  scaleY = 1.f;
  colorOperation = depthOperation = stencilOperation = RenderPassOperation::kDontCare;

  return *this;
}

RenderPassCreation& RenderPassCreation::addRenderTexture(TextureHandle texture)
{
  outputTextures[numRenderTargets++] = texture;

  return *this;
}

RenderPassCreation& RenderPassCreation::setScaling(float scaleX_, float scaleY_, uint8_t resize_)
{
  scaleX = scaleX_;
  scaleY = scaleY_;
  resize = resize_;

  return *this;
}

RenderPassCreation& RenderPassCreation::setDepthStencilTexture(TextureHandle texture)
{
  depthStencilTexture = texture;

  return *this;
}

RenderPassCreation& RenderPassCreation::setName(const char* name_)
{
  name = name_;

  return *this;
}

RenderPassCreation& RenderPassCreation::setType(RenderPassType::Enum type_)
{
  type = type_;

  return *this;
}

RenderPassCreation& RenderPassCreation::setOperations(
    RenderPassOperation::Enum color_,
    RenderPassOperation::Enum depth_,
    RenderPassOperation::Enum stencil_)
{
  colorOperation = color_;
  depthOperation = depth_;
  stencilOperation = stencil_;

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
} // namespace Graphics
