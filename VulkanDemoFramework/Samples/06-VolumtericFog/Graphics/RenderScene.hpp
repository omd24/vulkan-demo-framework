#pragma once
#include "Foundation/Array.hpp"
#include "Foundation/Prerequisites.hpp"
#include "Foundation/Color.hpp"

#include "Graphics/CommandBuffer.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/GpuResources.hpp"
#include "Graphics/FrameGraph.hpp"
#include "Graphics/ImguiHelper.hpp"

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
struct ImguiService;
struct Renderer;
struct RenderScene;
struct SceneGraph;
struct StackAllocator;
struct GameCamera;

static const uint16_t kInvalidSceneTextureIndex = UINT16_MAX;
static const uint32_t kMaterialDescriptorSetIndex = 1;
static const uint32_t kMaxJointCount = 12;
static const uint32_t k_max_depth_pyramid_levels = 16;

static const uint32_t k_num_lights = 256;
static const uint32_t k_light_z_bins = 16;
static const uint32_t k_tile_size = 8;
static const uint32_t k_num_words = (k_num_lights + 31) / 32;

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
struct alignas(16) GpuSceneData
{
  mat4s view_projection;
  mat4s view_projection_debug;
  mat4s inverse_view_projection;
  mat4s world_to_camera; // view matrix
  mat4s world_to_camera_debug;
  mat4s previous_view_projection;
  mat4s inverse_projection;
  mat4s inverse_view;

  vec4s camera_position;
  vec4s camera_position_debug;
  vec3s camera_direction;
  int current_frame;

  uint32_t active_lights;
  uint32_t use_tetrahedron_shadows;
  uint32_t dither_texture_index;
  float z_near;

  float z_far;
  float projection_00;
  float projection_11;
  uint32_t culling_options;

  float resolution_x;
  float resolution_y;
  float aspect_ratio;
  uint32_t num_mesh_instances;

  float halton_x;
  float halton_y;
  uint32_t depth_texture_index;
  uint32_t blue_noise_128_rg_texture_index;

  vec2s jitter_xy;
  vec2s previous_jitter_xy;

  float forced_metalness;
  float forced_roughness;
  float volumetric_fog_application_dithering_scale;
  uint32_t volumetric_fog_application_options;

  vec4s frustum_planes[6];

  // Helpers for bit packing. Would be perfect for code generation
  // NOTE: must be in sync with scene.h!
  bool frustum_cull_meshes() const { return (culling_options & 1) == 1; }
  bool frustum_cull_meshlets() const { return (culling_options & 2) == 2; }
  bool occlusion_cull_meshes() const { return (culling_options & 4) == 4; }
  bool occlusion_cull_meshlets() const { return (culling_options & 8) == 8; }
  bool freeze_occlusion_camera() const { return (culling_options & 16) == 16; }
  bool shadow_meshlets_cone_cull() const { return (culling_options & 32) == 32; }
  bool shadow_meshlets_sphere_cull() const { return (culling_options & 64) == 64; }
  bool shadow_meshlets_cubemap_face_cull() const { return (culling_options & 128) == 128; }
  bool shadow_mesh_sphere_cull() const { return (culling_options & 256) == 256; }

  void set_frustum_cull_meshes(bool value)
  {
    value ? (culling_options |= 1) : (culling_options &= ~(1));
  }
  void set_frustum_cull_meshlets(bool value)
  {
    value ? (culling_options |= 2) : (culling_options &= ~(2));
  }
  void set_occlusion_cull_meshes(bool value)
  {
    value ? (culling_options |= 4) : (culling_options &= ~(4));
  }
  void set_occlusion_cull_meshlets(bool value)
  {
    value ? (culling_options |= 8) : (culling_options &= ~(8));
  }
  void set_freeze_occlusion_camera(bool value)
  {
    value ? (culling_options |= 16) : (culling_options &= ~(16));
  }
  void set_shadow_meshlets_cone_cull(bool value)
  {
    value ? (culling_options |= 32) : (culling_options &= ~(32));
  }
  void set_shadow_meshlets_sphere_cull(bool value)
  {
    value ? (culling_options |= 64) : (culling_options &= ~(64));
  }
  void set_shadow_meshlets_cubemap_face_cull(bool value)
  {
    value ? (culling_options |= 128) : (culling_options &= ~(128));
  }
  void set_shadow_mesh_sphere_cull(bool value)
  {
    value ? (culling_options |= 256) : (culling_options &= ~(256));
  }

}; // struct GpuSceneData

struct glTFScene;
struct Material;

//
//
struct PBRMaterial
{

  RendererUtil::Material* material = nullptr;

  Graphics::BufferHandle materialBuffer = kInvalidBuffer;
  Graphics::DescriptorSetHandle descriptor_set_transparent = kInvalidSet;
  Graphics::DescriptorSetHandle descriptor_set_main = kInvalidSet;

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

  uint32_t meshlet_offset;
  uint32_t meshlet_count;
  uint32_t meshlet_index_count;

  uint32_t gpu_mesh_index = UINT32_MAX;

  int skinIndex = INT_MAX;

  vec4s bounding_sphere;

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
  uint32_t gpu_mesh_instance_index = UINT32_MAX;
  uint32_t scene_graph_node_index = UINT32_MAX;

}; // struct MeshInstance

//
//
struct MeshInstanceDraw
{
  MeshInstance* mesh_instance;
  uint32_t material_pass_index = UINT32_MAX;
};

//
//
struct alignas(16) GpuMeshlet
{

  vec3s center;
  float radius;

  int8_t cone_axis[3];
  int8_t cone_cutoff;

  uint32_t data_offset;
  uint32_t mesh_index;
  uint8_t vertex_count;
  uint8_t triangle_count;
}; // struct GpuMeshlet

//
//
struct MeshletToMeshIndex
{
  uint32_t mesh_index;
  uint32_t primitive_index;
}; // struct MeshletToMeshIndex

//
//
struct GpuMeshletVertexPosition
{

  float position[3];
  float padding;
}; // struct GpuMeshletVertexPosition

//
//
struct GpuMeshletVertexData
{

  uint8_t normal[4];
  uint8_t tangent[4];
  uint16_t uv_coords[2];
  float padding;
}; // struct GpuMeshletVertexData

//
//
struct alignas(16) GpuMaterialData
{

  uint32_t textures[4]; // diffuse, roughness, normal, occlusion
  // PBR
  vec4s emissive; // emissive_color_factor + emissive texture index
  vec4s base_color_factor;
  vec4s metallic_roughness_occlusion_factor; // metallic, roughness, occlusion

  uint32_t flags;
  float alpha_cutoff;
  uint32_t vertex_offset;
  uint32_t mesh_index;

  uint32_t meshlet_offset;
  uint32_t meshlet_count;
  uint32_t meshlet_index_count;
  uint32_t padding1_;

}; // struct GpuMaterialData

//
//
struct alignas(16) GpuMeshInstanceData
{
  mat4s world;
  mat4s inverse_world;

  uint32_t mesh_index;
  uint32_t pad000;
  uint32_t pad001;
  uint32_t pad002;
}; // struct GpuMeshInstanceData

//
//
struct alignas(16) GpuMeshDrawCommand
{
  uint32_t drawId;
  VkDrawIndexedIndirectCommand indirect;       // 5 uint32_t
  VkDrawMeshTasksIndirectCommandNV indirectMS; // 2 uint32_t
};                                             // struct GpuMeshDrawCommand

//
//
struct alignas(16) GpuMeshDrawCounts
{
  uint32_t opaque_mesh_visible_count;
  uint32_t opaque_mesh_culled_count;
  uint32_t transparent_mesh_visible_count;
  uint32_t transparent_mesh_culled_count;

  uint32_t total_count;
  uint32_t depth_pyramid_texture_index;
  uint32_t late_flag;
  uint32_t meshlet_index_count;

  uint32_t dispatch_task_x;
  uint32_t dispatch_task_y;
  uint32_t dispatch_task_z;
  uint32_t pad001;

}; // struct GpuMeshDrawCounts

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
  int target_node;
  TargetType target_type;

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

  Array<float> key_frames;
  vec4s* data; // Aligned-allocated data. Count is the same as key_frames.
  Interpolation interpolation_type;

}; // struct AnimationSampler

//
//
struct Animation
{

  float time_start;
  float time_end;

  Array<AnimationChannel> channels;
  Array<AnimationSampler> samplers;

}; // struct Animation

//
//
struct AnimationInstance
{
  Animation* animation;
  float current_time;
}; // struct AnimationInstance

// Skinning ///////////////////////////////////////////////////////////
//
//
struct Skin
{

  uint32_t skeleton_root_index;
  Array<int> joints;
  mat4s* inverse_bind_matrices; // Align-allocated data. Count is same as joints.

  BufferHandle joint_transforms;

}; // struct Skin

// Transform //////////////////////////////////////////////////////////

//
struct Transform
{

  vec3s scale;
  versors rotation;
  vec3s translation;

  void reset();
  mat4s calculate_matrix() const;

}; // struct Transform

// Light //////////////////////////////////////////////////////////////

//
struct alignas(16) Light
{

  vec3s world_position;
  float radius;

  vec3s color;
  float intensity;

  vec4s aabb_min;
  vec4s aabb_max;

  float shadow_map_resolution;
  uint32_t tile_x;
  uint32_t tile_y;
  float solid_angle;

}; // struct Light

// Separated from Light struct as it could contain unpacked data.
struct alignas(16) GpuLight
{

  vec3s world_position;
  float radius;

  vec3s color;
  float intensity;

  float shadow_map_resolution;
  float rcp_n_minus_f; // Calculation of 1 / (n - f) used to retrieve cubemap shadows depth value.
  float pad1;
  float pad2;

}; // struct GpuLight

struct UploadGpuDataContext
{
  Framework::GameCamera& game_camera;
  Framework::StackAllocator* scratch_allocator;

  vec2s last_clicked_position_left_button;

  uint8_t skip_invisible_lights : 1;
  uint8_t use_mcguire_method : 1;
  uint8_t use_view_aabb : 1;
  uint8_t enable_camera_inside : 1;
  uint8_t force_fullscreen_light_aabb : 1;
  uint8_t pad000 : 3;

}; // struct UploadGpuDataContext

// Volumetric Fog /////////////////////////////////////////////////////
struct alignas(16) GpuVolumetricFogConstants
{

  mat4s froxel_inverse_view_projection;

  float froxel_near;
  float froxel_far;
  float scattering_factor;
  float density_modifier;

  uint32_t light_scattering_texture_index;
  uint32_t integrated_light_scattering_texture_index;
  uint32_t froxel_data_texture_index;
  uint32_t previous_light_scattering_texture_index;

  uint32_t use_temporal_reprojection;
  float time_random_01;
  float temporal_reprojection_percentage;
  float phase_anisotropy_01;

  uint32_t froxel_dimension_x;
  uint32_t froxel_dimension_y;
  uint32_t froxel_dimension_z;
  uint32_t phase_function_type;

  float height_fog_density;
  float height_fog_falloff;
  float pad1;
  float noise_scale;

  float lighting_noise_scale;
  uint32_t noise_type;
  uint32_t pad0;
  uint32_t use_spatial_filtering;

  uint32_t volumetric_noise_texture_index;
  float volumetric_noise_position_multiplier;
  float volumetric_noise_speed_multiplier;
  float temporal_reprojection_jitter_scale;

  vec3s box_position;
  float box_fog_density;

  vec3s box_half_size;
  uint32_t box_color;

}; // struct GpuVolumetricFogConstants

//
struct alignas(16) GpuTaaConstants
{

  uint32_t history_color_texture_index;
  uint32_t taa_output_texture_index;
  uint32_t velocity_texture_index;
  uint32_t current_color_texture_index;

  uint32_t taa_modes;
  uint32_t options;
  uint32_t pad0;
  uint32_t pad1;

  uint32_t velocity_sampling_mode;
  uint32_t history_sampling_filter;
  uint32_t history_constraint_mode;
  uint32_t current_color_filter;

}; // struct GpuTaaConstants

// Render Passes //////////////////////////////////////////////////////

//
//
struct DepthPrePass : public FrameGraphRenderPass
{
  void render(
      uint32_t current_frame_index,
      Graphics::CommandBuffer* gpu_commands,
      Graphics::RenderScene* render_scene) override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  Array<MeshInstanceDraw> mesh_instance_draws;
  Renderer* renderer;
  uint32_t meshlet_technique_index;
}; // struct DepthPrePass

//
//
struct DepthPyramidPass : public FrameGraphRenderPass
{
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;
  void on_resize(
      GpuDevice& gpu, FrameGraph* frame_graph, uint32_t new_width, uint32_t new_height) override;
  void post_render(
      uint32_t current_frame_index,
      CommandBuffer* gpu_commands,
      FrameGraph* frame_graph,
      RenderScene* render_scene) override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  void create_depth_pyramid_resource(Texture* depth_texture);

  Renderer* renderer;

  PipelineHandle depth_pyramid_pipeline;
  TextureHandle depth_pyramid;
  SamplerHandle depth_pyramid_sampler;
  TextureHandle depth_pyramid_views[k_max_depth_pyramid_levels];
  DescriptorSetHandle depth_hierarchy_descriptor_set[k_max_depth_pyramid_levels];

  uint32_t depth_pyramid_levels = 0;

  bool update_depth_pyramid;
}; // struct DepthPrePass

//
//
struct GBufferPass : public FrameGraphRenderPass
{
  void pre_render(
      uint32_t current_frame_index,
      CommandBuffer* gpu_commands,
      FrameGraph* frame_graph,
      RenderScene* render_scene) override;
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  Array<MeshInstanceDraw> mesh_instance_draws;
  Renderer* renderer;

  PipelineHandle meshlet_draw_pipeline;
  PipelineHandle meshlet_emulation_draw_pipeline;

  BufferHandle generate_meshlet_dispatch_indirect_buffer[k_max_frames];
  PipelineHandle generate_meshlet_index_buffer_pipeline;
  DescriptorSetHandle generate_meshlet_index_buffer_descriptor_set[k_max_frames];
  PipelineHandle generate_meshlets_instances_pipeline;
  DescriptorSetHandle generate_meshlets_instances_descriptor_set[k_max_frames];
  BufferHandle meshlet_instance_culling_indirect_buffer[k_max_frames];
  PipelineHandle meshlet_instance_culling_pipeline;
  DescriptorSetHandle meshlet_instance_culling_descriptor_set[k_max_frames];
  PipelineHandle meshlet_write_counts_pipeline;

}; // struct GBufferPass

//
//
struct LateGBufferPass : public FrameGraphRenderPass
{
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  Array<MeshInstanceDraw> mesh_instance_draws;
  Renderer* renderer;
  uint32_t meshlet_technique_index;
}; // struct LateGBufferPass

//
//
struct LightPass : public FrameGraphRenderPass
{
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;
  void on_resize(
      GpuDevice& gpu, FrameGraph* frame_graph, uint32_t new_width, uint32_t new_height) override;
  void post_render(
      uint32_t current_frame_index,
      CommandBuffer* gpu_commands,
      FrameGraph* frame_graph,
      RenderScene* render_scene) override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void upload_gpu_data(RenderScene& scene) override;
  void free_gpu_resources(GpuDevice& gpu) override;
  void update_dependent_resources(
      GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene) override;

  Mesh mesh;
  Renderer* renderer;
  bool use_compute;

  DescriptorSetHandle lighting_descriptor_set[k_max_frames];
  TextureHandle lighting_debug_texture;

  DescriptorSetHandle fragment_rate_descriptor_set[k_max_frames];
  BufferHandle fragment_rate_texture_index[k_max_frames];

  FrameGraphResource* color_texture;
  FrameGraphResource* normal_texture;
  FrameGraphResource* roughness_texture;
  FrameGraphResource* depth_texture;
  FrameGraphResource* emissive_texture;

  FrameGraphResource* output_texture;
}; // struct LightPass

//
//
struct TransparentPass : public FrameGraphRenderPass
{
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  Array<MeshInstanceDraw> mesh_instance_draws;
  Renderer* renderer;
  uint32_t meshlet_technique_index;
}; // struct TransparentPass

//
//
struct PointlightShadowPass : public FrameGraphRenderPass
{
  void pre_render(
      uint32_t current_frame_index,
      CommandBuffer* gpu_commands,
      FrameGraph* frame_graph,
      RenderScene* render_scene) override;
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator);
  void upload_gpu_data(RenderScene& scene) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  void recreate_lightcount_dependent_resources(RenderScene& scene);
  void update_dependent_resources(
      GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene) override;

  Array<MeshInstanceDraw> mesh_instance_draws;
  Renderer* renderer;

  uint32_t last_active_lights = 0;

  BufferHandle pointlight_view_projections_cb[k_max_frames];
  BufferHandle pointlight_spheres_cb[k_max_frames];
  // Manual pass generation, add support in framegraph for special cases like this?
  RenderPassHandle cubemap_render_pass;
  FramebufferHandle cubemap_framebuffer;
  // Cubemap rendering
  TextureHandle cubemap_shadow_array_texture;
  DescriptorSetHandle cubemap_meshlet_draw_descriptor_set[k_max_frames];
  PipelineHandle cubemap_meshlets_pipeline;
  // Tetrahedron rendering
  TextureHandle tetrahedron_shadow_texture;
  PipelineHandle tetrahedron_meshlet_pipeline;
  FramebufferHandle tetrahedron_framebuffer;

  // Culling pass
  PipelineHandle meshlet_culling_pipeline;
  DescriptorSetHandle meshlet_culling_descriptor_set[k_max_frames];
  BufferHandle meshlet_visible_instances[k_max_frames];
  BufferHandle per_light_meshlet_instances[k_max_frames];

  // Write command pass
  PipelineHandle meshlet_write_commands_pipeline;
  DescriptorSetHandle meshlet_write_commands_descriptor_set[k_max_frames];
  BufferHandle meshlet_shadow_indirect_cb[k_max_frames];

  // Shadow resolution pass
  PipelineHandle shadow_resolution_pipeline;
  DescriptorSetHandle shadow_resolution_descriptor_set[k_max_frames];
  BufferHandle light_aabbs;
  BufferHandle shadow_resolutions[k_max_frames];
  BufferHandle shadow_resolutions_readback[k_max_frames];

  PagePoolHandle shadow_maps_pool = k_invalid_page_pool;

  TextureHandle cubemap_debug_face_texture;

}; // struct PointlightShadowPass

//
//
struct VolumetricFogPass : public FrameGraphRenderPass
{
  void pre_render(
      uint32_t current_frame_index,
      CommandBuffer* gpu_commands,
      FrameGraph* frame_graph,
      RenderScene* render_scene) override;
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;

  void on_resize(
      GpuDevice& gpu, FrameGraph* frame_graph, uint32_t new_width, uint32_t new_height) override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void upload_gpu_data(RenderScene& scene) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  void update_dependent_resources(
      GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene) override;

  // Inject Data
  PipelineHandle inject_data_pipeline;
  TextureHandle froxel_data_texture_0;

  // Light Scattering
  PipelineHandle light_scattering_pipeline;
  TextureHandle light_scattering_texture[2]; // Temporal reprojection between 2 textures
  DescriptorSetHandle light_scattering_descriptor_set[k_max_frames];
  uint32_t current_light_scattering_texture_index = 1;
  uint32_t previous_light_scattering_texture_index = 0;

  // Light Integration
  PipelineHandle light_integration_pipeline;
  TextureHandle integrated_light_scattering_texture;

  // Spatial Filtering
  PipelineHandle spatial_filtering_pipeline;
  // Temporal Filtering
  PipelineHandle temporal_filtering_pipeline;
  // Volumetric Noise baking
  PipelineHandle volumetric_noise_baking;
  TextureHandle volumetric_noise_texture;
  SamplerHandle volumetric_tiling_sampler;
  bool has_baked_noise = false;

  DescriptorSetHandle fog_descriptor_set;
  BufferHandle fog_constants;

  Renderer* renderer;

}; // struct VolumetricFogPass

//
//
struct TemporalAntiAliasingPass : public FrameGraphRenderPass
{

  void pre_render(
      uint32_t current_frame_index,
      CommandBuffer* gpu_commands,
      FrameGraph* frame_graph,
      RenderScene* render_scene) override;
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;

  void on_resize(
      GpuDevice& gpu, FrameGraph* frame_graph, uint32_t new_width, uint32_t new_height) override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void upload_gpu_data(RenderScene& scene) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  void update_dependent_resources(
      GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene) override;

  PipelineHandle taa_pipeline;
  TextureHandle history_textures[2];
  DescriptorSetHandle taa_descriptor_set;
  BufferHandle taa_constants;

  uint32_t current_history_texture_index = 1;
  uint32_t previous_history_texture_index = 0;

  Renderer* renderer;

}; // struct TemporalAntiAliasingPass

//
//
struct MotionVectorPass : public FrameGraphRenderPass
{

  void pre_render(
      uint32_t current_frame_index,
      CommandBuffer* gpu_commands,
      FrameGraph* frame_graph,
      RenderScene* render_scene) override;
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;

  void on_resize(
      GpuDevice& gpu, FrameGraph* frame_graph, uint32_t new_width, uint32_t new_height) override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void upload_gpu_data(RenderScene& scene) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  void update_dependent_resources(
      GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene) override;

  PipelineHandle camera_composite_pipeline;
  DescriptorSetHandle camera_composite_descriptor_set;
  Renderer* renderer;

}; // struct MotionVectorPass

//
//
struct DebugPass : public FrameGraphRenderPass
{
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;
  void pre_render(
      uint32_t current_frame_index,
      CommandBuffer* gpu_commands,
      FrameGraph* frame_graph,
      RenderScene* render_scene) override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  Graphics::RendererUtil::BufferResource* sphere_mesh_buffer;
  Graphics::RendererUtil::BufferResource* sphere_mesh_indices;
  Graphics::RendererUtil::BufferResource* sphere_matrices_buffer;
  Graphics::RendererUtil::BufferResource* sphere_draw_indirect_buffer;
  uint32_t sphere_index_count;

  Graphics::RendererUtil::BufferResource* cone_mesh_buffer;
  Graphics::RendererUtil::BufferResource* cone_mesh_indices;
  Graphics::RendererUtil::BufferResource* cone_matrices_buffer;
  Graphics::RendererUtil::BufferResource* cone_draw_indirect_buffer;
  uint32_t cone_index_count;

  Graphics::RendererUtil::BufferResource* line_buffer;

  uint32_t bounding_sphere_count;

  DescriptorSetHandle sphere_mesh_descriptor_set;
  DescriptorSetHandle cone_mesh_descriptor_set;
  DescriptorSetHandle line_descriptor_set;

  PipelineHandle debug_lines_finalize_pipeline;
  DescriptorSetHandle debug_lines_finalize_set;

  PipelineHandle debug_lines_draw_pipeline;
  PipelineHandle debug_lines_2d_draw_pipeline;
  DescriptorSetHandle debug_lines_draw_set;

  BufferHandle debug_line_commands_sb_cache;

  Material* debug_material;

  SceneGraph* scene_graph;
  Renderer* renderer;
}; // struct DebugPass

//
//
struct DoFPass : public FrameGraphRenderPass
{

  struct DoFData
  {
    uint32_t textures[4]; // diffuse, depth
    float znear;
    float zfar;
    float focal_length;
    float plane_in_focus;
    float aperture;
  }; // struct DoFData

  void add_ui() override;
  void pre_render(
      uint32_t current_frame_index,
      CommandBuffer* gpu_commands,
      FrameGraph* frame_graph,
      RenderScene* render_scene) override;
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;
  void on_resize(
      GpuDevice& gpu, FrameGraph* frame_graph, uint32_t new_width, uint32_t new_height) override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void upload_gpu_data(RenderScene& scene) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  Mesh mesh;
  Renderer* renderer;

  TextureResource* scene_mips;
  FrameGraphResource* depth_texture;

  float znear;
  float zfar;
  float focal_length;
  float plane_in_focus;
  float aperture;
}; // struct DoFPass

//
//
struct CullingEarlyPass : public FrameGraphRenderPass
{
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  Renderer* renderer;

  PipelineHandle frustum_cull_pipeline;
  DescriptorSetHandle frustum_cull_descriptor_set[k_max_frames];
  SamplerHandle depth_pyramid_sampler;
  uint32_t depth_pyramid_texture_index;

}; // struct CullingPrePass

//
//
struct CullingLatePass : public FrameGraphRenderPass
{
  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  Renderer* renderer;

  PipelineHandle frustum_cull_pipeline;
  DescriptorSetHandle frustum_cull_descriptor_set[k_max_frames];
  SamplerHandle depth_pyramid_sampler;
  uint32_t depth_pyramid_texture_index;

}; // struct CullingLatePass

//
//
struct RayTracingTestPass : public FrameGraphRenderPass
{
  struct GpuData
  {
    uint32_t sbt_offset; // shader binding table offset
    uint32_t sbt_stride; // shader binding table stride
    uint32_t miss_index;
    uint32_t out_image_index;
  };

  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;
  void on_resize(
      GpuDevice& gpu, FrameGraph* frame_graph, uint32_t new_width, uint32_t new_height) override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void upload_gpu_data(RenderScene& scene) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  Renderer* renderer;

  PipelineHandle pipeline;
  DescriptorSetHandle descriptor_set[k_max_frames];
  TextureHandle render_target;
  bool owns_render_target;
  BufferHandle uniform_buffer[k_max_frames];

}; // struct RayTracingTestPass

//
//
struct ShadowVisbilityPass : public FrameGraphRenderPass
{
  struct GpuShadowVisibilityConstants
  {
    uint32_t visibility_cache_texture_index;
    uint32_t variation_texture_index;
    uint32_t variation_cache_texture_index;
    uint32_t samples_count_cache_texture_index;

    uint32_t motion_vectors_texture_index;
    uint32_t normals_texture_index;
    uint32_t filtered_visibility_texture;
    uint32_t filetered_variation_texture;

    uint32_t frame_index;
  };

  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
      override;
  void on_resize(
      GpuDevice& gpu, FrameGraph* frame_graph, uint32_t new_width, uint32_t new_height) override;

  void prepare_draws(
      RenderScene& scene,
      FrameGraph* frame_graph,
      Allocator* resident_allocator,
      StackAllocator* scratch_allocator) override;
  void upload_gpu_data(RenderScene& scene) override;
  void free_gpu_resources(GpuDevice& gpu) override;

  void recreate_textures(GpuDevice& gpu, uint32_t lights_count);

  Renderer* renderer;

  PipelineHandle variance_pipeline;
  PipelineHandle visibility_pipeline;
  PipelineHandle visibility_filtering_pipeline;
  DescriptorSetHandle descriptor_set[k_max_frames];

  TextureHandle variation_texture;
  TextureHandle variation_cache_texture;
  TextureHandle visibility_cache_texture;
  TextureHandle samples_count_cache_texture;

  TextureHandle filtered_visibility_texture;
  TextureHandle filtered_variation_texture;

  TextureHandle normals_texture;

  BufferHandle gpu_pass_constants;

  FrameGraphResource* shadow_visibility_resource;

  bool clear_resources;
  uint32_t last_active_lights_count = 0;

}; // struct RayTracingTestPass

//
//
struct DebugRenderer
{

  void init(RenderScene& scene, Allocator* resident_allocator, StackAllocator* scratch_allocator);
  void shutdown();

  void render(uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene);

  void line(const vec3s& from, const vec3s& to, Color color);
  void line_2d(const vec2s& from, const vec2s& to, Color color);
  void line(const vec3s& from, const vec3s& to, Color color0, Color color1);

  void aabb(const vec3s& min, const vec3s max, Color color);

  Renderer* renderer;

  // CPU rendering resources
  BufferHandle lines_vb;
  BufferHandle lines_vb_2d;

  uint32_t current_line;
  uint32_t current_line_2d;

  // Shared resources
  PipelineHandle debug_lines_draw_pipeline;
  PipelineHandle debug_lines_2d_draw_pipeline;
  DescriptorSetHandle debug_lines_draw_set;

}; // struct DebugRenderer

//
//
struct RenderScene
{
  virtual ~RenderScene(){};

  virtual void init(
      cstring filename,
      cstring path,
      Framework::Allocator* resident_allocator,
      Framework::StackAllocator* temp_allocator,
      AsynchronousLoader* async_loader){};
  virtual void shutdown(RendererUtil::Renderer* renderer){};

  void on_resize(GpuDevice& gpu, FrameGraph* frame_graph, uint32_t new_width, uint32_t new_height);

  virtual void
  prepare_draws(Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph){};

  CommandBuffer* update_physics(
      float delta_time,
      float air_density,
      float spring_stiffness,
      float spring_damping,
      vec3s wind_direction,
      bool reset_simulation);
  void update_animations(float delta_time);
  void update_joints();

  void upload_gpu_data(UploadGpuDataContext& context);
  void
  draw_mesh_instance(CommandBuffer* gpu_commands, MeshInstance& mesh_instance, bool transparent);

  // Helpers based on shaders. Ideally this would be coming from generated cpp files.
  void add_scene_descriptors(
      DescriptorSetCreation& descriptor_set_creation,
      Graphics::RendererUtil::GpuTechniquePass& pass);
  void add_mesh_descriptors(
      DescriptorSetCreation& descriptor_set_creation,
      Graphics::RendererUtil::GpuTechniquePass& pass);
  void add_meshlet_descriptors(
      DescriptorSetCreation& descriptor_set_creation,
      Graphics::RendererUtil::GpuTechniquePass& pass);
  void add_debug_descriptors(
      DescriptorSetCreation& descriptor_set_creation,
      Graphics::RendererUtil::GpuTechniquePass& pass);
  void add_lighting_descriptors(
      DescriptorSetCreation& descriptor_set_creation,
      Graphics::RendererUtil::GpuTechniquePass& pass,
      uint32_t frame_index);

  DebugRenderer debug_renderer;

  // Mesh and MeshInstances
  Array<Mesh> meshes;
  Array<MeshInstance> mesh_instances;
  Array<uint32_t> gltf_mesh_to_mesh_offset;

  // Meshlet data
  Array<GpuMeshlet> meshlets;
  Array<GpuMeshletVertexPosition> meshlets_vertex_positions;
  Array<GpuMeshletVertexData> meshlets_vertex_data;
  Array<uint32_t> meshlets_data;

  // Animation and skinning data
  Array<Animation> animations;
  Array<Skin> skins;

  // Lights
  Array<Light> lights;
  Array<uint32_t> lights_lut;
  vec3s mesh_aabb[2]; // 0 min, 1 max
  uint32_t active_lights = 1;
  bool shadow_constants_cpu_update = true;

  StringBuffer names_buffer; // Buffer containing all names of nodes, resources, etc.

  SceneGraph* scene_graph;

  GpuSceneData scene_data;

  // Gpu buffers
  BufferHandle scene_cb = k_invalid_buffer;
  BufferHandle meshes_sb = k_invalid_buffer;
  BufferHandle mesh_bounds_sb = k_invalid_buffer;
  BufferHandle mesh_instances_sb = k_invalid_buffer;
  BufferHandle physics_cb = k_invalid_buffer;
  BufferHandle meshlets_sb = k_invalid_buffer;
  BufferHandle meshlets_vertex_pos_sb = k_invalid_buffer;
  BufferHandle meshlets_vertex_data_sb = k_invalid_buffer;
  BufferHandle meshlets_data_sb = k_invalid_buffer;
  BufferHandle meshlets_instances_sb[k_max_frames];
  BufferHandle meshlets_index_buffer_sb[k_max_frames];
  BufferHandle meshlets_visible_instances_sb[k_max_frames];

  // Light buffers
  BufferHandle lights_list_sb = k_invalid_buffer;
  BufferHandle lights_lut_sb[k_max_frames];
  BufferHandle lights_tiles_sb[k_max_frames];
  BufferHandle lights_indices_sb[k_max_frames];
  BufferHandle lighting_constants_cb[k_max_frames];

  // Gpu debug draw
  BufferHandle debug_line_sb = k_invalid_buffer;
  BufferHandle debug_line_count_sb = k_invalid_buffer;
  BufferHandle debug_line_commands_sb = k_invalid_buffer;
  DescriptorSetHandle debug_line_finalize_set = k_invalid_set;
  DescriptorSetHandle debug_line_draw_set = k_invalid_set;

  // Indirect data
  BufferHandle mesh_task_indirect_count_early_sb[k_max_frames];
  BufferHandle mesh_task_indirect_early_commands_sb[k_max_frames];
  BufferHandle mesh_task_indirect_culled_commands_sb[k_max_frames];

  BufferHandle mesh_task_indirect_count_late_sb[k_max_frames];
  BufferHandle mesh_task_indirect_late_commands_sb[k_max_frames];

  BufferHandle meshlet_instances_indirect_count_sb[k_max_frames];

  TextureHandle fragment_shading_rate_image;
  TextureHandle motion_vector_texture;
  TextureHandle visibility_motion_vector_texture;

  Array<VkAccelerationStructureGeometryKHR> geometries;
  Array<VkAccelerationStructureBuildRangeInfoKHR> build_range_infos;

  VkAccelerationStructureKHR blas;
  BufferHandle blas_buffer;

  VkAccelerationStructureKHR tlas;
  BufferHandle tlas_buffer;

  GpuMeshDrawCounts mesh_draw_counts;

  DescriptorSetHandle meshlet_emulation_descriptor_set[k_max_frames];
  DescriptorSetHandle meshlet_visibility_descriptor_set[k_max_frames];
  DescriptorSetHandle mesh_shader_early_descriptor_set[k_max_frames];
  DescriptorSetHandle mesh_shader_late_descriptor_set[k_max_frames];
  DescriptorSetHandle mesh_shader_transparent_descriptor_set[k_max_frames];

  Allocator* resident_allocator;
  Renderer* renderer;

  uint32_t cubemap_shadows_index = 0;
  uint32_t lighting_debug_texture_index = 0;
  uint32_t cubemap_debug_array_index = 0;
  uint32_t cubemap_debug_face_index = 5;
  bool cubemap_face_debug_enabled = false;
  uint32_t blue_noise_128_rg_texture_index = 0;

  // PBR
  float forced_metalness = -1.f;
  float forced_roughness = -1.f;

  // Volumetric Fog controls
  uint32_t volumetric_fog_texture_index = 0;
  uint32_t volumetric_fog_tile_size = 16;
  uint32_t volumetric_fog_tile_count_x = 128;
  uint32_t volumetric_fog_tile_count_y = 128;
  uint32_t volumetric_fog_slices = 128;
  float volumetric_fog_density = 0.0f;
  float volumetric_fog_scattering_factor = 0.1f;
  float volumetric_fog_temporal_reprojection_percentage = 0.2f;
  float volumetric_fog_phase_anisotropy_01 = 0.2f;
  bool volumetric_fog_use_temporal_reprojection = true;
  bool volumetric_fog_use_spatial_filtering = true;
  uint32_t volumetric_fog_phase_function_type = 0;
  float volumetric_fog_height_fog_density = 0.0f;
  float volumetric_fog_height_fog_falloff = 1.0f;
  float volumetric_fog_noise_scale = 0.5f;
  float volumetric_fog_lighting_noise_scale = 0.11f;
  uint32_t volumetric_fog_noise_type = 0;
  float volumetric_fog_noise_position_scale = 1.0f;
  float volumetric_fog_noise_speed_scale = 0.2f;
  vec3s volumetric_fog_box_position = vec3s{0, 0, 0};
  vec3s volumetric_fog_box_size = vec3s{1.f, 2.f, 0.5f};
  float volumetric_fog_box_density = 3.0f;
  uint32_t volumetric_fog_box_color = Framework::Color::green;
  float volumetric_fog_temporal_reprojection_jittering_scale = 0.2f;
  float volumetric_fog_application_dithering_scale = 0.005f;
  bool volumetric_fog_application_apply_opacity_anti_aliasing = false;
  bool volumetric_fog_application_apply_tricubic_filtering = false;
  // Temporal Anti-Aliasing
  bool taa_enabled = true;
  bool taa_jittering_enabled = true;
  int taa_mode = 1;
  bool taa_use_inverse_luminance_filtering = true;
  bool taa_use_temporal_filtering = true;
  bool taa_use_luminance_difference_filtering = true;
  bool taa_use_ycocg = false;
  int taa_velocity_sampling_mode = 1;
  int taa_history_sampling_filter = 1;
  int taa_history_constraint_mode = 4;
  int taa_current_color_filter = 1;
  // Post process
  int post_tonemap_mode = 0;
  float post_exposure = 1.0f;
  float post_sharpening_amount = 0.1f;
  uint32_t post_zoom_scale = 2;
  bool post_enable_zoom = false;

  bool use_meshlets = true;
  bool use_meshlets_emulation = false;
  bool show_debug_gpu_draws = false;
  bool pointlight_rendering = true;
  bool pointlight_use_meshlets = true;
  bool use_tetrahedron_shadows = false;
  bool show_light_edit_debug_draws = false;

  bool cubeface_flip[6];

  float global_scale = 1.f;
}; // struct RenderScene

//
//
struct FrameRenderer
{

  void init(
      Framework::Allocator* resident_allocator,
      Graphics::RendererUtil::Renderer* renderer,
      FrameGraph* frame_graph,
      SceneGraph* scene_graph,
      RenderScene* scene);
  void shutdown();

  void upload_gpu_data(UploadGpuDataContext& context);
  void render(CommandBuffer* gpu_commands, RenderScene* render_scene);

  void prepare_draws(Framework::StackAllocator* scratch_allocator);
  void update_dependent_resources();

  Framework::Allocator* resident_allocator;
  SceneGraph* scene_graph;

  Renderer* renderer;
  FrameGraph* frame_graph;

  RenderScene* scene;

  Array<FrameGraphRenderPass*> render_passes;

  // Render passes
  DepthPrePass depth_pre_pass;
  GBufferPass gbuffer_pass_early;
  LateGBufferPass gbuffer_pass_late;
  LightPass light_pass;
  TransparentPass transparent_pass;
  DoFPass dof_pass;
  DebugPass debug_pass;
  CullingEarlyPass mesh_occlusion_early_pass;
  CullingLatePass mesh_occlusion_late_pass;
  DepthPyramidPass depth_pyramid_pass;
  PointlightShadowPass pointlight_shadow_pass;
  VolumetricFogPass volumetric_fog_pass;
  TemporalAntiAliasingPass temporal_anti_aliasing_pass;
  MotionVectorPass motion_vector_pass;
  RayTracingTestPass ray_tracing_test_pass;
  ShadowVisbilityPass shadow_visiblity_pass;

  // Fullscreen data
  Graphics::RendererUtil::GpuTechnique* fullscreen_tech = nullptr;
  DescriptorSetHandle fullscreen_ds;
  PipelineHandle passthrough_pipeline;
  PipelineHandle main_post_pipeline;
  BufferHandle post_uniforms_buffer;

}; // struct FrameRenderer

// DrawTask ///////////////////////////////////////////////////////////

//
//
struct DrawTask : public enki::ITaskSet
{

  Graphics::GpuDevice* gpu = nullptr;
  FrameGraph* frame_graph = nullptr;
  Renderer* renderer = nullptr;
  ImGuiService* imgui = nullptr;
  GpuVisualProfiler* gpu_profiler = nullptr;
  RenderScene* scene = nullptr;
  FrameRenderer* frame_renderer = nullptr;
  uint32_t thread_id = 0;
  // NOTE: gpu state might change between init and execute!
  uint32_t current_frame_index = 0;
  FramebufferHandle current_framebuffer = {kInvalidIndex};

  void init(
      GpuDevice* gpu_,
      FrameGraph* frame_graph_,
      RendererUtil::Renderer* renderer_,
      ImguiUtil::ImguiService* imgui_,
      RenderScene* scene_,
      FrameRenderer* frame_renderer);

  void ExecuteRange(enki::TaskSetPartition range_, uint32_t threadnum_) override;

}; // struct DrawTask

// Math utils /////////////////////////////////////////////////////////
void get_bounds_for_axis(const vec3s& a, const vec3s& C, float r, float nearZ, vec3s& L, vec3s& U);
vec3s project(const mat4s& P, const vec3s& Q);

void project_aabb_cubemap_positive_x(
    const vec3s aabb[2], float& s_min, float& s_max, float& t_min, float& t_max);
void project_aabb_cubemap_negative_x(
    const vec3s aabb[2], float& s_min, float& s_max, float& t_min, float& t_max);
void project_aabb_cubemap_positive_y(
    const vec3s aabb[2], float& s_min, float& s_max, float& t_min, float& t_max);
void project_aabb_cubemap_negative_y(
    const vec3s aabb[2], float& s_min, float& s_max, float& t_min, float& t_max);
void project_aabb_cubemap_positive_z(
    const vec3s aabb[2], float& s_min, float& s_max, float& t_min, float& t_max);
void project_aabb_cubemap_negative_z(
    const vec3s aabb[2], float& s_min, float& s_max, float& t_min, float& t_max);

// Numerical sequences, used to calculate jittering values.
float halton(int i, int b);
float interleaved_gradient_noise(vec2s pixel, int index);

vec2s halton23_sequence(int index);
vec2s m_robert_r2_sequence(int index);
vec2s interleaved_gradient_sequence(int index);
vec2s hammersley_sequence(int index, int num_samples);

} // namespace Graphics
