#include "FrameGraph.hpp"

#include "Foundation/Memory.hpp"
#include "Foundation/File.hpp"
#include "Foundation/String.hpp"

#include "Graphics/GpuDevice.hpp"

#include "Externals/json.hpp"

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
        // TODO(marco)
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

    FrameGraphNodeHandle node_handle = builder->createNode(nodeCreation);
    nodes.push(node_handle);
  }

  p_TempAllocator->freeMarker(currentAllocatorMarker);
}
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
