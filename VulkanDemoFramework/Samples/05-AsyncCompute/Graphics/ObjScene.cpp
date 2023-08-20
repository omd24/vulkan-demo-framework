#include "Graphics/ObjScene.hpp"
#include "Graphics/ImguiHelper.hpp"
#include "Graphics/SceneGraph.hpp"
#include "Graphics/AsynchronousLoader.hpp"

#include "Foundation/File.hpp"
#include "Foundation/Time.hpp"

#include "Externals/stb_image.h"

#include "Externals/cglm/struct/affine.h"
#include "Externals/cglm/struct/mat4.h"
#include "Externals/cglm/struct/vec3.h"

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace Graphics
{

static bool isSharedVertex(PhysicsVertex* vertices, PhysicsVertex& src, uint32_t dst)
{
  float maxDistance = 0.0f;
  float minDistance = 10000.0f;

  for (uint32_t j = 0; j < src.jointCount; ++j)
  {
    PhysicsVertex& jointVertex = vertices[src.joints[j].vertexIndex];
    float distance = glms_vec3_distance(src.startPosition, jointVertex.startPosition);

    maxDistance = (distance > maxDistance) ? distance : maxDistance;
    minDistance = (distance < minDistance) ? distance : minDistance;
  }

  // NOTE: this is to add joints with the next-next vertex either in horizontal
  // or vertical direction.
  minDistance *= 2;
  maxDistance = (minDistance > maxDistance) ? minDistance : maxDistance;

  PhysicsVertex& dstVertex = vertices[dst];
  float distance = glms_vec3_distance(src.startPosition, dstVertex.startPosition);

  // NOTE: this only works if we work with a plane with equal size subdivision
  return (distance <= maxDistance);
}

void ObjScene::init(
    const char* filename,
    const char* path,
    Framework::Allocator* residentAllocator_,
    Framework::StackAllocator* tempAllocator,
    AsynchronousLoader* asyncLoader_)
{
  asyncLoader = asyncLoader_;
  residentAllocator = residentAllocator_;
  renderer = asyncLoader->renderer;

  size_t tempAllocatorInitialMarker = tempAllocator->getMarker();

  // Time statistics
  int64_t startSceneLoading = Time::getCurrentTime();

  assimpScene = aiImportFile(
      filename,
      aiProcess_CalcTangentSpace | aiProcess_GenNormals | aiProcess_Triangulate |
          aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);

  int64_t endLoadingFile = Time::getCurrentTime();

  // If the import failed, report it
  if (assimpScene == nullptr)
  {
    assert(false);
    return;
  }

  SamplerCreation samplerCreation{};
  samplerCreation.setAddressModeUV(VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT)
      .setMinMagMip(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR);
  sampler = renderer->createSampler(samplerCreation);

  images.init(residentAllocator, 1024);

  Array<PBRMaterial> materials;
  materials.init(residentAllocator, assimpScene->mNumMaterials);

  for (uint32_t materialIndex = 0; materialIndex < assimpScene->mNumMaterials; ++materialIndex)
  {
    aiMaterial* material = assimpScene->mMaterials[materialIndex];

    PBRMaterial graphicsMaterial{};

    aiString texture_file;

    if (aiGetMaterialString(material, AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0), &texture_file) ==
        AI_SUCCESS)
    {
      graphicsMaterial.diffuseTextureIndex = loadTexture(texture_file.C_Str(), path, tempAllocator);
    }
    else
    {
      graphicsMaterial.diffuseTextureIndex = kInvalidSceneTextureIndex;
    }

    if (aiGetMaterialString(material, AI_MATKEY_TEXTURE(aiTextureType_NORMALS, 0), &texture_file) ==
        AI_SUCCESS)
    {
      graphicsMaterial.normalTextureIndex = loadTexture(texture_file.C_Str(), path, tempAllocator);
    }
    else
    {
      graphicsMaterial.normalTextureIndex = kInvalidSceneTextureIndex;
    }

    graphicsMaterial.roughnessTextureIndex = kInvalidSceneTextureIndex;
    graphicsMaterial.occlusionTextureIndex = kInvalidSceneTextureIndex;

    aiColor4D color;
    if (aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS)
    {
      graphicsMaterial.diffuseColour = {color.r, color.g, color.b, 1.0f};
    }

    if (aiGetMaterialColor(material, AI_MATKEY_COLOR_AMBIENT, &color) == AI_SUCCESS)
    {
      graphicsMaterial.ambientColour = {color.r, color.g, color.b};
    }

    if (aiGetMaterialColor(material, AI_MATKEY_COLOR_SPECULAR, &color) == AI_SUCCESS)
    {
      graphicsMaterial.specularColour = {color.r, color.g, color.b};
    }

    float fValue;
    if (aiGetMaterialFloat(material, AI_MATKEY_SHININESS, &fValue) == AI_SUCCESS)
    {
      graphicsMaterial.specularExp = fValue;
    }

    if (aiGetMaterialFloat(material, AI_MATKEY_OPACITY, &fValue) == AI_SUCCESS)
    {
      graphicsMaterial.diffuseColour.w = fValue;
    }

    materials.push(graphicsMaterial);
  }

  int64_t endLoadingTexturesFiles = Time::getCurrentTime();

  int64_t endCreatingTextures = Time::getCurrentTime();

  const uint32_t kNumBuffers = 5;
  cpuBuffers.init(residentAllocator, kNumBuffers);
  gpuBuffers.init(residentAllocator, kNumBuffers);

  // Init runtime meshes
  meshes.init(residentAllocator, assimpScene->mNumMeshes);

  Array<vec3s> positions;
  positions.init(residentAllocator, FRAMEWORK_KILO(64));
  size_t positionsOffset = 0;

  Array<vec3s> tangents;
  tangents.init(residentAllocator, FRAMEWORK_KILO(64));
  size_t tangentsOffset = 0;

  Array<vec3s> normals;
  normals.init(residentAllocator, FRAMEWORK_KILO(64));
  size_t normalsOffset = 0;

  Array<vec2s> uvCoords;
  uvCoords.init(residentAllocator, FRAMEWORK_KILO(64));
  size_t uvCoordsOffset = 0;

  Array<uint32_t> indices;
  indices.init(residentAllocator, FRAMEWORK_KILO(64));
  size_t indicesOffset = 0;

  for (uint32_t meshIndex = 0; meshIndex < assimpScene->mNumMeshes; ++meshIndex)
  {
    aiMesh* mesh = assimpScene->mMeshes[meshIndex];

    Mesh renderMesh{};
    PhysicsMesh* physicsMesh = (PhysicsMesh*)residentAllocator->allocate(sizeof(PhysicsMesh), 64);

    physicsMesh->vertices.init(residentAllocator, mesh->mNumVertices);

    assert((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) != 0);

    for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
    {
      vec3s position{
          mesh->mVertices[vertexIndex].x,
          mesh->mVertices[vertexIndex].y,
          mesh->mVertices[vertexIndex].z};

      positions.push(position);

      PhysicsVertex physicsVertex{};
      physicsVertex.startPosition = position;
      physicsVertex.previousPosition = position;
      physicsVertex.position = position;
      physicsVertex.mass = 1.0f;
      physicsVertex.fixed = false;

      vec3s normal = vec3s{
          mesh->mNormals[vertexIndex].x,
          mesh->mNormals[vertexIndex].y,
          mesh->mNormals[vertexIndex].z};

      normals.push(normal);

      physicsVertex.normal = normal;

      tangents.push(vec3s{
          mesh->mTangents[vertexIndex].x,
          mesh->mTangents[vertexIndex].y,
          mesh->mTangents[vertexIndex].z});

      uvCoords.push(vec2s{
          mesh->mTextureCoords[0][vertexIndex].x,
          mesh->mTextureCoords[0][vertexIndex].y,
      });

      physicsMesh->vertices.push(physicsVertex);
    }

    for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
    {
      assert(mesh->mFaces[faceIndex].mNumIndices == 3);

      uint32_t indexA = mesh->mFaces[faceIndex].mIndices[0];
      uint32_t indexB = mesh->mFaces[faceIndex].mIndices[1];
      uint32_t indexC = mesh->mFaces[faceIndex].mIndices[2];

      indices.push(indexA);
      indices.push(indexB);
      indices.push(indexC);

      // NOTE: compute cloth joints

      PhysicsVertex& vertexA = physicsMesh->vertices[indexA];
      vertexA.addJoint(indexB);
      vertexA.addJoint(indexC);

      PhysicsVertex& vertexB = physicsMesh->vertices[indexB];
      vertexB.addJoint(indexA);
      vertexB.addJoint(indexC);

      PhysicsVertex& vertexC = physicsMesh->vertices[indexC];
      vertexC.addJoint(indexA);
      vertexC.addJoint(indexB);
    }

    for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
    {
      uint32_t indexA = mesh->mFaces[faceIndex].mIndices[0];
      uint32_t indexB = mesh->mFaces[faceIndex].mIndices[1];
      uint32_t indexC = mesh->mFaces[faceIndex].mIndices[2];

      PhysicsVertex& vertexA = physicsMesh->vertices[indexA];

      PhysicsVertex& vertexB = physicsMesh->vertices[indexB];

      PhysicsVertex& vertexC = physicsMesh->vertices[indexC];

      // NOTE: check for adjacent triangles to get diagonal joints
      for (uint32_t otherFaceIndex = 0; otherFaceIndex < mesh->mNumFaces; ++otherFaceIndex)
      {
        if (otherFaceIndex == faceIndex)
        {
          continue;
        }

        uint32_t otherIndexA = mesh->mFaces[otherFaceIndex].mIndices[0];
        uint32_t otherIndexB = mesh->mFaces[otherFaceIndex].mIndices[1];
        uint32_t otherIndexC = mesh->mFaces[otherFaceIndex].mIndices[2];

        // check for vertexA
        if (otherIndexA == indexB && otherIndexB == indexC)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexA, otherIndexC))
          {
            vertexA.addJoint(otherIndexC);
          }
        }
        if (otherIndexA == indexC && otherIndexB == indexB)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexA, otherIndexC))
          {
            vertexA.addJoint(otherIndexC);
          }
        }
        if (otherIndexA == indexB && otherIndexC == indexC)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexA, otherIndexB))
          {
            vertexA.addJoint(otherIndexB);
          }
        }
        if (otherIndexA == indexC && otherIndexC == indexB)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexA, otherIndexB))
          {
            vertexA.addJoint(otherIndexB);
          }
        }
        if (otherIndexC == indexB && otherIndexB == indexC)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexA, otherIndexA))
          {
            vertexA.addJoint(otherIndexA);
          }
        }
        if (otherIndexC == indexC && otherIndexB == indexB)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexA, otherIndexA))
          {
            vertexA.addJoint(otherIndexA);
          }
        }

        // check for vertexB
        if (otherIndexA == indexA && otherIndexB == indexC)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexB, otherIndexC))
          {
            vertexB.addJoint(otherIndexC);
          }
        }
        if (otherIndexA == indexC && otherIndexB == indexA)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexB, otherIndexC))
          {
            vertexB.addJoint(otherIndexC);
          }
        }
        if (otherIndexA == indexA && otherIndexC == indexC)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexB, otherIndexB))
          {
            vertexB.addJoint(otherIndexB);
          }
        }
        if (otherIndexA == indexC && otherIndexC == indexA)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexB, otherIndexB))
          {
            vertexB.addJoint(otherIndexB);
          }
        }
        if (otherIndexC == indexA && otherIndexB == indexC)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexB, otherIndexA))
          {
            vertexB.addJoint(otherIndexA);
          }
        }
        if (otherIndexC == indexC && otherIndexB == indexA)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexB, otherIndexA))
          {
            vertexB.addJoint(otherIndexA);
          }
        }

        // check for vertexC
        if (otherIndexA == indexA && otherIndexB == indexB)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexC, otherIndexC))
          {
            vertexC.addJoint(otherIndexC);
          }
        }
        if (otherIndexA == indexB && otherIndexB == indexA)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexC, otherIndexC))
          {
            vertexC.addJoint(otherIndexC);
          }
        }
        if (otherIndexA == indexA && otherIndexC == indexB)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexC, otherIndexB))
          {
            vertexC.addJoint(otherIndexB);
          }
        }
        if (otherIndexA == indexB && otherIndexC == indexA)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexC, otherIndexB))
          {
            vertexC.addJoint(otherIndexB);
          }
        }

        if (otherIndexC == indexA && otherIndexB == indexB)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexC, otherIndexA))
          {
            vertexC.addJoint(otherIndexA);
          }
        }
        if (otherIndexC == indexB && otherIndexB == indexA)
        {
          if (isSharedVertex(physicsMesh->vertices.m_Data, vertexC, otherIndexA))
          {
            vertexC.addJoint(otherIndexA);
          }
        }
      }
    }

    renderMesh.positionOffset = positionsOffset;
    positionsOffset = positions.m_Size * sizeof(vec3s);

    renderMesh.tangentOffset = tangentsOffset;
    tangentsOffset = tangents.m_Size * sizeof(vec3s);

    renderMesh.normalOffset = normalsOffset;
    normalsOffset = normals.m_Size * sizeof(vec3s);

    renderMesh.texcoordOffset = uvCoordsOffset;
    uvCoordsOffset = uvCoords.m_Size * sizeof(vec2s);

    renderMesh.indexOffset = indicesOffset;
    indicesOffset = indices.m_Size * sizeof(uint32_t);
    renderMesh.indexType = VK_INDEX_TYPE_UINT32;

    renderMesh.primitiveCount = mesh->mNumFaces * 3;

    renderMesh.physicsMesh = physicsMesh;

    renderMesh.pbrMaterial = materials[mesh->mMaterialIndex];
    renderMesh.pbrMaterial.flags = DrawFlagsHasNormals;
    renderMesh.pbrMaterial.flags |= DrawFlagsHasTangents;
    renderMesh.pbrMaterial.flags |= DrawFlagsHasTexCoords;

    {
      BufferCreation creation{};
      creation
          .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, sizeof(GpuMeshData))
          .setName("mesh_data");

      renderMesh.pbrMaterial.materialBuffer = renderer->m_GpuDevice->createBuffer(creation);
    }

    // Physics data
    {
      BufferCreation creation{};
      size_t bufferSize =
          positions.m_Size * sizeof(PhysicsVertexGpuData) + sizeof(PhysicsMeshGpuData);
      creation.set(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::kImmutable, bufferSize)
          .setData(nullptr)
          .setName("physicsMeshDataCpu")
          .setPersistent(true);

      BufferHandle cpuBuffer = renderer->m_GpuDevice->createBuffer(creation);

      Buffer* physicsVertexBuffer =
          (Buffer*)renderer->m_GpuDevice->m_Buffers.accessResource(cpuBuffer.index);

      PhysicsMeshGpuData* meshData = (PhysicsMeshGpuData*)(physicsVertexBuffer->mappedData);
      meshData->indexCount = renderMesh.primitiveCount;
      meshData->vertexCount = positions.m_Size;

      PhysicsVertexGpuData* vertexData =
          (PhysicsVertexGpuData*)(physicsVertexBuffer->mappedData + sizeof(PhysicsMeshGpuData));

      Array<VkDrawIndirectCommand> indirectCommands;
      indirectCommands.init(
          residentAllocator, physicsMesh->vertices.m_Size, physicsMesh->vertices.m_Size);

      // TODO: some of these might change at runtime
      for (uint32_t vertexIndex = 0; vertexIndex < physicsMesh->vertices.m_Size; ++vertexIndex)
      {
        PhysicsVertex& cpuData = physicsMesh->vertices[vertexIndex];

        VkDrawIndirectCommand& indirectCommand = indirectCommands[vertexIndex];

        PhysicsVertexGpuData gpuData{};
        gpuData.position = cpuData.position;
        gpuData.startPosition = cpuData.startPosition;
        gpuData.previousPosition = cpuData.previousPosition;
        gpuData.normal = cpuData.normal;
        gpuData.jointCount = cpuData.jointCount;
        gpuData.velocity = cpuData.velocity;
        gpuData.mass = cpuData.mass;
        gpuData.force = cpuData.force;

        for (uint32_t j = 0; j < cpuData.jointCount; ++j)
        {
          gpuData.joints[j] = cpuData.joints[j].vertexIndex;
        }

        indirectCommand.vertexCount = 2;
        indirectCommand.instanceCount = cpuData.jointCount;
        indirectCommand.firstVertex = 0;
        indirectCommand.firstInstance = 0;

        vertexData[vertexIndex] = gpuData;
      }

      creation.reset()
          .set(
              VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
              ResourceUsageType::kImmutable,
              bufferSize)
          .setDeviceOnly(true)
          .setName("physicsMeshDataGpu");

      RendererUtil::BufferResource* gpuBuffer = renderer->createBuffer(creation);
      gpuBuffers.push(*gpuBuffer);

      physicsMesh->gpuBuffer = gpuBuffer->m_Handle;

      asyncLoader->requestBufferCopy(cpuBuffer, gpuBuffer->m_Handle);

      // NOTE: indirect command data
      bufferSize = sizeof(VkDrawIndirectCommand) * indirectCommands.m_Size;
      creation.reset()
          .set(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::kImmutable, bufferSize)
          .setData(indirectCommands.m_Data)
          .setName("indirectBufferCpu");

      cpuBuffer = renderer->m_GpuDevice->createBuffer(creation);

      creation.reset()
          .set(
              VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
              ResourceUsageType::kImmutable,
              bufferSize)
          .setDeviceOnly(true)
          .setName("indirectBufferGpu");

      gpuBuffer = renderer->createBuffer(creation);
      gpuBuffers.push(*gpuBuffer);

      physicsMesh->drawIndirectBuffer = gpuBuffer->m_Handle;

      asyncLoader->requestBufferCopy(cpuBuffer, gpuBuffer->m_Handle);

      indirectCommands.shutdown();
    }

    meshes.push(renderMesh);
  }

  materials.shutdown();

  VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  // Positions
  {
    BufferCreation creation{};
    size_t bufferSize = positions.m_Size * sizeof(vec3s);
    creation.set(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::kImmutable, bufferSize)
        .setData(positions.m_Data)
        .setName("obj_positions")
        .setPersistent(true);

    BufferHandle cpuBuffer = renderer->m_GpuDevice->createBuffer(creation);

    creation.reset()
        .set(flags, ResourceUsageType::kImmutable, bufferSize)
        .setDeviceOnly(true)
        .setName("position_attribute_buffer");

    RendererUtil::BufferResource* gpuBuffer = renderer->createBuffer(creation);
    gpuBuffers.push(*gpuBuffer);

    // TODO: ideally the CPU buffer would be using staging memory
    asyncLoader->requestBufferCopy(cpuBuffer, gpuBuffer->m_Handle);
  }

  // Tangents
  {
    BufferCreation creation{};
    size_t bufferSize = tangents.m_Size * sizeof(vec3s);
    creation.set(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::kImmutable, bufferSize)
        .setData(tangents.m_Data)
        .setName("obj_tangents")
        .setPersistent(true);

    BufferHandle cpuBuffer = renderer->m_GpuDevice->createBuffer(creation);

    creation.reset()
        .set(flags, ResourceUsageType::kImmutable, bufferSize)
        .setDeviceOnly(true)
        .setName("tangent_attribute_buffer");

    RendererUtil::BufferResource* gpuBuffer = renderer->createBuffer(creation);
    gpuBuffers.push(*gpuBuffer);

    asyncLoader->requestBufferCopy(cpuBuffer, gpuBuffer->m_Handle);
  }

  // Normals
  {
    BufferCreation creation{};
    size_t bufferSize = normals.m_Size * sizeof(vec3s);
    creation.set(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::kImmutable, bufferSize)
        .setData(normals.m_Data)
        .setName("obj_normals")
        .setPersistent(true);

    BufferHandle cpuBuffer = renderer->m_GpuDevice->createBuffer(creation);

    creation.reset()
        .set(flags, ResourceUsageType::kImmutable, bufferSize)
        .setDeviceOnly(true)
        .setName("normal_attribute_buffer");

    RendererUtil::BufferResource* gpuBuffer = renderer->createBuffer(creation);
    gpuBuffers.push(*gpuBuffer);

    asyncLoader->requestBufferCopy(cpuBuffer, gpuBuffer->m_Handle);
  }

  // TexCoords
  {
    BufferCreation creation{};
    size_t bufferSize = uvCoords.m_Size * sizeof(vec2s);
    creation.set(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::kImmutable, bufferSize)
        .setData(uvCoords.m_Data)
        .setName("obj_tex_coords");

    BufferHandle cpuBuffer = renderer->m_GpuDevice->createBuffer(creation);

    creation.reset()
        .set(flags, ResourceUsageType::kImmutable, bufferSize)
        .setDeviceOnly(true)
        .setName("texcoords_attribute_buffer");

    RendererUtil::BufferResource* gpuBuffer = renderer->createBuffer(creation);
    gpuBuffers.push(*gpuBuffer);

    asyncLoader->requestBufferCopy(cpuBuffer, gpuBuffer->m_Handle);
  }

  // Indices
  {
    BufferCreation creation{};
    size_t bufferSize = indices.m_Size * sizeof(uint32_t);
    creation.set(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::kImmutable, bufferSize)
        .setData(indices.m_Data)
        .setName("obj_indices")
        .setPersistent(true);

    BufferHandle cpuBuffer = renderer->m_GpuDevice->createBuffer(creation);

    creation.reset()
        .set(flags, ResourceUsageType::kImmutable, bufferSize)
        .setDeviceOnly(true)
        .setName("index_buffer");

    RendererUtil::BufferResource* gpuBuffer = renderer->createBuffer(creation);
    gpuBuffers.push(*gpuBuffer);

    asyncLoader->requestBufferCopy(cpuBuffer, gpuBuffer->m_Handle);
  }

  positions.shutdown();
  normals.shutdown();
  uvCoords.shutdown();
  tangents.shutdown();
  indices.shutdown();

  tempAllocator->freeMarker(tempAllocatorInitialMarker);

  animations.init(residentAllocator, 0);
  skins.init(residentAllocator, 0);

  int64_t endReadingBuffersData = Time::getCurrentTime();

  int64_t endCreatingBuffers = Time::getCurrentTime();

  int64_t endLoading = Time::getCurrentTime();

  printf(
      "Loaded scene %s in %f seconds.\nStats:\n\tReading GLTF file %f seconds\n\tTextures Creating "
      "%f seconds\n\tReading Buffers Data %f seconds\n\tCreating Buffers %f seconds\n",
      filename,
      Time::deltaSeconds(startSceneLoading, endLoading),
      Time::deltaSeconds(startSceneLoading, endLoadingFile),
      Time::deltaSeconds(endLoadingFile, endCreatingTextures),
      Time::deltaSeconds(endCreatingTextures, endReadingBuffersData),
      Time::deltaSeconds(endReadingBuffersData, endCreatingBuffers));
}

uint32_t ObjScene::loadTexture(
    const char* texturePath, const char* path, Framework::StackAllocator* tempAllocator)
{
  using namespace RendererUtil;
  int comp, width, height;

  stbi_info(texturePath, &width, &height, &comp);

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
      .setName(nullptr);
  TextureResource* tr = renderer->createTexture(tc);
  assert(tr != nullptr);

  images.push(*tr);

  renderer->m_GpuDevice->linkTextureSampler(tr->m_Handle, sampler->m_Handle);

  StringBuffer nameBuffer;
  nameBuffer.init(4096, tempAllocator);

  // Reconstruct file path
  char* fullFilename = nameBuffer.appendUseFormatted("%s%s", path, texturePath);
  asyncLoader->requestTextureData(fullFilename, tr->m_Handle);
  // Reset name buffer
  nameBuffer.clear();

  return tr->m_Handle.index;
}

void ObjScene::shutdown(RendererUtil::Renderer* renderer)
{
  GpuDevice& gpu = *renderer->m_GpuDevice;

  for (uint32_t meshIndex = 0; meshIndex < meshes.m_Size; ++meshIndex)
  {
    Mesh& mesh = meshes[meshIndex];

    gpu.destroyBuffer(mesh.pbrMaterial.materialBuffer);
    gpu.destroyDescriptorSet(mesh.pbrMaterial.descriptorSet);

    PhysicsMesh* physicsMesh = mesh.physicsMesh;

    if (physicsMesh != nullptr)
    {
      gpu.destroyDescriptorSet(physicsMesh->descriptorSet);
      gpu.destroyDescriptorSet(physicsMesh->debugMeshDescriptorSet);

      physicsMesh->vertices.shutdown();

      residentAllocator->deallocate(physicsMesh);
    }
  }

  gpu.destroyBuffer(sceneCb);
  gpu.destroyBuffer(physicsCb);

  for (uint32_t i = 0; i < images.m_Size; ++i)
  {
    renderer->destroyTexture(&images[i]);
  }

  renderer->destroySampler(sampler);

  for (uint32_t i = 0; i < cpuBuffers.m_Size; ++i)
  {
    renderer->destroyBuffer(&cpuBuffers[i]);
  }

  for (uint32_t i = 0; i < gpuBuffers.m_Size; ++i)
  {
    renderer->destroyBuffer(&gpuBuffers[i]);
  }

  meshes.shutdown();

  // Free scene buffers
  images.shutdown();
  cpuBuffers.shutdown();
  gpuBuffers.shutdown();
}

void ObjScene::prepareDraws(
    RendererUtil::Renderer* renderer,
    Framework::StackAllocator* scratchAllocator,
    SceneGraph* sceneGraph_)
{
  using namespace RendererUtil;

  sceneGraph = sceneGraph_;

  // Create pipeline state
  PipelineCreation pipelineCreation;

  size_t cached_scratch_size = scratchAllocator->getMarker();

  StringBuffer pathBuffer;
  pathBuffer.init(1024, scratchAllocator);

  // Create material
  const uint64_t mainHashedName = hashCalculate("main");
  GpuTechnique* mainTechnique = renderer->m_ResourceCache.m_Techniques.get(mainHashedName);

  MaterialCreation materialCreation;
  materialCreation.setName("material_noCull_opaque").setTechnique(mainTechnique).setRenderIndex(0);

  RendererUtil::Material* pbrMaterial = renderer->createMaterial(materialCreation);

  const uint64_t clothHashedName = hashCalculate("cloth");
  GpuTechnique* clothTechnique = renderer->m_ResourceCache.m_Techniques.get(clothHashedName);

  const uint64_t debugHashedName = hashCalculate("debug");
  GpuTechnique* debugTechnique = renderer->m_ResourceCache.m_Techniques.get(debugHashedName);

  // Constant buffer
  BufferCreation bufferCreation;
  bufferCreation.reset()
      .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, sizeof(GpuSceneData))
      .setName("scene_cb");
  sceneCb = renderer->m_GpuDevice->createBuffer(bufferCreation);

  bufferCreation.reset()
      .set(
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::kDynamic, sizeof(PhysicsSceneData))
      .setName("physics_cb");
  physicsCb = renderer->m_GpuDevice->createBuffer(bufferCreation);

  // Add a dummy single node used by all meshes.
  sceneGraph->resize(1);
  sceneGraph->setLocalMatrix(0, glms_mat4_identity());
  sceneGraph->setDebugData(0, "Dummy");

  // TODO: not all meshes will create physics buffers
  uint32_t bufferIndexOffset = meshes.m_Size * 2;
  for (uint32_t meshIndex = 0; meshIndex < meshes.m_Size; ++meshIndex)
  {
    Mesh& mesh = meshes[meshIndex];

    mesh.positionBuffer = gpuBuffers[bufferIndexOffset + 0].m_Handle;
    mesh.tangentBuffer = gpuBuffers[bufferIndexOffset + 1].m_Handle;
    mesh.normalBuffer = gpuBuffers[bufferIndexOffset + 2].m_Handle;
    mesh.texcoordBuffer = gpuBuffers[bufferIndexOffset + 3].m_Handle;
    mesh.indexBuffer = gpuBuffers[bufferIndexOffset + 4].m_Handle;

    mesh.sceneGraphNodeIndex = 0;
    mesh.pbrMaterial.material = pbrMaterial;

    mesh.pbrMaterial.flags |= DrawFlagsPhong;
    if (mesh.pbrMaterial.diffuseColour.w < 1.0f)
    {
      mesh.pbrMaterial.flags |= DrawFlagsTransparent;
    }

    // Descriptor set
    const uint32_t passIndex = mesh.hasSkinning() ? 5 : 3;

    DescriptorSetCreation dsCreation{};
    DescriptorSetLayoutHandle mainLayout = renderer->m_GpuDevice->getDescriptorSetLayout(
        mesh.pbrMaterial.material->m_Technique->passes[passIndex].pipeline,
        kMaterialDescriptorSetIndex);
    dsCreation.reset()
        .buffer(sceneCb, 0)
        .buffer(mesh.pbrMaterial.materialBuffer, 2)
        .setLayout(mainLayout);
    mesh.pbrMaterial.descriptorSet = renderer->m_GpuDevice->createDescriptorSet(dsCreation);

    if (mesh.physicsMesh != nullptr)
    {
      DescriptorSetLayoutHandle physicsLayout = renderer->m_GpuDevice->getDescriptorSetLayout(
          clothTechnique->passes[0].pipeline, kMaterialDescriptorSetIndex);
      dsCreation.reset()
          .buffer(physicsCb, 0)
          .buffer(mesh.physicsMesh->gpuBuffer, 1)
          .buffer(mesh.positionBuffer, 2)
          .buffer(mesh.normalBuffer, 3)
          .buffer(mesh.indexBuffer, 4)
          .setLayout(physicsLayout);

      mesh.physicsMesh->descriptorSet = renderer->m_GpuDevice->createDescriptorSet(dsCreation);

      DescriptorSetLayoutHandle debugMeshLayout = renderer->m_GpuDevice->getDescriptorSetLayout(
          debugTechnique->passes[0].pipeline, kMaterialDescriptorSetIndex);
      dsCreation.reset()
          .buffer(sceneCb, 0)
          .buffer(mesh.physicsMesh->gpuBuffer, 1)
          .setLayout(debugMeshLayout);

      mesh.physicsMesh->debugMeshDescriptorSet =
          renderer->m_GpuDevice->createDescriptorSet(dsCreation);
    }
  }

  // We're done. Release all resources associated with this import
  aiReleaseImport(assimpScene);
  assimpScene = nullptr;
}

} // namespace Graphics
