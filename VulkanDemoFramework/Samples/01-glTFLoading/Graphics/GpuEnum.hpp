#pragma once

#include "Foundation/Prerequisites.hpp"

namespace Graphics
{
//----------------------------------------------------------------------------//
namespace ColorWriteEnabled
{
enum Enum
{
  kRed,
  kGreen,
  kBlue,
  kAlpha,
  kAll,
  kCount
};
enum Mask
{
  kRedMask = 1 << 0,
  kGreenMask = 1 << 1,
  kBlueMask = 1 << 2,
  kAlphaMask = 1 << 3,
  kAllMask = kRedMask | kGreenMask | kBlueMask | kAlphaMask
};
static const char* kEnumNames[] = {"Red", "Green", "Blue", "Alpha", "All", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace ColorWriteEnabled
//----------------------------------------------------------------------------//
namespace CullMode
{
enum Enum
{
  kNone,
  kFront,
  kBack,
  kCount
};

enum Mask
{
  kNoneMask = 1 << 0,
  kFrontMask = 1 << 1,
  kBackMask = 1 << 2,
  kCountMask = 1 << 3
};

static const char* kEnumNames[] = {"None", "Front", "Back", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace CullMode
//----------------------------------------------------------------------------//
namespace DepthWriteMask
{
enum Enum
{
  kZero,
  kAll,
  kCount
};

enum Mask
{
  kZeroMask = 1 << 0,
  kAllMask = 1 << 1,
  kCountMask = 1 << 2
};

static const char* kEnumNames[] = {"Zero", "All", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace DepthWriteMask
//----------------------------------------------------------------------------//
namespace FillMode
{
enum Enum
{
  kWireframe,
  kSolid,
  kPoint,
  kCount
};

enum Mask
{
  kWireframeMask = 1 << 0,
  kSolidMask = 1 << 1,
  kPointMask = 1 << 2,
  kCountMask = 1 << 3
};

static const char* kEnumNames[] = {"Wireframe", "Solid", "Point", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace FillMode
//----------------------------------------------------------------------------//
namespace FrontClockwise
{
enum Enum
{
  kTrue,
  kFalse,
  kCount
};

enum Mask
{
  kTrueMask = 1 << 0,
  kFalseMask = 1 << 1,
  kCountMask = 1 << 2
};

static const char* kEnumNames[] = {"True", "False", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace FrontClockwise
//----------------------------------------------------------------------------//
namespace StencilOperation
{
enum Enum
{
  kKeep,
  kZero,
  kReplace,
  kIncrSat,
  kDecrSat,
  kInvert,
  kIncr,
  kDecr,
  kCount
};

enum Mask
{
  KeepMask = 1 << 0,
  ZeroMask = 1 << 1,
  ReplaceMask = 1 << 2,
  IncrSatMask = 1 << 3,
  DecrSatMask = 1 << 4,
  InvertMask = 1 << 5,
  IncrMask = 1 << 6,
  DecrMask = 1 << 7,
  CountMask = 1 << 8
};

static const char* kEnumNames[] = {
    "Keep", "Zero", "Replace", "IncrSat", "DecrSat", "Invert", "Incr", "Decr", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace StencilOperation
//----------------------------------------------------------------------------//
namespace TopologyType
{
enum Enum
{
  kUnknown,
  kPoint,
  kLine,
  kTriangle,
  kPatch,
  kCount
};

enum Mask
{
  kUnknownMask = 1 << 0,
  kPointMask = 1 << 1,
  kLineMask = 1 << 2,
  kTriangleMask = 1 << 3,
  kPatchMask = 1 << 4,
  kCountMask = 1 << 5
};

static const char* kEnumNames[] = {"Unknown", "Point", "Line", "Triangle", "Patch", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace TopologyType
//----------------------------------------------------------------------------//
namespace ResourceUsageType
{
enum Enum
{
  kImmutable,
  kDynamic,
  kStream,
  kCount
};

enum Mask
{
  kImmutableMask = 1 << 0,
  kDynamicMask = 1 << 1,
  kStreamMask = 1 << 2,
  kCountMask = 1 << 3
};

static const char* kEnumNames[] = {"Immutable", "Dynamic", "Stream", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace ResourceUsageType
//----------------------------------------------------------------------------//
namespace IndexType
{
enum Enum
{
  kUint16,
  kUint32,
  kCount
};

enum Mask
{
  kUint16Mask = 1 << 0,
  kUint32Mask = 1 << 1,
  kCountMask = 1 << 2
};

static const char* kEnumNames[] = {"Uint16", "Uint32", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace IndexType
//----------------------------------------------------------------------------//
namespace TextureType
{
enum Enum
{
  kTexture1D,
  kTexture2D,
  kTexture3D,
  kTexture_1D_Array,
  kTexture_2D_Array,
  kTexture_Cube_Array,
  kCount
};

enum Mask
{
  kTexture1DMask = 1 << 0,
  kTexture2DMask = 1 << 1,
  kTexture3DMask = 1 << 2,
  kTexture_1D_ArrayMask = 1 << 3,
  kTexture_2D_ArrayMask = 1 << 4,
  kTexture_Cube_ArrayMask = 1 << 5,
  kCountMask = 1 << 6
};

static const char* kEnumNames[] = {
    "Texture1D",
    "Texture2D",
    "Texture3D",
    "Texture_1D_Array",
    "Texture_2D_Array",
    "Texture_Cube_Array",
    "Count"};

static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace TextureType
//----------------------------------------------------------------------------//
namespace VertexComponentFormat
{
enum Enum
{
  kFloat,
  kFloat2,
  kFloat3,
  kFloat4,
  kMat4,
  kByte,
  kByte4N,
  kUByte,
  kUByte4N,
  kShort2,
  kShort2N,
  kShort4,
  kShort4N,
  kUint,
  kUint2,
  kUint4,
  kCount
};

static const char* kEnumNames[] = {
    "Float",
    "Float2",
    "Float3",
    "Float4",
    "Mat4",
    "Byte",
    "Byte4N",
    "UByte",
    "UByte4N",
    "Short2",
    "Short2N",
    "Short4",
    "Short4N",
    "Uint",
    "Uint2",
    "Uint4",
    "Count"};

static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace VertexComponentFormat
//----------------------------------------------------------------------------//
namespace VertexInputRate
{
enum Enum
{
  kPerVertex,
  kPerInstance,
  kCount
};

enum Mask
{
  kPerVertexMask = 1 << 0,
  kPerInstanceMask = 1 << 1,
  kCountMask = 1 << 2
};

static const char* kEnumNames[] = {"PerVertex", "PerInstance", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace VertexInputRate
//----------------------------------------------------------------------------//
namespace LogicOperation
{
enum Enum
{
  kClear,
  kSet,
  kCopy,
  kCopyInverted,
  kNoop,
  kInvert,
  kAnd,
  kNand,
  kOr,
  kNor,
  kXor,
  kEquiv,
  kAndReverse,
  kAndInverted,
  kOrReverse,
  kOrInverted,
  kCount
};

enum Mask
{
  kClearMask = 1 << 0,
  kSetMask = 1 << 1,
  kCopyMask = 1 << 2,
  kCopyInvertedMask = 1 << 3,
  kNoopMask = 1 << 4,
  kInvertMask = 1 << 5,
  kAndMask = 1 << 6,
  kNandMask = 1 << 7,
  kOrMask = 1 << 8,
  kNorMask = 1 << 9,
  kXorMask = 1 << 10,
  kEquivMask = 1 << 11,
  kAndReverseMask = 1 << 12,
  kAndInvertedMask = 1 << 13,
  kOrReverseMask = 1 << 14,
  kOrInvertedMask = 1 << 15,
  kCountMask = 1 << 16
};

static const char* kEnumNames[] = {
    "Clear",
    "Set",
    "Copy",
    "CopyInverted",
    "Noop",
    "Invert",
    "And",
    "Nand",
    "Or",
    "Nor",
    "Xor",
    "Equiv",
    "AndReverse",
    "AndInverted",
    "OrReverse",
    "OrInverted",
    "Count"};

static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace LogicOperation
//----------------------------------------------------------------------------//
namespace QueueType
{
enum Enum
{
  kGraphics,
  kCompute,
  kCopyTransfer,
  kCount
};

enum Mask
{
  kGraphicsMask = 1 << 0,
  kComputeMask = 1 << 1,
  kCopyTransferMask = 1 << 2,
  kCountMask = 1 << 3
};

static const char* kEnumNames[] = {"Graphics", "Compute", "CopyTransfer", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace QueueType
//----------------------------------------------------------------------------//
namespace CommandType
{
enum Enum
{
  kBindPipeline,
  kBindResourceTable,
  kBindVertexBuffer,
  kBindIndexBuffer,
  kBindResourceSet,
  kDraw,
  kDrawIndexed,
  kDrawInstanced,
  kDrawIndexedInstanced,
  kDispatch,
  kCopyResource,
  kSetScissor,
  kSetViewport,
  kClear,
  kClearDepth,
  kClearStencil,
  kBeginPass,
  kEndPass,
  kCount
};

static const char* kEnumNames[] = {
    "BindPipeline",
    "BindResourceTable",
    "BindVertexBuffer",
    "BindIndexBuffer",
    "BindResourceSet",
    "Draw",
    "DrawIndexed",
    "DrawInstanced",
    "DrawIndexedInstanced",
    "Dispatch",
    "CopyResource",
    "SetScissor",
    "SetViewport",
    "Clear",
    "ClearDepth",
    "ClearStencil",
    "BeginPass",
    "EndPass",
    "Count"};

static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace CommandType
//---------------------------------------------------------------------------//
enum DeviceExtensions
{
  DeviceExtensions_DebugCallback = 1 << 0,
};
//---------------------------------------------------------------------------//
namespace TextureFlags
{
enum Enum
{
  kDefault,
  kRenderTarget,
  kCompute,
  kCount
};

enum Mask
{
  kDefaultMask = 1 << 0,
  kRenderTargetMask = 1 << 1,
  kComputeMask = 1 << 2
};

static const char* kEnumNames[] = {"Default", "RenderTarget", "Compute", "Count"};
static const char* toString(Enum p_Enum)
{
  return ((uint32_t)p_Enum < Enum::kCount ? kEnumNames[(int)p_Enum] : "unsupported");
}
} // namespace TextureFlags
//---------------------------------------------------------------------------//
namespace PipelineStage
{
enum Enum
{
  kDrawIndirect = 0,
  kVertexInput = 1,
  kVertexShader = 2,
  kFragmentShader = 3,
  kRenderTarget = 4,
  kComputeShader = 5,
  kTransfer = 6
};

enum Mask
{
  kDrawIndirectMask = 1 << 0,
  kVertexInputMask = 1 << 1,
  kVertexShaderMask = 1 << 2,
  kFragmentShaderMask = 1 << 3,
  kRenderTargetMask = 1 << 4,
  kComputeShaderMask = 1 << 5,
  kTransferMask = 1 << 6
};
} // namespace PipelineStage
//---------------------------------------------------------------------------//
namespace RenderPassType
{
enum Enum
{
  kGeometry,
  kSwapchain,
  kCompute
};
} // namespace RenderPassType
//---------------------------------------------------------------------------//
namespace ResourceDeletionType
{
enum Enum
{
  kBuffer,
  kTexture,
  kPipeline,
  kSampler,
  kDescriptorSetLayout,
  kDescriptorSet,
  kRenderPass,
  kShaderState,
  kCount
};
} // namespace ResourceDeletionType
//---------------------------------------------------------------------------//
namespace PresentMode
{
enum Enum
{
  kImmediate,
  kVSync,
  kVSyncFast,
  kVSyncRelaxed,
  kCount
}; // enum Enum
} // namespace PresentMode
//---------------------------------------------------------------------------//
namespace RenderPassOperation
{
enum Enum
{
  kDontCare,
  kLoad,
  kClear,
  kCount
}; // enum Enum
} // namespace RenderPassOperation
//---------------------------------------------------------------------------//
// Taken from the Forge
typedef enum ResourceState
{
  RESOURCE_STATE_UNDEFINED = 0,
  RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER = 0x1,
  RESOURCE_STATE_INDEX_BUFFER = 0x2,
  RESOURCE_STATE_RENDER_TARGET = 0x4,
  RESOURCE_STATE_UNORDERED_ACCESS = 0x8,
  RESOURCE_STATE_DEPTH_WRITE = 0x10,
  RESOURCE_STATE_DEPTH_READ = 0x20,
  RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE = 0x40,
  RESOURCE_STATE_PIXEL_SHADER_RESOURCE = 0x80,
  RESOURCE_STATE_SHADER_RESOURCE = 0x40 | 0x80,
  RESOURCE_STATE_STREAM_OUT = 0x100,
  RESOURCE_STATE_INDIRECT_ARGUMENT = 0x200,
  RESOURCE_STATE_COPY_DEST = 0x400,
  RESOURCE_STATE_COPY_SOURCE = 0x800,
  RESOURCE_STATE_GENERIC_READ = (((((0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800),
  RESOURCE_STATE_PRESENT = 0x1000,
  RESOURCE_STATE_COMMON = 0x2000,
  RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE = 0x4000,
  RESOURCE_STATE_SHADING_RATE_SOURCE = 0x8000,
} ResourceState;
//---------------------------------------------------------------------------//
} // namespace Graphics
