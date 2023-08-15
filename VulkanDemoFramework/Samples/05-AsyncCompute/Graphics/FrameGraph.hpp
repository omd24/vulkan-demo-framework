#pragma once

#include "Foundation/Array.hpp"
#include "Foundation/ResourcePool.hpp"
#include "Foundation/HashMap.hpp"
#include "Foundation/Service.hpp"

#include "Graphics/GpuResources.hpp"

#include <vulkan/vulkan.h>

namespace Graphics
{
using namespace Framework;
struct Allocator;
struct CommandBuffer;
struct FrameGraph;
struct GpuDevice;
struct RenderScene;

typedef uint32_t FrameGraphHandle;

struct FrameGraphResourceHandle
{
  FrameGraphHandle index;
};

struct FrameGraphNodeHandle
{
  FrameGraphHandle index;
};

enum FrameGraphResourceType
{
  kFrameGraphResourceTypeInvalid = -1,

  kFrameGraphResourceTypeBuffer = 0,
  kFrameGraphResourceTypeTexture = 1,
  kFrameGraphResourceTypeAttachment = 2,
  kFrameGraphResourceTypeReference = 3
};

struct FrameGraphResourceInfo
{
  bool external = false;

  union
  {
    struct
    {
      size_t size;
      VkBufferUsageFlags flags;

      BufferHandle handle[kMaxFrames];
    } buffer;

    struct
    {
      uint32_t width;
      uint32_t height;
      uint32_t depth;
      float scaleWidth;
      float scaleHeight;

      VkFormat format;
      VkImageUsageFlags flags;

      RenderPassOperation::Enum loadOp;

      TextureHandle handle[kMaxFrames];
      float clearValues[4]; // Reused between color or depth/stencil.

      bool compute;
    } texture;
  };
};

// NOTE: an input could be used as a texture or as an attachment.
// If it's an attachment we want to control whether to discard previous
// content - for instance the first time we use it - or to load the data
// from a previous pass
// NOTE: an output always implies an attachment and a store op
struct FrameGraphResource
{
  FrameGraphResourceType type;
  FrameGraphResourceInfo resourceInfo;

  FrameGraphNodeHandle producer;
  FrameGraphResourceHandle outputHandle;

  int refCount = 0;

  const char* m_Name = nullptr;
};

struct FrameGraphResourceInputCreation
{
  FrameGraphResourceType type;
  FrameGraphResourceInfo resourceInfo;

  const char* name;
};

struct FrameGraphResourceOutputCreation
{
  FrameGraphResourceType type;
  FrameGraphResourceInfo resourceInfo;

  const char* name;
};

struct FrameGraphNodeCreation
{
  Framework::Array<FrameGraphResourceInputCreation> inputs;
  Framework::Array<FrameGraphResourceOutputCreation> outputs;

  bool enabled;

  const char* name;
};

struct FrameGraphRenderPass
{
  virtual void addUi() {}
  virtual void
  preRender(uint32_t currentFrameIndex, CommandBuffer* gpuCommands, FrameGraph* renderScene)
  {
  }
  virtual void render(CommandBuffer* gpuCommands, RenderScene* renderScene) {}
  virtual void onResize(GpuDevice& gpu, uint32_t newWidth, uint32_t newHeight) {}
};

struct FrameGraphNode
{
  int refCount = 0;

  RenderPassHandle renderPass;
  FramebufferHandle framebuffer[kMaxFrames];

  FrameGraphRenderPass* graphRenderPass;

  Framework::Array<FrameGraphResourceHandle> inputs;
  Framework::Array<FrameGraphResourceHandle> outputs;

  Framework::Array<FrameGraphNodeHandle> edges;

  float resolutionScaleWidth = 0.f;
  float resolutionScaleHeight = 0.f;

  bool compute = false;
  bool rayTracing = false;
  bool enabled = true;

  const char* name = nullptr;
};

struct FrameGraphRenderPassCache
{
  void init(Framework::Allocator* allocator);
  void shutdown();

  Framework::FlatHashMap<uint64_t, FrameGraphRenderPass*> renderPassMap;
};

struct FrameGraphResourceCache
{
  void init(Framework::Allocator* allocator, GpuDevice* device);
  void shutdown();

  GpuDevice* device;

  Framework::FlatHashMap<uint64_t, uint32_t> resourceMap;
  Framework::ResourcePoolTyped<FrameGraphResource> resources;
};

struct FrameGraphNodeCache
{
  void init(Framework::Allocator* allocator, GpuDevice* device);
  void shutdown();

  GpuDevice* device;

  Framework::FlatHashMap<uint64_t, uint32_t> nodeMap;
  Framework::ResourcePool nodes;
};

//
//
struct FrameGraphBuilder : public Framework::Service
{
  void init(GpuDevice* device);
  void shutdown();

  void registerRenderPass(const char* name, FrameGraphRenderPass* renderPass);

  FrameGraphResourceHandle
  createNodeOutput(const FrameGraphResourceOutputCreation& creation, FrameGraphNodeHandle producer);
  FrameGraphResourceHandle createNodeInput(const FrameGraphResourceInputCreation& creation);
  FrameGraphNodeHandle createNode(const FrameGraphNodeCreation& creation);

  FrameGraphNode* getNode(const char* name);
  FrameGraphNode* accessNode(FrameGraphNodeHandle handle);

  FrameGraphResource* getResource(const char* name);
  FrameGraphResource* accessResource(FrameGraphResourceHandle handle);

  FrameGraphResourceCache resourceCache;
  FrameGraphNodeCache nodeCache;
  FrameGraphRenderPassCache renderPassCache;

  Framework::Allocator* allocator;

  GpuDevice* device;

  static constexpr uint32_t kMaxRenderPassCount = 256;
  static constexpr uint32_t kMaxResourcesCount = 1024;
  static constexpr uint32_t kMaxNodesCount = 1024;

  static constexpr const char* kName = "frame_graph_builder_service";
};

//
//
struct FrameGraph
{
  void init(FrameGraphBuilder* builder);
  void shutdown();

  void parse(const char* filePath, Framework::StackAllocator* tempAllocator);

  // NOTE: each frame we rebuild the graph so that we can enable only
  // the nodes we are interested in
  void reset();
  void enableRenderPass(const char* renderPassName);
  void disableRenderPass(const char* renderPassName);
  void compile();
  void addUi();
  void render(uint32_t currentFrameIndex, CommandBuffer* gpuCommands, RenderScene* renderScene);
  void onResize(GpuDevice& gpu, uint32_t newWidth, uint32_t newHeight);

  FrameGraphNode* getNode(const char* name);
  FrameGraphNode* accessNode(FrameGraphNodeHandle handle);

  FrameGraphResource* getResource(const char* name);
  FrameGraphResource* accessResource(FrameGraphResourceHandle handle);

  // TODO: in case we want to add a pass in code
  void addNode(FrameGraphNodeCreation& node);

  // NOTE: nodes sorted in topological order
  Framework::Array<FrameGraphNodeHandle> nodes;
  Framework::Array<FrameGraphNodeHandle> allNodes;

  FrameGraphBuilder* builder;
  Framework::Allocator* allocator;

  Framework::LinearAllocator localAllocator;

  const char* name = nullptr;
};

} // namespace Graphics
