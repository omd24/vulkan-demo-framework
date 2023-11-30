#include "Graphics/GltfScene.hpp"

#include "Foundation/Time.hpp"

#include "Graphics/SceneGraph.hpp"
#include "Graphics/ImguiHelper.hpp"
#include "Graphics/AsynchronousLoader.hpp"

#include "Externals/cglm/struct/affine.h"
#include "Externals/cglm/struct/mat4.h"
#include "Externals/cglm/struct/vec3.h"
#include "Externals/cglm/struct/quat.h"

#include "Externals/imgui/imgui.h"
#include "Externals/stb_image.h"

namespace Graphics
{
//---------------------------------------------------------------------------//
void glTFScene::init(
    const char* filename,
    const char* path,
    Framework::Allocator* residentAllocator,
    Framework::StackAllocator* tempAllocator,
    AsynchronousLoader* asyncLoader)
{
  renderer = asyncLoader->renderer;
  enki::TaskScheduler* taskScheduler = asyncLoader->taskScheduler;
  size_t tempAllocatorInitialMarker = tempAllocator->getMarker();

  // Time statistics
  int64_t startSceneLoading = Time::getCurrentTime();

  gltfScene = gltfLoadFile(filename);

  int64_t endLoadingFile = Time::getCurrentTime();

  // Load all textures
  images.init(residentAllocator, gltfScene.imagesCount);

  Array<TextureCreation> tcs;
  tcs.init(tempAllocator, gltfScene.imagesCount, gltfScene.imagesCount);

  StringBuffer nameBuffer;
  nameBuffer.init(4096, tempAllocator);

  for (uint32_t imageIndex = 0; imageIndex < gltfScene.imagesCount; ++imageIndex)
  {
    glTF::Image& image = gltfScene.images[imageIndex];

    int comp, width, height;

    stbi_info(image.uri.m_Data, &width, &height, &comp);

    uint32_t mipLevels = 1;
    if (true)
    {
      uint32_t w = width;
      uint32_t h = height;

      while (w > 1 && h > 1)
      {
        w /= 2;
        h /= 2;

        ++mipLevels;
      }
    }

    TextureCreation tc;
    tc.setData(nullptr)
        .setFormatType(VK_FORMAT_R8G8B8A8_UNORM, TextureType::kTexture2D)
        .setFlags(mipLevels, 0)
        .setSize((uint16_t)width, (uint16_t)height, 1)
        .setName(image.uri.m_Data);
    RendererUtil::TextureResource* tr = renderer->createTexture(tc);
    assert(tr != nullptr);

    images.push(*tr);

    // Reconstruct file path
    char* fullFilename = nameBuffer.appendUseFormatted("%s%s", path, image.uri.m_Data);
    asyncLoader->requestTextureData(fullFilename, tr->m_Handle);
    // Reset name buffer
    nameBuffer.clear();
  }

  int64_t endLoadingTexturesFiles = Time::getCurrentTime();

  int64_t endCreatingTextures = Time::getCurrentTime();

  // Load all samplers
  samplers.init(residentAllocator, gltfScene.samplersCount);

  for (uint32_t samplerIndex = 0; samplerIndex < gltfScene.samplersCount; ++samplerIndex)
  {
    glTF::Sampler& sampler = gltfScene.samplers[samplerIndex];

    char* samplerName = nameBuffer.appendUseFormatted("sampler_%u", samplerIndex);

    SamplerCreation creation;
    switch (sampler.minFilter)
    {
    case glTF::Sampler::NEAREST:
      creation.minFilter = VK_FILTER_NEAREST;
      break;
    case glTF::Sampler::LINEAR:
      creation.minFilter = VK_FILTER_LINEAR;
      break;
    case glTF::Sampler::LINEAR_MIPMAP_NEAREST:
      creation.minFilter = VK_FILTER_LINEAR;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case glTF::Sampler::LINEAR_MIPMAP_LINEAR:
      creation.minFilter = VK_FILTER_LINEAR;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    case glTF::Sampler::NEAREST_MIPMAP_NEAREST:
      creation.minFilter = VK_FILTER_NEAREST;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case glTF::Sampler::NEAREST_MIPMAP_LINEAR:
      creation.minFilter = VK_FILTER_NEAREST;
      creation.mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    }

    creation.magFilter =
        sampler.magFilter == glTF::Sampler::Filter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

    switch (sampler.wrapS)
    {
    case glTF::Sampler::CLAMP_TO_EDGE:
      creation.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      break;
    case glTF::Sampler::MIRRORED_REPEAT:
      creation.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      break;
    case glTF::Sampler::REPEAT:
      creation.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      break;
    }

    switch (sampler.wrapT)
    {
    case glTF::Sampler::CLAMP_TO_EDGE:
      creation.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      break;
    case glTF::Sampler::MIRRORED_REPEAT:
      creation.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      break;
    case glTF::Sampler::REPEAT:
      creation.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      break;
    }

    creation.name = samplerName;

    RendererUtil::SamplerResource* sr = renderer->createSampler(creation);
    assert(sr != nullptr);

    samplers.push(*sr);
  }

  int64_t endCreatingSamplers = Time::getCurrentTime();

  // Temporary array of buffer data
  Array<void*> buffersData;
  buffersData.init(residentAllocator, gltfScene.buffersCount);

  for (uint32_t bufferIndex = 0; bufferIndex < gltfScene.buffersCount; ++bufferIndex)
  {
    glTF::Buffer& buffer = gltfScene.buffers[bufferIndex];

    FileReadResult bufferData = fileReadBinary(buffer.uri.m_Data, residentAllocator);
    buffersData.push(bufferData.data);
  }

  int64_t endReadingBuffersData = Time::getCurrentTime();

  // Load all buffers and initialize them with buffer data
  buffers.init(residentAllocator, gltfScene.bufferViewsCount);

  for (uint32_t bufferIndex = 0; bufferIndex < gltfScene.bufferViewsCount; ++bufferIndex)
  {
    glTF::BufferView& buffer = gltfScene.bufferViews[bufferIndex];

    int offset = buffer.byteOffset;
    if (offset == glTF::INVALID_INT_VALUE)
    {
      offset = 0;
    }

    uint8_t* bufferData = (uint8_t*)buffersData[buffer.buffer] + offset;

    // NOTE: the target attribute of a BufferView is not mandatory, so we prepare for both
    // uses
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    char* bufferName = buffer.name.m_Data;
    if (bufferName == nullptr)
    {
      bufferName = nameBuffer.appendUseFormatted("buffer_%u", bufferIndex);
    }

    RendererUtil::BufferResource* br = renderer->createBuffer(
        flags, ResourceUsageType::kImmutable, buffer.byteLength, bufferData, bufferName);
    assert(br != nullptr);

    buffers.push(*br);
  }

  for (uint32_t bufferIndex = 0; bufferIndex < gltfScene.buffersCount; ++bufferIndex)
  {
    void* buffer = buffersData[bufferIndex];
    residentAllocator->deallocate(buffer);
  }
  buffersData.shutdown();

  int64_t endCreatingBuffers = Time::getCurrentTime();

  // This is not needed anymore, free all temp memory after.
  // resourceNameBuffer.shutdown();
  tempAllocator->freeMarker(tempAllocatorInitialMarker);

  // Init runtime meshes
  meshes.init(residentAllocator, gltfScene.meshesCount);

  int64_t endLoading = Time::getCurrentTime();

  printf(
      "Loaded scene %s in %f seconds.\nStats:\n\tReading GLTF file %f seconds\n\tTextures Creating "
      "%f seconds\n\tCreating Samplers %f seconds\n\tReading Buffers Data %f seconds\n\tCreating "
      "Buffers %f seconds\n",
      filename,
      Time::deltaSeconds(startSceneLoading, endLoading),
      Time::deltaSeconds(startSceneLoading, endLoadingFile),
      Time::deltaSeconds(endLoadingFile, endCreatingTextures),
      Time::deltaSeconds(endCreatingTextures, endCreatingSamplers),
      Time::deltaSeconds(endCreatingSamplers, endReadingBuffersData),
      Time::deltaSeconds(endReadingBuffersData, endCreatingBuffers));
}
//---------------------------------------------------------------------------//
void glTFScene::shutdown(RendererUtil::Renderer* p_Renderer)
{
  GpuDevice& gpu = *p_Renderer->m_GpuDevice;

  // Unload animations
  for (uint32_t ai = 0; ai < animations.m_Size; ++ai)
  {
    Animation& animation = animations[ai];
    animation.channels.shutdown();

    for (uint32_t si = 0; si < animation.samplers.m_Size; ++si)
    {
      AnimationSampler& sampler = animation.samplers[si];
      sampler.keyFrames.shutdown();
      FRAMEWORK_FREE(sampler.data, residentAllocator);
    }
    animation.samplers.shutdown();
  }
  animations.shutdown();

  // Unload skins
  for (uint32_t si = 0; si < skins.m_Size; ++si)
  {
    Skin& skin = skins[si];
    skin.joints.shutdown();
    FRAMEWORK_FREE(skin.inverseBindMatrices, residentAllocator);

    renderer->m_GpuDevice->destroyBuffer(skin.jointTransforms);
  }
  skins.shutdown();

  // Unload meshes
  for (uint32_t meshIndex = 0; meshIndex < meshes.m_Size; ++meshIndex)
  {
    Mesh& mesh = meshes[meshIndex];

    gpu.destroyBuffer(mesh.pbrMaterial.materialBuffer);
    gpu.destroyDescriptorSet(mesh.pbrMaterial.descriptorSet);
  }

  gpu.destroyBuffer(sceneCb);

  for (uint32_t i = 0; i < images.m_Size; ++i)
  {
    p_Renderer->destroyTexture(&images[i]);
  }

  for (uint32_t i = 0; i < samplers.m_Size; ++i)
  {
    p_Renderer->destroySampler(&samplers[i]);
  }

  for (uint32_t i = 0; i < buffers.m_Size; ++i)
  {
    p_Renderer->destroyBuffer(&buffers[i]);
  }

  meshes.shutdown();

  namesBuffer.shutdown();

  // Free scene buffers
  samplers.shutdown();
  images.shutdown();
  buffers.shutdown();

  // NOTE: we can't destroy this sooner as textures and buffers
  // hold a pointer to the names stored here
  gltfFree(gltfScene);
}
//---------------------------------------------------------------------------//
void glTFScene::prepareDraws(
    RendererUtil::Renderer* p_Renderer,
    Framework::StackAllocator* p_ScratchAllocator,
    SceneGraph* p_SceneGraph)
{
  using namespace RendererUtil;

  sceneGraph = p_SceneGraph;

  size_t cachedScratchSize = p_ScratchAllocator->getMarker();

  // Scene constant buffer
  BufferCreation bufferCreation;
  bufferCreation.reset()
      .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, sizeof(GpuSceneData))
      .setName("scene_cb");
  sceneCb = p_Renderer->m_GpuDevice->createBuffer(bufferCreation);

  // Create material
  const uint64_t hashedName = Framework::hashCalculate("main");
  GpuTechnique* mainTechnique = p_Renderer->m_ResourceCache.m_Techniques.get(hashedName);

  MaterialCreation materialCreation;
  materialCreation.setName("material_no_cull_opaque").setTechnique(mainTechnique).setRenderIndex(0);

  RendererUtil::Material* pbrMaterial = p_Renderer->createMaterial(materialCreation);

  glTF::Scene& rootGltfScene = gltfScene.scenes[gltfScene.scene];

  //
  Array<int> nodesToVisit;
  nodesToVisit.init(p_ScratchAllocator, 4);

  // Calculate total node count: add first the root nodes.
  uint32_t totalNodeCount = rootGltfScene.nodesCount;

  // Add initial nodes
  for (uint32_t nodeIndex = 0; nodeIndex < rootGltfScene.nodesCount; ++nodeIndex)
  {
    const int node = rootGltfScene.nodes[nodeIndex];
    nodesToVisit.push(node);
  }
  // Visit nodes
  while (nodesToVisit.m_Size)
  {
    int nodeIndex = nodesToVisit.front();
    nodesToVisit.deleteSwap(0);

    glTF::Node& node = gltfScene.nodes[nodeIndex];
    for (uint32_t ch = 0; ch < node.childrenCount; ++ch)
    {
      const int childrenIndex = node.children[ch];
      nodesToVisit.push(childrenIndex);
    }

    // Add only children nodes to the count, as the current node is
    // already calculated when inserting it.
    totalNodeCount += node.childrenCount;
  }

  sceneGraph->resize(totalNodeCount);

  // Populate scene graph: visit again
  nodesToVisit.clear();
  // Add initial nodes
  for (uint32_t nodeIndex = 0; nodeIndex < rootGltfScene.nodesCount; ++nodeIndex)
  {
    const int node = rootGltfScene.nodes[nodeIndex];
    nodesToVisit.push(node);
  }

  while (nodesToVisit.m_Size)
  {
    int nodeIndex = nodesToVisit.front();
    nodesToVisit.deleteSwap(0);

    glTF::Node& node = gltfScene.nodes[nodeIndex];

    // Compute local transform: read either raw matrix or individual Scale/Rotation/Translation
    // components
    if (node.matrixCount)
    {
      // CGLM and glTF have the same matrix layout, just memcopy it
      memcpy(&sceneGraph->localMatrices[nodeIndex], node.matrix, sizeof(mat4s));
      sceneGraph->updatedNodes.setBit(nodeIndex);
    }
    else
    {
      // Handle individual transform components: SRT (scale, rotation, translation)
      vec3s nodeScale{1.0f, 1.0f, 1.0f};
      if (node.scaleCount)
      {
        assert(node.scaleCount == 3);
        nodeScale = vec3s{node.scale[0], node.scale[1], node.scale[2]};
      }
      mat4s scaleMatrix = glms_scale_make(nodeScale);

      vec3s translation{0.f, 0.f, 0.f};
      if (node.translationCount)
      {
        assert(node.translationCount == 3);
        translation = vec3s{node.translation[0], node.translation[1], node.translation[2]};
      }
      mat4s translation_matrix = glms_translate_make(translation);
      // Rotation is written as a plain quaternion
      versors rotation = glms_quat_identity();
      if (node.rotationCount)
      {
        assert(node.rotationCount == 4);
        rotation =
            glms_quat_init(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]);
      }
      // Final SRT composition
      const mat4s localMatrix =
          glms_mat4_mul(glms_mat4_mul(scaleMatrix, glms_quat_mat4(rotation)), translation_matrix);
      sceneGraph->setLocalMatrix(nodeIndex, localMatrix);
    }

    // Handle parent-relationship
    if (node.childrenCount)
    {
      const Hierarchy& nodeHierarchy = sceneGraph->nodesHierarchy[nodeIndex];

      for (uint32_t ch = 0; ch < node.childrenCount; ++ch)
      {
        const int childrenIndex = node.children[ch];
        Hierarchy& childrenHierarchy = sceneGraph->nodesHierarchy[childrenIndex];
        sceneGraph->setHierarchy(childrenIndex, nodeIndex, nodeHierarchy.level + 1);

        nodesToVisit.push(childrenIndex);
      }
    }

    // Cache node name
    sceneGraph->setDebugData(nodeIndex, node.name.m_Data);

    if (node.mesh == glTF::INVALID_INT_VALUE)
    {
      continue;
    }

    glTF::Mesh& gltfMesh = gltfScene.meshes[node.mesh];

    // Gltf primitives are conceptually submeshes.
    for (uint32_t primitive_index = 0; primitive_index < gltfMesh.primitivesCount;
         ++primitive_index)
    {
      Mesh mesh{};
      // Assign scene graph node index
      mesh.sceneGraphNodeIndex = nodeIndex;

      glTF::MeshPrimitive& meshPrimitive = gltfMesh.primitives[primitive_index];

      const int positionAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "POSITION");
      const int tangentAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "TANGENT");
      const int normalAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "NORMAL");
      const int texcoordAccessorIndex = gltfGetAttributeAccessorIndex(
          meshPrimitive.attributes, meshPrimitive.attributeCount, "TEXCOORD_0");

      getMeshVertexBuffer(
          positionAccessorIndex,
          0,
          mesh.positionBuffer,
          mesh.positionOffset,
          mesh.pbrMaterial.flags);
      getMeshVertexBuffer(
          tangentAccessorIndex,
          DrawFlagsHasTangents,
          mesh.tangentBuffer,
          mesh.tangentOffset,
          mesh.pbrMaterial.flags);
      getMeshVertexBuffer(
          normalAccessorIndex,
          DrawFlagsHasNormals,
          mesh.normalBuffer,
          mesh.normalOffset,
          mesh.pbrMaterial.flags);
      getMeshVertexBuffer(
          texcoordAccessorIndex,
          DrawFlagsHasTexCoords,
          mesh.texcoordBuffer,
          mesh.texcoordOffset,
          mesh.pbrMaterial.flags);

      // Read skinning data
      mesh.skinIndex = INT_MAX;
      if (node.skin != glTF::INVALID_INT_VALUE)
      {
        assert(node.skin < skins.m_Size);
        const int jointsAccessorIndex = gltfGetAttributeAccessorIndex(
            meshPrimitive.attributes, meshPrimitive.attributeCount, "JOINTS_0");
        const int weightsAccessorIndex = gltfGetAttributeAccessorIndex(
            meshPrimitive.attributes, meshPrimitive.attributeCount, "WEIGHTS_0");

        getMeshVertexBuffer(
            jointsAccessorIndex,
            DrawFlagsHasJoints,
            mesh.jointsBuffer,
            mesh.jointsOffset,
            mesh.pbrMaterial.flags);
        getMeshVertexBuffer(
            weightsAccessorIndex,
            DrawFlagsHasWeights,
            mesh.weightsBuffer,
            mesh.weightsOffset,
            mesh.pbrMaterial.flags);

        mesh.skinIndex = node.skin;
      }

      // Create index buffer
      glTF::Accessor& indicesAccessor = gltfScene.accessors[meshPrimitive.indices];
      assert(
          indicesAccessor.componentType == glTF::Accessor::ComponentType::UNSIGNED_SHORT ||
          indicesAccessor.componentType == glTF::Accessor::ComponentType::UNSIGNED_INT);
      mesh.indexType =
          (indicesAccessor.componentType == glTF::Accessor::ComponentType::UNSIGNED_SHORT)
              ? VK_INDEX_TYPE_UINT16
              : VK_INDEX_TYPE_UINT32;

      glTF::BufferView& indicesBufferView = gltfScene.bufferViews[indicesAccessor.bufferView];
      RendererUtil::BufferResource& indicesBufferGpu = buffers[indicesBufferView.buffer];
      mesh.indexBuffer = indicesBufferGpu.m_Handle;
      mesh.indexOffset =
          glTF::getDataOffset(indicesAccessor.byteOffset, indicesBufferView.byteOffset);
      mesh.primitiveCount = indicesAccessor.count;

      // Read pbr material data
      if (meshPrimitive.material != glTF::INVALID_INT_VALUE)
      {
        glTF::Material& material = gltfScene.materials[meshPrimitive.material];
        fillPbrMaterial(*p_Renderer, material, mesh.pbrMaterial);
      }

      // Create material buffer
      BufferCreation bufferCreation;
      bufferCreation.reset()
          .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, sizeof(GpuMeshData))
          .setName("mesh_data");
      mesh.pbrMaterial.materialBuffer = p_Renderer->m_GpuDevice->createBuffer(bufferCreation);

      DescriptorSetCreation dsCreation{};
      uint32_t passIndex = 0;
      if (mesh.hasSkinning())
      {
        passIndex =
            mainTechnique->nameHashToIndex.get(hashCalculate("transparent_skinning_no_cull"));
      }
      else
      {
        passIndex = mainTechnique->nameHashToIndex.get(hashCalculate("transparent_no_cull"));
      }

      DescriptorSetLayoutHandle layout = renderer->m_GpuDevice->getDescriptorSetLayout(
          mainTechnique->passes[passIndex].pipeline, kMaterialDescriptorSetIndex);
      dsCreation.buffer(sceneCb, 0).buffer(mesh.pbrMaterial.materialBuffer, 2).setLayout(layout);

      if (mesh.hasSkinning())
      {
        dsCreation.buffer(skins[mesh.skinIndex].jointTransforms, 3);
      }
      mesh.pbrMaterial.descriptorSet = p_Renderer->m_GpuDevice->createDescriptorSet(dsCreation);

      mesh.pbrMaterial.material = pbrMaterial;

      meshes.push(mesh);
    }
  }

  // qsort(meshes.m_Data, meshes.m_Size, sizeof(Mesh), gltfMeshMaterialCompare);

  p_ScratchAllocator->freeMarker(cachedScratchSize);
}

//---------------------------------------------------------------------------//
void glTFScene::getMeshVertexBuffer(
    int accessorIndex,
    uint32_t flag,
    BufferHandle& outBufferHandle,
    uint32_t& outBufferOffset,
    uint32_t& outFlags)
{
  if (accessorIndex != -1)
  {
    glTF::Accessor& bufferAccessor = gltfScene.accessors[accessorIndex];
    glTF::BufferView& bufferView = gltfScene.bufferViews[bufferAccessor.bufferView];
    RendererUtil::BufferResource& bufferGpu = buffers[bufferAccessor.bufferView];

    outBufferHandle = bufferGpu.m_Handle;
    outBufferOffset = glTF::getDataOffset(bufferAccessor.byteOffset, bufferView.byteOffset);

    outFlags |= flag;
  }
}
//---------------------------------------------------------------------------//
uint16_t glTFScene::getMaterialTexture(GpuDevice& gpu, Framework::glTF::TextureInfo* textureInfo)
{
  if (textureInfo != nullptr)
  {
    glTF::Texture& gltfTexture = gltfScene.textures[textureInfo->index];
    RendererUtil::TextureResource& textureGpu = images[gltfTexture.source];
    RendererUtil::SamplerResource& samplerGpu = samplers[gltfTexture.sampler];

    gpu.linkTextureSampler(textureGpu.m_Handle, samplerGpu.m_Handle);

    return textureGpu.m_Handle.index;
  }
  else
  {
    return kInvalidSceneTextureIndex;
  }
}
//---------------------------------------------------------------------------//
uint16_t glTFScene::getMaterialTexture(GpuDevice& gpu, int gltfTextureIndex)
{
  if (gltfTextureIndex >= 0)
  {
    glTF::Texture& gltfTexture = gltfScene.textures[gltfTextureIndex];
    RendererUtil::TextureResource& textureGpu = images[gltfTexture.source];
    RendererUtil::SamplerResource& samplerGpu = samplers[gltfTexture.sampler];

    gpu.linkTextureSampler(textureGpu.m_Handle, samplerGpu.m_Handle);

    return textureGpu.m_Handle.index;
  }
  else
  {
    return kInvalidSceneTextureIndex;
  }
}
//---------------------------------------------------------------------------//
void glTFScene::fillPbrMaterial(
    RendererUtil::Renderer& p_Renderer,
    Framework::glTF::Material& p_Material,
    PBRMaterial& p_PbrMaterial)
{
  GpuDevice& gpu = *p_Renderer.m_GpuDevice;

  // Handle flags
  if (p_Material.alphaMode.m_Data != nullptr && strcmp(p_Material.alphaMode.m_Data, "MASK") == 0)
  {
    p_PbrMaterial.flags |= DrawFlagsAlphaMask;
  }
  else if (
      p_Material.alphaMode.m_Data != nullptr && strcmp(p_Material.alphaMode.m_Data, "BLEND") == 0)
  {
    p_PbrMaterial.flags |= DrawFlagsTransparent;
  }

  p_PbrMaterial.flags |= p_Material.doubleSided ? DrawFlagsDoubleSided : 0;
  // Alpha cutoff
  p_PbrMaterial.alphaCutoff =
      p_Material.alphaCutoff != glTF::INVALID_FLOAT_VALUE ? p_Material.alphaCutoff : 1.f;

  if (p_Material.pbrMetallicRoughness != nullptr)
  {
    if (p_Material.pbrMetallicRoughness->baseColorFactorCount != 0)
    {
      assert(p_Material.pbrMetallicRoughness->baseColorFactorCount == 4);

      memcpy(
          p_PbrMaterial.baseColorFactor.raw,
          p_Material.pbrMetallicRoughness->baseColorFactor,
          sizeof(vec4s));
    }
    else
    {
      p_PbrMaterial.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    p_PbrMaterial.metallicRoughnessOcclusionFactor.x =
        p_Material.pbrMetallicRoughness->roughnessFactor != glTF::INVALID_FLOAT_VALUE
            ? p_Material.pbrMetallicRoughness->roughnessFactor
            : 1.f;
    p_PbrMaterial.metallicRoughnessOcclusionFactor.y =
        p_Material.pbrMetallicRoughness->metallicFactor != glTF::INVALID_FLOAT_VALUE
            ? p_Material.pbrMetallicRoughness->metallicFactor
            : 1.f;

    p_PbrMaterial.diffuseTextureIndex =
        getMaterialTexture(gpu, p_Material.pbrMetallicRoughness->baseColorTexture);
    p_PbrMaterial.roughnessTextureIndex =
        getMaterialTexture(gpu, p_Material.pbrMetallicRoughness->metallicRoughnessTexture);
  }

  if (p_Material.emissiveTexture != nullptr)
  {
    p_PbrMaterial.emissiveTextureIndex = getMaterialTexture(gpu, p_Material.emissiveTexture);
  }

  if (p_Material.emissiveFactorCount != 0)
  {
    assert(p_Material.emissiveFactorCount == 3);

    memcpy(p_PbrMaterial.emissiveFactor.raw, p_Material.emissiveFactor, sizeof(vec3s));
  }
  else
  {
    p_PbrMaterial.emissiveFactor = {0.0f, 0.0f, 0.0f};
  }

  p_PbrMaterial.occlusionTextureIndex = getMaterialTexture(
      gpu, (p_Material.occlusionTexture != nullptr) ? p_Material.occlusionTexture->index : -1);
  p_PbrMaterial.normalTextureIndex = getMaterialTexture(
      gpu, (p_Material.normalTexture != nullptr) ? p_Material.normalTexture->index : -1);

  if (p_Material.occlusionTexture != nullptr)
  {
    if (p_Material.occlusionTexture->strength != glTF::INVALID_FLOAT_VALUE)
    {
      p_PbrMaterial.metallicRoughnessOcclusionFactor.z = p_Material.occlusionTexture->strength;
    }
    else
    {
      p_PbrMaterial.metallicRoughnessOcclusionFactor.z = 1.0f;
    }
  }
}
//---------------------------------------------------------------------------//
} // namespace Graphics
