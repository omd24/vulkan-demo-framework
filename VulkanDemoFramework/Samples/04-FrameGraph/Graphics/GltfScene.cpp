#include "Graphics/GltfScene.hpp"

#include "Graphics/SceneGraph.hpp"
#include "Graphics/ImguiHelper.hpp"

#include "Externals/cglm/struct/affine.h"
#include "Externals/cglm/struct/mat4.h"
#include "Externals/cglm/struct/vec3.h"
#include "Externals/cglm/struct/quat.h"

namespace Graphics
{
//---------------------------------------------------------------------------//
// Internal methods:
//---------------------------------------------------------------------------//
static int gltfMeshMaterialCompare(const void* a, const void* b)
{
  const Mesh* meshA = (const Mesh*)a;
  const Mesh* meshB = (const Mesh*)b;

  if (meshA->pbrMaterial.material->m_RenderIndex < meshB->pbrMaterial.material->m_RenderIndex)
    return -1;
  if (meshA->pbrMaterial.material->m_RenderIndex > meshB->pbrMaterial.material->m_RenderIndex)
    return 1;
  return 0;
}
//---------------------------------------------------------------------------//
static void copyGpuMaterialData(GpuMeshData& p_GpuMeshData, const Mesh& p_Mesh)
{
  p_GpuMeshData.textures[0] = p_Mesh.pbrMaterial.diffuseTextureIndex;
  p_GpuMeshData.textures[1] = p_Mesh.pbrMaterial.roughnessTextureIndex;
  p_GpuMeshData.textures[2] = p_Mesh.pbrMaterial.normalTextureIndex;
  p_GpuMeshData.textures[3] = p_Mesh.pbrMaterial.occlusionTextureIndex;
  p_GpuMeshData.baseColorFactor = p_Mesh.pbrMaterial.baseColorFactor;
  p_GpuMeshData.metallicRoughnessOcclusionFactor =
      p_Mesh.pbrMaterial.metallicRoughnessOcclusionFactor;
  p_GpuMeshData.alphaCutoff = p_Mesh.pbrMaterial.alphaCutoff;
  p_GpuMeshData.flags = p_Mesh.pbrMaterial.flags;
}
//---------------------------------------------------------------------------//
static void copyGpuMeshMatrix(
    GpuMeshData& p_GpuMeshData,
    const Mesh& p_Mesh,
    const float p_GlobalScale,
    const SceneGraph* p_SceneGraph)
{
  if (p_SceneGraph)
  {
    // Apply global scale matrix
    // NOTE: for left-handed systems (as defined in cglm) need to invert positive and negative Z.
    const mat4s scale_matrix = glms_scale_make({p_GlobalScale, p_GlobalScale, -p_GlobalScale});
    p_GpuMeshData.world =
        glms_mat4_mul(scale_matrix, p_SceneGraph->worldMatrices[p_Mesh.sceneGraphNodeIndex]);

    p_GpuMeshData.inverseWorld = glms_mat4_inv(glms_mat4_transpose(p_GpuMeshData.world));
  }
  else
  {
    p_GpuMeshData.world = glms_mat4_identity();
    p_GpuMeshData.inverseWorld = glms_mat4_identity();
  }
}
//---------------------------------------------------------------------------//
// Depth pre pass:
//---------------------------------------------------------------------------//
void DepthPrePass::render(CommandBuffer* gpuCommands, RenderScene* renderScene);
//---------------------------------------------------------------------------//
void DepthPrePass::prepareDraws(
    glTFScene& scene,
    FrameGraph* frameGraph,
    Allocator* residentAllocator,
    StackAllocator* scratchAllocator);
//---------------------------------------------------------------------------//
void DepthPrePass::freeGpuResources();
//---------------------------------------------------------------------------//
// Gbuffer pass:
//---------------------------------------------------------------------------//
void GBufferPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene);
//---------------------------------------------------------------------------//
void GBufferPass::prepareDraws(
    glTFScene& scene,
    FrameGraph* frameGraph,
    Allocator* residentAllocator,
    StackAllocator* scratchAllocator);
//---------------------------------------------------------------------------//
void GBufferPass::freeGpuResources();
//---------------------------------------------------------------------------//
// Light pass:
void LightPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene);
//---------------------------------------------------------------------------//
void LightPass::prepareDraws(
    glTFScene& scene,
    FrameGraph* frameGraph,
    Allocator* residentAllocator,
    StackAllocator* scratchAllocator);
void LightPass::uploadMaterials();
//---------------------------------------------------------------------------//
void LightPass::freeGpuResources();
//---------------------------------------------------------------------------//
// Transparent pass:
//---------------------------------------------------------------------------//
void TransparentPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene);
//---------------------------------------------------------------------------//
void TransparentPass::prepareDraws(
    glTFScene& scene,
    FrameGraph* frameGraph,
    Allocator* residentAllocator,
    StackAllocator* scratchAllocator);
//---------------------------------------------------------------------------//
void TransparentPass::freeGpuResources();
//---------------------------------------------------------------------------//
// DoF pass:
//---------------------------------------------------------------------------//
void TransparentPass::addUi();
//---------------------------------------------------------------------------//
void TransparentPass::preRender(CommandBuffer* gpuCommands, RenderScene* renderScene);
//---------------------------------------------------------------------------//
void TransparentPass::render(CommandBuffer* gpuCommands, RenderScene* renderScene);
//---------------------------------------------------------------------------//
void TransparentPass::onResize(GpuDevice& gpu, uint32_t new_width, uint32_t new_height);
//---------------------------------------------------------------------------//
void TransparentPass::prepareDraws(
    glTFScene& scene,
    FrameGraph* frameGraph,
    Allocator* residentAllocator,
    StackAllocator* scratchAllocator);
//---------------------------------------------------------------------------//
void TransparentPass::uploadMaterials();
//---------------------------------------------------------------------------//
void TransparentPass::freeGpuResources();
//---------------------------------------------------------------------------//
void glTFScene::init(
    const char* filename,
    const char* path,
    Allocator* residentAllocator,
    StackAllocator* tempAllocator,
    AsynchronousLoader* asyncLoader);
void glTFScene::shutdown(Renderer* renderer);

void glTFScene::registerRenderPasses(FrameGraph* frameGraph);
void glTFScene::prepareDraws(
    Renderer* renderer, StackAllocator* scratchAllocator, SceneGraph* sceneGraph);
void glTFScene::uploadMaterials();
void glTFScene::submitDrawTask(ImGuiService* imgui, enki::TaskScheduler* taskScheduler);

void glTFScene::drawMesh(CommandBuffer* gpuCommands, Mesh& mesh);

void glTFScene::getMeshVertexBuffer(
    int accessorIndex, BufferHandle& outBufferHandle, uint32_t& outBufferOffset);
uint16_t getMaterialTexture(GpuDevice& gpu, Framework::glTF::TextureInfo* textureInfo);
uint16_t getMaterialTexture(GpuDevice& gpu, int gltf_texture_index);

void glTFScene::fillPbrMaterial(
    Renderer& renderer, Framework::glTF::Material& material, PBRMaterial& pbrMaterial);
//---------------------------------------------------------------------------//

void glTFDrawTask::init(
    GpuDevice* p_Gpu,
    FrameGraph* p_FrameGraph,
    RendererUtil::Renderer* p_Renderer,
    ImguiUtil::ImguiService* p_Imgui,
    glTFScene* p_Scene)
{
  gpu = p_Gpu;
  frameGraph = p_FrameGraph;
  renderer = p_Renderer;
  imgui = p_Imgui;
  scene = p_Scene;
}

//---------------------------------------------------------------------------//
void glTFDrawTask::ExecuteRange(enki::TaskSetPartition p_Range, uint32_t p_ThreadNum)
{
  using namespace Framework;

  threadId = p_ThreadNum;

  // printf( "Executing draw task from thread %u\n", p_ThreadNum );
  // TODO: improve getting a command buffer/pool
  CommandBuffer* gpuCommands = gpu->getCommandBuffer(p_ThreadNum, true);

  frameGraph->render(gpuCommands, scene);

  gpuCommands->clear(0.3f, 0.3f, 0.3f, 1.f);
  gpuCommands->clearDepthStencil(1.0f, 0);
  gpuCommands->bindPass(gpu->m_SwapchainRenderPass, gpu->getCurrentFramebuffer(), false);
  gpuCommands->setScissor(nullptr);
  gpuCommands->setViewport(nullptr);

  // TODO: add global switch
  if (false)
  {
    RendererUtil::Material* lastMaterial = nullptr;
    // TODO: loop by material so that we can deal with multiple passes
    for (uint32_t meshIndex = 0; meshIndex < scene->meshes.m_Size; ++meshIndex)
    {
      Mesh& mesh = scene->meshes[meshIndex];

      if (mesh.pbrMaterial.material != lastMaterial)
      {
        PipelineHandle pipeline = renderer->getPipeline(mesh.pbrMaterial.material, 3);

        gpuCommands->bindPipeline(pipeline);

        lastMaterial = mesh.pbrMaterial.material;
      }

      scene->drawMesh(gpuCommands, mesh);
    }
  }
  else
  {
    // Apply fullscreen material
    gpuCommands->bindPipeline(scene->fullscreenTech->passes[0].pipeline);
    gpuCommands->bindDescriptorSet(&scene->fullscreenDS, 1, nullptr, 0);
    gpuCommands->draw(TopologyType::kTriangle, 0, 3, scene->fullscreenInputRT, 1);
  }

  imgui->render(*gpuCommands, false);

  // Send commands to GPU
  gpu->queueCommandBuffer(gpuCommands);
}
//---------------------------------------------------------------------------//
} // namespace Graphics
