#pragma once

#include "Foundation/Gltf.hpp"

#include "Graphics/GpuResources.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/RenderScene.hpp"
#include "Graphics/FrameGraph.hpp"
#include "Graphics/ImguiHelper.hpp"

#include "Externals/enkiTS/TaskScheduler.h"

namespace Graphics
{
//---------------------------------------------------------------------------//
struct glTFScene : public RenderScene
{
  void init(
      const char* filename,
      const char* path,
      Framework::Allocator* residentAllocator,
      Framework::StackAllocator* tempAllocator,
      AsynchronousLoader* asyncLoader) override;
  void shutdown(RendererUtil::Renderer* renderer) override;

  void prepareDraws(
      RendererUtil::Renderer* renderer,
      Framework::StackAllocator* scratchAllocator,
      SceneGraph* sceneGraph) override;

  void getMeshVertexBuffer(
      int accessorIndex,
      uint32_t flag,
      BufferHandle& outBufferHandle,
      uint32_t& outBufferOffset,
      uint32_t& outFlags);
  uint16_t getMaterialTexture(GpuDevice& gpu, Framework::glTF::TextureInfo* textureInfo);
  uint16_t getMaterialTexture(GpuDevice& gpu, int gltfTextureIndex);

  void fillPbrMaterial(
      RendererUtil::Renderer& renderer,
      Framework::glTF::Material& material,
      PBRMaterial& pbrMaterial);

  // All graphics resources used by the scene
  Framework::Array<RendererUtil::TextureResource> images;
  Framework::Array<RendererUtil::SamplerResource> samplers;
  Framework::Array<RendererUtil::BufferResource> buffers;

  Framework::glTF::glTF gltfScene; // Source gltf scene
};
//---------------------------------------------------------------------------//
} // namespace Graphics
