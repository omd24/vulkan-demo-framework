#include "GpuDevice.hpp"
#include "CommandBuffer.hpp"

#include "Externals/SDL2-2.0.18/include/SDL.h"        // SDL_Window
#include "Externals/SDL2-2.0.18/include/SDL_vulkan.h" // SDL_Vulkan_CreateSurface

#include <assert.h>

// VMA link prequisities:
#define VMA_USE_STL_CONTAINERS 0
#define VMA_USE_STL_VECTOR 0
#define VMA_USE_STL_UNORDERED_MAP 0
#define VMA_USE_STL_LIST 0

#define VMA_IMPLEMENTATION
#include "Externals/vk_mem_alloc.h"

namespace Graphics
{
#define CHECKRES(result) assert(result == VK_SUCCESS && "Vulkan assert")
//---------------------------------------------------------------------------//
struct CommandBufferRing
{

  void init(GpuDevice* p_Gpu)
  {
    m_Gpu = p_Gpu;

    for (uint32_t i = 0; i < ms_MaxPools; i++)
    {
      VkCommandPoolCreateInfo cmdPoolCi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
      cmdPoolCi.queueFamilyIndex = m_Gpu->m_VulkanQueueFamily;
      cmdPoolCi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

      CHECKRES(vkCreateCommandPool(
          m_Gpu->m_VulkanDevice, &cmdPoolCi, m_Gpu->m_VulkanAllocCallbacks, &m_VulkanCmdPools[i]));
    }

    for (uint32_t i = 0; i < ms_MaxBuffers; i++)
    {
      VkCommandBufferAllocateInfo cmd = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
      const uint32_t poolIndex = poolFromIndex(i);
      cmd.commandPool = m_VulkanCmdPools[poolIndex];
      cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cmd.commandBufferCount = 1;
      CHECKRES(vkAllocateCommandBuffers(
          m_Gpu->m_VulkanDevice, &cmd, &m_CmdBuffers[i].m_VulkanCmdBuffer));

      m_CmdBuffers[i].m_GpuDevice = m_Gpu;
      m_CmdBuffers[i].m_Handle = i;
      m_CmdBuffers[i].reset();
    }
  }
  void shutdown()
  {
    for (uint32_t i = 0; i < kMaxSwapchainImages * ms_MaxThreads; i++)
    {
      vkDestroyCommandPool(
          m_Gpu->m_VulkanDevice, m_VulkanCmdPools[i], m_Gpu->m_VulkanAllocCallbacks);
    }
  }

  void reset_pools(uint32_t p_FrameIndex)
  {
    for (uint32_t i = 0; i < ms_MaxThreads; i++)
    {
      vkResetCommandPool(
          m_Gpu->m_VulkanDevice, m_VulkanCmdPools[p_FrameIndex * ms_MaxThreads + i], 0);
    }
  }

  CommandBuffer* getCmdBuffer(uint32_t p_FrameIndex, bool p_Begin)
  {
    // TODO: take in account threads
    CommandBuffer* cmdBuffer = &m_CmdBuffers[p_FrameIndex * ms_BufferPerPool];

    if (p_Begin)
    {
      cmdBuffer->reset();

      VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(cmdBuffer->m_VulkanCmdBuffer, &beginInfo);
    }

    return cmdBuffer;
  }
  CommandBuffer* getCmdBufferInstant(uint32_t p_FrameIndex, bool p_Begin)
  {
    CommandBuffer* cmdBuffer = &m_CmdBuffers[p_FrameIndex * ms_BufferPerPool + 1];
    return cmdBuffer;
  }

  static uint16_t poolFromIndex(uint32_t p_Index) { return (uint16_t)p_Index / ms_BufferPerPool; }

  static const uint16_t ms_MaxThreads = 1;
  static const uint16_t ms_MaxPools = kMaxSwapchainImages * ms_MaxThreads;
  static const uint16_t ms_BufferPerPool = 4;
  static const uint16_t ms_MaxBuffers = ms_BufferPerPool * ms_MaxPools;

  GpuDevice* m_Gpu;
  VkCommandPool m_VulkanCmdPools[ms_MaxPools];
  CommandBuffer m_CmdBuffers[ms_MaxBuffers];
  uint8_t m_NextFreePerThreadFrame[ms_MaxPools];

}; // struct CommandBufferRing
//---------------------------------------------------------------------------//
DeviceCreation& DeviceCreation::setWindow(uint32_t p_Width, uint32_t p_Height, void* p_Handle)
{
  width = static_cast<uint16_t>(p_Width);
  height = static_cast<uint16_t>(p_Height);
  window = p_Handle;
  return *this;
}
DeviceCreation& DeviceCreation::setAllocator(Framework::Allocator* p_Allocator)
{
  allocator = p_Allocator;
  return *this;
}
DeviceCreation& DeviceCreation::setTemporaryAllocator(Framework::StackAllocator* p_Allocator)
{
  temporaryAllocator = p_Allocator;
  return *this;
}
//---------------------------------------------------------------------------//
// Debug helpers:
//---------------------------------------------------------------------------//

// Enable this to add debugging capabilities.
// https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_debug_utils.html
#define VULKAN_DEBUG_REPORT

//#define VULKAN_SYNCHRONIZATION_VALIDATION

static const char* kRequestedLayers[] = {
    "VK_LAYER_KHRONOS_validation",
    //"VK_LAYER_LUNARG_core_validation",
    //"VK_LAYER_LUNARG_image",
    //"VK_LAYER_LUNARG_parameter_validation",
    //"VK_LAYER_LUNARG_object_tracker"
};
static const char* kRequestedExtensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    // Platform specific extension
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#if defined(VULKAN_DEBUG_REPORT)
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif // VULKAN_DEBUG_REPORT
};
//---------------------------------------------------------------------------//
static VkBool32 debugUtilsCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT p_Severity,
    VkDebugUtilsMessageTypeFlagsEXT p_Types,
    const VkDebugUtilsMessengerCallbackDataEXT* p_CallbackData,
    void* p_UserData)
{
  char msg[1024]{};
  sprintf(
      msg,
      " MessageID: %s %i\nMessage: %s\n\n",
      p_CallbackData->pMessageIdName,
      p_CallbackData->messageIdNumber,
      p_CallbackData->pMessage);
  OutputDebugStringA(msg);
  return VK_FALSE;
}
//---------------------------------------------------------------------------//
VkDebugUtilsMessengerCreateInfoEXT createDebugUtilsMessengerInfo()
{
  VkDebugUtilsMessengerCreateInfoEXT ci = {};
  ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  ci.pfnUserCallback = debugUtilsCallback;
  ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
  ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
  return ci;
}
//---------------------------------------------------------------------------//

PFN_vkSetDebugUtilsObjectNameEXT pfnSetDebugUtilsObjectNameEXT;
PFN_vkCmdBeginDebugUtilsLabelEXT pfnCmdBeginDebugUtilsLabelEXT;
PFN_vkCmdEndDebugUtilsLabelEXT pfnCmdEndDebugUtilsLabelEXT;

//---------------------------------------------------------------------------//
// Internal context
//---------------------------------------------------------------------------//

static size_t kUboAlignment = 256;
static size_t kSboAlignemnt = 256;

static SDL_Window* g_SdlWindow;

static CommandBufferRing g_CmdBufferRing;

static VkPresentModeKHR toVkPresentMode(PresentMode::Enum p_Mode)
{
  switch (p_Mode)
  {
  case PresentMode::kVSyncFast:
    return VK_PRESENT_MODE_MAILBOX_KHR;
  case PresentMode::kVSyncRelaxed:
    return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
  case PresentMode::kImmediate:
    return VK_PRESENT_MODE_IMMEDIATE_KHR;
  case PresentMode::kVSync:
  default:
    return VK_PRESENT_MODE_FIFO_KHR;
  }
}

//---------------------------------------------------------------------------//
// Device implementation:
//---------------------------------------------------------------------------//

void GpuDevice::init(const DeviceCreation& p_Creation)
{
  OutputDebugStringA("Gpu Device init\n");

  // Init allocators:
  m_Allocator = p_Creation.allocator;
  m_TemporaryAllocator = p_Creation.temporaryAllocator;
  m_StringBuffer.init(1024 * 1024, p_Creation.allocator);

  // Init vulkan instance:
  {
    m_VulkanAllocCallbacks = nullptr;

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = nullptr;
    appInfo.pApplicationName = "Graphics Device";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "Vulkan Demo Framework";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_MAKE_VERSION(1, 2, 0);

    VkInstanceCreateInfo instanceCi = {};
    instanceCi.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCi.pNext = nullptr;
    instanceCi.flags = 0;
    instanceCi.pApplicationInfo = &appInfo;
    instanceCi.enabledLayerCount = arrayCount32(kRequestedLayers);
    instanceCi.ppEnabledLayerNames = kRequestedLayers;
    instanceCi.enabledExtensionCount = arrayCount32(kRequestedExtensions);
    instanceCi.ppEnabledExtensionNames = kRequestedExtensions;

    const VkDebugUtilsMessengerCreateInfoEXT debugCi = createDebugUtilsMessengerInfo();
    instanceCi.pNext = &debugCi;

    VkResult result = vkCreateInstance(&instanceCi, m_VulkanAllocCallbacks, &m_VulkanInstance);
    CHECKRES(result);
  }

  m_SwapchainWidth = p_Creation.width;
  m_SwapchainHeight = p_Creation.height;

  // Choose extensions:
  {
    uint32_t numInstanceExtensions;
    vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, nullptr);
    VkExtensionProperties* extensions = (VkExtensionProperties*)FRAMEWORK_ALLOCA(
        sizeof(VkExtensionProperties) * numInstanceExtensions, m_Allocator);
    vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, extensions);
    for (size_t i = 0; i < numInstanceExtensions; i++)
    {

      if (!strcmp(extensions[i].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
      {
        m_DebugUtilsExtensionPresent = true;
        break;
      }
    }

    FRAMEWORK_FREE(extensions, m_Allocator);

    if (!m_DebugUtilsExtensionPresent)
    {
      char msg[256]{};
      sprintf(msg, "Extension %s for debugging non present.", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      OutputDebugStringA(msg);
    }
    else
    {
      // Create new debug utils callback
      PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT =
          (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
              m_VulkanInstance, "vkCreateDebugUtilsMessengerEXT");
      VkDebugUtilsMessengerCreateInfoEXT debugMessengerCi = createDebugUtilsMessengerInfo();

      vkCreateDebugUtilsMessengerEXT(
          m_VulkanInstance,
          &debugMessengerCi,
          m_VulkanAllocCallbacks,
          &m_VulkanDebugUtilsMessenger);
    }
  }

  // Choose physical device
  {
    uint32_t numPhysicalDevice;
    VkResult result = vkEnumeratePhysicalDevices(m_VulkanInstance, &numPhysicalDevice, NULL);
    CHECKRES(result);

    VkPhysicalDevice* gpus = (VkPhysicalDevice*)FRAMEWORK_ALLOCA(
        sizeof(VkPhysicalDevice) * numPhysicalDevice, m_Allocator);
    result = vkEnumeratePhysicalDevices(m_VulkanInstance, &numPhysicalDevice, gpus);
    CHECKRES(result);

    // TODO: improve - choose the first gpu.
    m_VulkanPhysicalDevice = gpus[0];
    FRAMEWORK_FREE(gpus, m_Allocator);

    vkGetPhysicalDeviceProperties(m_VulkanPhysicalDevice, &m_VulkanPhysicalDeviceProps);

    // print selected gpu:
    {
      char msg[256]{};
      sprintf(msg, "GPU Used: %s\n", m_VulkanPhysicalDeviceProps.deviceName);
      OutputDebugStringA(msg);
    }

    kUboAlignment = m_VulkanPhysicalDeviceProps.limits.minUniformBufferOffsetAlignment;
    kSboAlignemnt = m_VulkanPhysicalDeviceProps.limits.minStorageBufferOffsetAlignment;
  }

  // Create logical device
  {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_VulkanPhysicalDevice, &queueFamilyCount, nullptr);

    VkQueueFamilyProperties* queueFamilies = (VkQueueFamilyProperties*)FRAMEWORK_ALLOCA(
        sizeof(VkQueueFamilyProperties) * queueFamilyCount, m_Allocator);
    vkGetPhysicalDeviceQueueFamilyProperties(
        m_VulkanPhysicalDevice, &queueFamilyCount, queueFamilies);

    uint32_t familyIndex = 0;
    for (; familyIndex < queueFamilyCount; ++familyIndex)
    {
      VkQueueFamilyProperties queue_family = queueFamilies[familyIndex];
      if (queue_family.queueCount > 0 &&
          queue_family.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
      {
        // indices.graphicsFamily = i;
        break;
      }
    }

    FRAMEWORK_FREE(queueFamilies, m_Allocator);

    uint32_t deviceExtensionCount = 1;
    const char* deviceExtensions[] = {"VK_KHR_swapchain"};
    const float queuePriority[] = {1.0f};
    VkDeviceQueueCreateInfo queueInfo[1] = {};
    queueInfo[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo[0].queueFamilyIndex = familyIndex;
    queueInfo[0].queueCount = 1;
    queueInfo[0].pQueuePriorities = queuePriority;

    // Enable all features: just pass the physical features 2 struct.
    VkPhysicalDeviceFeatures2 physicalFeatures2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    vkGetPhysicalDeviceFeatures2(m_VulkanPhysicalDevice, &physicalFeatures2);

    VkDeviceCreateInfo deviceCi = {};
    deviceCi.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCi.queueCreateInfoCount = sizeof(queueInfo) / sizeof(queueInfo[0]);
    deviceCi.pQueueCreateInfos = queueInfo;
    deviceCi.enabledExtensionCount = deviceExtensionCount;
    deviceCi.ppEnabledExtensionNames = deviceExtensions;
    deviceCi.pNext = &physicalFeatures2;

    VkResult result =
        vkCreateDevice(m_VulkanPhysicalDevice, &deviceCi, m_VulkanAllocCallbacks, &m_VulkanDevice);
    CHECKRES(result);

    vkGetDeviceQueue(m_VulkanDevice, familyIndex, 0, &m_VulkanQueue);
    m_VulkanQueueFamily = familyIndex;
  }

  //  Get the function pointers to Debug Utils functions.
  if (m_DebugUtilsExtensionPresent)
  {
    pfnSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(
        m_VulkanDevice, "vkSetDebugUtilsObjectNameEXT");
    pfnCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(
        m_VulkanDevice, "vkCmdBeginDebugUtilsLabelEXT");
    pfnCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(
        m_VulkanDevice, "vkCmdEndDebugUtilsLabelEXT");
  }

  // Create surface and swapchain
  {
    SDL_Window* window = (SDL_Window*)p_Creation.window;
    if (SDL_Vulkan_CreateSurface(window, m_VulkanInstance, &m_VulkanWindowSurface) == SDL_FALSE)
    {
      OutputDebugStringA("Failed to create Vulkan surface.\n");
    }
    g_SdlWindow = window;
    int windowWidth, windowHeight;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    // Select Surface Format
    const VkFormat surfaceImageFormats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_R8G8B8_UNORM};
    const VkColorSpaceKHR surfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;

    uint32_t supportedCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_VulkanPhysicalDevice, m_VulkanWindowSurface, &supportedCount, NULL);
    VkSurfaceFormatKHR* supportedFormats = (VkSurfaceFormatKHR*)FRAMEWORK_ALLOCA(
        sizeof(VkSurfaceFormatKHR) * supportedCount, m_Allocator);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_VulkanPhysicalDevice, m_VulkanWindowSurface, &supportedCount, supportedFormats);

    // Cache render pass output
    m_SwapchainOutput.reset();

    // Check for supported formats
    bool formatFound = false;
    const uint32_t surfaceFormatCount = arrayCount32(surfaceImageFormats);

    for (int i = 0; i < surfaceFormatCount; i++)
    {
      for (uint32_t j = 0; j < supportedCount; j++)
      {
        if (supportedFormats[j].format == surfaceImageFormats[i] &&
            supportedFormats[j].colorSpace == surfaceColorSpace)
        {
          m_VulkanSurfaceFormat = supportedFormats[j];
          m_SwapchainOutput.color(surfaceImageFormats[j]);
          formatFound = true;
          break;
        }
      }

      if (formatFound)
        break;
    }

    // Default to the first format supported.
    if (!formatFound)
    {
      m_VulkanSurfaceFormat = supportedFormats[0];
      assert(false);
    }
    FRAMEWORK_FREE(supportedFormats, m_Allocator);

    setPresentMode(m_PresentMode);

    createSwapchain();
  }

  // Create VMA allocator:
  {
    VmaAllocatorCreateInfo ci = {};
    ci.physicalDevice = m_VulkanPhysicalDevice;
    ci.device = m_VulkanDevice;
    ci.instance = m_VulkanInstance;

    VkResult result = vmaCreateAllocator(&ci, &m_VmaAllocator);
    CHECKRES(result);
  }

  // Create descriptor pool
  {
    static const uint32_t kPoolSize = 128;
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kPoolSize},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kPoolSize},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, kPoolSize}};
    VkDescriptorPoolCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.maxSets = kPoolSize * arrayCount32(poolSizes);
    ci.poolSizeCount = arrayCount32(poolSizes);
    ci.pPoolSizes = poolSizes;
    VkResult result = vkCreateDescriptorPool(
        m_VulkanDevice, &ci, m_VulkanAllocCallbacks, &m_VulkanDescriptorPool);
    CHECKRES(result);
  }

  // Init pools
  m_Buffers.init(m_Allocator, 512, sizeof(Buffer));
  m_Textures.init(m_Allocator, 512, sizeof(Texture));
  m_RenderPasses.init(m_Allocator, 256, sizeof(RenderPass));
  m_DescriptorSetLayouts.init(m_Allocator, 128, sizeof(DesciptorSetLayout));
  m_Pipelines.init(m_Allocator, 128, sizeof(Pipeline));
  m_Shaders.init(m_Allocator, 128, sizeof(ShaderState));
  m_DescriptorSets.init(m_Allocator, 128, sizeof(DesciptorSet));
  m_Samplers.init(m_Allocator, 32, sizeof(Sampler));

  // Create synchronization objects
  VkSemaphoreCreateInfo semaphoreCi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  vkCreateSemaphore(
      m_VulkanDevice, &semaphoreCi, m_VulkanAllocCallbacks, &m_VulkanImageAquiredSempaphore);

  for (size_t i = 0; i < kMaxSwapchainImages; i++)
  {

    vkCreateSemaphore(
        m_VulkanDevice, &semaphoreCi, m_VulkanAllocCallbacks, &m_VulkanRenderCompleteSempaphore[i]);

    VkFenceCreateInfo fenceCi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceCi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(
        m_VulkanDevice, &fenceCi, m_VulkanAllocCallbacks, &m_VulkanCmdBufferExectuedFence[i]);
  }

  // init the command buffer ring:
  g_CmdBufferRing.init(this);

  // Setup resource delection queue and descrptr set updates

  // create sampler, depth images and textures and renderpass

  // dynamic buffer handling
}
//---------------------------------------------------------------------------//
void GpuDevice::shutdown()
{
  vkDeviceWaitIdle(m_VulkanDevice);

  g_CmdBufferRing.shutdown();

  for (size_t i = 0; i < kMaxSwapchainImages; i++)
  {
    vkDestroySemaphore(m_VulkanDevice, m_VulkanRenderCompleteSempaphore[i], m_VulkanAllocCallbacks);
    vkDestroyFence(m_VulkanDevice, m_VulkanCmdBufferExectuedFence[i], m_VulkanAllocCallbacks);
  }

  vkDestroySemaphore(m_VulkanDevice, m_VulkanImageAquiredSempaphore, m_VulkanAllocCallbacks);

  destroySwapchain();
  vkDestroySurfaceKHR(m_VulkanInstance, m_VulkanWindowSurface, m_VulkanAllocCallbacks);

  vmaDestroyAllocator(m_VmaAllocator);

  m_Buffers.shutdown();
  m_Textures.shutdown();
  m_RenderPasses.shutdown();
  m_DescriptorSetLayouts.shutdown();
  m_Pipelines.shutdown();
  m_Shaders.shutdown();
  m_DescriptorSets.shutdown();
  m_Samplers.shutdown();

  auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      m_VulkanInstance, "vkDestroyDebugUtilsMessengerEXT");
  vkDestroyDebugUtilsMessengerEXT(
      m_VulkanInstance, m_VulkanDebugUtilsMessenger, m_VulkanAllocCallbacks);

  vkDestroyDescriptorPool(m_VulkanDevice, m_VulkanDescriptorPool, m_VulkanAllocCallbacks);

  vkDestroyDevice(m_VulkanDevice, m_VulkanAllocCallbacks);
  vkDestroyInstance(m_VulkanInstance, m_VulkanAllocCallbacks);

  OutputDebugStringA("Gpu device shutdown\n");
}
//---------------------------------------------------------------------------//
void GpuDevice::setPresentMode(PresentMode::Enum p_Mode)
{

  // Request a certain mode and confirm that it is available.
  // If not use VK_PRESENT_MODE_FIFO_KHR which is mandatory
  uint32_t supportedCount = 0;

  static VkPresentModeKHR supportedModeAllocated[8];
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      m_VulkanPhysicalDevice, m_VulkanWindowSurface, &supportedCount, NULL);
  assert(supportedCount < 8);
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      m_VulkanPhysicalDevice, m_VulkanWindowSurface, &supportedCount, supportedModeAllocated);

  bool modeFound = false;
  VkPresentModeKHR requestedMode = toVkPresentMode(p_Mode);
  for (uint32_t j = 0; j < supportedCount; j++)
  {
    if (requestedMode == supportedModeAllocated[j])
    {
      modeFound = true;
      break;
    }
  }

  // Default to VK_PRESENT_MODE_FIFO_KHR that is guaranteed to always be supported
  m_VulkanPresentMode = modeFound ? requestedMode : VK_PRESENT_MODE_FIFO_KHR;
  // Use 4 for immediate ?
  m_VulkanSwapchainImageCount = 3;

  m_PresentMode = modeFound ? p_Mode : PresentMode::kVSync;
}
//---------------------------------------------------------------------------//
void GpuDevice::createSwapchain()
{
  // Check if surface is supported
  VkBool32 surfaceSupported;
  vkGetPhysicalDeviceSurfaceSupportKHR(
      m_VulkanPhysicalDevice, m_VulkanQueueFamily, m_VulkanWindowSurface, &surfaceSupported);
  if (surfaceSupported != VK_TRUE)
  {
    OutputDebugStringA("Error no WSI support on physical device 0\n");
  }

  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      m_VulkanPhysicalDevice, m_VulkanWindowSurface, &surfaceCapabilities);

  VkExtent2D swapchainExtent = surfaceCapabilities.currentExtent;
  if (swapchainExtent.width == UINT32_MAX)
  {
    swapchainExtent.width = clamp(
        swapchainExtent.width,
        surfaceCapabilities.minImageExtent.width,
        surfaceCapabilities.maxImageExtent.width);
    swapchainExtent.height = clamp(
        swapchainExtent.height,
        surfaceCapabilities.minImageExtent.height,
        surfaceCapabilities.maxImageExtent.height);
  }

  {
    char msg[256]{};
    sprintf(
        msg,
        "Create swapchain %u %u - saved %u %u, min image %u\n",
        swapchainExtent.width,
        swapchainExtent.height,
        m_SwapchainWidth,
        m_SwapchainHeight,
        surfaceCapabilities.minImageCount);
    OutputDebugStringA(msg);
  }

  m_SwapchainWidth = (uint16_t)swapchainExtent.width;
  m_SwapchainHeight = (uint16_t)swapchainExtent.height;

  // m_VulkanSwapchainImageCount = surfaceCapabilities.minImageCount + 2;

  VkSwapchainCreateInfoKHR swapchainCi = {};
  swapchainCi.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainCi.surface = m_VulkanWindowSurface;
  swapchainCi.minImageCount = m_VulkanSwapchainImageCount;
  swapchainCi.imageFormat = m_VulkanSurfaceFormat.format;
  swapchainCi.imageExtent = swapchainExtent;
  swapchainCi.clipped = VK_TRUE;
  swapchainCi.imageArrayLayers = 1;
  swapchainCi.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  swapchainCi.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainCi.preTransform = surfaceCapabilities.currentTransform;
  swapchainCi.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchainCi.presentMode = m_VulkanPresentMode;

  VkResult result = vkCreateSwapchainKHR(m_VulkanDevice, &swapchainCi, 0, &m_VulkanSwapchain);
  CHECKRES(result);

  //// Cache swapchain images
  vkGetSwapchainImagesKHR(m_VulkanDevice, m_VulkanSwapchain, &m_VulkanSwapchainImageCount, NULL);
  vkGetSwapchainImagesKHR(
      m_VulkanDevice, m_VulkanSwapchain, &m_VulkanSwapchainImageCount, m_VulkanSwapchainImages);

  for (size_t iv = 0; iv < m_VulkanSwapchainImageCount; iv++)
  {
    // Create an image view which we can render into.
    VkImageViewCreateInfo imageViewCi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    imageViewCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCi.format = m_VulkanSurfaceFormat.format;
    imageViewCi.image = m_VulkanSwapchainImages[iv];
    imageViewCi.subresourceRange.levelCount = 1;
    imageViewCi.subresourceRange.layerCount = 1;
    imageViewCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCi.components.r = VK_COMPONENT_SWIZZLE_R;
    imageViewCi.components.g = VK_COMPONENT_SWIZZLE_G;
    imageViewCi.components.b = VK_COMPONENT_SWIZZLE_B;
    imageViewCi.components.a = VK_COMPONENT_SWIZZLE_A;

    result = vkCreateImageView(
        m_VulkanDevice, &imageViewCi, m_VulkanAllocCallbacks, &m_VulkanSwapchainImageViews[iv]);
    CHECKRES(result);
  }
}
//---------------------------------------------------------------------------//
void GpuDevice::destroySwapchain()
{
  for (size_t iv = 0; iv < m_VulkanSwapchainImageCount; iv++)
  {
    vkDestroyImageView(m_VulkanDevice, m_VulkanSwapchainImageViews[iv], m_VulkanAllocCallbacks);
    vkDestroyFramebuffer(m_VulkanDevice, m_VulkanSwapchainFramebuffers[iv], m_VulkanAllocCallbacks);
  }

  vkDestroySwapchainKHR(m_VulkanDevice, m_VulkanSwapchain, m_VulkanAllocCallbacks);
}
//---------------------------------------------------------------------------//
} // namespace Graphics
