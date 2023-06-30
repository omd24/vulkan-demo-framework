#include "FrameGraph.hpp"

#include "Foundation/Memory.hpp"
#include "Foundation/File.hpp"
#include "Foundation/String.hpp"

#include "Graphics/GpuDevice.hpp"
#include "Graphics/CommandBuffer.hpp"

#include "Externals/json.hpp"

namespace Graphics
{
using namespace Framework;
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
static void createFramebuffer(FrameGraph* frameGraph, FrameGraphNode* p_Node)
{
  FramebufferCreation framebufferCreation{};
  framebufferCreation.renderPass = p_Node->renderPass;
  framebufferCreation.setName(p_Node->name);

  uint32_t width = 0;
  uint32_t height = 0;

  for (uint32_t r = 0; r < p_Node->outputs.m_Size; ++r)
  {
    FrameGraphResource* resource = frameGraph->accessResource(p_Node->outputs[r]);

    FrameGraphResourceInfo& info = resource->resourceInfo;

    if (resource->type == kFrameGraphResourceTypeBuffer ||
        resource->type == kFrameGraphResourceTypeReference)
    {
      continue;
    }

    if (width == 0)
    {
      width = info.texture.width;
    }
    else
    {
      assert(width == info.texture.width);
    }

    if (height == 0)
    {
      height = info.texture.height;
    }
    else
    {
      assert(height == info.texture.height);
    }

    if (info.texture.format == VK_FORMAT_D32_SFLOAT)
    {
      framebufferCreation.setDepthStencilTexture(info.texture.texture);
    }
    else
    {
      framebufferCreation.addRenderTexture(info.texture.texture);
    }
  }

  for (uint32_t r = 0; r < p_Node->inputs.m_Size; ++r)
  {
    FrameGraphResource* inputResource = frameGraph->accessResource(p_Node->inputs[r]);

    if (inputResource->type == kFrameGraphResourceTypeBuffer ||
        inputResource->type == kFrameGraphResourceTypeReference)
    {
      continue;
    }

    FrameGraphResource* resource = frameGraph->getResource(inputResource->m_Name);

    FrameGraphResourceInfo& info = resource->resourceInfo;

    inputResource->resourceInfo.texture.texture = info.texture.texture;

    if (width == 0)
    {
      width = info.texture.width;
    }
    else
    {
      assert(width == info.texture.width);
    }

    if (height == 0)
    {
      height = info.texture.height;
    }
    else
    {
      assert(height == info.texture.height);
    }

    if (inputResource->type == kFrameGraphResourceTypeTexture)
    {
      continue;
    }

    if (info.texture.format == VK_FORMAT_D32_SFLOAT)
    {
      framebufferCreation.setDepthStencilTexture(info.texture.texture);
    }
    else
    {
      framebufferCreation.addRenderTexture(info.texture.texture);
    }
  }

  framebufferCreation.width = width;
  framebufferCreation.height = height;
  p_Node->framebuffer = frameGraph->builder->device->createFramebuffer(framebufferCreation);
}
//---------------------------------------------------------------------------//
static void createRenderPass(FrameGraph* p_FrameGraph, FrameGraphNode* p_Node)
{
  RenderPassCreation renderPassCreation{};
  renderPassCreation.setName(p_Node->name);

  // NOTE: first create the outputs, then we can patch the input resources
  // with the right handles
  for (size_t i = 0; i < p_Node->outputs.m_Size; ++i)
  {
    FrameGraphResource* outputResource = p_FrameGraph->accessResource(p_Node->outputs[i]);

    FrameGraphResourceInfo& info = outputResource->resourceInfo;

    if (outputResource->type == kFrameGraphResourceTypeAttachment)
    {
      if (info.texture.format == VK_FORMAT_D32_SFLOAT)
      {
        renderPassCreation.setDepthStencilTexture(
            info.texture.format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        renderPassCreation.depthOperation = RenderPassOperation::kClear;
      }
      else
      {
        renderPassCreation.addAttachment(
            info.texture.format, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, info.texture.loadOp);
      }
    }
  }

  for (size_t i = 0; i < p_Node->inputs.m_Size; ++i)
  {
    FrameGraphResource* inputResource = p_FrameGraph->accessResource(p_Node->inputs[i]);

    FrameGraphResourceInfo& info = inputResource->resourceInfo;

    if (inputResource->type == kFrameGraphResourceTypeAttachment)
    {
      if (info.texture.format == VK_FORMAT_D32_SFLOAT)
      {
        renderPassCreation.setDepthStencilTexture(
            info.texture.format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        renderPassCreation.depthOperation = RenderPassOperation::kLoad;
      }
      else
      {
        renderPassCreation.addAttachment(
            info.texture.format,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            RenderPassOperation::kLoad);
      }
    }
  }

  // TODO: make sure formats are valid for attachment
  p_Node->renderPass = p_FrameGraph->builder->device->createRenderPass(renderPassCreation);
}
//---------------------------------------------------------------------------//
static void computeEdges(FrameGraph* p_FrameGraph, FrameGraphNode* p_Node, uint32_t p_NodeIndex)
{
  for (uint32_t r = 0; r < p_Node->inputs.m_Size; ++r)
  {
    FrameGraphResource* resource = p_FrameGraph->accessResource(p_Node->inputs[r]);

    FrameGraphResource* outputResource = p_FrameGraph->getResource(resource->m_Name);
    if (outputResource == nullptr && !resource->resourceInfo.external)
    {
      // TODO: external resources
      assert(false && "Requested resource is not produced by any node and is not external.");
      continue;
    }

    resource->producer = outputResource->producer;
    resource->resourceInfo = outputResource->resourceInfo;
    resource->outputHandle = outputResource->outputHandle;

    FrameGraphNode* parentNode = p_FrameGraph->accessNode(resource->producer);

    // printf( "Adding edge from %s [%d] to %s [%d]\n", parentNode->name, resource->producer.index,
    // p_Node->name, p_NodeIndex )

    parentNode->edges.push(p_FrameGraph->nodes[p_NodeIndex]);
  }
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
void FrameGraphRenderPassCache::init(Framework::Allocator* p_Allocator)
{
  renderPassMap.init(p_Allocator, FrameGraphBuilder::kMaxRenderPassCount);
}
//---------------------------------------------------------------------------//
void FrameGraphRenderPassCache::shutdown() { renderPassMap.shutdown(); }
//---------------------------------------------------------------------------//
// FrameGraphResourceCache
//---------------------------------------------------------------------------//
void FrameGraphResourceCache::init(Framework::Allocator* p_Allocator, GpuDevice* p_Device)
{
  device = p_Device;

  resources.init(p_Allocator, FrameGraphBuilder::kMaxResourcesCount);
  resourceMap.init(p_Allocator, FrameGraphBuilder::kMaxResourcesCount);
}
//---------------------------------------------------------------------------//
void FrameGraphResourceCache::shutdown()
{
  Framework::FlatHashMapIterator it = resourceMap.iteratorBegin();
  while (it.isValid())
  {

    uint32_t resourceIndex = resourceMap.get(it);
    FrameGraphResource* resource = resources.get(resourceIndex);

    if (resource->type == kFrameGraphResourceTypeTexture ||
        resource->type == kFrameGraphResourceTypeAttachment)
    {
      Texture* texture =
          (Texture*)device->m_Textures.accessResource(resource->resourceInfo.texture.texture.index);
      device->destroyTexture(texture->handle);
    }
    else if (resource->type == kFrameGraphResourceTypeBuffer)
    {
      Buffer* buffer =
          (Buffer*)device->m_Buffers.accessResource(resource->resourceInfo.buffer.buffer.index);
      device->destroyBuffer(buffer->handle);
    }

    resourceMap.iteratorAdvance(it);
  }

  resources.freeAllResources();
  resources.shutdown();
  resourceMap.shutdown();
}
//---------------------------------------------------------------------------//
// FrameGraphNodeCache
//---------------------------------------------------------------------------//
void FrameGraphNodeCache::init(Framework::Allocator* p_Allocator, GpuDevice* p_Device)
{
  device = p_Device;

  nodes.init(p_Allocator, FrameGraphBuilder::kMaxNodesCount, sizeof(FrameGraphNode));
  nodeMap.init(p_Allocator, FrameGraphBuilder::kMaxNodesCount);
}
//---------------------------------------------------------------------------//
void FrameGraphNodeCache::shutdown()
{
  nodes.freeAllResources();
  nodes.shutdown();
  nodeMap.shutdown();
}
//---------------------------------------------------------------------------//
// FrameGraphBuilder
//---------------------------------------------------------------------------//
void FrameGraphBuilder::init(GpuDevice* p_Device)
{
  device = p_Device;
  allocator = device->m_Allocator;

  resourceCache.init(allocator, device);
  nodeCache.init(allocator, device);
  renderPassCache.init(allocator);
}
//---------------------------------------------------------------------------//
void FrameGraphBuilder::shutdown()
{
  resourceCache.shutdown();
  nodeCache.shutdown();
  renderPassCache.shutdown();
}
//---------------------------------------------------------------------------//
void FrameGraphBuilder::registerRenderPass(const char* p_Name, FrameGraphRenderPass* p_RenderPass)
{
  uint64_t key = Framework::hashCalculate(p_Name);

  FlatHashMapIterator it = renderPassCache.renderPassMap.find(key);
  if (it.isValid())
  {
    return;
  }

  renderPassCache.renderPassMap.insert(key, p_RenderPass);

  it = nodeCache.nodeMap.find(key);
  assert(it.isValid());

  FrameGraphNode* node = (FrameGraphNode*)nodeCache.nodes.accessResource(nodeCache.nodeMap.get(it));
  node->graphRenderPass = p_RenderPass;
}
//---------------------------------------------------------------------------//
FrameGraphResourceHandle FrameGraphBuilder::createNodeOutput(
    const FrameGraphResourceOutputCreation& p_Creation, FrameGraphNodeHandle p_Producer)
{
  FrameGraphResourceHandle resourceHandle{kInvalidIndex};
  resourceHandle.index = resourceCache.resources.obtainResource();

  if (resourceHandle.index == kInvalidIndex)
  {
    return resourceHandle;
  }

  FrameGraphResource* resource = resourceCache.resources.get(resourceHandle.index);
  resource->m_Name = p_Creation.name;
  resource->type = p_Creation.type;

  if (p_Creation.type != kFrameGraphResourceTypeReference)
  {
    resource->resourceInfo = p_Creation.resourceInfo;
    resource->outputHandle = resourceHandle;
    resource->producer = p_Producer;
    resource->refCount = 0;

    resourceCache.resourceMap.insert(
        Framework::hashBytes((void*)resource->m_Name, strlen(p_Creation.name)),
        resourceHandle.index);
  }

  return resourceHandle;
}
//---------------------------------------------------------------------------//
FrameGraphResourceHandle
FrameGraphBuilder::createNodeInput(const FrameGraphResourceInputCreation& creation)
{
  FrameGraphResourceHandle resourceHandle = {kInvalidIndex};

  resourceHandle.index = resourceCache.resources.obtainResource();

  if (resourceHandle.index == kInvalidIndex)
  {
    return resourceHandle;
  }

  FrameGraphResource* resource = resourceCache.resources.get(resourceHandle.index);

  resource->resourceInfo = {};
  resource->producer.index = kInvalidIndex;
  resource->outputHandle.index = kInvalidIndex;
  resource->type = creation.type;
  resource->m_Name = creation.name;
  resource->refCount = 0;

  return resourceHandle;
}
//---------------------------------------------------------------------------//
FrameGraphNodeHandle FrameGraphBuilder::createNode(const FrameGraphNodeCreation& p_Creation)
{
  FrameGraphNodeHandle nodeHandle{kInvalidIndex};
  nodeHandle.index = nodeCache.nodes.obtainResource();

  if (nodeHandle.index == kInvalidIndex)
  {
    return nodeHandle;
  }

  FrameGraphNode* node = (FrameGraphNode*)nodeCache.nodes.accessResource(nodeHandle.index);
  node->name = p_Creation.name;
  node->enabled = p_Creation.enabled;
  node->inputs.init(allocator, p_Creation.inputs.m_Size);
  node->outputs.init(allocator, p_Creation.outputs.m_Size);
  node->edges.init(allocator, p_Creation.outputs.m_Size);
  node->framebuffer = kInvalidFramebuffer;
  node->renderPass = {kInvalidIndex};

  nodeCache.nodeMap.insert(
      Framework::hashBytes((void*)node->name, strlen(node->name)), nodeHandle.index);

  // NOTE: first create the outputs, then we can patch the input resources
  // with the right handles
  for (size_t i = 0; i < p_Creation.outputs.m_Size; ++i)
  {
    const FrameGraphResourceOutputCreation& outputCreation = p_Creation.outputs[i];

    FrameGraphResourceHandle output = createNodeOutput(outputCreation, nodeHandle);

    node->outputs.push(output);
  }

  for (size_t i = 0; i < p_Creation.inputs.m_Size; ++i)
  {
    const FrameGraphResourceInputCreation& inputCreation = p_Creation.inputs[i];

    FrameGraphResourceHandle inputHandle = createNodeInput(inputCreation);

    node->inputs.push(inputHandle);
  }

  return nodeHandle;
}
//---------------------------------------------------------------------------//
FrameGraphNode* FrameGraphBuilder::getNode(const char* name)
{
  FlatHashMapIterator it = nodeCache.nodeMap.find(Framework::hashCalculate(name));
  if (it.isInvalid())
  {
    return nullptr;
  }

  FrameGraphNode* node = (FrameGraphNode*)nodeCache.nodes.accessResource(nodeCache.nodeMap.get(it));

  return node;
}
//---------------------------------------------------------------------------//
FrameGraphNode* FrameGraphBuilder::accessNode(FrameGraphNodeHandle handle)
{
  FrameGraphNode* node = (FrameGraphNode*)nodeCache.nodes.accessResource(handle.index);

  return node;
}
//---------------------------------------------------------------------------//
FrameGraphResource* FrameGraphBuilder::getResource(const char* name)
{
  FlatHashMapIterator it = resourceCache.resourceMap.find(Framework::hashCalculate(name));
  if (it.isInvalid())
  {
    return nullptr;
  }

  FrameGraphResource* resource = resourceCache.resources.get(resourceCache.resourceMap.get(it));

  return resource;
}
//---------------------------------------------------------------------------//
FrameGraphResource* FrameGraphBuilder::accessResource(FrameGraphResourceHandle handle)
{
  FrameGraphResource* resource = resourceCache.resources.get(handle.index);

  return resource;
}
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
void FrameGraph::parse(const char* p_FilePath, Framework::StackAllocator* p_TempAllocator)
{
  using json = nlohmann::json;

  if (!Framework::fileExists(p_FilePath))
  {
    printf("Cannot find file %s\n", p_FilePath);
    return;
  }

  size_t currentAllocatorMarker = p_TempAllocator->getMarker();

  Framework::FileReadResult readResult = Framework::fileReadText(p_FilePath, p_TempAllocator);

  json graphData = json::parse(readResult.data);

  Framework::StringBuffer stringBuffer;
  stringBuffer.init(1024, &localAllocator);

  std::string nameValue = graphData.value("name", "");
  name = stringBuffer.appendUseFormatted("%s", nameValue.c_str());

  json passes = graphData["passes"];
  for (size_t i = 0; i < passes.size(); ++i)
  {
    json pass = passes[i];

    json passInputs = pass["inputs"];
    json passOutputs = pass["outputs"];

    FrameGraphNodeCreation nodeCreation{};
    nodeCreation.inputs.init(p_TempAllocator, passInputs.size());
    nodeCreation.outputs.init(p_TempAllocator, passOutputs.size());

    for (size_t ii = 0; ii < passInputs.size(); ++ii)
    {
      json passInput = passInputs[ii];

      FrameGraphResourceInputCreation inputCreation{};

      std::string inputType = passInput.value("type", "");
      assert(!inputType.empty());

      std::string inputName = passInput.value("name", "");
      assert(!inputName.empty());

      inputCreation.type = stringToResourceType(inputType.c_str());
      inputCreation.resourceInfo.external = false;

      inputCreation.name = stringBuffer.appendUseFormatted("%s", inputName.c_str());

      nodeCreation.inputs.push(inputCreation);
    }

    for (size_t oi = 0; oi < passOutputs.size(); ++oi)
    {
      json passOutput = passOutputs[oi];

      FrameGraphResourceOutputCreation outputCreation{};

      std::string outputType = passOutput.value("type", "");
      assert(!outputType.empty());

      std::string output_name = passOutput.value("name", "");
      assert(!output_name.empty());

      outputCreation.type = stringToResourceType(outputType.c_str());

      outputCreation.name = stringBuffer.appendUseFormatted("%s", output_name.c_str());

      switch (outputCreation.type)
      {
      case kFrameGraphResourceTypeAttachment:
      case kFrameGraphResourceTypeTexture: {
        std::string format = passOutput.value("format", "");
        assert(!format.empty());

        outputCreation.resourceInfo.texture.format = utilStringToVkFormat(format.c_str());

        std::string loadOp = passOutput.value("op", "");
        assert(!loadOp.empty());

        outputCreation.resourceInfo.texture.loadOp = stringToRenderPassOperation(loadOp.c_str());

        json resolution = passOutput["resolution"];

        outputCreation.resourceInfo.texture.width = resolution[0];
        outputCreation.resourceInfo.texture.height = resolution[1];
        outputCreation.resourceInfo.texture.depth = 1;
      }
      break;
      case kFrameGraphResourceTypeBuffer: {
        // TODO
        assert(false);
      }
      break;
      }

      nodeCreation.outputs.push(outputCreation);
    }

    nameValue = pass.value("name", "");
    assert(!nameValue.empty());

    bool enabled = pass.value("enabled", true);

    nodeCreation.name = stringBuffer.appendUseFormatted("%s", nameValue.c_str());
    nodeCreation.enabled = enabled;

    FrameGraphNodeHandle nodeHandle = builder->createNode(nodeCreation);
    nodes.push(nodeHandle);
  }

  p_TempAllocator->freeMarker(currentAllocatorMarker);
}
//---------------------------------------------------------------------------//
void FrameGraph::enableRenderPass(const char* p_RenderPassName)
{
  FrameGraphNode* node = builder->getNode(p_RenderPassName);
  node->enabled = true;
}
//---------------------------------------------------------------------------//
void FrameGraph::disableRenderPass(const char* p_RenderPassName)
{
  FrameGraphNode* node = builder->getNode(p_RenderPassName);
  node->enabled = false;
}
//---------------------------------------------------------------------------//
void FrameGraph::compile()
{
  // - check that input has been produced by a different node
  // - cull inactive nodes

  for (uint32_t i = 0; i < nodes.m_Size; ++i)
  {
    FrameGraphNode* node = builder->accessNode(nodes[i]);

    // NOTE: we want to clear all edges first, then populate them. If we clear them inside
    // the loop below we risk clearing the list after it has already been used by one of the child
    // nodes
    node->edges.clear();
  }

  for (uint32_t i = 0; i < nodes.m_Size; ++i)
  {
    FrameGraphNode* node = builder->accessNode(nodes[i]);
    if (!node->enabled)
    {
      continue;
    }

    computeEdges(this, node, i);
  }

  Framework::Array<FrameGraphNodeHandle> sortedNodes;
  sortedNodes.init(&localAllocator, nodes.m_Size);

  Framework::Array<uint8_t> visited;
  visited.init(&localAllocator, nodes.m_Size, nodes.m_Size);
  memset(visited.m_Data, 0, sizeof(bool) * nodes.m_Size);

  Framework::Array<FrameGraphNodeHandle> stack;
  stack.init(&localAllocator, nodes.m_Size);

  // Topological sorting
  for (uint32_t n = 0; n < nodes.m_Size; ++n)
  {
    FrameGraphNode* node = builder->accessNode(nodes[n]);
    if (!node->enabled)
    {
      continue;
    }

    stack.push(nodes[n]);

    while (stack.m_Size > 0)
    {
      FrameGraphNodeHandle nodeHandle = stack.back();

      if (visited[nodeHandle.index] == 2)
      {
        stack.pop();

        continue;
      }

      if (visited[nodeHandle.index] == 1)
      {
        visited[nodeHandle.index] = 2; // added

        sortedNodes.push(nodeHandle);

        stack.pop();

        continue;
      }

      visited[nodeHandle.index] = 1; // visited

      FrameGraphNode* node = builder->accessNode(nodeHandle);

      // Leaf node
      if (node->edges.m_Size == 0)
      {
        continue;
      }

      for (uint32_t r = 0; r < node->edges.m_Size; ++r)
      {
        FrameGraphNodeHandle child_handle = node->edges[r];

        if (!visited[child_handle.index])
        {
          stack.push(child_handle);
        }
      }
    }
  }

  assert(sortedNodes.m_Size == nodes.m_Size);

  nodes.clear();

  for (int i = sortedNodes.m_Size - 1; i >= 0; --i)
  {
    nodes.push(sortedNodes[i]);
  }

  visited.shutdown();
  stack.shutdown();
  sortedNodes.shutdown();

  // NOTE: allocations and deallocations are used for verification purposes only
  size_t resourceCount = builder->resourceCache.resources.m_UsedIndices;
  Framework::Array<FrameGraphNodeHandle> allocations;
  allocations.init(&localAllocator, resourceCount, resourceCount);
  for (uint32_t i = 0; i < resourceCount; ++i)
  {
    allocations[i].index = kInvalidIndex;
  }

  Framework::Array<FrameGraphNodeHandle> deallocations;
  deallocations.init(&localAllocator, resourceCount, resourceCount);
  for (uint32_t i = 0; i < resourceCount; ++i)
  {
    deallocations[i].index = kInvalidIndex;
  }

  Framework::Array<TextureHandle> freeList;
  freeList.init(&localAllocator, resourceCount);

  size_t peak_memory = 0;
  size_t instant_memory = 0;

  for (uint32_t i = 0; i < nodes.m_Size; ++i)
  {
    FrameGraphNode* node = builder->accessNode(nodes[i]);
    if (!node->enabled)
    {
      continue;
    }

    for (uint32_t j = 0; j < node->inputs.m_Size; ++j)
    {
      FrameGraphResource* inputResource = builder->accessResource(node->inputs[j]);
      FrameGraphResource* resource = builder->accessResource(inputResource->outputHandle);

      resource->refCount++;
    }
  }

  for (uint32_t i = 0; i < nodes.m_Size; ++i)
  {
    FrameGraphNode* node = builder->accessNode(nodes[i]);
    if (!node->enabled)
    {
      continue;
    }

    for (uint32_t j = 0; j < node->outputs.m_Size; ++j)
    {
      uint32_t resourceIndex = node->outputs[j].index;
      FrameGraphResource* resource = builder->accessResource(node->outputs[j]);

      if (!resource->resourceInfo.external && allocations[resourceIndex].index == kInvalidIndex)
      {
        assert(deallocations[resourceIndex].index == kInvalidIndex);
        allocations[resourceIndex] = nodes[i];

        if (resource->type == kFrameGraphResourceTypeAttachment)
        {
          FrameGraphResourceInfo& info = resource->resourceInfo;

          if (freeList.m_Size > 0)
          {
            // TODO: find best fit
            TextureHandle aliasTexture = freeList.back();
            freeList.pop();

            TextureCreation textureCreation{};
            textureCreation.setData(nullptr)
                .setAlias(aliasTexture)
                .setName(resource->m_Name)
                .setFormatType(info.texture.format, TextureType::Enum::kTexture2D)
                .setSize(info.texture.width, info.texture.height, info.texture.depth)
                .setFlags(1, TextureFlags::kRenderTargetMask);
            TextureHandle handle = builder->device->createTexture(textureCreation);

            info.texture.texture = handle;
          }
          else
          {
            TextureCreation textureCreation{};
            textureCreation.setData(nullptr)
                .setName(resource->m_Name)
                .setFormatType(info.texture.format, TextureType::Enum::kTexture2D)
                .setSize(info.texture.width, info.texture.height, info.texture.depth)
                .setFlags(1, TextureFlags::kRenderTargetMask);
            TextureHandle handle = builder->device->createTexture(textureCreation);

            info.texture.texture = handle;
          }
        }

        printf("Output %s allocated on node %d\n", resource->m_Name, nodes[i].index);
      }
    }

    for (uint32_t j = 0; j < node->inputs.m_Size; ++j)
    {
      FrameGraphResource* inputResource = builder->accessResource(node->inputs[j]);

      uint32_t resourceIndex = inputResource->outputHandle.index;
      FrameGraphResource* resource = builder->accessResource(inputResource->outputHandle);

      resource->refCount--;

      if (!resource->resourceInfo.external && resource->refCount == 0)
      {
        assert(deallocations[resourceIndex].index == kInvalidIndex);
        deallocations[resourceIndex] = nodes[i];

        if (resource->type == kFrameGraphResourceTypeAttachment ||
            resource->type == kFrameGraphResourceTypeTexture)
        {
          freeList.push(resource->resourceInfo.texture.texture);
        }

        printf("Output %s deallocated on node %d\n", resource->m_Name, nodes[i].index);
      }
    }
  }

  allocations.shutdown();
  deallocations.shutdown();
  freeList.shutdown();

  for (uint32_t i = 0; i < nodes.m_Size; ++i)
  {
    FrameGraphNode* node = builder->accessNode(nodes[i]);
    if (!node->enabled)
    {
      continue;
    }

    if (node->renderPass.index == kInvalidIndex)
    {
      createRenderPass(this, node);
    }

    if (node->framebuffer.index == kInvalidIndex)
    {
      createFramebuffer(this, node);
    }
  }
}
//---------------------------------------------------------------------------//
void FrameGraph::addUi()
{
  for (uint32_t n = 0; n < nodes.m_Size; ++n)
  {
    FrameGraphNode* node = builder->accessNode(nodes[n]);
    if (!node->enabled)
    {
      continue;
    }

    node->graphRenderPass->addUi();
  }
}
//---------------------------------------------------------------------------//
void FrameGraph::render(CommandBuffer* p_GpuCommands, RenderScene* p_RenderScene)
{
  for (uint32_t n = 0; n < nodes.m_Size; ++n)
  {
    FrameGraphNode* node = builder->accessNode(nodes[n]);
    if (!node->enabled)
    {
      continue;
    }

    // TODO: add clear colour to json
    p_GpuCommands->clear(0.3f, 0.3f, 0.3f, 1.f);
    p_GpuCommands->clearDepthStencil(1.0f, 0);

    uint32_t width = 0;
    uint32_t height = 0;

    for (uint32_t i = 0; i < node->inputs.m_Size; ++i)
    {
      FrameGraphResource* resource = builder->accessResource(node->inputs[i]);

      if (resource->type == kFrameGraphResourceTypeTexture)
      {
        Texture* texture = (Texture*)p_GpuCommands->m_GpuDevice->m_Textures.accessResource(
            resource->resourceInfo.texture.texture.index);

        utilAddImageBarrier(
            p_GpuCommands->m_VulkanCmdBuffer,
            texture->vkImage,
            RESOURCE_STATE_RENDER_TARGET,
            RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            0,
            1,
            resource->resourceInfo.texture.format == VK_FORMAT_D32_SFLOAT);
      }
      else if (resource->type == kFrameGraphResourceTypeAttachment)
      {
        Texture* texture = (Texture*)p_GpuCommands->m_GpuDevice->m_Textures.accessResource(
            resource->resourceInfo.texture.texture.index);

        width = texture->width;
        height = texture->height;
      }
    }

    for (uint32_t o = 0; o < node->outputs.m_Size; ++o)
    {
      FrameGraphResource* resource = builder->accessResource(node->outputs[o]);

      if (resource->type == kFrameGraphResourceTypeAttachment)
      {
        Texture* texture = (Texture*)p_GpuCommands->m_GpuDevice->m_Textures.accessResource(
            resource->resourceInfo.texture.texture.index);

        width = texture->width;
        height = texture->height;

        if (texture->vkFormat == VK_FORMAT_D32_SFLOAT)
        {
          utilAddImageBarrier(
              p_GpuCommands->m_VulkanCmdBuffer,
              texture->vkImage,
              RESOURCE_STATE_UNDEFINED,
              RESOURCE_STATE_DEPTH_WRITE,
              0,
              1,
              resource->resourceInfo.texture.format == VK_FORMAT_D32_SFLOAT);
        }
        else
        {
          utilAddImageBarrier(
              p_GpuCommands->m_VulkanCmdBuffer,
              texture->vkImage,
              RESOURCE_STATE_UNDEFINED,
              RESOURCE_STATE_RENDER_TARGET,
              0,
              1,
              resource->resourceInfo.texture.format == VK_FORMAT_D32_SFLOAT);
        }
      }
    }

    Rect2DInt scissor{0, 0, (uint16_t)width, (uint16_t)height};
    p_GpuCommands->setScissor(&scissor);

    Viewport viewport{};
    viewport.rect = {0, 0, (uint16_t)width, (uint16_t)height};
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    p_GpuCommands->setViewport(&viewport);

    node->graphRenderPass->preRender(p_GpuCommands, p_RenderScene);

    p_GpuCommands->bindPass(node->renderPass, node->framebuffer, false);

    node->graphRenderPass->render(p_GpuCommands, p_RenderScene);

    p_GpuCommands->endCurrentRenderPass();
  }
}

//---------------------------------------------------------------------------//
void FrameGraph::onResize(GpuDevice& gpu, uint32_t newWidth, uint32_t newHeight)
{
  for (uint32_t n = 0; n < nodes.m_Size; ++n)
  {
    FrameGraphNode* node = builder->accessNode(nodes[n]);
    if (!node->enabled)
    {
      continue;
    }

    node->graphRenderPass->onResize(gpu, newWidth, newHeight);

    gpu.resizeOutputTextures(node->framebuffer, newWidth, newHeight);
  }
}
//---------------------------------------------------------------------------//
FrameGraphNode* FrameGraph::getNode(const char* name) { return builder->getNode(name); }
//---------------------------------------------------------------------------//
FrameGraphNode* FrameGraph::accessNode(FrameGraphNodeHandle handle)
{
  return builder->accessNode(handle);
}
//---------------------------------------------------------------------------//
FrameGraphResource* FrameGraph::getResource(const char* name) { return builder->getResource(name); }
//---------------------------------------------------------------------------//
FrameGraphResource* FrameGraph::accessResource(FrameGraphResourceHandle handle)
{
  return builder->accessResource(handle);
}
//---------------------------------------------------------------------------//
} // namespace Graphics