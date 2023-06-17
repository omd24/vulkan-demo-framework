#include "Graphics/RenderResourcesLoader.hpp"

#include "Graphics/FrameGraph.hpp"
#include "Foundation/File.hpp"

#include "Externals/json.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "Externals/stb_image.h"

namespace Graphics
{
//---------------------------------------------------------------------------//
// Local utils:
//---------------------------------------------------------------------------//
Directory g_Cwd{};
//---------------------------------------------------------------------------//
static void shaderConcatenate(
    const char* filename,
    Framework::StringBuffer& pathBuffer,
    Framework::StringBuffer& p_ShaderBuffer,
    Framework::Allocator* p_TempAllocator)
{
  using namespace Framework;

  Framework::directoryCurrent(&g_Cwd);

  // Read file and concatenate it
  pathBuffer.clear();
  cstring shaderPath = pathBuffer.appendUseFormatted("%s%s%s", g_Cwd.path, SHADER_FOLDER, filename);
  FileReadResult shaderReadResult = fileReadText(shaderPath, p_TempAllocator);
  if (shaderReadResult.data)
  {
    // Append without null termination and add termination later.
    p_ShaderBuffer.appendMemory(shaderReadResult.data, strlen(shaderReadResult.data));
  }
  else
  {
    printf("Cannot read file %s\n", shaderPath);
  }
}
//---------------------------------------------------------------------------//
static VkBlendFactor getBlendFactor(const std::string factor)
{
  if (factor == "ZERO")
  {
    return VK_BLEND_FACTOR_ZERO;
  }
  if (factor == "ONE")
  {
    return VK_BLEND_FACTOR_ONE;
  }
  if (factor == "SRC_COLOR")
  {
    return VK_BLEND_FACTOR_SRC_COLOR;
  }
  if (factor == "ONE_MINUS_SRC_COLOR")
  {
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  }
  if (factor == "DST_COLOR")
  {
    return VK_BLEND_FACTOR_DST_COLOR;
  }
  if (factor == "ONE_MINUS_DST_COLOR")
  {
    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
  }
  if (factor == "SRC_ALPHA")
  {
    return VK_BLEND_FACTOR_SRC_ALPHA;
  }
  if (factor == "ONE_MINUS_SRC_ALPHA")
  {
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  }
  if (factor == "DST_ALPHA")
  {
    return VK_BLEND_FACTOR_DST_ALPHA;
  }
  if (factor == "ONE_MINUS_DST_ALPHA")
  {
    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  }
  if (factor == "CONSTANT_COLOR")
  {
    return VK_BLEND_FACTOR_CONSTANT_COLOR;
  }
  if (factor == "ONE_MINUS_CONSTANT_COLOR")
  {
    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
  }
  if (factor == "CONSTANT_ALPHA")
  {
    return VK_BLEND_FACTOR_CONSTANT_ALPHA;
  }
  if (factor == "ONE_MINUS_CONSTANT_ALPHA")
  {
    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
  }
  if (factor == "SRC_ALPHA_SATURATE")
  {
    return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
  }
  if (factor == "SRC1_COLOR")
  {
    return VK_BLEND_FACTOR_SRC1_COLOR;
  }
  if (factor == "ONE_MINUS_SRC1_COLOR")
  {
    return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
  }
  if (factor == "SRC1_ALPHA")
  {
    return VK_BLEND_FACTOR_SRC1_ALPHA;
  }
  if (factor == "ONE_MINUS_SRC1_ALPHA")
  {
    return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
  }

  return VK_BLEND_FACTOR_ONE;
}
//---------------------------------------------------------------------------//
static VkBlendOp getBlendOp(const std::string op)
{
  if (op == "ADD")
  {
    VK_BLEND_OP_ADD;
  }
  if (op == "SUBTRACT")
  {
    VK_BLEND_OP_SUBTRACT;
  }
  if (op == "REVERSE_SUBTRACT")
  {
    VK_BLEND_OP_REVERSE_SUBTRACT;
  }
  if (op == "MIN")
  {
    VK_BLEND_OP_MIN;
  }
  if (op == "MAX")
  {
    VK_BLEND_OP_MAX;
  }

  return VK_BLEND_OP_ADD;
}
//---------------------------------------------------------------------------//
static void parseGpuPipeline(
    nlohmann::json& p_Pipeline,
    Graphics::PipelineCreation& p_PipelineCreation,
    Framework::StringBuffer& p_PathBuffer,
    Framework::StringBuffer& p_ShaderBuffer,
    Framework::Allocator* p_TempAllocator,
    RendererUtil::Renderer* p_Renderer,
    Graphics::FrameGraph* p_FrameGraph)
{
  using json = nlohmann::json;
  using namespace Framework;

  json shaders = p_Pipeline["shaders"];
  if (!shaders.is_null())
  {

    for (size_t s = 0; s < shaders.size(); ++s)
    {
      json shaderStage = shaders[s];

      std::string name;

      p_PathBuffer.clear();
      // Read file and concatenate it

      cstring code = p_ShaderBuffer.current();

      json includes = shaderStage["includes"];
      if (includes.is_array())
      {

        for (size_t in = 0; in < includes.size(); ++in)
        {
          includes[in].get_to(name);
          shaderConcatenate(name.c_str(), p_PathBuffer, p_ShaderBuffer, p_TempAllocator);
        }
      }

      shaderStage["shader"].get_to(name);
      // Concatenate main shader code
      shaderConcatenate(name.c_str(), p_PathBuffer, p_ShaderBuffer, p_TempAllocator);
      // Add terminator for final string.
      p_ShaderBuffer.closeCurrentString();

      shaderStage["stage"].get_to(name);

      // Debug print of final code if needed.
      // printf( "\n\n%s\n\n\n", code );

      if (name == "vertex")
      {
        p_PipelineCreation.shaders.addStage(code, strlen(code), VK_SHADER_STAGE_VERTEX_BIT);
      }
      else if (name == "fragment")
      {
        p_PipelineCreation.shaders.addStage(code, strlen(code), VK_SHADER_STAGE_FRAGMENT_BIT);
      }
      else if (name == "compute")
      {
        p_PipelineCreation.shaders.addStage(code, strlen(code), VK_SHADER_STAGE_COMPUTE_BIT);
      }
    }
  }

  json vertexInputs = p_Pipeline["vertex_input"];
  if (vertexInputs.is_array())
  {

    p_PipelineCreation.vertexInput.numVertexAttributes = 0;
    p_PipelineCreation.vertexInput.numVertexStreams = 0;

    for (size_t v = 0; v < vertexInputs.size(); ++v)
    {
      VertexAttribute vertexAttribute{};

      json vertexInput = vertexInputs[v];

      vertexAttribute.location = (uint16_t)vertexInput.value("attribute_location", 0u);
      vertexAttribute.binding = (uint16_t)vertexInput.value("attribute_binding", 0u);
      vertexAttribute.offset = vertexInput.value("attribute_offset", 0u);

      json attribute_format = vertexInput["attribute_format"];
      if (attribute_format.is_string())
      {
        std::string name;
        attribute_format.get_to(name);

        for (uint32_t e = 0; e < VertexComponentFormat::kCount; ++e)
        {
          VertexComponentFormat::Enum enum_value = (VertexComponentFormat::Enum)e;
          if (name == VertexComponentFormat::toString(enum_value))
          {
            vertexAttribute.format = enum_value;
            break;
          }
        }
      }
      p_PipelineCreation.vertexInput.addVertexAttribute(vertexAttribute);

      VertexStream vertexStream{};

      vertexStream.binding = (uint16_t)vertexInput.value("stream_binding", 0u);
      vertexStream.stride = (uint16_t)vertexInput.value("stream_stride", 0u);

      json stream_rate = vertexInput["stream_rate"];
      if (stream_rate.is_string())
      {
        std::string name;
        stream_rate.get_to(name);

        if (name == "Vertex")
        {
          vertexStream.inputRate = VertexInputRate::kPerVertex;
        }
        else if (name == "Instance")
        {
          vertexStream.inputRate = VertexInputRate::kPerInstance;
        }
        else
        {
          assert(false);
        }
      }

      p_PipelineCreation.vertexInput.addVertexStream(vertexStream);
    }
  }

  json depth = p_Pipeline["depth"];
  if (!depth.is_null())
  {
    p_PipelineCreation.depthStencil.depthEnable = 1;
    p_PipelineCreation.depthStencil.depthWriteEnable = depth.value("write", false);

    // TODO:
    json comparison = depth["test"];
    if (comparison.is_string())
    {
      std::string name;
      comparison.get_to(name);

      if (name == "less_or_equal")
      {
        p_PipelineCreation.depthStencil.depthComparison = VK_COMPARE_OP_LESS_OR_EQUAL;
      }
      else if (name == "equal")
      {
        p_PipelineCreation.depthStencil.depthComparison = VK_COMPARE_OP_EQUAL;
      }
      else if (name == "never")
      {
        p_PipelineCreation.depthStencil.depthComparison = VK_COMPARE_OP_NEVER;
      }
      else if (name == "always")
      {
        p_PipelineCreation.depthStencil.depthComparison = VK_COMPARE_OP_ALWAYS;
      }
      else
      {
        assert(false);
      }
    }
  }

  json blendStates = p_Pipeline["blend"];
  if (!blendStates.is_null())
  {

    for (size_t b = 0; b < blendStates.size(); ++b)
    {
      json blend = blendStates[b];

      std::string enabled = blend.value("enable", "");
      std::string srcColour = blend.value("src_colour", "");
      std::string dstColour = blend.value("dst_colour", "");
      std::string blendOp = blend.value("op", "");

      BlendState& blend_state = p_PipelineCreation.blendState.addBlendState();
      blend_state.blendEnabled = (enabled == "true");
      blend_state.setColor(
          getBlendFactor(srcColour), getBlendFactor(dstColour), getBlendOp(blendOp));
    }
  }

  json cull = p_Pipeline["cull"];
  if (cull.is_string())
  {
    std::string name;
    cull.get_to(name);

    if (name == "back")
    {
      p_PipelineCreation.rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
    }
    else
    {
      assert(false);
    }
  }

  json renderPass = p_Pipeline["render_pass"];
  if (renderPass.is_string())
  {
    std::string name;
    renderPass.get_to(name);

    FrameGraphNode* node = p_FrameGraph->getNode(name.c_str());

    if (node)
    {

      // TODO: handle better
      if (name == "swapchain")
      {
        p_PipelineCreation.renderPass = p_Renderer->m_GpuDevice->m_SwapchainOutput;
      }
      else
      {
        const RenderPass* renderPass =
            (const RenderPass*)p_Renderer->m_GpuDevice->m_RenderPasses.accessResource(
                node->renderPass.index);

        p_PipelineCreation.renderPass = renderPass->output;
      }
    }
    else
    {
      printf("Cannot find render pass %s. Defaulting to swapchain\n", name.c_str());
      p_PipelineCreation.renderPass = p_Renderer->m_GpuDevice->m_SwapchainOutput;
    }
  }
}
//---------------------------------------------------------------------------//
void Graphics::RenderResourcesLoader::init(
    Graphics::RendererUtil::Renderer* p_Renderer,
    Framework::StackAllocator* p_TempAllocator,
    Graphics::FrameGraph* p_FrameGraph)
{
  renderer = p_Renderer;
  tempAllocator = p_TempAllocator;
  frameGraph = p_FrameGraph;
}
//---------------------------------------------------------------------------//
void Graphics::RenderResourcesLoader::shutdown() {}
//---------------------------------------------------------------------------//
void Graphics::RenderResourcesLoader::loadGpuTechnique(const char* p_JsonPath)
{
  using namespace Framework;
  size_t allocatedMarker = tempAllocator->getMarker();

  FileReadResult readResult = fileReadText(p_JsonPath, tempAllocator);

  StringBuffer pathBuffer;
  pathBuffer.init(1024, tempAllocator);

  StringBuffer shaderCodeBuffer;
  shaderCodeBuffer.init(FRAMEWORK_KILO(64), tempAllocator);

  using json = nlohmann::json;

  json jsonData = json::parse(readResult.data);

  // parse 1 pipeline
  json name = jsonData["name"];
  std::string nameString;
  if (name.is_string())
  {
    name.get_to(nameString);
    printf("Parsing GPU Technique %s\n", nameString.c_str());
  }

  RendererUtil::GpuTechniqueCreation techniqueCreation;
  techniqueCreation.name = nameString.c_str();

  json pipelines = jsonData["pipelines"];
  if (pipelines.is_array())
  {
    uint32_t size = uint32_t(pipelines.size());
    for (uint32_t i = 0; i < size; ++i)
    {
      json pipeline = pipelines[i];
      PipelineCreation pc{};
      pc.shaders.reset();

      json inheritFrom = pipeline["inherit_from"];
      if (inheritFrom.is_string())
      {
        std::string inheritedName;
        inheritFrom.get_to(inheritedName);

        for (uint32_t ii = 0; ii < size; ++ii)
        {
          json p = pipelines[ii];
          std::string name;
          p["name"].get_to(name);

          if (name == inheritedName)
          {
            parseGpuPipeline(
                p, pc, pathBuffer, shaderCodeBuffer, tempAllocator, renderer, frameGraph);
            break;
          }
        }
      }

      parseGpuPipeline(
          pipeline, pc, pathBuffer, shaderCodeBuffer, tempAllocator, renderer, frameGraph);

      techniqueCreation.creations[techniqueCreation.numCreations++] = pc;
    }
  }

  // Create technique and cache it.
  RendererUtil::GpuTechnique* technique = renderer->createTechnique(techniqueCreation);

  tempAllocator->freeMarker(allocatedMarker);
}
//---------------------------------------------------------------------------//
void Graphics::RenderResourcesLoader::loadTexture(const char* p_Path)
{
  int comp, width, height;
  uint8_t* imageData = stbi_load(p_Path, &width, &height, &comp, 4);
  if (!imageData)
  {
    printf("Error loading texture %s", p_Path);
    return;
  }

  uint32_t mipLevels = 1;
  if (true)
  {
    uint32_t w = width;
    uint32_t h = height;

    while (w > 1 && h > 1)
    {
      w /= 2;
      h /= 2;

      ++mipLevels;
    }
  }

  size_t allocatedMarker = tempAllocator->getMarker();
  StringBuffer pathBuffer;
  pathBuffer.init(1024, tempAllocator);
  char* copiedPath = pathBuffer.appendUseFormatted("%s", p_Path);
  filenameFromPath(copiedPath);

  TextureCreation creation;
  creation.setData(imageData)
      .setFormatType(VK_FORMAT_R8G8B8A8_UNORM, TextureType::kTexture2D)
      .setFlags(mipLevels, 0)
      .setSize((uint16_t)width, (uint16_t)height, 1)
      .setName(copiedPath);

  renderer->createTexture(creation);

  // IMPORTANT:
  // Free memory loaded from file, it should not matter!
  free(imageData);

  tempAllocator->freeMarker(allocatedMarker);
}
//---------------------------------------------------------------------------//
} // namespace Graphics
