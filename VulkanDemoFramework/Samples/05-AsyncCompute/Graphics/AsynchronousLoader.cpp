#include "AsynchronousLoader.hpp"

#include "Foundation/Time.hpp"
#include "Graphics/Renderer.hpp"

#include "Externals/stb_image.h"
#include <atomic>

namespace Graphics
{
//---------------------------------------------------------------------------//
void AsynchronousLoader::init(
    RendererUtil::Renderer* p_Renderer,
    enki::TaskScheduler* p_TaskScheduler,
    Framework::Allocator* p_ResidentAllocator)
{
  renderer = p_Renderer;
  taskScheduler = p_TaskScheduler;
  allocator = p_ResidentAllocator;

  fileLoadRequests.init(allocator, 16);
  uploadRequests.init(allocator, 16);

  textureReady.index = kInvalidTexture.index;
  cpuBufferReady.index = kInvalidBuffer.index;
  gpuBufferReady.index = kInvalidBuffer.index;
  completed = nullptr;

  using namespace Framework;

  // Create a persistently-mapped staging buffer
  BufferCreation bc;
  bc.reset()
      .set(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::kStream, FRAMEWORK_MEGA(64))
      .setName("staging_buffer")
      .setPersistent(true);
  BufferHandle stagingBufferHandle = renderer->m_GpuDevice->createBuffer(bc);

  stagingBuffer =
      (Buffer*)renderer->m_GpuDevice->m_Buffers.accessResource(stagingBufferHandle.index);

  stagingBufferOffset = 0;

  for (uint32_t i = 0; i < kMaxFrames; ++i)
  {
    VkCommandPoolCreateInfo cmdPoolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
    cmdPoolInfo.queueFamilyIndex = renderer->m_GpuDevice->m_VulkanTransferQueueFamily;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    vkCreateCommandPool(
        renderer->m_GpuDevice->m_VulkanDevice,
        &cmdPoolInfo,
        renderer->m_GpuDevice->m_VulkanAllocCallbacks,
        &commandPools[i]);

    VkCommandBufferAllocateInfo cmd = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
    cmd.commandPool = commandPools[i];
    cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd.commandBufferCount = 1;

    vkAllocateCommandBuffers(
        renderer->m_GpuDevice->m_VulkanDevice, &cmd, &commandBuffers[i].m_VulkanCmdBuffer);

    commandBuffers[i].m_IsRecording = false;
    commandBuffers[i].m_GpuDevice = (renderer->m_GpuDevice);
  }

  VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  vkCreateSemaphore(
      renderer->m_GpuDevice->m_VulkanDevice,
      &semaphoreInfo,
      renderer->m_GpuDevice->m_VulkanAllocCallbacks,
      &transferCompleteSemaphore);

  VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  vkCreateFence(
      renderer->m_GpuDevice->m_VulkanDevice,
      &fenceInfo,
      renderer->m_GpuDevice->m_VulkanAllocCallbacks,
      &transferFence);
}
//---------------------------------------------------------------------------//
void AsynchronousLoader::update(Framework::Allocator* scratchAllocator)
{
  using namespace Framework;

  // If a texture was processed in the previous commands, signal the renderer
  if (textureReady.index != kInvalidTexture.index)
  {
    // Add update request.
    // This method is multithreaded_safe
    renderer->addTextureToUpdate(textureReady);
  }

  if (cpuBufferReady.index != kInvalidBuffer.index && cpuBufferReady.index != kInvalidBuffer.index)
  {
    assert(completed != nullptr);
    (*completed)++;

    // TODO: free cpu buffer

    gpuBufferReady.index = kInvalidBuffer.index;
    cpuBufferReady.index = kInvalidBuffer.index;
    completed = nullptr;
  }

  textureReady.index = kInvalidTexture.index;

  // Process upload requests
  if (uploadRequests.m_Size > 0)
  {
    // Wait for transfer fence to be finished
    if (vkGetFenceStatus(renderer->m_GpuDevice->m_VulkanDevice, transferFence) != VK_SUCCESS)
    {
      return;
    }
    // Reset if file requests are present.
    vkResetFences(renderer->m_GpuDevice->m_VulkanDevice, 1, &transferFence);

    // Get last request
    UploadRequest request = uploadRequests.back();
    uploadRequests.pop();

    CommandBuffer* cb = &commandBuffers[renderer->m_GpuDevice->m_CurrentFrameIndex];
    cb->begin();

    if (request.texture.index != kInvalidTexture.index)
    {
      Texture* texture =
          (Texture*)renderer->m_GpuDevice->m_Textures.accessResource(request.texture.index);
      const uint32_t kTextureChannels = 4;
      const uint32_t kTextureAlignment = 4;
      const size_t alignedImageSize =
          memoryAlign(texture->width * texture->height * kTextureChannels, kTextureAlignment);
      // Request place in buffer
      const size_t currentOffset = std::atomic_fetch_add(&stagingBufferOffset, alignedImageSize);

      cb->uploadTextureData(texture->handle, request.data, stagingBuffer->handle, currentOffset);

      free(request.data);
    }
    else if (
        request.cpuBuffer.index != kInvalidBuffer.index &&
        request.gpuBuffer.index != kInvalidBuffer.index)
    {
      Buffer* src =
          (Buffer*)renderer->m_GpuDevice->m_Buffers.accessResource(request.cpuBuffer.index);
      Buffer* dst =
          (Buffer*)renderer->m_GpuDevice->m_Buffers.accessResource(request.gpuBuffer.index);

      cb->uploadBufferData(src->handle, dst->handle);
    }
    else if (request.cpuBuffer.index != kInvalidBuffer.index)
    {
      Buffer* buffer =
          (Buffer*)renderer->m_GpuDevice->m_Buffers.accessResource(request.cpuBuffer.index);
      // TODO: proper alignment
      const size_t alignedImageSize = memoryAlign(buffer->size, 64);
      const size_t currentOffset = std::atomic_fetch_add(&stagingBufferOffset, alignedImageSize);
      cb->uploadBufferData(buffer->handle, request.data, stagingBuffer->handle, currentOffset);

      free(request.data);
    }

    cb->end();

    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb->m_VulkanCmdBuffer;
    VkPipelineStageFlags wait_flag[]{VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSemaphore wait_semaphore[]{transferCompleteSemaphore};
    submitInfo.pWaitSemaphores = wait_semaphore;
    submitInfo.pWaitDstStageMask = wait_flag;

    VkQueue usedQueue = renderer->m_GpuDevice->m_VulkanTransferQueue;
    vkQueueSubmit(usedQueue, 1, &submitInfo, transferFence);

    // TODO: better management for state machine. We need to account for file -> buffer,
    // buffer -> texture and buffer -> buffer. One the CPU buffer has been used it should be freed.
    if (request.texture.index != kInvalidIndex)
    {
      assert(textureReady.index == kInvalidTexture.index);
      textureReady = request.texture;
    }
    else if (
        request.cpuBuffer.index != kInvalidBuffer.index &&
        request.gpuBuffer.index != kInvalidBuffer.index)
    {
      assert(cpuBufferReady.index == kInvalidIndex);
      assert(gpuBufferReady.index == kInvalidIndex);
      assert(completed == nullptr);
      cpuBufferReady = request.cpuBuffer;
      gpuBufferReady = request.gpuBuffer;
      completed = request.completed;
    }
    else if (request.cpuBuffer.index != kInvalidIndex)
    {
      assert(cpuBufferReady.index == kInvalidIndex);
      cpuBufferReady = request.cpuBuffer;
    }
  }

  // Process a file request
  if (fileLoadRequests.m_Size > 0)
  {
    FileLoadRequest loadRequest = fileLoadRequests.back();
    fileLoadRequests.pop();

    int64_t startReadingFile = Time::getCurrentTime();
    // Process request
    int x, y, comp;
    uint8_t* textureData = stbi_load(loadRequest.path, &x, &y, &comp, 4);

    if (textureData)
    {
      printf(
          "File %s read in %f ms\n",
          loadRequest.path,
          Time::deltaFromStartMilliseconds(startReadingFile));

      UploadRequest& uploadRequest = uploadRequests.pushUse();
      uploadRequest.data = textureData;
      uploadRequest.texture = loadRequest.texture;
      uploadRequest.cpuBuffer = kInvalidBuffer;
    }
    else
    {
      printf("Error reading file %s\n", loadRequest.path);
    }
  }

  stagingBufferOffset = 0;
}
//---------------------------------------------------------------------------//
void AsynchronousLoader::shutdown()
{
  renderer->m_GpuDevice->destroyBuffer(stagingBuffer->handle);

  fileLoadRequests.shutdown();
  uploadRequests.shutdown();

  for (uint32_t i = 0; i < kMaxFrames; ++i)
  {
    vkDestroyCommandPool(
        renderer->m_GpuDevice->m_VulkanDevice,
        commandPools[i],
        renderer->m_GpuDevice->m_VulkanAllocCallbacks);
    // Command buffers are destroyed with the pool associated.
  }

  vkDestroySemaphore(
      renderer->m_GpuDevice->m_VulkanDevice,
      transferCompleteSemaphore,
      renderer->m_GpuDevice->m_VulkanAllocCallbacks);
  vkDestroyFence(
      renderer->m_GpuDevice->m_VulkanDevice,
      transferFence,
      renderer->m_GpuDevice->m_VulkanAllocCallbacks);
}
//---------------------------------------------------------------------------//
void AsynchronousLoader::requestTextureData(const char* filename, TextureHandle texture)
{
  FileLoadRequest& request = fileLoadRequests.pushUse();
  strcpy(request.path, filename);
  request.texture = texture;
  request.buffer = kInvalidBuffer;
}
//---------------------------------------------------------------------------//
void AsynchronousLoader::requestBufferUpload(void* data, BufferHandle buffer)
{
  UploadRequest& uploadRequest = uploadRequests.pushUse();
  uploadRequest.data = data;
  uploadRequest.cpuBuffer = buffer;
  uploadRequest.texture = kInvalidTexture;
}
//---------------------------------------------------------------------------//
void AsynchronousLoader::requestBufferCopy(BufferHandle src, BufferHandle dst)
{
  UploadRequest& uploadRequest = uploadRequests.pushUse();
  uploadRequest.completed = completed;
  uploadRequest.data = nullptr;
  uploadRequest.cpuBuffer = src;
  uploadRequest.gpuBuffer = dst;
  uploadRequest.texture = kInvalidTexture;

  Buffer* buffer = (Buffer*)renderer->m_GpuDevice->m_Buffers.accessResource(dst.index);
  buffer->ready = false;
}
//---------------------------------------------------------------------------//
} // namespace Graphics
