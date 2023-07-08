#pragma once

#include "Graphics/Renderer.hpp"

namespace Graphics
{
//---------------------------------------------------------------------------//
struct FrameGraph;
//---------------------------------------------------------------------------//
struct RenderResourcesLoader
{
  void init(
      RendererUtil::Renderer* p_Renderer,
      Framework::StackAllocator* p_TempAllocator,
      FrameGraph* p_FrameGraph);
  void shutdown();

  Graphics::RendererUtil::GpuTechnique* loadGpuTechnique(const char* p_JsonPath);
  Graphics::RendererUtil::TextureResource*
  loadTexture(const char* p_Path, bool p_GenerateMipMaps = true);

  RendererUtil::Renderer* renderer;
  FrameGraph* frameGraph;
  Framework::StackAllocator* tempAllocator;
};
//---------------------------------------------------------------------------//
} // namespace Graphics
