#pragma once

#include "Graphics/GpuResources.hpp"
#include "Graphics/GpuDevice.hpp"
#include "Graphics/CommandBuffer.hpp"

#include <atomic>

namespace enki
{
class TaskScheduler;
}
//---------------------------------------------------------------------------//
namespace Graphics
{
struct Allocator;
struct FrameGraph;
struct GPUProfiler;
struct ImGuiService;
struct StackAllocator;

namespace RendererUtil
{
struct Renderer;
}
//---------------------------------------------------------------------------//
struct FileLoadRequest
{
  char path[512];
  TextureHandle texture = kInvalidTexture;
  BufferHandle buffer = kInvalidBuffer;
}; // struct FileLoadRequest
//---------------------------------------------------------------------------//
struct UploadRequest
{

  void* data = nullptr;
  uint32_t* completed = nullptr;
  TextureHandle texture = kInvalidTexture;
  BufferHandle cpuBuffer = kInvalidBuffer;
  BufferHandle gpuBuffer = kInvalidBuffer;
}; // struct UploadRequest
//---------------------------------------------------------------------------//
struct AsynchronousLoader
{

  void init(
      RendererUtil::Renderer* renderer,
      enki::TaskScheduler* taskScheduler,
      Framework::Allocator* residentAllocator);
  void update(Framework::Allocator* scratchAllocator);
  void shutdown();

  void requestTextureData(const char* filename, TextureHandle texture);
  void requestBufferUpload(void* data, BufferHandle buffer);
  void requestBufferCopy(BufferHandle src, BufferHandle dst, uint32_t* completed);

  Framework::Allocator* allocator = nullptr;
  RendererUtil::Renderer* renderer = nullptr;
  enki::TaskScheduler* taskScheduler = nullptr;

  Framework::Array<FileLoadRequest> fileLoadRequests;
  Framework::Array<UploadRequest> uploadRequests;

  Buffer* stagingBuffer = nullptr;

  std::atomic_size_t stagingBufferOffset;
  TextureHandle textureReady;
  BufferHandle cpuBufferReady;
  BufferHandle gpuBufferReady;
  uint32_t* completed;

  VkCommandPool commandPools[GpuDevice::kMaxFrames];
  CommandBuffer commandBuffers[GpuDevice::kMaxFrames];
  VkSemaphore transferCompleteSemaphore;
  VkFence transferFence;

}; // struct AsynchonousLoader
//---------------------------------------------------------------------------//
} // namespace Graphics
