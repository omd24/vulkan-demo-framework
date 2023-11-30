#include "FrameGraph.hpp"

#include "Foundation/Memory.hpp"
#include "Foundation/File.hpp"
#include "Foundation/String.hpp"

#include "Graphics/GpuDevice.hpp"
#include "Graphics/CommandBuffer.hpp"

#include "Externals/json.hpp"

namespace Graphics
{
using namespace Framework;
//---------------------------------------------------------------------------//
// Helper functions:
//---------------------------------------------------------------------------//
static FrameGraphResourceType stringToResourceType(const char* p_InputType)
{
  if (strcmp(p_InputType, "texture") == 0)
  {
    return kFrameGraphResourceTypeTexture;
  }

  if (strcmp(p_InputType, "attachment") == 0)
  {
    return kFrameGraphResourceTypeAttachment;
  }

  if (strcmp(p_InputType, "buffer") == 0)
  {
    return kFrameGraphResourceTypeBuffer;
  }

  if (strcmp(p_InputType, "reference") == 0)
  {
    // This is used for resources that need to create an edge but are not actually
    // used by the render pass
    return kFrameGraphResourceTypeReference;
  }

  if (strcmp(p_InputType, "shading_rate") == 0)
  {
    return FrameGraphResourceType_ShadingRate;
  }

  assert(false);
  return kFrameGraphResourceTypeInvalid;
}
//---------------------------------------------------------------------------//

RenderPassOperation::Enum string_to_render_pass_operation(cstring op)
{
  if (strcmp(op, "clear") == 0)
  {
    return RenderPassOperation::kClear;
  }
  else if (strcmp(op, "load") == 0)
  {
    return RenderPassOperation::kLoad;
  }

  assert(false);
  return RenderPassOperation::kDontCare;
}

// FrameGraph /////////////////////////////////////////////////////////////

void FrameGraph::init(FrameGraphBuilder* builder_)
{
  allocator = &MemoryService::instance()->m_SystemAllocator;

  local_allocator.init(FRAMEWORK_MEGA(1));

  builder = builder_;

  nodes.init(allocator, FrameGraphBuilder::k_max_nodes_count);
  all_nodes.init(allocator, FrameGraphBuilder::k_max_nodes_count);
}

void FrameGraph::shutdown()
{
  for (uint32_t i = 0; i < all_nodes.m_Size; ++i)
  {
    FrameGraphNodeHandle handle = all_nodes[i];
    FrameGraphNode* node = builder->access_node(handle);

    builder->device->destroyRenderPass(node->render_pass);
    builder->device->destroyFramebuffer(node->framebuffer);

    node->inputs.shutdown();
    node->outputs.shutdown();
    node->edges.shutdown();
  }

  all_nodes.shutdown();
  nodes.shutdown();

  local_allocator.shutdown();
}

void FrameGraph::parse(cstring file_path, Framework::StackAllocator* temp_allocator)
{
  using json = nlohmann::json;

  if (!Framework::fileExists(file_path))
  {
    assert(false);
    return;
  }

  size_t current_allocator_marker = temp_allocator->getMarker();

  FileReadResult read_result = Framework::fileReadText(file_path, temp_allocator);

  json graph_data = json::parse(read_result.data);

  StringBuffer string_buffer;
  string_buffer.init(2048, &local_allocator);

  std::string name_value = graph_data.value("name", "");
  name = string_buffer.appendUseFormatted("%s", name_value.c_str());

  json passes = graph_data["passes"];
  for (size_t i = 0; i < passes.size(); ++i)
  {
    json pass = passes[i];

    json pass_inputs = pass["inputs"];
    json pass_outputs = pass["outputs"];

    FrameGraphNodeCreation node_creation{};
    node_creation.inputs.init(temp_allocator, (uint32_t)pass_inputs.size());
    node_creation.outputs.init(temp_allocator, (uint32_t)pass_outputs.size());

    std::string node_type = pass.value("type", "");
    node_creation.compute = node_type.compare("compute") == 0;
    node_creation.ray_tracing = node_type.compare("ray_tracing") == 0;

    for (size_t ii = 0; ii < pass_inputs.size(); ++ii)
    {
      json pass_input = pass_inputs[ii];

      FrameGraphResourceInputCreation input_creation{};

      std::string input_type = pass_input.value("type", "");
      assert(!input_type.empty());

      std::string input_name = pass_input.value("name", "");
      assert(!input_name.empty());

      input_creation.type = stringToResourceType(input_type.c_str());
      input_creation.resourceInfo.external = false;

      input_creation.name = string_buffer.appendUseFormatted("%s", input_name.c_str());

      node_creation.inputs.push(input_creation);
    }

    for (size_t oi = 0; oi < pass_outputs.size(); ++oi)
    {
      json pass_output = pass_outputs[oi];

      FrameGraphResourceOutputCreation output_creation{};

      std::string output_type = pass_output.value("type", "");
      assert(!output_type.empty());

      std::string output_name = pass_output.value("name", "");
      assert(!output_name.empty());

      bool external = pass_output.value("external", false);
      output_creation.resourceInfo.external = external;

      output_creation.type = stringToResourceType(output_type.c_str());
      output_creation.name = string_buffer.appendUseFormatted("%s", output_name.c_str());

      switch (output_creation.type)
      {
      case kFrameGraphResourceTypeTexture: {
        // NOTE: for now output textures are all managed manually. We add them to the graph
        // to make sure they are considered when performing the topological sort
      }
      break;
      case kFrameGraphResourceTypeAttachment: {
        std::string format = pass_output.value("format", "");
        assert(!format.empty());

        output_creation.resourceInfo.texture.format = utilStringToVkFormat(format.c_str());

        std::string load_op = pass_output.value("load_operation", "");
        assert(!load_op.empty());

        output_creation.resourceInfo.texture.load_op =
            string_to_render_pass_operation(load_op.c_str());

        json resolution = pass_output["resolution"];
        json scaling = pass_output["resolution_scale"];

        if (resolution.is_array())
        {
          output_creation.resourceInfo.texture.width = resolution[0];
          output_creation.resourceInfo.texture.height = resolution[1];
          output_creation.resourceInfo.texture.depth = 1;
          output_creation.resourceInfo.texture.scale_width = 0.f;
          output_creation.resourceInfo.texture.scale_height = 0.f;
        }
        else if (scaling.is_array())
        {
          output_creation.resourceInfo.texture.width = 0;
          output_creation.resourceInfo.texture.height = 0;
          output_creation.resourceInfo.texture.depth = 1;
          output_creation.resourceInfo.texture.scale_width = scaling[0];
          output_creation.resourceInfo.texture.scale_height = scaling[1];
        }
        else
        {
          // Defaults
          output_creation.resourceInfo.texture.width = 0;
          output_creation.resourceInfo.texture.height = 0;
          output_creation.resourceInfo.texture.depth = 1;
          output_creation.resourceInfo.texture.scale_width = 1.f;
          output_creation.resourceInfo.texture.scale_height = 1.f;
        }

        output_creation.resourceInfo.texture.compute = node_creation.compute;

        // Parse depth/stencil values
        if (TextureFormat::hasDepth(output_creation.resourceInfo.texture.format))
        {
          output_creation.resourceInfo.texture.clear_values[0] =
              pass_output.value("clear_depth", 1.0f);
          output_creation.resourceInfo.texture.clear_values[1] =
              pass_output.value("clear_stencil", 0.0f);
        }
        else
        {
          // Parse color array
          json clear_color_array = pass_output["clear_color"];
          if (clear_color_array.is_array())
          {
            for (uint32_t c = 0; c < clear_color_array.size(); ++c)
            {
              output_creation.resourceInfo.texture.clear_values[c] = clear_color_array[c];
            }
          }
          else
          {
            if (output_creation.resourceInfo.texture.load_op == RenderPassOperation::kClear)
            {
              assert(false);
            }
            output_creation.resourceInfo.texture.clear_values[0] = 0.0f;
            output_creation.resourceInfo.texture.clear_values[1] = 0.0f;
            output_creation.resourceInfo.texture.clear_values[2] = 0.0f;
            output_creation.resourceInfo.texture.clear_values[3] = 0.0f;
          }
        }
      }
      break;
      case kFrameGraphResourceTypeBuffer: {
        // NOTE: for now buffers are all managed manually. We add them to the graph
        // to make sure they are considered when performing the topological sort
      }
      break;
      }

      node_creation.outputs.push(output_creation);
    }

    name_value = pass.value("name", "");
    assert(!name_value.empty());

    bool enabled = pass.value("enabled", true);

    node_creation.name = string_buffer.appendUseFormatted("%s", name_value.c_str());
    node_creation.enabled = enabled;

    FrameGraphNodeHandle node_handle = builder->create_node(node_creation);
    all_nodes.push(node_handle);
  }

  temp_allocator->freeMarker(current_allocator_marker);
}

static void compute_edges(FrameGraph* frame_graph, FrameGraphNode* node, uint32_t node_index)
{

  FrameGraphNodeHandle node_handle = frame_graph->all_nodes[node_index];

  for (uint32_t r = 0; r < node->inputs.m_Size; ++r)
  {
    FrameGraphResource* resource = frame_graph->access_resource(node->inputs[r]);

    {
      FrameGraphResource* output_resource = frame_graph->get_resource(resource->m_Name);
      if (output_resource == nullptr && !resource->resourceInfo.external)
      {
        // TODO: external resources
        assert(false);
        continue;
      }

      resource->producer = output_resource->producer;
      resource->resourceInfo = output_resource->resourceInfo;
      resource->outputHandle = output_resource->outputHandle;
    }

    for (uint32_t n = 0; n < frame_graph->all_nodes.m_Size; ++n)
    {
      if (n == node_index)
      {
        continue;
      }

      FrameGraphNodeHandle parent_handle = frame_graph->all_nodes[n];
      FrameGraphNode* parent_node = frame_graph->access_node(parent_handle);

      for (uint32_t o = 0; o < parent_node->outputs.m_Size; ++o)
      {
        FrameGraphResource* output_resource = frame_graph->access_resource(parent_node->outputs[o]);

        if (strcmp(resource->m_Name, output_resource->m_Name) != 0)
        {
          continue;
        }

#if FRAME_GRAPH_DEBUG
        printf(
            "Adding edge for resource %s from %s [%d] to %s [%d]\n",
            output_resource->m_Name,
            parent_node->name,
            n,
            node->name,
            node_index)
#endif

            parent_node->edges.push(node_handle);
      }
    }
  }
}

static void create_framebuffer(FrameGraph* frame_graph, FrameGraphNode* node)
{

  FramebufferCreation framebuffer_creation{};
  framebuffer_creation.renderPass = node->render_pass;
  framebuffer_creation.setName(node->name);

  uint32_t width = 0;
  uint32_t height = 0;
  float scale_width = 0.f;
  float scale_height = 0.f;

  for (uint32_t r = 0; r < node->outputs.m_Size; ++r)
  {
    FrameGraphResource* resource = frame_graph->access_resource(node->outputs[r]);

    FrameGraphResourceInfo& info = resource->resourceInfo;

    if (resource->type != kFrameGraphResourceTypeAttachment)
    {
      continue;
    }

    if (width == 0)
    {
      width = info.texture.width;
      scale_width = info.texture.scale_width > 0.f ? info.texture.scale_width : 1.f;
    }
    else
    {
      assert(width == info.texture.width);
    }

    if (height == 0)
    {
      height = info.texture.height;
      scale_height = info.texture.scale_height > 0.f ? info.texture.scale_height : 1.f;
    }
    else
    {
      assert(height == info.texture.height);
    }

    if (TextureFormat::hasDepth(info.texture.format))
    {
      framebuffer_creation.setDepthStencilTexture(info.texture.handle);
    }
    else
    {
      framebuffer_creation.addRenderTexture(info.texture.handle);
    }
  }

  for (uint32_t r = 0; r < node->inputs.m_Size; ++r)
  {
    FrameGraphResource* input_resource = frame_graph->access_resource(node->inputs[r]);

    if (input_resource->type != kFrameGraphResourceTypeAttachment &&
        input_resource->type != FrameGraphResourceType_ShadingRate)
    {
      continue;
    }

    FrameGraphResource* resource = frame_graph->get_resource(input_resource->m_Name);

    if (resource == nullptr)
    {
      continue;
    }

    FrameGraphResourceInfo& info = resource->resourceInfo;

    input_resource->resourceInfo.texture.handle = info.texture.handle;

    if (width == 0)
    {
      width = info.texture.width;
      scale_width = info.texture.scale_width > 0.f ? info.texture.scale_width : 1.f;
    }
    else if (input_resource->type != FrameGraphResourceType_ShadingRate)
    {
      assert(width == info.texture.width);
    }

    if (height == 0)
    {
      height = info.texture.height;
      scale_height = info.texture.scale_height > 0.f ? info.texture.scale_height : 1.f;
    }
    else if (input_resource->type != FrameGraphResourceType_ShadingRate)
    {
      assert(height == info.texture.height);
    }

    if (input_resource->type == kFrameGraphResourceTypeTexture)
    {
      continue;
    }

    if (resource->type == FrameGraphResourceType_ShadingRate)
    {
      assert(false); // not implemented

      continue;
    }

    if (TextureFormat::hasDepth(info.texture.format))
    {
      framebuffer_creation.setDepthStencilTexture(info.texture.handle);
    }
    else
    {
      framebuffer_creation.addRenderTexture(info.texture.handle);
    }
  }

  framebuffer_creation.width = width;
  framebuffer_creation.height = height;
  framebuffer_creation.setScaling(scale_width, scale_height, 1);
  node->framebuffer = frame_graph->builder->device->createFramebuffer(framebuffer_creation);

  node->resolution_scale_width = scale_width;
  node->resolution_scale_height = scale_height;
}

static void create_render_pass(FrameGraph* frame_graph, FrameGraphNode* node)
{
  RenderPassCreation render_pass_creation{};
  render_pass_creation.setName(node->name);

  // NOTE: first create the outputs, then we can patch the input resources
  // with the right handles
  for (uint32_t i = 0; i < node->outputs.m_Size; ++i)
  {
    FrameGraphResource* output_resource = frame_graph->access_resource(node->outputs[i]);

    FrameGraphResourceInfo& info = output_resource->resourceInfo;

    if (output_resource->type == kFrameGraphResourceTypeAttachment)
    {
      if (TextureFormat::hasDepth(info.texture.format))
      {
        render_pass_creation.setDepthStencilTexture(
            info.texture.format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        render_pass_creation.depthOperation = RenderPassOperation::kClear;
      }
      else
      {
        render_pass_creation.addAttachment(
            info.texture.format, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, info.texture.load_op);
      }
    }
  }

  for (uint32_t i = 0; i < node->inputs.m_Size; ++i)
  {
    FrameGraphResource* input_resource = frame_graph->access_resource(node->inputs[i]);

    FrameGraphResourceInfo& info = input_resource->resourceInfo;

    if (input_resource->type == kFrameGraphResourceTypeAttachment)
    {
      if (TextureFormat::hasDepth(info.texture.format))
      {
        render_pass_creation.setDepthStencilTexture(
            info.texture.format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        render_pass_creation.depthOperation = RenderPassOperation::kLoad;
      }
      else
      {
        render_pass_creation.addAttachment(
            info.texture.format,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            RenderPassOperation::kLoad);
      }
    }

    if (input_resource->type == FrameGraphResourceType_ShadingRate)
    {
      if (!frame_graph->builder->device->m_DynamicRenderingExtensionPresent)
      {
        render_pass_creation.shading_rate_image_index = render_pass_creation.numRenderTargets++;
      }
    }
  }

  // TODO: make sure formats are valid for attachment
  node->render_pass = frame_graph->builder->device->createRenderPass(render_pass_creation);
}

void FrameGraph::enable_render_pass(cstring render_pass_name)
{
  FrameGraphNode* node = builder->get_node(render_pass_name);
  node->enabled = true;
}

void FrameGraph::disable_render_pass(cstring render_pass_name)
{
  FrameGraphNode* node = builder->get_node(render_pass_name);
  node->enabled = false;
}

namespace FrameGraphNodeVisitStatus
{
enum Enum
{
  New = 0,
  Visited,
  Added,
  Count
}; // enum Enum
}; // namespace FrameGraphNodeVisitStatus

void FrameGraph::compile()
{
  // TODO
  // - check that input has been produced by a different node
  // - cull inactive nodes

  for (uint32_t i = 0; i < all_nodes.m_Size; ++i)
  {
    FrameGraphNode* node = builder->access_node(all_nodes[i]);

    // NOTE: we want to clear all edges first, then populate them. If we clear them inside
    // the loop below we risk clearing the list after it has already been used by one of the child
    // nodes
    node->edges.clear();
  }

  for (uint32_t i = 0; i < all_nodes.m_Size; ++i)
  {
    FrameGraphNode* node = builder->access_node(all_nodes[i]);
    if (!node->enabled)
    {
      continue;
    }

    compute_edges(this, node, i);
  }

  Array<FrameGraphNodeHandle> sorted_nodes;
  sorted_nodes.init(&local_allocator, all_nodes.m_Size);

  Array<uint8_t> node_status;
  node_status.init(&local_allocator, all_nodes.m_Size, all_nodes.m_Size);
  memset(node_status.m_Data, 0, sizeof(bool) * all_nodes.m_Size);

  Array<FrameGraphNodeHandle> stack;
  stack.init(&local_allocator, nodes.m_Size);

  // Topological sorting
  for (uint32_t n = 0; n < all_nodes.m_Size; ++n)
  {
    FrameGraphNode* node = builder->access_node(all_nodes[n]);
    if (!node->enabled)
    {
      continue;
    }

    stack.push(all_nodes[n]);

    while (stack.m_Size > 0)
    {
      FrameGraphNodeHandle node_handle = stack.back();

      if (node_status[node_handle.index] == FrameGraphNodeVisitStatus::Added)
      {
        stack.pop();

        continue;
      }

      if (node_status[node_handle.index] == FrameGraphNodeVisitStatus::Visited)
      {
        node_status[node_handle.index] = FrameGraphNodeVisitStatus::Added;

        sorted_nodes.push(node_handle);

        stack.pop();

        continue;
      }

      node_status[node_handle.index] = FrameGraphNodeVisitStatus::Visited;

      FrameGraphNode* node = builder->access_node(node_handle);

      // Leaf node
      if (node->edges.m_Size == 0)
      {
        continue;
      }

      for (uint32_t r = 0; r < node->edges.m_Size; ++r)
      {
        FrameGraphNodeHandle child_handle = node->edges[r];

        if (node_status[child_handle.index] == FrameGraphNodeVisitStatus::New)
        {
          stack.push(child_handle);
        }
      }
    }
  }

  nodes.clear();

  for (int i = sorted_nodes.m_Size - 1; i >= 0; --i)
  {
    FrameGraphNode* node = builder->access_node(sorted_nodes[i]);
#if FRAME_GRAPH_DEBUG
    printf("Node %s is at position %d\n", node->name, nodes.m_Size);
#endif

    nodes.push(sorted_nodes[i]);
  }

  node_status.shutdown();
  stack.shutdown();
  sorted_nodes.shutdown();

  // NOTE: allocations and deallocations are used for verification purposes only
  uint32_t resource_count = builder->resource_cache.resources.m_UsedIndices;
  Array<FrameGraphNodeHandle> allocations;
  allocations.init(&local_allocator, resource_count, resource_count);
  for (uint32_t i = 0; i < resource_count; ++i)
  {
    allocations[i].index = kInvalidIndex;
  }

  Array<FrameGraphNodeHandle> deallocations;
  deallocations.init(&local_allocator, resource_count, resource_count);
  for (uint32_t i = 0; i < resource_count; ++i)
  {
    deallocations[i].index = kInvalidIndex;
  }

  Array<TextureHandle> free_list;
  free_list.init(&local_allocator, resource_count);

  size_t peak_memory = 0;
  size_t instant_memory = 0;

  for (uint32_t i = 0; i < nodes.m_Size; ++i)
  {
    FrameGraphNode* node = builder->access_node(nodes[i]);
    if (!node->enabled)
    {
      continue;
    }

    for (uint32_t j = 0; j < node->inputs.m_Size; ++j)
    {
      FrameGraphResource* input_resource = builder->access_resource(node->inputs[j]);
      FrameGraphResource* resource = builder->access_resource(input_resource->outputHandle);

      if (resource == nullptr)
      {
        continue;
      }

      resource->refCount++;
    }
  }

  for (uint32_t i = 0; i < nodes.m_Size; ++i)
  {
    FrameGraphNode* node = builder->access_node(nodes[i]);
    if (!node->enabled)
    {
      continue;
    }

    for (uint32_t j = 0; j < node->outputs.m_Size; ++j)
    {
      uint32_t resource_index = node->outputs[j].index;
      FrameGraphResource* resource = builder->access_resource(node->outputs[j]);

      if (!resource->resourceInfo.external && allocations[resource_index].index == kInvalidIndex)
      {
        assert(deallocations[resource_index].index == kInvalidIndex);
        allocations[resource_index] = nodes[i];

        if (resource->type == kFrameGraphResourceTypeAttachment)
        {
          FrameGraphResourceInfo& info = resource->resourceInfo;

          // Resolve texture size if needed
          if (info.texture.width == 0 || info.texture.height == 0)
          {
            info.texture.width = builder->device->m_SwapchainWidth * info.texture.scale_width;
            info.texture.height = builder->device->m_SwapchainHeight * info.texture.scale_height;
          }

          TextureFlags::Mask texture_creation_flags =
              info.texture.compute
                  ? (TextureFlags::Mask)(
                        TextureFlags::kRenderTargetMask | TextureFlags::kComputeMask)
                  : TextureFlags::kRenderTargetMask;

          if (free_list.m_Size > 0)
          {
            // TODO: find best fit
            TextureHandle alias_texture = free_list.back();
            free_list.pop();

            TextureCreation texture_creation{};
            texture_creation.setData(nullptr)
                .setAlias(alias_texture)
                .setName(resource->m_Name)
                .setFormatType(info.texture.format, TextureType::Enum::kTexture2D)
                .setSize(info.texture.width, info.texture.height, info.texture.depth)
                .setFlags(texture_creation_flags);
            TextureHandle handle = builder->device->createTexture(texture_creation);

            info.texture.handle = handle;
          }
          else
          {
            TextureCreation texture_creation{};
            texture_creation.setData(nullptr)
                .setName(resource->m_Name)
                .setFormatType(info.texture.format, TextureType::Enum::kTexture2D)
                .setSize(info.texture.width, info.texture.height, info.texture.depth)
                .setFlags(texture_creation_flags);
            TextureHandle handle = builder->device->createTexture(texture_creation);

            info.texture.handle = handle;
          }
        }

#if FRAME_GRAPH_DEBUG
        printf("Output %s allocated on node %d\n", resource->m_Name, nodes[i].index);
#endif
      }
    }

    for (uint32_t j = 0; j < node->inputs.m_Size; ++j)
    {
      FrameGraphResource* input_resource = builder->access_resource(node->inputs[j]);

      uint32_t resource_index = input_resource->outputHandle.index;
      FrameGraphResource* resource = builder->access_resource(input_resource->outputHandle);

      if (resource == nullptr)
      {
        continue;
      }

      resource->refCount--;

      if (!resource->resourceInfo.external && resource->refCount == 0)
      {
        assert(deallocations[resource_index].index == kInvalidIndex);
        deallocations[resource_index] = nodes[i];

        if (resource->type == kFrameGraphResourceTypeAttachment ||
            resource->type == kFrameGraphResourceTypeTexture)
        {
          free_list.push(resource->resourceInfo.texture.handle);
        }

#if FRAME_GRAPH_DEBUG
        printf("Output %s deallocated on node %d\n", resource->m_Name, nodes[i].index);
#endif
      }
    }
  }

  allocations.shutdown();
  deallocations.shutdown();
  free_list.shutdown();

  for (uint32_t i = 0; i < nodes.m_Size; ++i)
  {
    FrameGraphNode* node = builder->access_node(nodes[i]);
    assert(node->enabled);

    if (node->compute)
    {
      continue;
    }

    if (node->render_pass.index == kInvalidIndex)
    {
      create_render_pass(this, node);
    }

    if (node->framebuffer.index == kInvalidIndex)
    {
      create_framebuffer(this, node);
    }
  }
}

void FrameGraph::add_ui()
{
  for (uint32_t n = 0; n < nodes.m_Size; ++n)
  {
    FrameGraphNode* node = builder->access_node(nodes[n]);
    assert(node->enabled);

    node->graph_render_pass->add_ui();
  }
}

void FrameGraph::render(
    uint32_t current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene)
{
  for (uint32_t n = 0; n < nodes.m_Size; ++n)
  {
    FrameGraphNode* node = builder->access_node(nodes[n]);
    assert(node->enabled);

    if (node->compute)
    {
      gpu_commands->pushMarker(node->name);

      for (uint32_t i = 0; i < node->inputs.m_Size; ++i)
      {
        FrameGraphResource* input_resource = builder->access_resource(node->inputs[i]);
        FrameGraphResource* resource = builder->access_resource(input_resource->outputHandle);

        if (resource == nullptr || resource->resourceInfo.external)
        {
          continue;
        }

        if (input_resource->type == kFrameGraphResourceTypeTexture)
        {
          Texture* texture = (Texture*)gpu_commands->m_GpuDevice->m_Textures.accessResource(
              resource->resourceInfo.texture.handle.index);

          utilAddImageBarrier(
              gpu_commands->m_GpuDevice,
              gpu_commands->m_VulkanCmdBuffer,
              texture,
              RESOURCE_STATE_SHADER_RESOURCE,
              0,
              1,
              TextureFormat::hasDepth(texture->vkFormat));
        }
        else if (input_resource->type == kFrameGraphResourceTypeAttachment)
        {
          // TODO: what to do with attachments ?
          Texture* texture = (Texture*)gpu_commands->m_GpuDevice->m_Textures.accessResource(
              resource->resourceInfo.texture.handle.index);
          texture = texture;
        }
      }

      for (uint32_t o = 0; o < node->outputs.m_Size; ++o)
      {
        FrameGraphResource* resource = builder->access_resource(node->outputs[o]);

        if (resource->type == kFrameGraphResourceTypeAttachment)
        {
          Texture* texture = (Texture*)gpu_commands->m_GpuDevice->m_Textures.accessResource(
              resource->resourceInfo.texture.handle.index);

          if (TextureFormat::hasDepth(texture->vkFormat))
          {
            // Is this supported even ?
            assert(false);
          }
          else
          {
            utilAddImageBarrier(
                gpu_commands->m_GpuDevice,
                gpu_commands->m_VulkanCmdBuffer,
                texture,
                RESOURCE_STATE_UNORDERED_ACCESS,
                0,
                1,
                false);
          }
        }
      }

      node->graph_render_pass->pre_render(current_frame_index, gpu_commands, this, render_scene);
      node->graph_render_pass->render(current_frame_index, gpu_commands, render_scene);
      node->graph_render_pass->post_render(current_frame_index, gpu_commands, this, render_scene);

      gpu_commands->popMarker();
    }
    else if (node->ray_tracing)
    {
      gpu_commands->pushMarker(node->name);

      node->graph_render_pass->pre_render(current_frame_index, gpu_commands, this, render_scene);
      node->graph_render_pass->render(current_frame_index, gpu_commands, render_scene);
      node->graph_render_pass->post_render(current_frame_index, gpu_commands, this, render_scene);

      gpu_commands->popMarker();
    }
    else
    {
      gpu_commands->pushMarker(node->name);

      uint32_t width = 0;
      uint32_t height = 0;

      for (uint32_t i = 0; i < node->inputs.m_Size; ++i)
      {
        FrameGraphResource* input_resource = builder->access_resource(node->inputs[i]);
        FrameGraphResource* resource = builder->access_resource(input_resource->outputHandle);

        if (resource == nullptr || resource->resourceInfo.external)
        {
          continue;
        }

        if (input_resource->type == kFrameGraphResourceTypeTexture)
        {
          Texture* texture = (Texture*)gpu_commands->m_GpuDevice->m_Textures.accessResource(
              resource->resourceInfo.texture.handle.index);

          utilAddImageBarrier(
              gpu_commands->m_GpuDevice,
              gpu_commands->m_VulkanCmdBuffer,
              texture,
              RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
              0,
              1,
              TextureFormat::hasDepth(texture->vkFormat));
        }
        else if (input_resource->type == kFrameGraphResourceTypeAttachment)
        {
          Texture* texture = (Texture*)gpu_commands->m_GpuDevice->m_Textures.accessResource(
              resource->resourceInfo.texture.handle.index);

          width = texture->width;
          height = texture->height;

          // For textures that are read-write check if a transition is needed.
          if (!TextureFormat::hasDepthOrStencil(texture->vkFormat))
          {
            utilAddImageBarrier(
                gpu_commands->m_GpuDevice,
                gpu_commands->m_VulkanCmdBuffer,
                texture,
                RESOURCE_STATE_RENDER_TARGET,
                0,
                1,
                false);
          }
          else
          {
            utilAddImageBarrier(
                gpu_commands->m_GpuDevice,
                gpu_commands->m_VulkanCmdBuffer,
                texture,
                RESOURCE_STATE_DEPTH_WRITE,
                0,
                1,
                true);
          }
        }
      }

      for (uint32_t o = 0; o < node->outputs.m_Size; ++o)
      {
        FrameGraphResource* resource = builder->access_resource(node->outputs[o]);

        if (resource->type == kFrameGraphResourceTypeAttachment)
        {
          Texture* texture = (Texture*)gpu_commands->m_GpuDevice->m_Textures.accessResource(
              resource->resourceInfo.texture.handle.index);

          width = texture->width;
          height = texture->height;

          if (TextureFormat::hasDepth(texture->vkFormat))
          {
            utilAddImageBarrier(
                gpu_commands->m_GpuDevice,
                gpu_commands->m_VulkanCmdBuffer,
                texture,
                RESOURCE_STATE_DEPTH_WRITE,
                0,
                1,
                true);

            float* clear_color = resource->resourceInfo.texture.clear_values;
            gpu_commands->clearDepthStencil(clear_color[0], (uint8_t)clear_color[1]);
          }
          else
          {
            utilAddImageBarrier(
                gpu_commands->m_GpuDevice,
                gpu_commands->m_VulkanCmdBuffer,
                texture,
                RESOURCE_STATE_RENDER_TARGET,
                0,
                1,
                false);

            float* clear_color = resource->resourceInfo.texture.clear_values;
            gpu_commands->clear(clear_color[0], clear_color[1], clear_color[2], clear_color[3], o);
          }
        }
      }

      Rect2DInt scissor{0, 0, (uint16_t)width, (uint16_t)height};
      gpu_commands->setScissor(&scissor);

      Viewport viewport{};
      viewport.rect = {0, 0, (uint16_t)width, (uint16_t)height};
      viewport.minDepth = 0.0f;
      viewport.maxDepth = 1.0f;

      gpu_commands->setViewport(&viewport);

      node->graph_render_pass->pre_render(current_frame_index, gpu_commands, this, render_scene);

      gpu_commands->bindPass(node->render_pass, node->framebuffer, false);

      node->graph_render_pass->render(current_frame_index, gpu_commands, render_scene);

      gpu_commands->endCurrentRenderPass();

      node->graph_render_pass->post_render(current_frame_index, gpu_commands, this, render_scene);

      gpu_commands->popMarker();
    }
  }
}

void FrameGraph::on_resize(GpuDevice& gpu, uint32_t new_width, uint32_t new_height)
{
  for (uint32_t n = 0; n < nodes.m_Size; ++n)
  {
    FrameGraphNode* node = builder->access_node(nodes[n]);
    assert(node->enabled);

    gpu.resizeOutputTextures(node->framebuffer, new_width, new_height);

    node->graph_render_pass->on_resize(gpu, this, new_width, new_height);
  }
}

void FrameGraph::debug_ui()
{
  // TODO
}

void FrameGraph::add_node(FrameGraphNodeCreation& creation)
{
  FrameGraphNodeHandle handle = builder->create_node(creation);
  all_nodes.push(handle);
}

FrameGraphNode* FrameGraph::get_node(cstring name) { return builder->get_node(name); }

FrameGraphNode* FrameGraph::access_node(FrameGraphNodeHandle handle)
{
  return builder->access_node(handle);
}

void FrameGraph::add_resource(
    cstring name, FrameGraphResourceType type, FrameGraphResourceInfo resourceInfo)
{
  builder->add_resource(name, type, resourceInfo);
}

FrameGraphResource* FrameGraph::get_resource(cstring name) { return builder->get_resource(name); }

FrameGraphResource* FrameGraph::access_resource(FrameGraphResourceHandle handle)
{
  return builder->access_resource(handle);
}

// FrameGraphRenderPassCache /////////////////////////////////////////////////////////////

void FrameGraphRenderPassCache::init(Framework::Allocator* allocator)
{
  render_pass_map.init(allocator, FrameGraphBuilder::k_max_render_pass_count);
}

void FrameGraphRenderPassCache::shutdown() { render_pass_map.shutdown(); }

// FrameGraphResourceCache /////////////////////////////////////////////////////////////

void FrameGraphResourceCache::init(Framework::Allocator* allocator, GpuDevice* device_)
{
  device = device_;

  resources.init(allocator, FrameGraphBuilder::k_max_resources_count);
  resource_map.init(allocator, FrameGraphBuilder::k_max_resources_count);
}

void FrameGraphResourceCache::shutdown()
{
  FlatHashMapIterator it = resource_map.iteratorBegin();
  while (it.isValid())
  {

    uint32_t resource_index = resource_map.get(it);
    FrameGraphResource* resource = resources.get(resource_index);

    if ((resource->type == kFrameGraphResourceTypeTexture ||
         resource->type == kFrameGraphResourceTypeAttachment) &&
        (resource->resourceInfo.texture.handle.index > 0))
    {
      Texture* texture =
          (Texture*)device->m_Textures.accessResource(resource->resourceInfo.texture.handle.index);
      device->destroyTexture(texture->handle);
    }
    else if (
        (resource->type == kFrameGraphResourceTypeBuffer) &&
        (resource->resourceInfo.buffer.handle.index > 0))
    {
      Buffer* buffer =
          (Buffer*)device->m_Buffers.accessResource(resource->resourceInfo.buffer.handle.index);
      device->destroyBuffer(buffer->handle);
    }

    resource_map.iteratorAdvance(it);
  }

  resources.freeAllResources();
  resources.shutdown();
  resource_map.shutdown();
}

// FrameGraphNodeCache /////////////////////////////////////////////////////////////

void FrameGraphNodeCache::init(Framework::Allocator* allocator, GpuDevice* device_)
{
  device = device_;

  nodes.init(allocator, FrameGraphBuilder::k_max_nodes_count, sizeof(FrameGraphNode));
  node_map.init(allocator, FrameGraphBuilder::k_max_nodes_count);
}

void FrameGraphNodeCache::shutdown()
{
  nodes.freeAllResources();
  nodes.shutdown();
  node_map.shutdown();
}

// FrameGraphBuilder /////////////////////////////////////////////////////////////

void FrameGraphBuilder::init(GpuDevice* device_)
{
  device = device_;

  allocator = device->m_Allocator;

  resource_cache.init(allocator, device);
  node_cache.init(allocator, device);
  render_pass_cache.init(allocator);
}

void FrameGraphBuilder::shutdown()
{
  resource_cache.shutdown();
  node_cache.shutdown();
  render_pass_cache.shutdown();
}

FrameGraphResourceHandle FrameGraphBuilder::create_node_output(
    const FrameGraphResourceOutputCreation& creation, FrameGraphNodeHandle producer)
{
  FrameGraphResourceHandle resource_handle{kInvalidIndex};
  resource_handle.index = resource_cache.resources.obtainResource();

  if (resource_handle.index == kInvalidIndex)
  {
    return resource_handle;
  }

  FrameGraphResource* resource = resource_cache.resources.get(resource_handle.index);
  resource->m_Name = creation.name;
  resource->type = creation.type;

  if (creation.type != kFrameGraphResourceTypeReference)
  {
    resource->resourceInfo = creation.resourceInfo;
    resource->outputHandle = resource_handle;
    resource->producer = producer;
    resource->refCount = 0;

    FrameGraphNode* producer_node = access_node(producer);
    assert(producer_node != nullptr);

    if (producer_node->enabled)
    {
      // TODO: eventually we want to allow enabling/disabling a node at runtime.
      // We will need to patch the producer when the graph changes
      resource_cache.resource_map.insert(
          Framework::hashBytes((void*)resource->m_Name, strlen(creation.name)),
          resource_handle.index);
    }
  }

  return resource_handle;
}

FrameGraphResourceHandle
FrameGraphBuilder::create_node_input(const FrameGraphResourceInputCreation& creation)
{
  FrameGraphResourceHandle resource_handle = {kInvalidIndex};

  resource_handle.index = resource_cache.resources.obtainResource();

  if (resource_handle.index == kInvalidIndex)
  {
    return resource_handle;
  }

  FrameGraphResource* resource = resource_cache.resources.get(resource_handle.index);

  resource->resourceInfo = {};
  resource->producer.index = kInvalidIndex;
  resource->outputHandle.index = kInvalidIndex;
  resource->type = creation.type;
  resource->m_Name = creation.name;
  resource->refCount = 0;

  return resource_handle;
}

FrameGraphNodeHandle FrameGraphBuilder::create_node(const FrameGraphNodeCreation& creation)
{
  FrameGraphNodeHandle node_handle{kInvalidIndex};
  node_handle.index = node_cache.nodes.obtainResource();

  if (node_handle.index == kInvalidIndex)
  {
    return node_handle;
  }

  FrameGraphNode* node = (FrameGraphNode*)node_cache.nodes.accessResource(node_handle.index);
  node->name = creation.name;
  node->enabled = creation.enabled;
  node->compute = creation.compute;
  node->ray_tracing = creation.ray_tracing;
  node->inputs.init(allocator, creation.inputs.m_Size);
  node->outputs.init(allocator, creation.outputs.m_Size);
  node->edges.init(allocator, creation.outputs.m_Size);

  node->framebuffer = kInvalidFramebuffer;
  node->render_pass = {kInvalidIndex};

  node_cache.node_map.insert(
      Framework::hashBytes((void*)node->name, strlen(node->name)), node_handle.index);

  // NOTE: first create the outputs, then we can patch the input resources
  // with the right handles
  for (uint32_t i = 0; i < creation.outputs.m_Size; ++i)
  {
    const FrameGraphResourceOutputCreation& output_creation = creation.outputs[i];

    FrameGraphResourceHandle output = create_node_output(output_creation, node_handle);

    node->outputs.push(output);
  }

  for (uint32_t i = 0; i < creation.inputs.m_Size; ++i)
  {
    const FrameGraphResourceInputCreation& input_creation = creation.inputs[i];

    FrameGraphResourceHandle input_handle = create_node_input(input_creation);

    node->inputs.push(input_handle);
  }

  return node_handle;
}

FrameGraphNode* FrameGraphBuilder::get_node(cstring name)
{
  FlatHashMapIterator it = node_cache.node_map.find(Framework::hashCalculate(name));
  if (it.isInvalid())
  {
    return nullptr;
  }

  FrameGraphNode* node =
      (FrameGraphNode*)node_cache.nodes.accessResource(node_cache.node_map.get(it));

  return node;
}

FrameGraphNode* FrameGraphBuilder::access_node(FrameGraphNodeHandle handle)
{
  FrameGraphNode* node = (FrameGraphNode*)node_cache.nodes.accessResource(handle.index);

  return node;
}

void FrameGraphBuilder::add_resource(
    cstring name, FrameGraphResourceType type, FrameGraphResourceInfo resourceInfo)
{
  FlatHashMapIterator it = resource_cache.resource_map.find(Framework::hashCalculate(name));
  assert(it.isInvalid());

  FrameGraphResourceHandle resource_handle{kInvalidIndex};
  resource_handle.index = resource_cache.resources.obtainResource();

  if (resource_handle.index == kInvalidIndex)
  {
    return;
  }

  FrameGraphResource* resource = resource_cache.resources.get(resource_handle.index);
  resource->m_Name = name;
  resource->type = type;

  resource->resourceInfo = resourceInfo;
  resource->refCount = 0;

  resource_cache.resource_map.insert(
      Framework::hashBytes((void*)name, strlen(name)), resource_handle.index);
}

FrameGraphResource* FrameGraphBuilder::get_resource(cstring name)
{
  FlatHashMapIterator it = resource_cache.resource_map.find(Framework::hashCalculate(name));
  if (it.isInvalid())
  {
    return nullptr;
  }

  FrameGraphResource* resource = resource_cache.resources.get(resource_cache.resource_map.get(it));

  return resource;
}

FrameGraphResource* FrameGraphBuilder::access_resource(FrameGraphResourceHandle handle)
{
  FrameGraphResource* resource = resource_cache.resources.get(handle.index);

  return resource;
}

void FrameGraphBuilder::register_render_pass(cstring name, FrameGraphRenderPass* render_pass)
{
  uint64_t key = Framework::hashCalculate(name);

  FlatHashMapIterator it = render_pass_cache.render_pass_map.find(key);
  if (it.isValid())
  {
    return;
  }

  it = node_cache.node_map.find(key);
  if (it.isInvalid())
  {
    return;
  }

  render_pass_cache.render_pass_map.insert(key, render_pass);

  FrameGraphNode* node =
      (FrameGraphNode*)node_cache.nodes.accessResource(node_cache.node_map.get(it));
  node->graph_render_pass = render_pass;
}

FrameGraphResourceInfo& FrameGraphResourceInfo::set_external(bool value)
{
  external = value;
  return *this;
}

FrameGraphResourceInfo&
FrameGraphResourceInfo::set_buffer(size_t size, VkBufferUsageFlags flags, BufferHandle handle)
{
  buffer.size = size;
  buffer.flags = flags;
  buffer.handle = handle;
  return *this;
}

FrameGraphResourceInfo& FrameGraphResourceInfo::set_external_texture_2d(
    uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags flags, TextureHandle handle)
{

  texture.width = width;
  texture.height = height;
  texture.depth = 1;
  texture.format = format;
  texture.flags = flags;
  texture.handle = handle;

  external = true;

  return *this;
}

FrameGraphResourceInfo& FrameGraphResourceInfo::set_external_texture_3d(
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    VkFormat format,
    VkImageUsageFlags flags,
    TextureHandle handle)
{

  texture.width = width;
  texture.height = height;
  texture.depth = depth;
  texture.format = format;
  texture.flags = flags;
  texture.handle = handle;

  external = true;

  return *this;
}
//---------------------------------------------------------------------------//
} // namespace Graphics
