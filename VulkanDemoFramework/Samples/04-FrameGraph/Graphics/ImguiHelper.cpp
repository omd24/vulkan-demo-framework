#include "ImguiHelper.hpp"

#include "Externals/imgui/imgui.h"
#include "Externals/imgui/imgui_impl_sdl.h"

#include "Graphics/CommandBuffer.hpp"

#include "Foundation/File.hpp"

namespace Graphics
{
//---------------------------------------------------------------------------//
namespace ImguiUtil
{
//---------------------------------------------------------------------------//
// Internals:
//---------------------------------------------------------------------------//
static Graphics::TextureHandle g_FontTexture;
static Graphics::PipelineHandle g_ImguiPipeline;
static Graphics::BufferHandle g_Vb, g_Ib;
static Graphics::BufferHandle g_UiConstantBuffer;
static Graphics::DescriptorSetLayoutHandle g_DescriptorSetLayout;
static Graphics::DescriptorSetHandle g_UiDescriptorSet; // Font descriptor set
static uint32_t g_VbSize = 665536, g_IbSize = 665536;
//---------------------------------------------------------------------------//
Framework::FlatHashMap<Graphics::ResourceHandle, Graphics::ResourceHandle>
    g_TextureToDescriptorSetMap;
//---------------------------------------------------------------------------//
// ImguiService impl:
//---------------------------------------------------------------------------//
static ImguiService g_ImguiService;
ImguiService* ImguiService::instance() { return &g_ImguiService; }
//---------------------------------------------------------------------------//
void ImguiService::init(void* p_Configuration)
{
  ImguiServiceConfiguration* imguiConfig = (ImguiServiceConfiguration*)p_Configuration;
  m_GpuDevice = imguiConfig->gpuDevice;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer bindings
  ImGui_ImplSDL2_InitForVulkan((SDL_Window*)imguiConfig->windowHandle);

  ImGuiIO& io = ImGui::GetIO();
  io.BackendRendererName = "Framework ImGui Helper";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

  // Load font texture atlas
  unsigned char* pixels;
  int width, height;
  // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is
  // more likely to be compatible with user's existing shaders. If your ImTextureId represent a
  // higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead
  // to save on GPU memory.
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  TextureCreation textureCreation;
  textureCreation.setFormatType(VK_FORMAT_R8G8B8A8_UNORM, TextureType::kTexture2D)
      .setData(pixels)
      .setSize(width, height, 1)
      .setFlags(1, 0)
      .setName("ImGui Font");
  g_FontTexture = m_GpuDevice->createTexture(textureCreation);

  // Store our identifier
  io.Fonts->TexID = (ImTextureID)&g_FontTexture;

  // Manual code. Used to remove dependency from that.
  ShaderStateCreation shaderCreation{};

  Framework::StringBuffer shaderPath;
  shaderPath.init(MAX_PATH, m_GpuDevice->m_Allocator);

  // Reading vertex shader:
  shaderPath.append(m_GpuDevice->m_Cwd.path);
  if (m_GpuDevice->m_BindlessSupported)
    shaderPath.append(R"FOO(\Shaders\imgui_bindless.vert.glsl)FOO");
  else
    shaderPath.append(R"FOO(\Shaders\imgui.vert.glsl)FOO");
  const char* vsCode =
      Framework::fileReadText(shaderPath.m_Data, m_GpuDevice->m_TemporaryAllocator, nullptr);
  assert(vsCode != nullptr && "Error reading vertex shader");

  // Reset string buffer
  shaderPath.clear();

  // Reading fragment shader
  shaderPath.append(m_GpuDevice->m_Cwd.path);
  if (m_GpuDevice->m_BindlessSupported)
    shaderPath.append(R"FOO(\Shaders\imgui_bindless.frag.glsl)FOO");
  else
    shaderPath.append(R"FOO(\Shaders\imgui.frag.glsl)FOO");
  const char* fsCode =
      Framework::fileReadText(shaderPath.m_Data, m_GpuDevice->m_TemporaryAllocator, nullptr);
  assert(fsCode != nullptr && "Error reading fragment shader");

  // Release string buffer
  shaderPath.shutdown();

  shaderCreation.setName("ImGui")
      .addStage(vsCode, (uint32_t)strlen(vsCode), VK_SHADER_STAGE_VERTEX_BIT)
      .addStage(fsCode, (uint32_t)strlen(fsCode), VK_SHADER_STAGE_FRAGMENT_BIT);

  PipelineCreation pipelineCreation = {};
  pipelineCreation.name = "Pipeline_ImGui";
  pipelineCreation.shaders = shaderCreation;

  pipelineCreation.blendState.addBlendState().setColor(
      VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD);

  pipelineCreation.vertexInput.addVertexAttribute({0, 0, 0, VertexComponentFormat::kFloat2})
      .addVertexAttribute({1, 0, 8, VertexComponentFormat::kFloat2})
      .addVertexAttribute({2, 0, 16, VertexComponentFormat::kUByte4N});

  pipelineCreation.vertexInput.addVertexStream({0, 20, VertexInputRate::kPerVertex});
  pipelineCreation.renderPass = m_GpuDevice->m_SwapchainOutput;

  DescriptorSetLayoutCreation descriptorSetLayoutCreation{};
  if (m_GpuDevice->m_BindlessSupported)
  {
    descriptorSetLayoutCreation
        .addBinding({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 1, "LocalConstants"})
        .addBinding({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10, 1, "Texture"})
        .setName("ImGui Descriptors");
  }
  else
  {
    descriptorSetLayoutCreation
        .addBinding({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 1, "LocalConstants"})
        .setName("Descriptor Uniform ImGui");
    // TODO: Check to see we still need this or not !?!
    // descriptorSetLayoutCreation
    //    .addBinding({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, 1, "LocalConstants"})
    //    .setName("Descriptor Sampler ImGui");
  }

  g_DescriptorSetLayout = m_GpuDevice->createDescriptorSetLayout(descriptorSetLayoutCreation);

  pipelineCreation.addDescriptorSetLayout(g_DescriptorSetLayout);

  g_ImguiPipeline = m_GpuDevice->createPipeline(pipelineCreation);

  // Create constant buffer
  BufferCreation constantBufferCreation;
  constantBufferCreation.set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, 64)
      .setName("Constant buffer ImGui");
  g_UiConstantBuffer = m_GpuDevice->createBuffer(constantBufferCreation);

  // Create descriptor set
  DescriptorSetCreation descriptorSetCreation{};
  if (m_GpuDevice->m_BindlessSupported)
  {
    descriptorSetCreation.setLayout(pipelineCreation.descriptorSetLayouts[0])
        .buffer(g_UiConstantBuffer, 0)
        .texture(g_FontTexture, 1)
        .setName("Imgui Font Texture");
  }
  else
  {
    // TODO: Correct this for bindless
    assert(false);
    /*descriptorSetCreation.setLayout(pipelineCreation.descriptorSetLayouts[0])
        .buffer(g_UiConstantBuffer, 0)
        .texture(g_FontTexture, 1)
        .setName("Descriptor set ImGui");*/
  }
  g_UiDescriptorSet = m_GpuDevice->createDescriptorSet(descriptorSetCreation);

  // Add descriptor set to the map
  // Old Map
  g_TextureToDescriptorSetMap.init(&Framework::MemoryService::instance()->m_SystemAllocator, 4);
  g_TextureToDescriptorSetMap.insert(g_FontTexture.index, g_UiDescriptorSet.index);

  // Create vertex and index buffers
  BufferCreation vbCreation;
  vbCreation.set(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ResourceUsageType::kDynamic, g_VbSize)
      .setName("VB ImGui");
  g_Vb = m_GpuDevice->createBuffer(vbCreation);

  BufferCreation ibCreation;
  ibCreation.set(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ResourceUsageType::kDynamic, g_IbSize)
      .setName("IB ImGui");
  g_Ib = m_GpuDevice->createBuffer(ibCreation);
}
//---------------------------------------------------------------------------//
void ImguiService::shutdown()
{
  Framework::FlatHashMapIterator it = g_TextureToDescriptorSetMap.iteratorBegin();
  while (it.isValid())
  {
    Graphics::ResourceHandle handle = g_TextureToDescriptorSetMap.get(it);
    m_GpuDevice->destroyDescriptorSet({handle});

    g_TextureToDescriptorSetMap.iteratorAdvance(it);
  }

  g_TextureToDescriptorSetMap.shutdown();

  m_GpuDevice->destroyBuffer(g_Vb);
  m_GpuDevice->destroyBuffer(g_Ib);
  m_GpuDevice->destroyBuffer(g_UiConstantBuffer);
  m_GpuDevice->destroyDescriptorSetLayout(g_DescriptorSetLayout);

  m_GpuDevice->destroyPipeline(g_ImguiPipeline);
  m_GpuDevice->destroyTexture(g_FontTexture);

  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
}
//---------------------------------------------------------------------------//
void ImguiService::newFrame()
{
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
}
//---------------------------------------------------------------------------//
void ImguiService::render(CommandBuffer& p_Commands, bool p_UseSecondary)
{
  ImGui::Render();

  ImDrawData* drawData = ImGui::GetDrawData();

  // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates !=
  // framebuffer coordinates)
  int fbWidth = (int)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
  int fbHeight = (int)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
  if (fbWidth <= 0 || fbHeight <= 0)
    return;

  // Vulkan backend has a different origin than OpenGL.
  bool clipOriginLowerLeft = false;

  size_t vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
  size_t indexSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

  if (vertexSize >= g_VbSize || indexSize >= g_IbSize)
  {
    OutputDebugStringA("ImGui Backend Error: vertex/index overflow!\n");
    return;
  }

  if (vertexSize == 0 && indexSize == 0)
  {
    return;
  }

  // Upload data
  ImDrawVert* vtxDst = NULL;
  ImDrawIdx* idxDst = NULL;

  MapBufferParameters mapParametersVb = {g_Vb, 0, (uint32_t)vertexSize};
  vtxDst = (ImDrawVert*)m_GpuDevice->mapBuffer(mapParametersVb);

  if (vtxDst)
  {
    for (int n = 0; n < drawData->CmdListsCount; n++)
    {

      const ImDrawList* cmdList = drawData->CmdLists[n];
      memcpy(vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
      vtxDst += cmdList->VtxBuffer.Size;
    }

    m_GpuDevice->unmapBuffer(mapParametersVb);
  }

  MapBufferParameters mapParamsIb = {g_Ib, 0, (uint32_t)indexSize};
  idxDst = (ImDrawIdx*)m_GpuDevice->mapBuffer(mapParamsIb);

  if (idxDst)
  {
    for (int n = 0; n < drawData->CmdListsCount; n++)
    {

      const ImDrawList* cmdList = drawData->CmdLists[n];
      memcpy(idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
      idxDst += cmdList->IdxBuffer.Size;
    }

    m_GpuDevice->unmapBuffer(mapParamsIb);
  }

  // todo: key
  p_Commands.bindPass(m_GpuDevice->m_SwapchainPass, p_UseSecondary);
  p_Commands.bindPipeline(g_ImguiPipeline);
  p_Commands.bindVertexBuffer(g_Vb, 0, 0);
  p_Commands.bindIndexBuffer(g_Ib, 0);

  const Viewport viewport = {0, 0, (uint16_t)fbWidth, (uint16_t)fbHeight, 0.0f, 1.0f};
  p_Commands.setViewport(&viewport);

  // single viewport apps.
  float L = drawData->DisplayPos.x;
  float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
  float T = drawData->DisplayPos.y;
  float B = drawData->DisplayPos.y + drawData->DisplaySize.y;
  const float orthoProjection[4][4] = {
      {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
      {0.0f, 2.0f / (T - B), 0.0f, 0.0f},
      {0.0f, 0.0f, -1.0f, 0.0f},
      {(R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f},
  };

  MapBufferParameters mapParamsCb = {g_UiConstantBuffer, 0, 0};
  float* constantBufferData = (float*)m_GpuDevice->mapBuffer(mapParamsCb);
  if (constantBufferData)
  {
    memcpy(constantBufferData, &orthoProjection[0][0], 64);
    m_GpuDevice->unmapBuffer(mapParamsCb);
  }

  // Will project scissor/clipping rectangles into framebuffer space
  ImVec2 clipOff = drawData->DisplayPos; // (0,0) unless using multi-viewports
  ImVec2 clipScale =
      drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

  // Render command lists
  //
  int counts = drawData->CmdListsCount;

  TextureHandle lastTexture = g_FontTexture;
  // todo:map
  DescriptorSetHandle lastDescriptorSet = {g_TextureToDescriptorSetMap.get(lastTexture.index)};

  p_Commands.bindDescriptorSet(&lastDescriptorSet, 1, nullptr, 0);

  uint32_t vtxBufferOffset = 0, indexBufferOffset = 0;
  for (int n = 0; n < counts; n++)
  {
    const ImDrawList* cmdList = drawData->CmdLists[n];

    for (int cmd_i = 0; cmd_i < cmdList->CmdBuffer.Size; cmd_i++)
    {
      const ImDrawCmd* pcmd = &cmdList->CmdBuffer[cmd_i];
      if (pcmd->UserCallback)
      {
        // User callback (registered via ImDrawList::AddCallback)
        pcmd->UserCallback(cmdList, pcmd);
      }
      else
      {
        // Project scissor/clipping rectangles into framebuffer space
        ImVec4 clipRect;
        clipRect.x = (pcmd->ClipRect.x - clipOff.x) * clipScale.x;
        clipRect.y = (pcmd->ClipRect.y - clipOff.y) * clipScale.y;
        clipRect.z = (pcmd->ClipRect.z - clipOff.x) * clipScale.x;
        clipRect.w = (pcmd->ClipRect.w - clipOff.y) * clipScale.y;

        if (clipRect.x < fbWidth && clipRect.y < fbHeight && clipRect.z >= 0.0f &&
            clipRect.w >= 0.0f)
        {
          // Apply scissor/clipping rectangle
          if (clipOriginLowerLeft)
          {
            Rect2DInt scissorRect = {
                (int16_t)clipRect.x,
                (int16_t)(fbHeight - clipRect.w),
                (uint16_t)(clipRect.z - clipRect.x),
                (uint16_t)(clipRect.w - clipRect.y)};
            p_Commands.setScissor(&scissorRect);
          }
          else
          {
            Rect2DInt scissorRect = {
                (int16_t)clipRect.x,
                (int16_t)clipRect.y,
                (uint16_t)(clipRect.z - clipRect.x),
                (uint16_t)(clipRect.w - clipRect.y)};
            p_Commands.setScissor(&scissorRect);
          }

          // Retrieve
          TextureHandle newTexture = *(TextureHandle*)(pcmd->TextureId);
          if (!m_GpuDevice->m_BindlessSupported)
          {
            if (newTexture.index != lastTexture.index && newTexture.index != kInvalidTexture.index)
            {
              lastTexture = newTexture;
              Framework::FlatHashMapIterator it =
                  g_TextureToDescriptorSetMap.find(lastTexture.index);

              // TODO: invalidate handles and update descriptor set when needed ?
              // Found this problem when reusing the handle from a previous
              // If not present
              if (it.isInvalid())
              {
                // Create new descriptor set
                DescriptorSetCreation descriptorSetCreation{};

                descriptorSetCreation.setLayout(g_DescriptorSetLayout)
                    .buffer(g_UiConstantBuffer, 0)
                    .texture(lastTexture, 1)
                    .setName("Dynamic Descriptor ImGUI");
                lastDescriptorSet = m_GpuDevice->createDescriptorSet(descriptorSetCreation);

                g_TextureToDescriptorSetMap.insert(newTexture.index, lastDescriptorSet.index);
              }
              else
              {
                lastDescriptorSet.index = g_TextureToDescriptorSetMap.get(it);
              }
              p_Commands.bindDescriptorSet(&lastDescriptorSet, 1, nullptr, 0);
            }
          }

          p_Commands.drawIndexed(
              Graphics::TopologyType::kTriangle,
              pcmd->ElemCount,
              1,
              indexBufferOffset + pcmd->IdxOffset,
              vtxBufferOffset + pcmd->VtxOffset,
              newTexture.index);
        }
      }
    }
    indexBufferOffset += cmdList->IdxBuffer.Size;
    vtxBufferOffset += cmdList->VtxBuffer.Size;
  }
}
//---------------------------------------------------------------------------//
void ImguiService::removeCachedTexture(TextureHandle& p_Texture)
{
  Framework::FlatHashMapIterator it = g_TextureToDescriptorSetMap.find(p_Texture.index);
  if (it.isValid())
  {

    // Destroy descriptor set
    Graphics::DescriptorSetHandle descriptorSet{g_TextureToDescriptorSetMap.get(it)};
    m_GpuDevice->destroyDescriptorSet(descriptorSet);

    // Remove from cache
    g_TextureToDescriptorSetMap.remove(p_Texture.index);
  }
}
//---------------------------------------------------------------------------//
void ImguiService::setStyle(ImguiStyles p_Style)
{
  // TODO
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void imguiLogInit()
{
  // TODO
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void imguiLogShutdown()
{
  // TODO
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void imguiLogDraw()
{
  // TODO
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void fpsInit()
{
  // TODO
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void fpsShutdown()
{
  // TODO
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void fpsAdd(float p_DeltaTime)
{
  // TODO
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
void fpsDraw()
{
  // TODO
  assert(false && "Not implemented");
}
//---------------------------------------------------------------------------//
} // namespace ImguiUtil
//---------------------------------------------------------------------------//
} // namespace Graphics
