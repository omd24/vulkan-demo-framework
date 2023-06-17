#include <Foundation/File.hpp>
#include <Foundation/Numerics.hpp>
#include <Foundation/Time.hpp>
#include <Foundation/ResourceManager.hpp>

#include <Application/Window.hpp>
#include <Application/Input.hpp>
#include <Application/Keys.hpp>
#include <Application/GameCamera.hpp>

#include <Externals/cglm/struct/mat3.h>
#include <Externals/cglm/struct/mat4.h>
#include <Externals/cglm/struct/cam.h>
#include <Externals/cglm/struct/affine.h>
#include <Externals/enkiTS/TaskScheduler.h>
#include <Externals/json.hpp>

#include <Externals/imgui/imgui.h>
#include <Externals/stb_image.h>

#include <stdio.h>
#include <stdlib.h> // for exit()

//---------------------------------------------------------------------------//
// Graphics includes:
#include "Graphics/GpuDevice.hpp"
#include "Graphics/CommandBuffer.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/ImguiHelper.hpp"
#include "Graphics/RenderSceneBase.hpp"
#include "Graphics/GltfScene.hpp"
#include "Graphics/FrameGraph.hpp"
#include "Graphics/AsynchronousLoader.hpp"
#include "Graphics/SceneGraph.hpp"
#include "Graphics/RenderResourcesLoader.hpp"
//---------------------------------------------------------------------------//
// Window message loop callback:
//---------------------------------------------------------------------------//
static void inputOSMessagesCallback(void* p_OSEvent, void* p_UserData)
{
  Framework::InputService* input = (Framework::InputService*)p_UserData;
  input->onEvent(p_OSEvent);
}
//---------------------------------------------------------------------------//
// IO Tasks
//---------------------------------------------------------------------------//
struct RunPinnedTaskLoopTask : enki::IPinnedTask
{

  void Execute() override
  {
    while (m_TaskScheduler->GetIsRunning() && m_Execute)
    {
      m_TaskScheduler
          ->WaitForNewPinnedTasks(); // this thread will 'sleep' until there are new pinned tasks
      m_TaskScheduler->RunPinnedTasks();
    }
  }

  enki::TaskScheduler* m_TaskScheduler;
  bool m_Execute = true;
};
//---------------------------------------------------------------------------//
struct AsynchronousLoadTask : enki::IPinnedTask
{

  void Execute() override
  {
    // Do file IO
    while (m_Execute)
    {
      m_AsyncLoader->update(nullptr);
    }
  }

  Graphics::AsynchronousLoader* m_AsyncLoader;
  enki::TaskScheduler* m_TaskScheduler;
  bool m_Execute = true;
};
//---------------------------------------------------------------------------//
// Entry point:
//---------------------------------------------------------------------------//
int main(int argc, char** argv)
{
  const char* modelPath = "c:/gltf-models/Sponza/Sponza.gltf";

  using namespace Graphics;
  // Init services
  MemoryServiceConfiguration memoryConfiguration;
  memoryConfiguration.MaximumDynamicSize = FRAMEWORK_GIGA(2ull);

  MemoryService::instance()->init(&memoryConfiguration);
  Framework::Allocator* allocator = &MemoryService::instance()->m_SystemAllocator;

  Framework::StackAllocator scratchAllocator;
  scratchAllocator.init(FRAMEWORK_MEGA(8));

  enki::TaskSchedulerConfig config;
  // In this example we create more threads than the hardware can run,
  // because the IO thread will spend most of it's time idle or blocked
  // and therefore not scheduled for CPU time by the OS
  config.numTaskThreadsToCreate += 1;
  enki::TaskScheduler taskScheduler;

  taskScheduler.Initialize(config);

  // window
  WindowConfiguration wconf{
      1280, 800, "Framework Chapter 4", &MemoryService::instance()->m_SystemAllocator};
  Framework::Window window;
  window.init(&wconf);

  InputService input;
  input.init(allocator);

  // Callback register: input needs to react to OS messages.
  window.registerOSMessagesCallback(inputOSMessagesCallback, &input);

  // graphics
  DeviceCreation deviceCreation;
  deviceCreation.setWindow(window.m_Width, window.m_Height, window.m_PlatformHandle)
      .setAllocator(&MemoryService::instance()->m_SystemAllocator)
      .setNumThreads(taskScheduler.GetNumTaskThreads())
      .setTemporaryAllocator(&scratchAllocator);
  GpuDevice gpu;
  gpu.init(deviceCreation);

  ResourceManager rm;
  rm.init(allocator, nullptr);

  RendererUtil::Renderer renderer;
  renderer.init({&gpu, allocator});
  renderer.setLoaders(&rm);

  ImguiUtil::ImguiService* imgui = ImguiUtil::ImguiService::instance();
  ImguiUtil::ImguiServiceConfiguration imguiConfig{&gpu, window.m_PlatformHandle};
  imgui->init(&imguiConfig);

  GameCamera gameCamera;
  gameCamera.camera.initPerspective(0.1f, 1000.f, 60.f, wconf.m_Width * 1.f / wconf.m_Height);
  gameCamera.init(true, 20.f, 6.f, 0.1f);

  Framework::Time::serviceInit();

  FrameGraphBuilder frameGraphBuilder;
  frameGraphBuilder.init(&gpu);

  FrameGraph frameGraph;
  frameGraph.init(&frameGraphBuilder);

  RenderResourcesLoader renderResourcesLoader;

  // Load frame graph and parse gpu techniques
  {
    Directory cwd{};
    Framework::directoryCurrent(&cwd);

    size_t scratchMarker = scratchAllocator.getMarker();

    StringBuffer temporaryNameBuffer;
    temporaryNameBuffer.init(1024, &scratchAllocator);
    cstring frameGraphPath = temporaryNameBuffer.appendUseFormatted("%s/%s", cwd, "graph.json");

    frameGraph.parse(frameGraphPath, &scratchAllocator);
    frameGraph.compile();

    renderResourcesLoader.init(&renderer, &scratchAllocator, &frameGraph);

    // Parse techniques
    RendererUtil::GpuTechniqueCreation gtc;
    temporaryNameBuffer.clear();
    cstring fullScreenPipelinePath = temporaryNameBuffer.appendUseFormatted(
        "%s/%s%s", cwd.path, SHADER_FOLDER, "fullscreen.json");
    renderResourcesLoader.loadGpuTechnique(fullScreenPipelinePath);

    temporaryNameBuffer.clear();
    cstring mainPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s%s%s", cwd.path, SHADER_FOLDER, "main.json");
    renderResourcesLoader.loadGpuTechnique(mainPipelinePath);

    temporaryNameBuffer.clear();
    cstring pbrPipelinePath = temporaryNameBuffer.appendUseFormatted(
        "%s/%s%s", cwd.path, SHADER_FOLDER, "pbr_lighting.json");
    renderResourcesLoader.loadGpuTechnique(pbrPipelinePath);

    temporaryNameBuffer.clear();
    cstring dofPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s/%s%s", cwd.path, SHADER_FOLDER, "dof.json");
    renderResourcesLoader.loadGpuTechnique(dofPipelinePath);

    scratchAllocator.freeMarker(scratchMarker);
  }

  SceneGraph sceneGraph;
  sceneGraph.init(allocator, 4);

  // [TAG: Multithreading]
  AsynchronousLoader asyncLoader;
  asyncLoader.init(&renderer, &taskScheduler, allocator);

  Directory cwd{};
  directoryCurrent(&cwd);

  char fileBasePath[512]{};
  memcpy(fileBasePath, modelPath, strlen(modelPath));
  fileDirectoryFromPath(fileBasePath);

  directoryChange(fileBasePath);

  char filename[512]{};
  memcpy(filename, modelPath, strlen(modelPath));
  filenameFromPath(filename);

  RenderScene* scene = nullptr;

  char* fileExtension = fileExtensionFromPath(filename);

  if (strcmp(fileExtension, "gltf") != 0)
  {
    assert(false && "Other formats not implemented");
  }
  scene = new glTFScene;

  scene->init(filename, fileBasePath, allocator, &scratchAllocator, &asyncLoader);

  // Restore working directory
  directoryChange(cwd.path);

  scene->registerRenderPasses(&frameGraph);
  scene->prepareDraws(&renderer, &scratchAllocator, &sceneGraph);

  // Start multithreading IO
  // Create IO threads at the end
  RunPinnedTaskLoopTask runPinnedTask;
  runPinnedTask.threadNum = taskScheduler.GetNumTaskThreads() - 1;
  runPinnedTask.m_TaskScheduler = &taskScheduler;
  taskScheduler.AddPinnedTask(&runPinnedTask);

  // Send async load task to external thread FILE_IO
  AsynchronousLoadTask asyncLoadTask;
  asyncLoadTask.threadNum = runPinnedTask.threadNum;
  asyncLoadTask.m_TaskScheduler = &taskScheduler;
  asyncLoadTask.m_AsyncLoader = &asyncLoader;
  taskScheduler.AddPinnedTask(&asyncLoadTask);

  int64_t beginFrameTick = Time::getCurrentTime();
  int64_t absoluteBeginFrameTick = beginFrameTick;

  vec3s lightPosition = vec3s{0.0f, 4.0f, 0.0f};

  float lightRadius = 20.0f;
  float lightIntensity = 80.0f;

  while (!window.m_RequestedExit)
  {
    // New frame
    if (!window.m_Minimized)
    {
      gpu.newFrame();

      static bool checksz = true;
      if (asyncLoader.fileLoadRequests.m_Size == 0 && checksz)
      {
        checksz = false;
        printf(
            "Finished uploading textures in %f seconds\n",
            Time::deltaFromStartSeconds(absoluteBeginFrameTick));
      }
    }

    window.handleOSMessages();
    input.newFrame();

    if (window.m_Resized)
    {
      gpu.resize(window.m_Width, window.m_Height);
      window.m_Resized = false;
      frameGraph.onResize(gpu, window.m_Width, window.m_Height);

      gameCamera.camera.setAspectRatio(window.m_Width * 1.f / window.m_Height);
    }
    // This MUST be AFTER os messages!
    imgui->newFrame();

    const int64_t currentTick = Time::getCurrentTime();
    float deltaTime = (float)Time::deltaSeconds(beginFrameTick, currentTick);
    beginFrameTick = currentTick;

    input.update(deltaTime);
    gameCamera.update(&input, window.m_Width, window.m_Height, deltaTime);
    window.centerMouse(gameCamera.mouseDragging);

    {
      if (ImGui::Begin("Framework ImGui"))
      {
        ImGui::InputFloat("Scene global scale", &scene->globalScale, 0.001f);
        ImGui::SliderFloat3("Light position", lightPosition.raw, -30.0f, 30.0f);
        ImGui::InputFloat("Light radius", &lightRadius);
        ImGui::InputFloat("Light intensity", &lightIntensity);
        ImGui::InputFloat3("Camera position", gameCamera.camera.position.raw);
        ImGui::InputFloat3("Camera target movement", gameCamera.targetMovement.raw);
        ImGui::Separator();
        ImGui::Checkbox("Dynamically recreate descriptor sets", &g_RecreatePerThreadDescriptors);
        ImGui::Checkbox("Use secondary command buffers", &g_UseSecondaryCommandBuffers);

        static bool fullscreen = false;
        if (ImGui::Checkbox("Fullscreen", &fullscreen))
        {
          window.setFullscreen(fullscreen);
        }

        static int presentMode = renderer.m_GpuDevice->m_PresentMode;
        if (ImGui::Combo(
                "Present Mode",
                &presentMode,
                Graphics::PresentMode::sValueNames,
                Graphics::PresentMode::kCount))
        {
          renderer.setPresentationMode((Graphics::PresentMode::Enum)presentMode);
        }

        frameGraph.addUi();
      }
      ImGui::End();

      if (ImGui::Begin("GPU"))
      {
        renderer.imguiDraw();

        ImGui::Separator();
      }
      ImGui::End();
    }
    {
      sceneGraph.updateMatrices();
    }

    {
      // Update scene constant buffer
      MapBufferParameters cbMap = {scene->sceneCb, 0, 0};
      GpuSceneData* uniformData = (GpuSceneData*)gpu.mapBuffer(cbMap);
      if (uniformData)
      {
        uniformData->viewProj = gameCamera.camera.viewProjection;
        uniformData->eye = vec4s{
            gameCamera.camera.position.x,
            gameCamera.camera.position.y,
            gameCamera.camera.position.z,
            1.0f};
        uniformData->lightPosition = vec4s{lightPosition.x, lightPosition.y, lightPosition.z, 1.0f};
        uniformData->lightRange = lightRadius;
        uniformData->lightIntensity = lightIntensity;

        gpu.unmapBuffer(cbMap);
      }

      scene->uploadMaterials(/* model_scale */);
    }

    if (!window.m_Minimized)
    {

      scene->submitDrawTask(imgui, &taskScheduler);

      gpu.present();
    }
    else
    {
      ImGui::Render();
    }
  }

  runPinnedTask.m_Execute = false;
  asyncLoadTask.m_Execute = false;

  taskScheduler.WaitforAllAndShutdown();

  vkDeviceWaitIdle(gpu.m_VulkanDevice);

  asyncLoader.shutdown();

  imgui->shutdown();

  sceneGraph.shutdown();

  frameGraph.shutdown();
  frameGraphBuilder.shutdown();

  scene->shutdown(&renderer);

  rm.shutdown();
  renderer.shutdown();

  delete scene;

  input.shutdown();
  window.unregisterOSMessagesCallback(inputOSMessagesCallback);
  window.shutdown();

  scratchAllocator.shutdown();
  MemoryService::instance()->shutdown();

  return 0;
}
//---------------------------------------------------------------------------//
