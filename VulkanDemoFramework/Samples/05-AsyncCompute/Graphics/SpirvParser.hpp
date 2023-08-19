#pragma once

#include "Foundation/Array.hpp"
#include "Graphics/GpuResources.hpp"

#include <spirv-headers/spirv.h>

#include <vulkan/vulkan.h>

// Forward declare
namespace Framework
{
struct StringBuffer;
} // namespace Framework

namespace Graphics
{
namespace Spirv
{

static const uint32_t kMaxSetCount = 8;

struct ParseResult
{
  uint32_t setCount;
  DescriptorSetLayoutCreation sets[kMaxSetCount];

  ComputeLocalSize computeLocalSize;
};

void parseBinary(
    const uint32_t* p_Data,
    size_t p_DataSize,
    Framework::StringBuffer& p_NameBuffer,
    ParseResult* p_ParseResult);

} // namespace Spirv
} // namespace Graphics
