#if !defined(SPV_FOLDER)
#  define SPV_FOLDER "\\Shaders\\"
#endif

#if !defined(WORKING_FOLDER)
#  define WORKING_FOLDER "\\"
#endif

#if !defined(DATA_FOLDER)
#  define DATA_FOLDER "\\Data\\"
#endif

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

// TODOS:
// 1. Fix uniforms not getting updated
// 2. Double check DOF pass doesn't get disabled
// 3. fix memory leak reported on shutdown
// 4. Fix artifacts on Sponza curtains

//---------------------------------------------------------------------------//
// Graphics includes:
#include "Graphics/GpuDevice.hpp"
#include "Graphics/CommandBuffer.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/ImguiHelper.hpp"
#include "Graphics/RenderScene.hpp"
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
  using namespace Framework;

  // Init services
  MemoryServiceConfiguration memoryConfiguration;
  memoryConfiguration.MaximumDynamicSize = FRAMEWORK_GIGA(2ull);

  MemoryService::instance()->init(&memoryConfiguration);
  Allocator* allocator = &MemoryService::instance()->m_SystemAllocator;

  StackAllocator scratchAllocator;
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
      1280, 800, "Framework Chapter 5", &MemoryService::instance()->m_SystemAllocator};
  Framework::Window window;
  window.init(&wconf);

  InputService input;
  input.init(allocator);

  // Callback register: input needs to react to OS messages.
  window.registerOSMessagesCallback(inputOSMessagesCallback, &input);

  // graphics
  Graphics::DeviceCreation dc;
  dc.setWindow(window.m_Width, window.m_Height, window.m_PlatformHandle)
      .setAllocator(&MemoryService::instance()->m_SystemAllocator)
      .setNumThreads(taskScheduler.GetNumTaskThreads())
      .setTemporaryAllocator(&scratchAllocator);
  Graphics::GpuDevice gpu;
  gpu.init(dc);

  ResourceManager rm;
  rm.init(allocator, nullptr);

  Graphics::RendererUtil::Renderer renderer;
  renderer.init({&gpu, allocator});
  renderer.setLoaders(&rm);

  Graphics::ImguiUtil::ImguiService* imgui = Graphics::ImguiUtil::ImguiService::instance();
  Graphics::ImguiUtil::ImguiServiceConfiguration imguiConfig{&gpu, window.m_PlatformHandle};
  imgui->init(&imguiConfig);

  GameCamera gameCamera;
  gameCamera.camera.initPerspective(0.1f, 1000.f, 60.f, wconf.m_Width * 1.f / wconf.m_Height);
  gameCamera.init(true, 20.f, 6.f, 0.1f);

  Time::serviceInit();

  Graphics::FrameGraphBuilder frameGraphBuilder;
  frameGraphBuilder.init(&gpu);

  Graphics::FrameGraph frameGraph;
  frameGraph.init(&frameGraphBuilder);

  Graphics::RenderResourcesLoader renderResourcesLoader;
  Graphics::RendererUtil::TextureResource* ditherTexture = nullptr;

  size_t scratchMarker = scratchAllocator.getMarker();

  StringBuffer temporaryNameBuffer;
  temporaryNameBuffer.init(1024, &scratchAllocator);

  //#define WORKING_FOLDER ""
  //#define DATA_FOLDER ""

  // Load frame graph and parse gpu techniques
  {
    cstring frameGraphPath =
        temporaryNameBuffer.appendUseFormatted("%s/%s", WORKING_FOLDER, "graph.json");

    frameGraph.parse(frameGraphPath, &scratchAllocator);
    frameGraph.compile();

    renderResourcesLoader.init(&renderer, &scratchAllocator, &frameGraph);

    // TODO: add this to render graph itself.
    // Add utility textures (dithering, ...)
    temporaryNameBuffer.clear();
    cstring ditherTexturePath =
        temporaryNameBuffer.appendUseFormatted("%s/BayerDither4x4.png", DATA_FOLDER);
    ditherTexture = renderResourcesLoader.loadTexture(ditherTexturePath, false);

    // Parse techniques
    Graphics::RendererUtil::GpuTechniqueCreation gtc;
    temporaryNameBuffer.clear();
    cstring fullScreenPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s/%s", SHADER_FOLDER, "fullscreen.json");
    renderResourcesLoader.loadGpuTechnique(fullScreenPipelinePath);

    temporaryNameBuffer.clear();
    cstring mainPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s/%s", SHADER_FOLDER, "main.json");
    renderResourcesLoader.loadGpuTechnique(mainPipelinePath);

    temporaryNameBuffer.clear();
    cstring pbrPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s/%s", SHADER_FOLDER, "pbr_lighting.json");
    renderResourcesLoader.loadGpuTechnique(pbrPipelinePath);

    temporaryNameBuffer.clear();
    cstring dofPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s/%s", SHADER_FOLDER, "dof.json");
    renderResourcesLoader.loadGpuTechnique(dofPipelinePath);

    temporaryNameBuffer.clear();
    cstring clothPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s/%s", SHADER_FOLDER, "cloth.json");
    renderResourcesLoader.loadGpuTechnique(clothPipelinePath);

    temporaryNameBuffer.clear();
    cstring debugPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s/%s", SHADER_FOLDER, "debug.json");
    renderResourcesLoader.loadGpuTechnique(debugPipelinePath);
  }

  Graphics::SceneGraph sceneGraph;
  sceneGraph.init(allocator, 4);

  // [TAG: Multithreading]
  Graphics::AsynchronousLoader asyncLoader;
  asyncLoader.init(&renderer, &taskScheduler, allocator);

  Directory cwd{};
  directoryCurrent(&cwd);

  temporaryNameBuffer.clear();
  cstring scenePath = nullptr;
  if (argc > 1)
  {
    scenePath = argv[1];
  }
  else
  {
    scenePath = temporaryNameBuffer.appendUseFormatted("%s/%s", DATA_FOLDER, "plane.obj");
  }

  char fileBasePath[512]{};
  memcpy(fileBasePath, scenePath, strlen(scenePath));
  fileDirectoryFromPath(fileBasePath);

  directoryChange(fileBasePath);

  char fileName[512]{};
  memcpy(fileName, scenePath, strlen(scenePath));
  filenameFromPath(fileName);

  scratchAllocator.freeMarker(scratchMarker);

  Graphics::RenderScene* scene = nullptr;

  char* fileExtension = fileExtensionFromPath(fileName);

  assert(strcmp(fileExtension, "gltf") == 0);

  scene->init(fileName, fileBasePath, allocator, &scratchAllocator, &asyncLoader);

  // NOTE: restore working directory
  directoryChange(cwd.path);

  Graphics::FrameRenderer frameRenderer;
  frameRenderer.init(allocator, &renderer, &frameGraph, &sceneGraph, scene);
  frameRenderer.prepareDraws(&scratchAllocator);

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

  int16_t beginFrameTick = Time::getCurrentTime();
  int16_t absoluteBeginFrameTick = beginFrameTick;

  vec3s lightPosition = vec3s{0.0f, 4.0f, 0.0f};

  float lightRadius = 20.0f;
  float lightIntensity = 80.0f;

  float springStiffness = 10000.0f;
  float springDamping = 5000.0f;
  float airDensity = 10.0f;
  bool resetSimulation = false;
  vec3s windDirection{-5.0f, 0.0f, 0.0f};

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
      renderer.resizeSwapchain(window.m_Width, window.m_Height);
      window.m_Resized = false;
      frameGraph.onResize(gpu, window.m_Width, window.m_Height);

      gameCamera.camera.setAspectRatio(window.m_Width * 1.f / window.m_Height);
    }
    // This MUST be AFTER os messages!
    imgui->newFrame();

    const int16_t currentTick = Time::getCurrentTime();
    float deltaTime = (float)Time::deltaSeconds(beginFrameTick, currentTick);
    beginFrameTick = currentTick;

    input.update(deltaTime);
    gameCamera.update(&input, window.m_Width, window.m_Height, deltaTime);
    window.centerMouse(gameCamera.mouseDragging);

    static float animationSpeedMultiplier = 0.05f;

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
        ImGui::InputFloat3("Wind direction", windDirection.raw);
        ImGui::InputFloat("Air density", &airDensity);
        ImGui::InputFloat("Spring stiffness", &springStiffness);
        ImGui::InputFloat("Spring damping", &springDamping);
        ImGui::Checkbox("Reset simulation", &resetSimulation);
        ImGui::Separator();
        ImGui::Checkbox(
            "Dynamically recreate descriptor sets", &Graphics::g_RecreatePerThreadDescriptors);
        ImGui::Checkbox("Use secondary command buffers", &Graphics::g_UseSecondaryCommandBuffers);

        ImGui::SliderFloat("Animation Speed Multiplier", &animationSpeedMultiplier, 0.0f, 10.0f);

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

      if (ImGui::Begin("Scene"))
      {

        static uint32_t selectedNode = UINT32_MAX;

        ImGui::Text("Selected node %u", selectedNode);
        if (selectedNode < sceneGraph.nodesHierarchy.m_Size)
        {

          mat4s& localTransform = sceneGraph.localMatrices[selectedNode];
          float position[3]{localTransform.m30, localTransform.m31, localTransform.m32};

          if (ImGui::SliderFloat3("Node Position", position, -100.0f, 100.0f))
          {
            localTransform.m30 = position[0];
            localTransform.m31 = position[1];
            localTransform.m32 = position[2];

            sceneGraph.setLocalMatrix(selectedNode, localTransform);
          }
          ImGui::Separator();
        }

        for (uint32_t n = 0; n < sceneGraph.nodesHierarchy.m_Size; ++n)
        {
          const Graphics::SceneGraphNodeDebugData& node_debug_data = sceneGraph.nodesDebugData[n];
          if (ImGui::Selectable(
                  node_debug_data.name ? node_debug_data.name : "-", n == selectedNode))
          {
            selectedNode = n;
          }
        }
      }
      ImGui::End();

      if (ImGui::Begin("GPU"))
      {
        renderer.imguiDraw();
      }
      ImGui::End();
    }
    {
      scene->updateAnimations(deltaTime * animationSpeedMultiplier);
    }
    {
      sceneGraph.updateMatrices();
    }
    {
      scene->updateJoints();
    }

    {
      // Update scene constant buffer
      Graphics::MapBufferParameters sceneCbMap = {scene->sceneCb, 0, 0};
      Graphics::GpuSceneData* gpuSceneData = (Graphics::GpuSceneData*)gpu.mapBuffer(sceneCbMap);
      if (gpuSceneData)
      {
        gpuSceneData->viewProjection = gameCamera.camera.viewProjection;
        gpuSceneData->inverseViewProjection = glms_mat4_inv(gameCamera.camera.viewProjection);
        gpuSceneData->eye = vec4s{
            gameCamera.camera.position.x,
            gameCamera.camera.position.y,
            gameCamera.camera.position.z,
            1.0f};
        gpuSceneData->lightPosition =
            vec4s{lightPosition.x, lightPosition.y, lightPosition.z, 1.0f};
        gpuSceneData->lightRange = lightRadius;
        gpuSceneData->lightIntensity = lightIntensity;
        gpuSceneData->ditherTextureIndex = ditherTexture ? ditherTexture->m_Handle.index : 0;

        gpu.unmapBuffer(sceneCbMap);
      }

      frameRenderer.uploadGpuData();
    }

    if (!window.m_Minimized)
    {
      Graphics::DrawTask drawTask;
      drawTask.init(renderer.m_GpuDevice, &frameGraph, &renderer, imgui, scene, &frameRenderer);
      taskScheduler.AddTaskSetToPipe(&drawTask);

      Graphics::CommandBuffer* async_compute_command_buffer = nullptr;
      {
        async_compute_command_buffer = scene->updatePhysics(
            deltaTime, airDensity, springStiffness, springDamping, windDirection, resetSimulation);
        resetSimulation = false;
      }

      taskScheduler.WaitforTaskSet(&drawTask);

      // Avoid using the same command buffer
      renderer.addTextureUpdateCommands(
          (drawTask.threadId + 1) % taskScheduler.GetNumTaskThreads());
      gpu.present(async_compute_command_buffer);
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
  frameRenderer.shutdown();

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
