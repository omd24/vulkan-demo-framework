#include "ImguiHelper.hpp"

#include "Externals/imgui/imgui.h"
#include "Externals/imgui/imgui_impl_sdl.h"

#include "Graphics/CommandBuffer.hpp"

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
static const char* g_VertexShaderCode = {
    "#version 450\n"
    "layout( location = 0 ) in vec2 Position;\n"
    "layout( location = 1 ) in vec2 UV;\n"
    "layout( location = 2 ) in uvec4 Color;\n"
    "layout( location = 0 ) out vec2 Frag_UV;\n"
    "layout( location = 1 ) out vec4 Frag_Color;\n"
    "layout( std140, binding = 0 ) uniform LocalConstants { mat4 ProjMtx; };\n"
    "void main()\n"
    "{\n"
    "    Frag_UV = UV;\n"
    "    Frag_Color = Color / 255.0f;\n"
    "    gl_Position = ProjMtx * vec4( Position.xy,0,1 );\n"
    "}\n"};
//---------------------------------------------------------------------------//
static const char* g_VertexShaderCodeBindless = {
    "#version 450\n"
    "layout( location = 0 ) in vec2 Position;\n"
    "layout( location = 1 ) in vec2 UV;\n"
    "layout( location = 2 ) in uvec4 Color;\n"
    "layout( location = 0 ) out vec2 Frag_UV;\n"
    "layout( location = 1 ) out vec4 Frag_Color;\n"
    "layout (location = 2) flat out uint texture_id;\n"
    "layout( std140, binding = 0 ) uniform LocalConstants { mat4 ProjMtx; };\n"
    "void main()\n"
    "{\n"
    "    Frag_UV = UV;\n"
    "    Frag_Color = Color / 255.0f;\n"
    "    texture_id = gl_InstanceIndex;\n"
    "    gl_Position = ProjMtx * vec4( Position.xy,0,1 );\n"
    "}\n"};
//---------------------------------------------------------------------------//
static const char* g_FragmentShaderCode = {
    "#version 450\n"
    "#extension GL_EXT_nonuniform_qualifier : enable\n"
    "layout (location = 0) in vec2 Frag_UV;\n"
    "layout (location = 1) in vec4 Frag_Color;\n"
    "layout (location = 0) out vec4 Out_Color;\n"
    "layout (binding = 1) uniform sampler2D Texture;\n"
    "void main()\n"
    "{\n"
    "    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
    "}\n"};
//---------------------------------------------------------------------------//
static const char* g_FragmentShaderCodeBindless = {
    "#version 450\n"
    "#extension GL_EXT_nonuniform_qualifier : enable\n"
    "layout (location = 0) in vec2 Frag_UV;\n"
    "layout (location = 1) in vec4 Frag_Color;\n"
    "layout (location = 2) flat in uint texture_id;\n"
    "layout (location = 0) out vec4 Out_Color;\n"
    "#extension GL_EXT_nonuniform_qualifier : enable\n"
    "layout (set = 1, binding = 10) uniform sampler2D textures[];\n"
    "void main()\n"
    "{\n"
    "    Out_Color = Frag_Color * texture(textures[nonuniformEXT(texture_id)], Frag_UV.st);\n"
    "}\n"};
//---------------------------------------------------------------------------//
// ImguiService impl:
//---------------------------------------------------------------------------//
static ImguiService g_ImguiService;
ImguiService* ImguiService::instance() { return &g_ImguiService; }
//---------------------------------------------------------------------------//
void ImguiService::init(void* p_Configuration)
{
  ImguiServiceConfiguration* imgui_config = (ImguiServiceConfiguration*)p_Configuration;
  m_GpuDevice = imgui_config->gpuDevice;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer bindings
  ImGui_ImplSDL2_InitForVulkan((SDL_Window*)imgui_config->windowHandle);

  ImGuiIO& io = ImGui::GetIO();
  io.BackendRendererName = "Framework ImGui Helper";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

  // Load font texture atlas //////////////////////////////////////////////////
  unsigned char* pixels;
  int width, height;
  // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is
  // more likely to be compatible with user's existing shaders. If your ImTextureId represent a
  // higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead
  // to save on GPU memory.
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  TextureCreation texture_creation; // = { pixels, ( uint16_t )width, ( uint16_t )height, 1, 1, 0,
                                    // TextureFormat::R8G8B8A8_UNORM, TextureType::Texture2D };
  texture_creation.setFormatType(VK_FORMAT_R8G8B8A8_UNORM, TextureType::kTexture2D)
      .setData(pixels)
      .setSize(width, height, 1)
      .setFlags(1, 0)
      .setName("ImGui Font");
  g_FontTexture = m_GpuDevice->createTexture(texture_creation);

  // Store our identifier
  io.Fonts->TexID = (ImTextureID)&g_FontTexture;

  // Manual code. Used to remove dependency from that.
  ShaderStateCreation shader_creation{};

  shader_creation.setName("ImGui")
      .addStage(
          g_VertexShaderCode, (uint32_t)strlen(g_VertexShaderCode), VK_SHADER_STAGE_VERTEX_BIT)
      .addStage(
          g_FragmentShaderCode,
          (uint32_t)strlen(g_FragmentShaderCode),
          VK_SHADER_STAGE_FRAGMENT_BIT);

  PipelineCreation pipeline_creation = {};
  pipeline_creation.name = "Pipeline_ImGui";
  pipeline_creation.shaders = shader_creation;

  pipeline_creation.blendState.addBlendState().setColor(
      VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD);

  pipeline_creation.vertexInput.addVertexAttribute({0, 0, 0, VertexComponentFormat::kFloat2})
      .addVertexAttribute({1, 0, 8, VertexComponentFormat::kFloat2})
      .addVertexAttribute({2, 0, 16, VertexComponentFormat::kUByte4N});

  pipeline_creation.vertexInput.addVertexStream({0, 20, VertexInputRate::kPerVertex});
  pipeline_creation.renderPass = m_GpuDevice->m_SwapchainOutput;

  DescriptorSetLayoutCreation descriptor_set_layout_creation{};
  descriptor_set_layout_creation
      .addBinding({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 1, "LocalConstants"})
      .setName("Descriptor Uniform ImGui");
  descriptor_set_layout_creation
      .addBinding({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, 1, "LocalConstants"})
      .setName("Descriptor Sampler ImGui");

  g_DescriptorSetLayout = m_GpuDevice->createDescriptorSetLayout(descriptor_set_layout_creation);

  pipeline_creation.addDescriptorSetLayout(g_DescriptorSetLayout);

  g_ImguiPipeline = m_GpuDevice->createPipeline(pipeline_creation);

  // Create constant buffer
  BufferCreation cb_creation;
  cb_creation.set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, 64)
      .setName("Constant buffer ImGui");
  g_UiConstantBuffer = m_GpuDevice->createBuffer(cb_creation);

  // Create descriptor set
  DescriptorSetCreation ds_creation{};
  ds_creation.setLayout(pipeline_creation.descriptorSetLayouts[0])
      .buffer(g_UiConstantBuffer, 0)
      .texture(g_FontTexture, 1)
      .setName("RL_ImGui");
  g_UiDescriptorSet = m_GpuDevice->createDescriptorSet(ds_creation);

  // Add descriptor set to the map
  // Old Map
  g_TextureToDescriptorSetMap.init(&Framework::MemoryService::instance()->m_SystemAllocator, 4);
  g_TextureToDescriptorSetMap.insert(g_FontTexture.index, g_UiDescriptorSet.index);

  // Create vertex and index buffers //////////////////////////////////////////
  BufferCreation vb_creation;
  vb_creation.set(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ResourceUsageType::kDynamic, g_VbSize)
      .setName("VB ImGui");
  g_Vb = m_GpuDevice->createBuffer(vb_creation);

  BufferCreation ib_creation;
  ib_creation.set(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ResourceUsageType::kDynamic, g_IbSize)
      .setName("IB_ImGui");
  g_Ib = m_GpuDevice->createBuffer(ib_creation);
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
void ImguiService::render(CommandBuffer& p_Commands)
{
  ImGui::Render();

  ImDrawData* draw_data = ImGui::GetDrawData();

  // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates !=
  // framebuffer coordinates)
  int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
  int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
  if (fb_width <= 0 || fb_height <= 0)
    return;

  // Vulkan backend has a different origin than OpenGL.
  bool clip_origin_lower_left = false;

  size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
  size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

  if (vertex_size >= g_VbSize || index_size >= g_IbSize)
  {
    OutputDebugStringA("ImGui Backend Error: vertex/index overflow!\n");
    return;
  }

  if (vertex_size == 0 && index_size == 0)
  {
    return;
  }

  // Upload data
  ImDrawVert* vtx_dst = NULL;
  ImDrawIdx* idx_dst = NULL;

  MapBufferParameters map_parameters_vb = {g_Vb, 0, (uint32_t)vertex_size};
  vtx_dst = (ImDrawVert*)m_GpuDevice->mapBuffer(map_parameters_vb);

  if (vtx_dst)
  {
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {

      const ImDrawList* cmd_list = draw_data->CmdLists[n];
      memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
      vtx_dst += cmd_list->VtxBuffer.Size;
    }

    m_GpuDevice->unmapBuffer(map_parameters_vb);
  }

  MapBufferParameters map_parameters_ib = {g_Ib, 0, (uint32_t)index_size};
  idx_dst = (ImDrawIdx*)m_GpuDevice->mapBuffer(map_parameters_ib);

  if (idx_dst)
  {
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {

      const ImDrawList* cmd_list = draw_data->CmdLists[n];
      memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
      idx_dst += cmd_list->IdxBuffer.Size;
    }

    m_GpuDevice->unmapBuffer(map_parameters_ib);
  }

  // todo: key
  p_Commands.bindPass(m_GpuDevice->m_SwapchainPass);
  p_Commands.bindPipeline(g_ImguiPipeline);
  p_Commands.bindVertexBuffer(g_Vb, 0, 0);
  p_Commands.bindIndexBuffer(g_Ib, 0);

  const Viewport viewport = {0, 0, (uint16_t)fb_width, (uint16_t)fb_height, 0.0f, 1.0f};
  p_Commands.setViewport(&viewport);

  // single viewport apps.
  float L = draw_data->DisplayPos.x;
  float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
  float T = draw_data->DisplayPos.y;
  float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
  const float ortho_projection[4][4] = {
      {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
      {0.0f, 2.0f / (T - B), 0.0f, 0.0f},
      {0.0f, 0.0f, -1.0f, 0.0f},
      {(R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f},
  };

  MapBufferParameters cb_map = {g_UiConstantBuffer, 0, 0};
  float* cb_data = (float*)m_GpuDevice->mapBuffer(cb_map);
  if (cb_data)
  {
    memcpy(cb_data, &ortho_projection[0][0], 64);
    m_GpuDevice->unmapBuffer(cb_map);
  }

  // Will project scissor/clipping rectangles into framebuffer space
  ImVec2 clip_off = draw_data->DisplayPos; // (0,0) unless using multi-viewports
  ImVec2 clip_scale =
      draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

  // Render command lists
  //
  int counts = draw_data->CmdListsCount;

  TextureHandle last_texture = g_FontTexture;
  // todo:map
  DescriptorSetHandle last_descriptor_set = {g_TextureToDescriptorSetMap.get(last_texture.index)};

  p_Commands.bindDescriptorSet(&last_descriptor_set, 1, nullptr, 0);

  uint32_t vtx_buffer_offset = 0, index_buffer_offset = 0;
  for (int n = 0; n < counts; n++)
  {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];

    for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
    {
      const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
      if (pcmd->UserCallback)
      {
        // User callback (registered via ImDrawList::AddCallback)
        pcmd->UserCallback(cmd_list, pcmd);
      }
      else
      {
        // Project scissor/clipping rectangles into framebuffer space
        ImVec4 clip_rect;
        clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
        clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
        clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
        clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

        if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f &&
            clip_rect.w >= 0.0f)
        {
          // Apply scissor/clipping rectangle
          if (clip_origin_lower_left)
          {
            Rect2DInt scissor_rect = {
                (int16_t)clip_rect.x,
                (int16_t)(fb_height - clip_rect.w),
                (uint16_t)(clip_rect.z - clip_rect.x),
                (uint16_t)(clip_rect.w - clip_rect.y)};
            p_Commands.setScissor(&scissor_rect);
          }
          else
          {
            Rect2DInt scissor_rect = {
                (int16_t)clip_rect.x,
                (int16_t)clip_rect.y,
                (uint16_t)(clip_rect.z - clip_rect.x),
                (uint16_t)(clip_rect.w - clip_rect.y)};
            p_Commands.setScissor(&scissor_rect);
          }

          // Retrieve
          TextureHandle new_texture = *(TextureHandle*)(pcmd->TextureId);
          if (true)
          {
            if (new_texture.index != last_texture.index &&
                new_texture.index != kInvalidTexture.index)
            {
              last_texture = new_texture;
              Framework::FlatHashMapIterator it =
                  g_TextureToDescriptorSetMap.find(last_texture.index);

              // TODO: invalidate handles and update descriptor set when needed ?
              // Found this problem when reusing the handle from a previous
              // If not present
              if (it.isInvalid())
              {
                // Create new descriptor set
                DescriptorSetCreation ds_creation{};

                ds_creation.setLayout(g_DescriptorSetLayout)
                    .buffer(g_UiConstantBuffer, 0)
                    .texture(last_texture, 1)
                    .setName("RL_Dynamic_ImGUI");
                last_descriptor_set = m_GpuDevice->create_descriptor_set(ds_creation);

                g_TextureToDescriptorSetMap.insert(new_texture.index, last_descriptor_set.index);
              }
              else
              {
                last_descriptor_set.index = g_TextureToDescriptorSetMap.get(it);
              }
              p_Commands.bindDescriptorSet(&last_descriptor_set, 1, nullptr, 0);
            }
          }

          p_Commands.drawIndexed(
              Graphics::TopologyType::kTriangle,
              pcmd->ElemCount,
              1,
              index_buffer_offset + pcmd->IdxOffset,
              vtx_buffer_offset + pcmd->VtxOffset,
              new_texture.index);
        }
      }
    }
    index_buffer_offset += cmd_list->IdxBuffer.Size;
    vtx_buffer_offset += cmd_list->VtxBuffer.Size;
  }
}
//---------------------------------------------------------------------------//
void ImguiService::removeCachedTexture(TextureHandle& p_Texture)
{
  // TODO
  assert(false && "Not implemented");
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
