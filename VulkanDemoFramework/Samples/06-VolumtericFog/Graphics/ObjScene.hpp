#pragma once

#include "Graphics/GpuResources.hpp"
#include "Graphics/RenderScene.hpp"
#include "Graphics/Renderer.hpp"

struct aiScene;

namespace Graphics
{
struct ObjScene : public RenderScene
{
  void init(
      const char* filename,
      const char* path,
      Framework::Allocator* residentAllocator,
      Framework::StackAllocator* tempAllocator,
      AsynchronousLoader* asyncLoader_) override;
  void shutdown(RendererUtil::Renderer* renderer) override;

  void prepareDraws(
      RendererUtil::Renderer* renderer,
      Framework::StackAllocator* scratchAllocator,
      SceneGraph* sceneGraph) override;

  uint32_t
  loadTexture(const char* texturePath, const char* path, Framework::StackAllocator* tempAllocator);

  // All graphics resources used by the scene
  Framework::Array<RendererUtil::TextureResource> images;
  RendererUtil::SamplerResource* sampler;
  Framework::Array<RendererUtil::BufferResource> cpuBuffers;
  Framework::Array<RendererUtil::BufferResource> gpuBuffers;

  const aiScene* assimpScene;
  AsynchronousLoader* asyncLoader;

}; // struct ObjScene

} // namespace Graphics