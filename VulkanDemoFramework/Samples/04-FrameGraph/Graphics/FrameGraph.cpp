#include "FrameGraph.hpp"

#include "Foundation/Memory.hpp"

#include "Graphics/GpuDevice.hpp"

namespace Graphics
{
//---------------------------------------------------------------------------//
// Helper functions:
//---------------------------------------------------------------------------//
static FrameGraphResourceType stringToResourceType(const char* p_InputType)
{
  if (strcmp(p_InputType, "texture") == 0)
  {
    return kFrameGraphResourceTypeTexture;
  }

  if (strcmp(p_InputType, "attachment") == 0)
  {
    return kFrameGraphResourceTypeAttachment;
  }

  if (strcmp(p_InputType, "buffer") == 0)
  {
    return kFrameGraphResourceTypeBuffer;
  }

  if (strcmp(p_InputType, "reference") == 0)
  {
    // This is used for resources that need to create an edge but are not actually
    // used by the render pass
    return kFrameGraphResourceTypeReference;
  }

  assert(false);
  return kFrameGraphResourceTypeInvalid;
}
//---------------------------------------------------------------------------//
RenderPassOperation::Enum stringToRenderPassOperation(const char* p_OP)
{
  if (strcmp(p_OP, "VK_ATTACHMENT_LOAD_OP_CLEAR") == 0)
  {
    return RenderPassOperation::kClear;
  }
  else if (strcmp(p_OP, "VK_ATTACHMENT_LOAD_OP_LOAD") == 0)
  {
    return RenderPassOperation::kLoad;
  }

  assert(false);
  return RenderPassOperation::kDontCare;
}
//---------------------------------------------------------------------------//
// FrameGraphRenderPassCache
//---------------------------------------------------------------------------//
void FrameGraphRenderPassCache::init(Allocator* allocator);
//---------------------------------------------------------------------------//
void FrameGraphRenderPassCache::shutdown();
//---------------------------------------------------------------------------//
// FrameGraphResourceCache
//---------------------------------------------------------------------------//
void FrameGraphResourceCache::init(Allocator* allocator, GpuDevice* device);
//---------------------------------------------------------------------------//
void FrameGraphResourceCache::shutdown();
//---------------------------------------------------------------------------//
// FrameGraphNodeCache
//---------------------------------------------------------------------------//
void FrameGraphNodeCache::init(Allocator* allocator, GpuDevice* device);
//---------------------------------------------------------------------------//
void FrameGraphNodeCache::shutdown();
//---------------------------------------------------------------------------//
// FrameGraphBuilder
//---------------------------------------------------------------------------//
void FrameGraphBuilder::init(GpuDevice* device);
//---------------------------------------------------------------------------//
void FrameGraphBuilder::shutdown();
//---------------------------------------------------------------------------//
void FrameGraphBuilder::registerRenderPass(const char* name, FrameGraphRenderPass* renderPass);
//---------------------------------------------------------------------------//
FrameGraphResourceHandle FrameGraphBuilder::createNodeOutput(
    const FrameGraphResourceOutputCreation& creation, FrameGraphNodeHandle producer);
//---------------------------------------------------------------------------//
FrameGraphResourceHandle
FrameGraphBuilder::createNodeInput(const FrameGraphResourceInputCreation& creation);
//---------------------------------------------------------------------------//
FrameGraphNodeHandle FrameGraphBuilder::createNode(const FrameGraphNodeCreation& creation);
//---------------------------------------------------------------------------//
FrameGraphNode* FrameGraphBuilder::getNode(const char* name);
//---------------------------------------------------------------------------//
FrameGraphNode* FrameGraphBuilder::accessNode(FrameGraphNodeHandle handle);
//---------------------------------------------------------------------------//
FrameGraphResource* FrameGraphBuilder::getResource(const char* name);
//---------------------------------------------------------------------------//
FrameGraphResource* FrameGraphBuilder::accessResource(FrameGraphResourceHandle handle);
//---------------------------------------------------------------------------//
// FrameGraph
//---------------------------------------------------------------------------//
void FrameGraph::init(FrameGraphBuilder* p_Builder)
{
  allocator = &Framework::MemoryService::instance()->m_SystemAllocator;

  localAllocator.init(FRAMEWORK_MEGA(1));

  builder = p_Builder;

  nodes.init(allocator, FrameGraphBuilder::kMaxNodesCount);
}
//---------------------------------------------------------------------------//
void FrameGraph::shutdown()
{
  for (uint32_t i = 0; i < nodes.m_Size; ++i)
  {
    FrameGraphNodeHandle handle = nodes[i];
    FrameGraphNode* node = builder->accessNode(handle);

    builder->device->destroyRenderPass(node->renderPass);
    builder->device->destroyFramebuffer(node->framebuffer);

    node->inputs.shutdown();
    node->outputs.shutdown();
    node->edges.shutdown();
  }

  nodes.shutdown();

  localAllocator.shutdown();
}
//---------------------------------------------------------------------------//
void FrameGraph::parse(const char* filePath, Framework::StackAllocator* tempAllocator);
//---------------------------------------------------------------------------//
void FrameGraph::reset();
//---------------------------------------------------------------------------//
void FrameGraph::enableRenderPass(const char* renderPassName);
//---------------------------------------------------------------------------//
void FrameGraph::disableRenderPass(const char* renderPassName);
//---------------------------------------------------------------------------//
void FrameGraph::compile();
//---------------------------------------------------------------------------//
void FrameGraph::addUi();
//---------------------------------------------------------------------------//
void FrameGraph::render(CommandBuffer* gpuCommands, RenderScene* renderScene);
//---------------------------------------------------------------------------//
void FrameGraph::onResize(GpuDevice& gpu, uint32_t newWidth, uint32_t newHeight);
//---------------------------------------------------------------------------//
FrameGraphNode* FrameGraph::getNode(const char* name);
//---------------------------------------------------------------------------//
FrameGraphNode* FrameGraph::accessNode(FrameGraphNodeHandle handle);
//---------------------------------------------------------------------------//
FrameGraphResource* FrameGraph::getResource(const char* name);
//---------------------------------------------------------------------------//
FrameGraphResource* FrameGraph::accessResource(FrameGraphResourceHandle handle);
//---------------------------------------------------------------------------//
void FrameGraph::addNode(FrameGraphNodeCreation& node);
//---------------------------------------------------------------------------//
} // namespace Graphics
