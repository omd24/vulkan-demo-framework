#pragma once
#include "Foundation/Array.hpp"
#include "Foundation/Prerequisites.hpp"
#include "Foundation/Color.hpp"

#include "Graphics/CommandBuffer.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/GpuResources.hpp"
#include "Graphics/FrameGraph.hpp"

#include "Externals/cglm/types-struct.h"

#include "Externals/enkiTS/TaskScheduler.h"

#include <atomic>

namespace enki
{
class TaskScheduler;
}

namespace Graphics
{

struct Allocator;
struct AsynchronousLoader;
struct FrameGraph;
struct GpuVisualProfiler;
struct ImGuiService;
struct Renderer;
struct RenderScene;
struct SceneGraph;
struct StackAllocator;

static const uint16_t kInvalidSceneTextureIndex = UINT16_MAX;
static const uint32_t kMaterialDescriptorSetIndex = 1;
static const uint32_t kMaxJointCount = 12;

static bool g_RecreatePerThreadDescriptors = false;
static bool g_UseSecondaryCommandBuffers = false;

//
//
enum DrawFlags
{
  DrawFlagsAlphaMask = 1 << 0,
  DrawFlagsDoubleSided = 1 << 1,
  DrawFlagsTransparent = 1 << 2,
  DrawFlagsPhong = 1 << 3,
  DrawFlagsHasNormals = 1 << 4,
  DrawFlagsHasTexCoords = 1 << 5,
  DrawFlagsHasTangents = 1 << 6,
  DrawFlagsHasJoints = 1 << 7,
  DrawFlagsHasWeights = 1 << 8,
  DrawFlagsAlphaDither = 1 << 9,
  DrawFlagsCloth = 1 << 10,
}; // enum DrawFlags

//
//
struct GpuSceneData
{
  mat4s viewProjection;
  mat4s inverseViewProjection;

  vec4s eye;
  vec4s lightPosition;
  float lightRange;
  float lightIntensity;
  uint32_t ditherTextureIndex;
  float padding00;
}; // struct GpuSceneData

struct glTFScene;
struct Material;

//
//
struct PBRMaterial
{

  Material* material = nullptr;

  Graphics::BufferHandle materialBuffer = kInvalidBuffer;
  Graphics::DescriptorSetHandle descriptorSet = kInvalidSet;

  // Indices used for bindless textures.
  uint16_t diffuseTextureIndex = UINT16_MAX;
  uint16_t roughnessTextureIndex = UINT16_MAX;
  uint16_t normalTextureIndex = UINT16_MAX;
  uint16_t occlusionTextureIndex = UINT16_MAX;
  uint16_t emissiveTextureIndex = UINT16_MAX;

  // PBR
  vec4s baseColorFactor = {1.f, 1.f, 1.f, 1.f};
  vec3s emissiveFactor = {0.f, 0.f, 0.f};
  vec4s metallicRoughnessOcclusionFactor = {1.f, 1.f, 1.f, 1.f};
  float alphaCutoff = 1.f;

  // Phong
  vec4s diffuseColour = {1.f, 1.f, 1.f, 1.f};
  vec3s specularColour = {1.f, 1.f, 1.f};
  float specularExp = 1.f;
  vec3s ambientColour = {0.f, 0.f, 0.f};

  uint32_t flags = 0;
  ;
}; // struct PBRMaterial

//
//
struct PhysicsJoint
{
  int vertexIndex = -1;

  // TODO: for now this is only for cloth
  float stifness;
};

//
//
struct PhysicsVertex
{
  void addJoint(uint32_t vertexIndex);

  vec3s startPosition;
  vec3s previousPosition;
  vec3s position;
  vec3s normal;

  vec3s velocity;
  vec3s force;

  PhysicsJoint joints[kMaxJointCount];
  uint32_t jointCount;

  float mass;
  bool fixed;
};

//
//
struct PhysicsVertexGpuData
{
  vec3s position;
  float pad0_;

  vec3s startPosition;
  float pad1_;

  vec3s previousPosition;
  float pad2_;

  vec3s normal;
  uint32_t jointCount;

  vec3s velocity;
  float mass;

  vec3s force;

  // TODO: better storage, values are never greater than 12
  uint32_t joints[kMaxJointCount];
  uint32_t pad3_;
};

//
//
struct PhysicsMeshGpuData
{
  uint32_t indexCount;
  uint32_t vertexCount;

  uint32_t padding_[2];
};

//
//
struct PhysicsSceneData
{
  vec3s windDirection;
  uint32_t resetSimulation;

  float airDensity;
  float springStiffness;
  float springDamping;
  float padding_;
};

//
//
struct PhysicsMesh
{
  uint32_t meshIndex;

  Array<PhysicsVertex> vertices;

  Graphics::BufferHandle gpuBuffer;
  Graphics::BufferHandle drawIndirectBuffer;
  Graphics::DescriptorSetHandle descriptorSet;
  Graphics::DescriptorSetHandle debugMeshDescriptorSet;
};

//
//
struct Mesh
{

  PBRMaterial pbrMaterial;

  PhysicsMesh* physicsMesh;

  // Vertex data
  Graphics::BufferHandle positionBuffer;
  Graphics::BufferHandle tangentBuffer;
  Graphics::BufferHandle normalBuffer;
  Graphics::BufferHandle texcoordBuffer;
  // TODO: separate
  Graphics::BufferHandle jointsBuffer;
  Graphics::BufferHandle weightsBuffer;

  uint32_t positionOffset;
  uint32_t tangentOffset;
  uint32_t normalOffset;
  uint32_t texcoordOffset;
  uint32_t jointsOffset;
  uint32_t weightsOffset;

  // Index data
  Graphics::BufferHandle indexBuffer;
  VkIndexType indexType;
  uint32_t indexOffset;

  uint32_t primitiveCount;
  uint32_t sceneGraphNodeIndex = UINT32_MAX;
  int skinIndex = INT_MAX;

  bool hasSkinning() const { return skinIndex != INT_MAX; }
  bool isTransparent() const
  {
    return (pbrMaterial.flags & (DrawFlagsAlphaMask | DrawFlagsTransparent)) != 0;
  }
  bool isDoubleSided() const
  {
    return (pbrMaterial.flags & DrawFlagsDoubleSided) == DrawFlagsDoubleSided;
  }
  bool isCloth() const { return (pbrMaterial.flags & DrawFlagsCloth) == DrawFlagsCloth; }
}; // struct Mesh

//
//
struct MeshInstance
{

  Mesh* mesh;
  uint32_t materialPassIndex;

}; // struct MeshInstance

//
//
struct GpuMeshData
{
  mat4s world;
  mat4s inverseWorld;

  uint32_t textures[4]; // diffuse, roughness, normal, occlusion
  // PBR
  vec4s emissive; // emissiveColorFactor + emissive texture index
  vec4s baseColorFactor;
  vec4s metallicRoughnessOcclusionFactor; // metallic, roughness, occlusion

  uint32_t flags;
  float alphaCutoff;
  float padding_[2];

  // Phong
  vec4s diffuseColour;

  vec3s specularColour;
  float specularExp;

  vec3s ambientColour;
  float padding2_;

}; // struct GpuMeshData

// Animation structs //////////////////////////////////////////////////
//
//
struct AnimationChannel
{

  enum TargetType
  {
    Translation,
    Rotation,
    Scale,
    Weights,
    Count
  };

  int sampler;
  int targetNode;
  TargetType targetType;

}; // struct AnimationChannel

struct AnimationSampler
{

  enum Interpolation
  {
    Linear,
    Step,
    CubicSpline,
    Count
  };

  Array<float> keyFrames;
  vec4s* data; // Aligned-allocated data. Count is the same as keyFrames.
  Interpolation interpolationType;

}; // struct AnimationSampler

//
//
struct Animation
{

  float timeStart;
  float timeEnd;

  Array<AnimationChannel> channels;
  Array<AnimationSampler> samplers;

}; // struct Animation

//
//
struct AnimationInstance
{
  Animation* animation;
  float currentTime;
}; // struct AnimationInstance

// Skinning ///////////////////////////////////////////////////////////
//
//
struct Skin
{

  uint32_t skeletonRootIndex;
  Array<int> joints;
  mat4s* inverseBindMatrices; // Align-allocated data. Count is same as joints.

  Graphics::BufferHandle jointTransforms;

}; // struct Skin

// Transform //////////////////////////////////////////////////////////

//
struct Transform
{

  vec3s scale;
  versors rotation;
  vec3s translation;

  void reset();
  mat4s calculateMatrix() const;

}; // struct Transform

// Light //////////////////////////////////////////////////////////////

//
struct Light
{

  Color color;
  float intensity;

  vec3s position;
  float radius;

}; // struct Light

// Render Passes //////////////////////////////////////////////////////

//
//
struct DepthPrePass : public Graphics::FrameGraphRenderPass
{
  void render(Graphics::CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      RenderScene& scene,
      FrameGraph* frameGraph,
      Allocator* residentAllocator,
      StackAllocator* scratchAllocator);
  void freeGpuResources();

  Array<MeshInstance> meshInstances;
  Renderer* renderer;
}; // struct DepthPrePass

//
//
struct GBufferPass : public Graphics::FrameGraphRenderPass
{
  void render(Graphics::CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      RenderScene& scene,
      FrameGraph* frameGraph,
      Allocator* residentAllocator,
      StackAllocator* scratchAllocator);
  void freeGpuResources();

  Array<MeshInstance> meshInstances;
  Renderer* renderer;
}; // struct GBufferPass

//
//
struct LighPass : public Graphics::FrameGraphRenderPass
{
  void render(Graphics::CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      RenderScene& scene,
      FrameGraph* frameGraph,
      Allocator* residentAllocator,
      StackAllocator* scratchAllocator);
  void uploadGpuData();
  void freeGpuResources();

  Mesh mesh;
  Renderer* renderer;
  bool useCompute;

  Graphics::FrameGraphResource* colorTexture;
  Graphics::FrameGraphResource* normalTexture;
  Graphics::FrameGraphResource* roughnessTexture;
  Graphics::FrameGraphResource* depthTexture;
  Graphics::FrameGraphResource* emissiveTexture;

  Graphics::FrameGraphResource* outputTexture;
}; // struct LighPass

//
//
struct TransparentPass : public Graphics::FrameGraphRenderPass
{
  void render(Graphics::CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      RenderScene& scene,
      FrameGraph* frameGraph,
      Allocator* residentAllocator,
      StackAllocator* scratchAllocator);
  void freeGpuResources();

  Array<MeshInstance> meshInstances;
  Renderer* renderer;
}; // struct TransparentPass

//
//
struct DebugPass : public Graphics::FrameGraphRenderPass
{
  void render(Graphics::CommandBuffer* gpuCommands, RenderScene* renderScene) override;

  void prepareDraws(
      RenderScene& scene,
      FrameGraph* frameGraph,
      Allocator* residentAllocator,
      StackAllocator* scratchAllocator);
  void freeGpuResources();

  Graphics::RendererUtil::BufferResource* sphere_meshBuffer;
  Graphics::RendererUtil::BufferResource* sphere_mesh_indices;
  Graphics::RendererUtil::BufferResource* sphereMatrices;
  Graphics::RendererUtil::BufferResource* lineBuffer;

  uint32_t sphereIndexCount;

  Graphics::DescriptorSetHandle mesh_descriptorSet;
  Graphics::DescriptorSetHandle line_descriptorSet;

  Material* debugMaterial;

  Array<MeshInstance> meshInstances;
  SceneGraph* sceneGraph;
  Renderer* renderer;
}; // struct DebugPass

//
//
struct DoFPass : public Graphics::FrameGraphRenderPass
{

  struct DoFData
  {
    uint32_t textures[4]; // diffuse, depth
    float znear;
    float zfar;
    float focalLength;
    float planeInFocus;
    float aperture;
  }; // struct DoFData

  void addUi() override;
  void preRender(
      uint32_t currentFrameIndex,
      Graphics::CommandBuffer* gpuCommands,
      FrameGraph* frameGraph) override;
  void render(Graphics::CommandBuffer* gpuCommands, RenderScene* renderScene) override;
  void onResize(Graphics::GpuDevice& gpu, uint32_t new_width, uint32_t new_height) override;

  void prepareDraws(
      RenderScene& scene,
      FrameGraph* frameGraph,
      Allocator* residentAllocator,
      StackAllocator* scratchAllocator);
  void uploadGpuData();
  void freeGpuResources();

  Mesh mesh;
  Renderer* renderer;

  Graphics::RendererUtil::TextureResource* sceneMips[Graphics::kMaxFrames];
  Graphics::FrameGraphResource* depthTexture;

  float znear;
  float zfar;
  float focalLength;
  float planeInFocus;
  float aperture;
}; // struct DoFPass

//
//
struct RenderScene
{
  virtual ~RenderScene(){};

  virtual void init(
      cstring filename,
      cstring path,
      Allocator* residentAllocator,
      StackAllocator* tempAllocator,
      AsynchronousLoader* asyncLoader){};
  virtual void shutdown(Renderer* renderer){};

  virtual void
  prepareDraws(Renderer* renderer, StackAllocator* scratchAllocator, SceneGraph* sceneGraph){};

  Graphics::CommandBuffer* updatePhysics(
      float deltaTime,
      float airDensity,
      float springStiffness,
      float springDamping,
      vec3s windDirection,
      bool resetSimulation);
  void updateAnimations(float deltaTime);
  void updateJoints();

  void uploadGpuData();
  void drawMesh(Graphics::CommandBuffer* gpuCommands, Mesh& mesh);

  Array<Mesh> meshes;
  Array<Animation> animations;
  Array<Skin> skins;

  StringBuffer namesBuffer; // Buffer containing all names of nodes, resources, etc.

  SceneGraph* sceneGraph;
  Graphics::BufferHandle sceneCb;
  Graphics::BufferHandle physicsCb = kInvalidBuffer;

  Allocator* residentAllocator;
  Renderer* renderer;

  float globalScale = 1.f;
}; // struct RenderScene

//
//
struct FrameRenderer
{

  void init(
      Allocator* residentAllocator,
      Renderer* renderer,
      FrameGraph* frameGraph,
      SceneGraph* sceneGraph,
      RenderScene* scene);
  void shutdown();

  void uploadGpuData();
  void render(Graphics::CommandBuffer* gpuCommands, RenderScene* renderScene);

  void prepareDraws(StackAllocator* scratchAllocator);

  Allocator* residentAllocator;
  SceneGraph* sceneGraph;

  Renderer* renderer;
  FrameGraph* frameGraph;

  RenderScene* scene;

  // Render passes
  DepthPrePass depthPrePass;
  GBufferPass gbufferPass;
  LighPass lightPass;
  TransparentPass transparentPass;
  DoFPass dofPass;
  DebugPass debugPass;

  // Fullscreen data
  Graphics::RendererUtil::GpuTechnique* fullscreenTech = nullptr;
  Graphics::DescriptorSetHandle fullscreenDS;

}; // struct FrameRenderer

// DrawTask ///////////////////////////////////////////////////////////

//
//
struct DrawTask : public enki::ITaskSet
{

  Graphics::GpuDevice* gpu = nullptr;
  FrameGraph* frameGraph = nullptr;
  Renderer* renderer = nullptr;
  ImGuiService* imgui = nullptr;
  RenderScene* scene = nullptr;
  FrameRenderer* frameRenderer = nullptr;
  uint32_t thread_id = 0;
  // NOTE: gpu state might change between init and execute!
  uint32_t currentFrameIndex = 0;
  Graphics::FramebufferHandle currentFramebuffer = {kInvalidIndex};

  void init(
      Graphics::GpuDevice* p_Gpu,
      FrameGraph* p_FrameGraph,
      Renderer* p_Renderer,
      ImGuiService* p_Imgui,
      RenderScene* p_Scene,
      FrameRenderer* p_FrameRenderer);

  void ExecuteRange(enki::TaskSetPartition p_Range, uint32_t p_Threadnum) override;

}; // struct glTFDrawTask

} // namespace Graphics
