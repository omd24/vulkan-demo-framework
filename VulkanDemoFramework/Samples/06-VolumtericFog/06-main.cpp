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

#include "Externals/cglm/struct/vec2.h"
#include "Externals/cglm/struct/mat2.h"
#include "Externals/cglm/struct/mat3.h"
#include "Externals/cglm/struct/mat4.h"
#include "Externals/cglm/struct/cam.h"
#include "Externals/cglm/struct/affine.h"
#include "Externals/cglm/struct/box.h"

#include <stdio.h>
#include <stdlib.h> // for exit()

// TODOS:
// 1. Fix uniforms not getting updated
// 2. Double check DOF pass doesn't get disabled
// 3. fix memory leak reported on shutdown
// 4. fix validation issues on shutdown
// 5. Fix artifacts on Sponza curtains

//---------------------------------------------------------------------------//
// Graphics includes:
#include "Graphics/GpuDevice.hpp"
#include "Graphics/CommandBuffer.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/ImguiHelper.hpp"
#include "Graphics/RenderScene.hpp"
#include "Graphics/GltfScene.hpp"
#include "Graphics/ObjScene.hpp"
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
// Different helpers:
// TODO: Move to utility files
//---------------------------------------------------------------------------//
vec4s normalizePlane(vec4s plane)
{
  float len = glms_vec3_norm({plane.x, plane.y, plane.z});
  return glms_vec4_scale(plane, 1.0f / len);
}

float linearizeDepth(float depth, float zFar, float zNear)
{
  return zNear * zFar / (zFar + depth * (zNear - zFar));
}

static void testSphereAABB(Framework::GameCamera& gameCamera)
{
  vec4s pos{-14.5f, 1.28f, 0.f, 1.f};
  float radius = 0.5f;
  vec4s viewSpacePos = glms_mat4_mulv(gameCamera.camera.view, pos);
  bool camera_visible = viewSpacePos.z < radius + gameCamera.camera.nearPlane;

  // X is positive, then it returns the same values as the longer method.
  vec2s cx{viewSpacePos.x, -viewSpacePos.z};
  vec2s vx{sqrtf(glms_vec2_dot(cx, cx) - (radius * radius)), radius};
  mat2s xtransfMin{vx.x, vx.y, -vx.y, vx.x};
  vec2s minx = glms_mat2_mulv(xtransfMin, cx);
  mat2s xtransfMax{vx.x, -vx.y, vx.y, vx.x};
  vec2s maxx = glms_mat2_mulv(xtransfMax, cx);

  vec2s cy{-viewSpacePos.y, -viewSpacePos.z};
  vec2s vy{sqrtf(glms_vec2_dot(cy, cy) - (radius * radius)), radius};
  mat2s ytransfMin{vy.x, vy.y, -vy.y, vy.x};
  vec2s miny = glms_mat2_mulv(ytransfMin, cy);
  mat2s ytransfMax{vy.x, -vy.y, vy.y, vy.x};
  vec2s maxy = glms_mat2_mulv(ytransfMax, cy);

  vec4s aabb{
      minx.x / minx.y * gameCamera.camera.projection.m00,
      miny.x / miny.y * gameCamera.camera.projection.m11,
      maxx.x / maxx.y * gameCamera.camera.projection.m00,
      maxy.x / maxy.y * gameCamera.camera.projection.m11};
  vec4s aabb2{
      aabb.x * 0.5f + 0.5f, aabb.w * -0.5f + 0.5f, aabb.z * 0.5f + 0.5f, aabb.y * -0.5f + 0.5f};

  vec3s left, right, top, bottom;
  Graphics::get_bounds_for_axis(
      vec3s{1, 0, 0},
      {viewSpacePos.x, viewSpacePos.y, viewSpacePos.z},
      radius,
      gameCamera.camera.nearPlane,
      left,
      right);
  Graphics::get_bounds_for_axis(
      vec3s{0, 1, 0},
      {viewSpacePos.x, viewSpacePos.y, viewSpacePos.z},
      radius,
      gameCamera.camera.nearPlane,
      top,
      bottom);

  left = Graphics::project(gameCamera.camera.projection, left);
  right = Graphics::project(gameCamera.camera.projection, right);
  top = Graphics::project(gameCamera.camera.projection, top);
  bottom = Graphics::project(gameCamera.camera.projection, bottom);

  vec4s clip_space_pos = glms_mat4_mulv(gameCamera.camera.projection, viewSpacePos);

  // left,right,bottom and top are in clip space (-1,1). Convert to 0..1 for UV, as used from the
  // optimized version to read the depth pyramid.
  printf(
      "Camera visible %u, x %f, %f, width %f --- %f,%f width %f\n",
      camera_visible ? 1 : 0,
      aabb2.x,
      aabb2.z,
      aabb2.z - aabb2.x,
      left.x * 0.5 + 0.5,
      right.x * 0.5 + 0.5,
      (left.x - right.x) * 0.5);
  printf(
      "y %f, %f, height %f --- %f,%f height %f\n",
      aabb2.y,
      aabb2.w,
      aabb2.w - aabb2.y,
      top.y * 0.5 + 0.5,
      bottom.y * 0.5 + 0.5,
      (top.y - bottom.y) * 0.5);
}

// Light placement function ///////////////////////////////////////////////
void place_lights(Framework::Array<Graphics::Light>& lights, uint32_t active_lights, bool grid)
{

  using namespace Framework;

  if (grid)
  {
    const uint32_t lights_per_side = ::ceil(sqrtf(active_lights * 1.f));
    for (uint32_t i = 0; i < active_lights; ++i)
    {
      Graphics::Light& light = lights[i];

      const float x = (i % lights_per_side) - lights_per_side * .5f;
      const float y = 0.05f;
      const float z = (i / lights_per_side) - lights_per_side * .5f;

      light.world_position = {x, y, z};
      light.intensity = 10.f;
      light.radius = 0.25f;
      light.color = {1, 1, 1};
    }
  }

  //// TODO: we should take this into account when generating the lights positions
  // const float scale = 0.008f;

  // for ( uint32_t i = 0; i < k_num_lights; ++i ) {
  //    float x = get_random_value( mesh_aabb[ 0 ].x * scale, mesh_aabb[ 1 ].x * scale );
  //    float y = get_random_value( mesh_aabb[ 0 ].y * scale, mesh_aabb[ 1 ].y * scale );
  //    float z = get_random_value( mesh_aabb[ 0 ].z * scale, mesh_aabb[ 1 ].z * scale );

  //    float r = get_random_value( 0.0f, 1.0f );
  //    float g = get_random_value( 0.0f, 1.0f );
  //    float b = get_random_value( 0.0f, 1.0f );

  //    Light new_light{ };
  //    new_light.world_position = vec3s{ x, y, z };
  //    new_light.radius = 1.2f; // TODO: random as well?

  //    new_light.color = vec3s{ r, g, b };
  //    new_light.intensity = 30.0f;

  //    lights.push( new_light );
  //}
}

//
uint32_t get_cube_face_mask(vec3s cube_map_pos, vec3s aabb[2])
{

  vec3s plane_normals[] = {{-1, 1, 0}, {1, 1, 0}, {1, 0, 1}, {1, 0, -1}, {0, 1, 1}, {0, -1, 1}};
  vec3s abs_plane_normals[] = {{1, 1, 0}, {1, 1, 0}, {1, 0, 1}, {1, 0, 1}, {0, 1, 1}, {0, 1, 1}};

  vec3s center = glms_vec3_sub(glms_aabb_center(aabb), cube_map_pos);
  vec3s extents = glms_vec3_divs(glms_vec3_sub(aabb[1], aabb[0]), 2.0f);

  bool rp[6];
  bool rn[6];

  for (uint32_t i = 0; i < 6; ++i)
  {
    float dist = glms_vec3_dot(center, plane_normals[i]);
    float radius = glms_vec3_dot(extents, abs_plane_normals[i]);

    rp[i] = dist > -radius;
    rn[i] = dist < radius;
  }

  uint32_t fpx = rn[0] && rp[1] && rp[2] && rp[3] && aabb[1].x > cube_map_pos.x;
  uint32_t fnx = rp[0] && rn[1] && rn[2] && rn[3] && aabb[0].x < cube_map_pos.x;
  uint32_t fpy = rp[0] && rp[1] && rp[4] && rn[5] && aabb[1].y > cube_map_pos.y;
  uint32_t fny = rn[0] && rn[1] && rn[4] && rp[5] && aabb[0].y < cube_map_pos.y;
  uint32_t fpz = rp[2] && rn[3] && rp[4] && rp[5] && aabb[1].z > cube_map_pos.z;
  uint32_t fnz = rn[2] && rp[3] && rn[4] && rn[5] && aabb[0].z < cube_map_pos.z;

  return fpx | (fnx << 1) | (fpy << 2) | (fny << 3) | (fpz << 4) | (fnz << 5);
}

static void perform_geometric_tests(
    bool enable_aabb_cubemap_test,
    Graphics::RenderScene* scene,
    const vec3s& aabb_test_position,
    Graphics::GpuSceneData& scene_data,
    bool freeze_occlusion_camera,
    Framework::GameCamera& gameCamera,
    bool enable_light_tile_debug,
    Framework::Allocator* allocator,
    bool enable_light_cluster_debug)
{

  using namespace Framework;

  // float distance = glms_vec3_distance( { 0,0,0 }, light.world_position );
  // float distance_normalized = distance / (half_radius * 2.f);
  // float f = half_radius * 2;
  // float n = 0.01f;
  // float NormZComp = ( f + n ) / ( f - n ) - ( 2 * f * n ) / ( f - n ) / distance;
  // float NormZComp2 = ( f ) / ( n - f ) - ( f * n ) / ( n - f ) / distance;

  //// return zNear * zFar / (zFar + depth * (zNear - zFar));
  // float linear_d = n * f / ( f + 0.983 * ( n - f ) );
  // float linear_d2 = n * f / ( f + 1 * ( n - f ) );
  // float linear_d3 = n * f / ( f + 0.01 * ( n - f ) );

  //// ( f + z * ( n - f ) ) * lin_z = n * f;
  //// f * lin_z + (z * lin_z * (n - f ) = n * f
  //// ((n * f) - f * lin_z ) / (n - f) = z * lin_z

  // NormZComp = ( f + n ) / ( f - n ) - ( 2 * f * n ) / ( f - n ) / n;
  // NormZComp = ( f + n ) / ( f - n ) - ( 2 * f * n ) / ( f - n ) / f;
  // NormZComp2 = -( f ) / ( n - f ) - ( f * n ) / ( n - f ) / n;
  // NormZComp2 = -( f ) / ( n - f ) - ( f * n ) / ( n - f ) / f;

  // mat4s view = glms_look( light.world_position, { 0,0,-1 }, { 0,-1,0 } );
  //// TODO: this should be radius of the light.
  // mat4s projection = glms_perspective( glm_rad( 90.f ), 1.f, 0.01f, light.radius );
  // mat4s viewProjection = glms_mat4_mul( projection, view );

  // vec3s pos_cs = project( viewProjection, { 0,0,0 } );

  // printf( "DDDD %f %f %f %f\n", NormZComp, -NormZComp2, linear_d, pos_cs.z );
  //{
  //    float fn = 1.0f / ( 0.01f - light.radius );
  //    float a = ( 0.01f + light.radius ) * fn;
  //    float b = 2.0f * 0.01f * light.radius * fn;
  //    float projectedDistance = light.world_position.z;
  //    float z = projectedDistance * a + b;
  //    float dbDistance = z / projectedDistance;

  //    float bc = dbDistance - NormZComp;
  //    float bd = dbDistance - NormZComp2;
  //}

  // Test AABB cubemap intersection method
  if (enable_aabb_cubemap_test)
  {
    // Draw enclosing cubemap aabb
    vec3s cubemap_position = {0.f, 0.f, 0.f};
    vec3s cubemap_half_size = {1, 1, 1};
    scene->debug_renderer.aabb(
        glms_vec3_sub(cubemap_position, cubemap_half_size),
        glms_vec3_add(cubemap_position, cubemap_half_size),
        {Color::blue});

    vec3s aabb[] = {
        glms_vec3_subs(aabb_test_position, 0.2f), glms_vec3_adds(aabb_test_position, 0.2f)};
    uint32_t res = get_cube_face_mask(cubemap_position, aabb);
    // Positive X
    if ((res & 1))
    {
      scene->debug_renderer.aabb(
          glms_vec3_add(cubemap_position, {1, 0, 0}),
          glms_vec3_add(cubemap_position, {1.2, .2, .2}),
          {Color::getDistinctColor(0)});
    }
    // Negative X
    if ((res & 2))
    {
      scene->debug_renderer.aabb(
          glms_vec3_add(cubemap_position, {-1, 0, 0}),
          glms_vec3_add(cubemap_position, {-1.2, -.2, -.2}),
          {Color::getDistinctColor(1)});
    }
    // Positive Y
    if ((res & 4))
    {
      scene->debug_renderer.aabb(
          glms_vec3_add(cubemap_position, {0, 1, 0}),
          glms_vec3_add(cubemap_position, {.2, 1.2, .2}),
          {Color::getDistinctColor(2)});
    }
    // Negative Y
    if ((res & 8))
    {
      scene->debug_renderer.aabb(
          glms_vec3_add(cubemap_position, {0, -1, 0}),
          glms_vec3_add(cubemap_position, {.2, -1.2, .2}),
          {Color::getDistinctColor(3)});
    }
    // Positive Z
    if ((res & 16))
    {
      scene->debug_renderer.aabb(
          glms_vec3_add(cubemap_position, {0, 0, 1}),
          glms_vec3_add(cubemap_position, {.2, .2, 1.2}),
          {Color::getDistinctColor(4)});
    }
    // Negative Z
    if ((res & 32))
    {
      scene->debug_renderer.aabb(
          glms_vec3_add(cubemap_position, {0, 0, -1}),
          glms_vec3_add(cubemap_position, {.2, .2, -1.2}),
          {Color::getDistinctColor(5)});
    }
    // Draw aabb to test inside cubemap
    scene->debug_renderer.aabb(aabb[0], aabb[1], {Color::white});
    // scene->debug_renderer.line( { -1,-1,-1 }, { 1,1,1 }, { Color::white } );
    // scene->debug_renderer.line( { -1,-1,1 }, { 1,1,-1 }, { Color::white } );

    /*scene->debug_renderer.line({0.5,0,-0.5}, {-1 + .5,1,0 - .5}, {Color::blue});
    scene->debug_renderer.line( { -0.5,0,-0.5 }, { 1 - .5,1,0 - .5 }, { Color::green } );
    scene->debug_renderer.line( { 0,0,0 }, { 1,0,1 }, { Color::red } );
    scene->debug_renderer.line( { 0,0,0 }, { 1,0,-1 }, { Color::yellow } );
    scene->debug_renderer.line( { 0,0,0 }, { 0,1,1 }, { Color::white } );
    scene->debug_renderer.line( { 0,0,0 }, { 0,-1,1 }, { 0xffffff00 } ); */

    using namespace Graphics;

    // AABB -> cubemap face rectangle test
    float sMin, sMax, tMin, tMax;
    project_aabb_cubemap_positive_x(aabb, sMin, sMax, tMin, tMax);
    // printf( "POS X s %f,%f | t %f,%f\n", sMin, sMax, tMin, tMax );
    project_aabb_cubemap_negative_x(aabb, sMin, sMax, tMin, tMax);
    // printf( "NEG X s %f,%f | t %f,%f\n", sMin, sMax, tMin, tMax );
    project_aabb_cubemap_positive_y(aabb, sMin, sMax, tMin, tMax);
    // printf( "POS Y s %f,%f | t %f,%f\n", sMin, sMax, tMin, tMax );
    project_aabb_cubemap_negative_y(aabb, sMin, sMax, tMin, tMax);
    // printf( "NEG Y s %f,%f | t %f,%f\n", sMin, sMax, tMin, tMax );
    project_aabb_cubemap_positive_z(aabb, sMin, sMax, tMin, tMax);
    // printf( "POS Z s %f,%f | t %f,%f\n", sMin, sMax, tMin, tMax );
    project_aabb_cubemap_negative_z(aabb, sMin, sMax, tMin, tMax);
    // printf( "NEG Z s %f,%f | t %f,%f\n", sMin, sMax, tMin, tMax );
  }

  if (false)
  {
    // NOTE: adpated from http://www.aortiz.me/2018/12/21/CG.html#clustered-shading
    const uint32_t z_count = 32;
    const float tile_size = 64.0f;
    const float tile_pixels = tile_size * tile_size;
    const uint32_t tile_x_count = scene_data.resolution_x / float(tile_size);
    const uint32_t tile_y_count = scene_data.resolution_y / float(tile_size);

    const float tile_radius_sq = ((tile_size * 0.5f) * (tile_size * 0.5f)) * 2;

    const vec3s eye_pos = vec3s{0, 0, 0};

    static Camera last_camera{};

    if (!freeze_occlusion_camera)
    {
      last_camera = gameCamera.camera;
    }

    mat4s inverse_projection = glms_mat4_inv(last_camera.projection);
    mat4s inverse_view = glms_mat4_inv(last_camera.view);

    auto screen_to_view = [&](const vec4s& screen_pos) -> vec3s {
      // Convert to NDC
      vec2s text_coord{
          screen_pos.x / scene_data.resolution_x, screen_pos.y / scene_data.resolution_y};

      // Convert to clipSpace
      vec4s clip = vec4s{
          text_coord.x * 2.0f - 1.0f,
          (1.0f - text_coord.y) * 2.0f - 1.0f,
          screen_pos.z,
          screen_pos.w};

      // View space transform
      vec4s view = glms_mat4_mulv(inverse_projection, clip);

      // Perspective projection
      // view = glms_vec4_scale( view, 1.0f / view.w );

      return vec3s{view.x, view.y, view.z};
    };

    auto line_intersection_to_z_plane = [&](const vec3s& a, const vec3s& b, float z) -> vec3s {
      // all clusters planes are aligned in the same z direction
      vec3s normal = vec3s{0.0, 0.0, 1.0};

      // getting the line from the eye to the tile
      vec3s ab = glms_vec3_sub(b, a);

      // Computing the intersection length for the line and the plane
      float t = (z - glms_dot(normal, a)) / glms_dot(normal, ab);

      // Computing the actual xyz position of the point along the line
      vec3s result = glms_vec3_add(a, glms_vec3_scale(ab, t));

      return result;
    };

    const float zNear = scene_data.z_near;
    const float zFar = scene_data.z_far;
    const float z_ratio = zFar / zNear;
    const float z_bin_range = 1.0f / float(z_count);

    uint32_t light_count = scene->active_lights;

    Array<vec3s> lights_aabb_view;
    lights_aabb_view.init(allocator, light_count * 2, light_count * 2);

    using namespace Graphics;

    for (uint32_t l = 0; l < light_count; ++l)
    {
      Light& light = scene->lights[l];
      light.shadow_map_resolution = 0.0f;
      light.tile_x = 0;
      light.tile_y = 0;
      light.solid_angle = 0.0f;

      vec4s aabbMin_view = glms_mat4_mulv(last_camera.view, light.aabb_min);
      vec4s aabbMax_view = glms_mat4_mulv(last_camera.view, light.aabb_max);

      lights_aabb_view[l * 2] = vec3s{aabbMin_view.x, aabbMin_view.y, aabbMin_view.z};
      lights_aabb_view[l * 2 + 1] = vec3s{aabbMax_view.x, aabbMax_view.y, aabbMax_view.z};
    }

    for (uint32_t z = 0; z < z_count; ++z)
    {
      for (uint32_t y = 0; y < tile_y_count; ++y)
      {
        for (uint32_t x = 0; x < tile_x_count; ++x)
        {
          // Calculating the min and max point in screen space
          vec4s max_point_screen = vec4s{
              float((x + 1) * tile_size), float((y + 1) * tile_size), 0.0f, 1.0f}; // Top Right

          vec4s min_point_screen =
              vec4s{float(x * tile_size), float(y * tile_size), 0.0f, 1.0f}; // Top Right

          vec4s tile_center_screen =
              glms_vec4_scale(glms_vec4_add(min_point_screen, max_point_screen), 0.5f);
          vec2s tile_center{tile_center_screen.x, tile_center_screen.y};

          // Pass min and max to view space
          vec3s max_point_view = screen_to_view(max_point_screen);
          vec3s min_point_view = screen_to_view(min_point_screen);

          // Near and far values of the cluster in view space
          // We use equation (2) directly to obtain the tile values
          float tile_near = zNear * pow(z_ratio, float(z) * z_bin_range);
          float tile_far = zNear * pow(z_ratio, float(z + 1) * z_bin_range);

          // Finding the 4 intersection points made from each point to the cluster near/far plane
          vec3s min_point_near = line_intersection_to_z_plane(eye_pos, min_point_view, tile_near);
          vec3s min_point_far = line_intersection_to_z_plane(eye_pos, min_point_view, tile_far);
          vec3s max_point_near = line_intersection_to_z_plane(eye_pos, max_point_view, tile_near);
          vec3s max_point_far = line_intersection_to_z_plane(eye_pos, max_point_view, tile_far);

          vec3s min_point_aabb_view = glms_vec3_minv(
              glms_vec3_minv(min_point_near, min_point_far),
              glms_vec3_minv(max_point_near, max_point_far));
          vec3s max_point_aabb_view = glms_vec3_maxv(
              glms_vec3_maxv(min_point_near, min_point_far),
              glms_vec3_maxv(max_point_near, max_point_far));

          vec4s min_point_aabb_world{
              min_point_aabb_view.x, min_point_aabb_view.y, min_point_aabb_view.z, 1.0f};
          vec4s max_point_aabb_world{
              max_point_aabb_view.x, max_point_aabb_view.y, max_point_aabb_view.z, 1.0f};

          min_point_aabb_world = glms_mat4_mulv(inverse_view, min_point_aabb_world);
          max_point_aabb_world = glms_mat4_mulv(inverse_view, max_point_aabb_world);

          bool intersects_light = false;
          for (uint32_t l = 0; l < scene->active_lights; ++l)
          {
            Light& light = scene->lights[l];

            vec3s& light_aabbMin = lights_aabb_view[l * 2];
            vec3s& light_aabbMax = lights_aabb_view[l * 2 + 1];

            float minx =
                min(min(light_aabbMin.x, light_aabbMax.x),
                    min(min_point_aabb_view.x, max_point_aabb_view.x));
            float miny =
                min(min(light_aabbMin.y, light_aabbMax.y),
                    min(min_point_aabb_view.y, max_point_aabb_view.y));
            float minz =
                min(min(light_aabbMin.z, light_aabbMax.z),
                    min(min_point_aabb_view.z, max_point_aabb_view.z));

            float maxx =
                max(max(light_aabbMin.x, light_aabbMax.x),
                    max(min_point_aabb_view.x, max_point_aabb_view.x));
            float maxy =
                max(max(light_aabbMin.y, light_aabbMax.y),
                    max(min_point_aabb_view.y, max_point_aabb_view.y));
            float maxz =
                max(max(light_aabbMin.z, light_aabbMax.z),
                    max(min_point_aabb_view.z, max_point_aabb_view.z));

            float dx = abs(maxx - minx);
            float dy = abs(maxy - miny);
            float dz = abs(maxz - minz);

            float allx = abs(light_aabbMax.x - light_aabbMin.x) +
                         abs(max_point_aabb_view.x - min_point_aabb_view.x);
            float ally = abs(light_aabbMax.y - light_aabbMin.y) +
                         abs(max_point_aabb_view.y - min_point_aabb_view.y);
            float allz = abs(light_aabbMax.z - light_aabbMin.z) +
                         abs(max_point_aabb_view.z - min_point_aabb_view.z);

            bool intersects = (dx <= allx) && (dy < ally) && (dz <= allz);

            if (intersects)
            {
              intersects_light = true;

              vec4s sphere_world{
                  light.world_position.x, light.world_position.y, light.world_position.z, 1.0f};
              vec4s sphere_ndc = glms_mat4_mulv(last_camera.viewProjection, sphere_world);

              sphere_ndc.x /= sphere_ndc.w;
              sphere_ndc.y /= sphere_ndc.w;

              vec2s sphere_screen{
                  ((sphere_ndc.x + 1.0f) * 0.5f) * scene_data.resolution_x,
                  ((sphere_ndc.y + 1.0f) * 0.5f) * scene_data.resolution_y,
              };

              float d = glms_vec2_distance(sphere_screen, tile_center);

              float diff = d * d - tile_radius_sq;

              if (diff < 1.0e-4)
              {
                continue;
              }

              // NOTE: as defined in
              // https://math.stackexchange.com/questions/73238/calculating-solid-angle-for-a-sphere-in-space
              float solid_angle = (2.0f * Framework::PI) * (1.0f - (sqrtf(diff) / d));

              // NOTE: following
              // https://efficientshading.com/wp-content/uploads/s2015_shadows.pdf
              float resolution = sqrtf((4.0f * Framework::PI * tile_pixels) / (6 * solid_angle));

              if (resolution > light.shadow_map_resolution)
              {
                light.shadow_map_resolution = resolution;
                light.tile_x = x;
                light.tile_y = y;
                light.solid_angle = solid_angle;
              }
            }
          }

          if (enable_light_cluster_debug && intersects_light)
          {
            scene->debug_renderer.aabb(
                vec3s{min_point_aabb_world.x, min_point_aabb_world.y, min_point_aabb_world.z},
                vec3s{max_point_aabb_world.x, max_point_aabb_world.y, max_point_aabb_world.z},
                {Color::getDistinctColor(z)});
          }
        }
      }
    }

    lights_aabb_view.shutdown();

    if (enable_light_tile_debug)
    {
      float light_pos_len = 0.01;
      for (uint32_t l = 0; l < light_count; ++l)
      {
        Light& light = scene->lights[l];

        // printf( "Light resolution %f\n", light.shadow_map_resolution );

        if (light.shadow_map_resolution != 0.0f)
        {
          {
            vec4s sphere_world{
                light.world_position.x, light.world_position.y, light.world_position.z, 1.0f};
            vec4s sphere_ndc = glms_mat4_mulv(last_camera.viewProjection, sphere_world);

            sphere_ndc.x /= sphere_ndc.w;
            sphere_ndc.y /= sphere_ndc.w;

            vec2s top_left{sphere_ndc.x - light_pos_len, sphere_ndc.y - light_pos_len};
            vec2s bottom_right{sphere_ndc.x + light_pos_len, sphere_ndc.y + light_pos_len};
            vec2s top_right{sphere_ndc.x + light_pos_len, sphere_ndc.y - light_pos_len};
            vec2s bottom_left{sphere_ndc.x - light_pos_len, sphere_ndc.y + light_pos_len};

            scene->debug_renderer.line_2d(top_left, bottom_right, {Color::getDistinctColor(l + 1)});
            scene->debug_renderer.line_2d(top_right, bottom_left, {Color::getDistinctColor(l + 1)});
          }

          {
            vec2s screen_scale{
                1.0f / float(scene_data.resolution_x), 1.0f / (scene_data.resolution_y)};

            vec2s bottom_right{
                float((light.tile_x + 1) * tile_size),
                float(scene_data.resolution_y - (light.tile_y + 1) * tile_size)};
            bottom_right = glms_vec2_subs(
                glms_vec2_scale(glms_vec2_mul(bottom_right, screen_scale), 2.0f), 1.0f);

            vec2s top_left{
                float((light.tile_x) * tile_size),
                float(scene_data.resolution_y - (light.tile_y) * tile_size)};
            top_left =
                glms_vec2_subs(glms_vec2_scale(glms_vec2_mul(top_left, screen_scale), 2.0f), 1.0f);

            vec2s top_right{bottom_right.x, top_left.y};
            vec2s bottom_left{top_left.x, bottom_right.y};

            scene->debug_renderer.line_2d(top_left, top_right, {Color::getDistinctColor(l + 1)});
            scene->debug_renderer.line_2d(
                top_right, bottom_right, {Color::getDistinctColor(l + 1)});
            scene->debug_renderer.line_2d(
                bottom_left, bottom_right, {Color::getDistinctColor(l + 1)});
            scene->debug_renderer.line_2d(bottom_left, top_left, {Color::getDistinctColor(l + 1)});
          }
        }
      }
    }
  }
}

// Enums
namespace JitterType
{
enum Enum
{
  Halton = 0,
  R2,
  Hammersley,
  InterleavedGradients
};

char const* names[] = {"Halton", "Martin Robert R2", "Hammersley", "Interleaved Gradients"};
} // namespace JitterType
//---------------------------------------------------------------------------//
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
      1280, 800, "Volumetric Fog Demo", &MemoryService::instance()->m_SystemAllocator};
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

  Directory cwd{};
  Framework::directoryCurrent(&cwd);

  // Load frame graph and parse gpu techniques
  {
    char const* frameGraphPath =
        temporaryNameBuffer.appendUseFormatted("%s%s%s", cwd, WORKING_FOLDER, "graph.json");

    frameGraph.parse(frameGraphPath, &scratchAllocator);
    frameGraph.compile();

    renderResourcesLoader.init(&renderer, &scratchAllocator, &frameGraph);

    // TODO: add this to render graph itself.
    // Add utility textures (dithering, ...)
    temporaryNameBuffer.clear();
    char const* ditherTexturePath =
        temporaryNameBuffer.appendUseFormatted("%s%sBayerDither4x4.png", cwd, DATA_FOLDER);
    ditherTexture = renderResourcesLoader.loadTexture(ditherTexturePath, false);

    // Parse techniques
    Graphics::RendererUtil::GpuTechniqueCreation gtc;
    temporaryNameBuffer.clear();
    char const* fullScreenPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s%s%s", cwd, SHADER_FOLDER, "fullscreen.json");
    renderResourcesLoader.loadGpuTechnique(fullScreenPipelinePath);

    temporaryNameBuffer.clear();
    char const* mainPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s%s%s", cwd, SHADER_FOLDER, "main.json");
    renderResourcesLoader.loadGpuTechnique(mainPipelinePath);

    temporaryNameBuffer.clear();
    char const* pbrPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s%s%s", cwd, SHADER_FOLDER, "pbr_lighting.json");
    renderResourcesLoader.loadGpuTechnique(pbrPipelinePath);

    temporaryNameBuffer.clear();
    char const* dofPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s%s%s", cwd, SHADER_FOLDER, "dof.json");
    renderResourcesLoader.loadGpuTechnique(dofPipelinePath);

    temporaryNameBuffer.clear();
    char const* clothPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s%s%s", cwd, SHADER_FOLDER, "cloth.json");
    renderResourcesLoader.loadGpuTechnique(clothPipelinePath);

    temporaryNameBuffer.clear();
    char const* debugPipelinePath =
        temporaryNameBuffer.appendUseFormatted("%s%s%s", cwd, SHADER_FOLDER, "debug.json");
    renderResourcesLoader.loadGpuTechnique(debugPipelinePath);
  }

  Graphics::SceneGraph sceneGraph;
  sceneGraph.init(allocator, 4);

  // [TAG: Multithreading]
  Graphics::AsynchronousLoader asyncLoader;
  asyncLoader.init(&renderer, &taskScheduler, allocator);

  // Directory cwd{};
  // directoryCurrent(&cwd);

  temporaryNameBuffer.clear();
  char const* scenePath = nullptr;
  if (argc > 1)
  {
    scenePath = argv[1];
  }
  else
  {
    scenePath = temporaryNameBuffer.appendUseFormatted("%s%s%s", cwd, DATA_FOLDER, "plane.obj");
  }

  char fileBasePath[512]{};
  memcpy(fileBasePath, scenePath, strlen(scenePath));
  fileDirectoryFromPath(fileBasePath);

  directoryChange(fileBasePath);

  char fileName[512]{};
  memcpy(fileName, scenePath, strlen(scenePath));
  filenameFromPath(fileName);

  scratchAllocator.freeMarker(scratchMarker);

  Graphics::RenderScene* scene = new Graphics::ObjScene;

  char* fileExtension = fileExtensionFromPath(fileName);

  scene->init(fileName, fileBasePath, allocator, &scratchAllocator, &asyncLoader);

  // NOTE: restore working directory
  directoryChange(cwd.path);

  Graphics::FrameRenderer frameRenderer;
  frameRenderer.init(allocator, &renderer, &frameGraph, &sceneGraph, scene);
  frameRenderer.prepare_draws(&scratchAllocator);

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
      frameGraph.on_resize(gpu, window.m_Width, window.m_Height);

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

    static float animationSpeedMultiplier = 0.05f;

    {
      if (ImGui::Begin("Framework ImGui"))
      {
        ImGui::InputFloat("Scene global scale", &scene->global_scale, 0.001f);
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

        frameGraph.add_ui();
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
      scene->update_animations(deltaTime * animationSpeedMultiplier);
    }
    {
      sceneGraph.updateMatrices();
    }
    {
      scene->update_joints();
    }

    {
      // Update scene constant buffer
      Graphics::MapBufferParameters sceneCbMap = {scene->scene_cb, 0, 0};
      Graphics::GpuSceneData* gpuSceneData = (Graphics::GpuSceneData*)gpu.mapBuffer(sceneCbMap);
      if (gpuSceneData)
      {
        gpuSceneData->view_projection = gameCamera.camera.viewProjection;
        gpuSceneData->inverse_view_projection = glms_mat4_inv(gameCamera.camera.viewProjection);
        gpuSceneData->camera_position = vec4s{
            gameCamera.camera.position.x,
            gameCamera.camera.position.y,
            gameCamera.camera.position.z,
            1.0f};
        /*gpuSceneData->lightPosition =
            vec4s{lightPosition.x, lightPosition.y, lightPosition.z, 1.0f};
        gpuSceneData->lightRange = lightRadius;
        gpuSceneData->lightIntensity = lightIntensity;*/
        gpuSceneData->dither_texture_index = ditherTexture ? ditherTexture->m_Handle.index : 0;

        gpu.unmapBuffer(sceneCbMap);
      }

      Graphics::UploadGpuDataContext upload_context{gameCamera, &scratchAllocator};
      frameRenderer.upload_gpu_data(upload_context);
    }

    if (!window.m_Minimized)
    {
      Graphics::DrawTask drawTask;
      drawTask.init(renderer.m_GpuDevice, &frameGraph, &renderer, imgui, scene, &frameRenderer);
      taskScheduler.AddTaskSetToPipe(&drawTask);

      Graphics::CommandBuffer* async_compute_command_buffer = nullptr;
      {
        async_compute_command_buffer = scene->update_physics(
            deltaTime, airDensity, springStiffness, springDamping, windDirection, resetSimulation);
        resetSimulation = false;
      }

      taskScheduler.WaitforTaskSet(&drawTask);

      // Avoid using the same command buffer
      renderer.addTextureUpdateCommands(
          (drawTask.thread_id + 1) % taskScheduler.GetNumTaskThreads());
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
