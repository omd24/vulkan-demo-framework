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

  void loadGpuTechnique(const char* p_JsonPath);
  void loadTexture(const char* p_Path);

  RendererUtil::Renderer* renderer;
  FrameGraph* frameGraph;
  StackAllocator* tempAllocator;
};
//---------------------------------------------------------------------------//
} // namespace Graphics
