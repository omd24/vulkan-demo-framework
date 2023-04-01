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
} // namespace Graphics
