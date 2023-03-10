#include "Gltf.hpp"
#include "File.hpp"

#include <Externals/json.hpp>
#include <assert.h>

using json = nlohmann::json;

namespace Framework
{

static void* allocateAndZero(Allocator* p_Allocator, size_t p_Size)
{
  void* result = p_Allocator->allocate(p_Size, 64);
  memset(result, 0, p_Size);

  return result;
}

static void tryLoadString(
    json& p_JsonData, const char* p_Key, StringBuffer& p_StringBuffer, Allocator* p_Allocator)
{
  auto it = p_JsonData.find(p_Key);
  if (it == p_JsonData.end())
    return;

  std::string value = p_JsonData.value(p_Key, "");

  p_StringBuffer.init(value.length() + 1, p_Allocator);
  p_StringBuffer.append(value.c_str());
}

static void tryLoadInt(json& p_JsonData, const char* p_Key, int& p_Value)
{
  auto it = p_JsonData.find(p_Key);
  if (it == p_JsonData.end())
  {
    p_Value = glTF::INVALID_INT_VALUE;
    return;
  }

  p_Value = p_JsonData.value(p_Key, 0);
}

static void tryLoadFloat(json& p_JsonData, const char* p_Key, float& p_Value)
{
  auto it = p_JsonData.find(p_Key);
  if (it == p_JsonData.end())
  {
    p_Value = glTF::INVALID_FLOAT_VALUE;
    return;
  }

  p_Value = p_JsonData.value(p_Key, 0.0f);
}

static void tryLoadBool(json& p_JsonData, const char* p_Key, bool& p_Value)
{
  auto it = p_JsonData.find(p_Key);
  if (it == p_JsonData.end())
  {
    p_Value = false;
    return;
  }

  p_Value = p_JsonData.value(p_Key, false);
}

static void tryLoadType(json& p_JsonData, const char* p_Key, glTF::Accessor::Type& p_Type)
{
  std::string value = p_JsonData.value(p_Key, "");
  if (value == "SCALAR")
  {
    p_Type = glTF::Accessor::Type::Scalar;
  }
  else if (value == "VEC2")
  {
    p_Type = glTF::Accessor::Type::Vec2;
  }
  else if (value == "VEC3")
  {
    p_Type = glTF::Accessor::Type::Vec3;
  }
  else if (value == "VEC4")
  {
    p_Type = glTF::Accessor::Type::Vec4;
  }
  else if (value == "MAT2")
  {
    p_Type = glTF::Accessor::Type::Mat2;
  }
  else if (value == "MAT3")
  {
    p_Type = glTF::Accessor::Type::Mat3;
  }
  else if (value == "MAT4")
  {
    p_Type = glTF::Accessor::Type::Mat4;
  }
  else
  {
    assert(false);
  }
}

static void tryLoadIntArray(
    json& p_JsonData, const char* p_Key, uint32_t& p_Count, int** p_Array, Allocator* p_Allocator)
{
  auto it = p_JsonData.find(p_Key);
  if (it == p_JsonData.end())
  {
    p_Count = 0;
    *p_Array = nullptr;
    return;
  }

  json jsonArray = p_JsonData.at(p_Key);

  p_Count = static_cast<uint32_t>(jsonArray.size());

  int* values = (int*)allocateAndZero(p_Allocator, sizeof(int) * p_Count);

  for (size_t i = 0; i < p_Count; ++i)
  {
    values[i] = jsonArray.at(i);
  }

  *p_Array = values;
}

static void tryLoadFloatArray(
    json& p_JsonData, const char* p_Key, uint32_t& p_Count, float** p_Array, Allocator* p_Allocator)
{
  auto it = p_JsonData.find(p_Key);
  if (it == p_JsonData.end())
  {
    p_Count = 0;
    *p_Array = nullptr;
    return;
  }

  json jsonArray = p_JsonData.at(p_Key);

  p_Count = static_cast<uint32_t>(jsonArray.size());

  float* values = (float*)allocateAndZero(p_Allocator, sizeof(float) * p_Count);

  for (size_t i = 0; i < p_Count; ++i)
  {
    values[i] = jsonArray.at(i);
  }

  *p_Array = values;
}

static void loadAsset(json& p_JsonData, glTF::Asset& asset, Allocator* p_Allocator)
{
  json jsonAsset = p_JsonData["asset"];

  tryLoadString(jsonAsset, "copyright", asset.copyright, p_Allocator);
  tryLoadString(jsonAsset, "generator", asset.generator, p_Allocator);
  tryLoadString(jsonAsset, "minVersion", asset.minVersion, p_Allocator);
  tryLoadString(jsonAsset, "version", asset.version, p_Allocator);
}

static void loadScene(json& p_JsonData, glTF::Scene& p_Scene, Allocator* p_Allocator)
{
  tryLoadIntArray(p_JsonData, "nodes", p_Scene.nodesCount, &p_Scene.nodes, p_Allocator);
}

static void loadScenes(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json scenes = p_JsonData["scenes"];

  size_t sceneCount = scenes.size();
  p_GltfData.scenes = (glTF::Scene*)allocateAndZero(p_Allocator, sizeof(glTF::Scene) * sceneCount);
  p_GltfData.scenesCount = static_cast<uint32_t>(sceneCount);

  for (size_t i = 0; i < sceneCount; ++i)
  {
    loadScene(scenes[i], p_GltfData.scenes[i], p_Allocator);
  }
}

static void loadBuffer(json& p_JsonData, glTF::Buffer& p_Buffer, Allocator* p_Allocator)
{
  tryLoadString(p_JsonData, "uri", p_Buffer.uri, p_Allocator);
  tryLoadInt(p_JsonData, "byteLength", p_Buffer.byteLength);
  tryLoadString(p_JsonData, "name", p_Buffer.name, p_Allocator);
}

static void loadBuffers(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json buffers = p_JsonData["buffers"];

  size_t bufferCount = buffers.size();
  p_GltfData.buffers =
      (glTF::Buffer*)allocateAndZero(p_Allocator, sizeof(glTF::Buffer) * bufferCount);
  p_GltfData.buffersCount = static_cast<uint32_t>(bufferCount);

  for (size_t i = 0; i < bufferCount; ++i)
  {
    loadBuffer(buffers[i], p_GltfData.buffers[i], p_Allocator);
  }
}

static void loadBufferView(json& p_JsonData, glTF::BufferView& bufferView, Allocator* p_Allocator)
{
  tryLoadInt(p_JsonData, "buffer", bufferView.buffer);
  tryLoadInt(p_JsonData, "byteLength", bufferView.byteLength);
  tryLoadInt(p_JsonData, "byteOffset", bufferView.byteOffset);
  tryLoadInt(p_JsonData, "byteStride", bufferView.byteStride);
  tryLoadInt(p_JsonData, "target", bufferView.target);
  tryLoadString(p_JsonData, "name", bufferView.name, p_Allocator);
}

static void loadBufferViews(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json buffers = p_JsonData["bufferViews"];

  size_t bufferCount = buffers.size();
  p_GltfData.bufferViews =
      (glTF::BufferView*)allocateAndZero(p_Allocator, sizeof(glTF::BufferView) * bufferCount);
  p_GltfData.bufferViewsCount = static_cast<uint32_t>(bufferCount);

  for (size_t i = 0; i < bufferCount; ++i)
  {
    loadBufferView(buffers[i], p_GltfData.bufferViews[i], p_Allocator);
  }
}

static void load_node(json& p_JsonData, glTF::Node& node, Allocator* p_Allocator)
{
  tryLoadInt(p_JsonData, "camera", node.camera);
  tryLoadInt(p_JsonData, "mesh", node.mesh);
  tryLoadInt(p_JsonData, "skin", node.skin);
  tryLoadIntArray(p_JsonData, "children", node.childrenCount, &node.children, p_Allocator);
  tryLoadFloatArray(p_JsonData, "matrix", node.matrixCount, &node.matrix, p_Allocator);
  tryLoadFloatArray(p_JsonData, "rotation", node.rotationCount, &node.rotation, p_Allocator);
  tryLoadFloatArray(p_JsonData, "scale", node.scaleCount, &node.scale, p_Allocator);
  tryLoadFloatArray(
      p_JsonData, "translation", node.translationCount, &node.translation, p_Allocator);
  tryLoadFloatArray(p_JsonData, "weights", node.weightsCount, &node.weights, p_Allocator);
  tryLoadString(p_JsonData, "name", node.name, p_Allocator);
}

static void load_nodes(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json array = p_JsonData["nodes"];

  size_t arrayCount = array.size();
  p_GltfData.nodes = (glTF::Node*)allocateAndZero(p_Allocator, sizeof(glTF::Node) * arrayCount);
  p_GltfData.nodesCount = static_cast<uint32_t>(arrayCount);

  for (size_t i = 0; i < arrayCount; ++i)
  {
    load_node(array[i], p_GltfData.nodes[i], p_Allocator);
  }
}

static void
loadMeshPrimitive(json& p_JsonData, glTF::MeshPrimitive& mesh_primitive, Allocator* p_Allocator)
{
  tryLoadInt(p_JsonData, "indices", mesh_primitive.indices);
  tryLoadInt(p_JsonData, "material", mesh_primitive.material);
  tryLoadInt(p_JsonData, "mode", mesh_primitive.mode);

  json attributes = p_JsonData["attributes"];

  mesh_primitive.attributes = (glTF::MeshPrimitive::Attribute*)allocateAndZero(
      p_Allocator, sizeof(glTF::MeshPrimitive::Attribute) * attributes.size());
  mesh_primitive.attributeCount = static_cast<uint32_t>(attributes.size());

  uint32_t index = 0;
  for (auto jsonAttribute : attributes.items())
  {
    std::string key = jsonAttribute.key();
    glTF::MeshPrimitive::Attribute& attribute = mesh_primitive.attributes[index];

    attribute.key.init(key.size() + 1, p_Allocator);
    attribute.key.append(key.c_str());

    attribute.accessorIndex = jsonAttribute.value();

    ++index;
  }
}

static void loadMeshPrimitives(json& p_JsonData, glTF::Mesh& mesh, Allocator* p_Allocator)
{
  json array = p_JsonData["primitives"];

  size_t arrayCount = array.size();
  mesh.primitives =
      (glTF::MeshPrimitive*)allocateAndZero(p_Allocator, sizeof(glTF::MeshPrimitive) * arrayCount);
  mesh.primitivesCount = static_cast<uint32_t>(arrayCount);
  for (size_t i = 0; i < arrayCount; ++i)
  {
    loadMeshPrimitive(array[i], mesh.primitives[i], p_Allocator);
  }
}

static void loadMesh(json& p_JsonData, glTF::Mesh& p_Mesh, Allocator* p_Allocator)
{
  loadMeshPrimitives(p_JsonData, p_Mesh, p_Allocator);
  tryLoadFloatArray(p_JsonData, "weights", p_Mesh.weightsCount, &p_Mesh.weights, p_Allocator);
  tryLoadString(p_JsonData, "name", p_Mesh.name, p_Allocator);
}

static void loadMeshes(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json array = p_JsonData["meshes"];

  size_t arrayCount = array.size();
  p_GltfData.meshes = (glTF::Mesh*)allocateAndZero(p_Allocator, sizeof(glTF::Mesh) * arrayCount);
  p_GltfData.meshesCount = static_cast<uint32_t>(arrayCount);

  for (size_t i = 0; i < arrayCount; ++i)
  {
    loadMesh(array[i], p_GltfData.meshes[i], p_Allocator);
  }
}

static void loadAccessor(json& p_JsonData, glTF::Accessor& accessor, Allocator* p_Allocator)
{
  tryLoadInt(p_JsonData, "bufferView", accessor.bufferView);
  tryLoadInt(p_JsonData, "byteOffset", accessor.byteOffset);
  tryLoadInt(p_JsonData, "componentType", accessor.componentType);
  tryLoadInt(p_JsonData, "count", accessor.count);
  tryLoadInt(p_JsonData, "sparse", accessor.sparse);
  tryLoadFloatArray(p_JsonData, "max", accessor.maxCount, &accessor.max, p_Allocator);
  tryLoadFloatArray(p_JsonData, "min", accessor.minCount, &accessor.min, p_Allocator);
  tryLoadBool(p_JsonData, "normalized", accessor.normalized);
  tryLoadType(p_JsonData, "type", accessor.type);
}

static void loadAccessors(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json array = p_JsonData["accessors"];

  size_t arrayCount = array.size();
  p_GltfData.accessors =
      (glTF::Accessor*)allocateAndZero(p_Allocator, sizeof(glTF::Accessor) * arrayCount);
  p_GltfData.accessorsCount = static_cast<uint32_t>(arrayCount);

  for (size_t i = 0; i < arrayCount; ++i)
  {
    loadAccessor(array[i], p_GltfData.accessors[i], p_Allocator);
  }
}

static void tryLoadTextureInfo(
    json& p_JsonData, const char* p_Key, glTF::TextureInfo** p_TextureInfo, Allocator* p_Allocator)
{
  auto it = p_JsonData.find(p_Key);
  if (it == p_JsonData.end())
  {
    *p_TextureInfo = nullptr;
    return;
  }

  glTF::TextureInfo* ti = (glTF::TextureInfo*)p_Allocator->allocate(sizeof(glTF::TextureInfo), 64);

  tryLoadInt(*it, "index", ti->index);
  tryLoadInt(*it, "texCoord", ti->texCoord);

  *p_TextureInfo = ti;
}

static void tryLoadMaterialNormalTextureInfo(
    json& p_JsonData,
    const char* p_Key,
    glTF::MaterialNormalTextureInfo** p_TextureInfo,
    Allocator* p_Allocator)
{
  auto it = p_JsonData.find(p_Key);
  if (it == p_JsonData.end())
  {
    *p_TextureInfo = nullptr;
    return;
  }

  glTF::MaterialNormalTextureInfo* ti = (glTF::MaterialNormalTextureInfo*)p_Allocator->allocate(
      sizeof(glTF::MaterialNormalTextureInfo), 64);

  tryLoadInt(*it, "index", ti->index);
  tryLoadInt(*it, "texCoord", ti->texCoord);
  tryLoadFloat(*it, "scale", ti->scale);

  *p_TextureInfo = ti;
}

static void tryLoadMaterialOcclusionTextureInfo(
    json& p_JsonData,
    const char* p_Key,
    glTF::MaterialOcclusionTextureInfo** p_TextureInfo,
    Allocator* p_Allocator)
{
  auto it = p_JsonData.find(p_Key);
  if (it == p_JsonData.end())
  {
    *p_TextureInfo = nullptr;
    return;
  }

  glTF::MaterialOcclusionTextureInfo* ti =
      (glTF::MaterialOcclusionTextureInfo*)p_Allocator->allocate(
          sizeof(glTF::MaterialOcclusionTextureInfo), 64);

  tryLoadInt(*it, "index", ti->index);
  tryLoadInt(*it, "texCoord", ti->texCoord);
  tryLoadFloat(*it, "strength", ti->strength);

  *p_TextureInfo = ti;
}

static void tryLoadMaterialPBRMetallicRoughness(
    json& p_JsonData,
    const char* p_Key,
    glTF::MaterialPBRMetallicRoughness** p_TextureInfo,
    Allocator* p_Allocator)
{
  auto it = p_JsonData.find(p_Key);
  if (it == p_JsonData.end())
  {
    *p_TextureInfo = nullptr;
    return;
  }

  glTF::MaterialPBRMetallicRoughness* ti =
      (glTF::MaterialPBRMetallicRoughness*)p_Allocator->allocate(
          sizeof(glTF::MaterialPBRMetallicRoughness), 64);

  tryLoadFloatArray(
      *it, "baseColorFactor", ti->baseColorFactorCount, &ti->baseColorFactor, p_Allocator);
  tryLoadTextureInfo(*it, "baseColorTexture", &ti->baseColorTexture, p_Allocator);
  tryLoadFloat(*it, "metallicFactor", ti->metallicFactor);
  tryLoadTextureInfo(*it, "metallicRoughnessTexture", &ti->metallicRoughnessTexture, p_Allocator);
  tryLoadFloat(*it, "roughnessFactor", ti->roughnessFactor);

  *p_TextureInfo = ti;
}

static void load_material(json& p_JsonData, glTF::Material& material, Allocator* p_Allocator)
{
  tryLoadFloatArray(
      p_JsonData,
      "emissiveFactor",
      material.emissiveFactorCount,
      &material.emissiveFactor,
      p_Allocator);
  tryLoadFloat(p_JsonData, "alphaCutoff", material.alphaCutoff);
  tryLoadString(p_JsonData, "alphaMode", material.alphaMode, p_Allocator);
  tryLoadBool(p_JsonData, "doubleSided", material.doubleSided);

  tryLoadTextureInfo(p_JsonData, "emissiveTexture", &material.emissiveTexture, p_Allocator);
  tryLoadMaterialNormalTextureInfo(
      p_JsonData, "normalTexture", &material.normalTexture, p_Allocator);
  tryLoadMaterialOcclusionTextureInfo(
      p_JsonData, "occlusionTexture", &material.occlusionTexture, p_Allocator);
  tryLoadMaterialPBRMetallicRoughness(
      p_JsonData, "pbrMetallicRoughness", &material.pbrMetallicRoughness, p_Allocator);

  tryLoadString(p_JsonData, "name", material.name, p_Allocator);
}

static void loadMaterials(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json array = p_JsonData["materials"];

  size_t arrayCount = array.size();
  p_GltfData.materials =
      (glTF::Material*)allocateAndZero(p_Allocator, sizeof(glTF::Material) * arrayCount);
  p_GltfData.materialsCount = static_cast<uint32_t>(arrayCount);

  for (size_t i = 0; i < arrayCount; ++i)
  {
    load_material(array[i], p_GltfData.materials[i], p_Allocator);
  }
}

static void loadTexture(json& p_JsonData, glTF::Texture& texture, Allocator* p_Allocator)
{
  tryLoadInt(p_JsonData, "sampler", texture.sampler);
  tryLoadInt(p_JsonData, "source", texture.source);
  tryLoadString(p_JsonData, "name", texture.name, p_Allocator);
}

static void loadTextures(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json array = p_JsonData["textures"];

  size_t arrayCount = array.size();
  p_GltfData.textures =
      (glTF::Texture*)allocateAndZero(p_Allocator, sizeof(glTF::Texture) * arrayCount);
  p_GltfData.texturesCount = static_cast<uint32_t>(arrayCount);

  for (size_t i = 0; i < arrayCount; ++i)
  {
    loadTexture(array[i], p_GltfData.textures[i], p_Allocator);
  }
}

static void load_image(json& p_JsonData, glTF::Image& image, Allocator* p_Allocator)
{
  tryLoadInt(p_JsonData, "bufferView", image.bufferView);
  tryLoadString(p_JsonData, "mimeType", image.mimeType, p_Allocator);
  tryLoadString(p_JsonData, "uri", image.uri, p_Allocator);
}

static void loadImages(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json array = p_JsonData["images"];

  size_t arrayCount = array.size();
  p_GltfData.images = (glTF::Image*)allocateAndZero(p_Allocator, sizeof(glTF::Image) * arrayCount);
  p_GltfData.imagesCount = static_cast<uint32_t>(arrayCount);

  for (size_t i = 0; i < arrayCount; ++i)
  {
    load_image(array[i], p_GltfData.images[i], p_Allocator);
  }
}

static void loadSampler(json& p_JsonData, glTF::Sampler& p_Sampler, Allocator* p_Allocator)
{
  tryLoadInt(p_JsonData, "magFilter", p_Sampler.magFilter);
  tryLoadInt(p_JsonData, "minFilter", p_Sampler.minFilter);
  tryLoadInt(p_JsonData, "wrapS", p_Sampler.wrapS);
  tryLoadInt(p_JsonData, "wrapT", p_Sampler.wrapT);
}

static void loadSamplers(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json array = p_JsonData["samplers"];

  size_t arrayCount = array.size();
  p_GltfData.samplers =
      (glTF::Sampler*)allocateAndZero(p_Allocator, sizeof(glTF::Sampler) * arrayCount);
  p_GltfData.samplersCount = static_cast<uint32_t>(arrayCount);

  for (size_t i = 0; i < arrayCount; ++i)
  {
    loadSampler(array[i], p_GltfData.samplers[i], p_Allocator);
  }
}

static void loadSkin(json& p_JsonData, glTF::Skin& p_Skin, Allocator* p_Allocator)
{
  tryLoadInt(p_JsonData, "skeleton", p_Skin.skeletonRootNodeIndex);
  tryLoadInt(p_JsonData, "inverseBindMatrices", p_Skin.inverseBindMatricesBufferIndex);
  tryLoadIntArray(p_JsonData, "joints", p_Skin.jointsCount, &p_Skin.joints, p_Allocator);
}

static void loadSkins(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json array = p_JsonData["skins"];

  size_t arrayCount = array.size();
  p_GltfData.skins = (glTF::Skin*)allocateAndZero(p_Allocator, sizeof(glTF::Skin) * arrayCount);
  p_GltfData.skinsCount = static_cast<uint32_t>(arrayCount);

  for (size_t i = 0; i < arrayCount; ++i)
  {
    loadSkin(array[i], p_GltfData.skins[i], p_Allocator);
  }
}

static void loadAnimation(json& p_JsonData, glTF::Animation& p_Animation, Allocator* p_Allocator)
{

  json jsonArray = p_JsonData.at("samplers");
  if (jsonArray.is_array())
  {
    size_t count = jsonArray.size();

    glTF::AnimationSampler* values = (glTF::AnimationSampler*)allocateAndZero(
        p_Allocator, sizeof(glTF::AnimationSampler) * count);

    for (size_t i = 0; i < count; ++i)
    {
      json element = jsonArray.at(i);
      glTF::AnimationSampler& sampler = values[i];

      tryLoadInt(element, "input", sampler.m_InputKeyframeBufferIndex);
      tryLoadInt(element, "output", sampler.m_OutputKeyframeBufferIndex);

      std::string value = element.value("interpolation", "");
      if (value == "LINEAR")
      {
        sampler.m_Interpolation = glTF::AnimationSampler::Linear;
      }
      else if (value == "STEP")
      {
        sampler.m_Interpolation = glTF::AnimationSampler::Step;
      }
      else if (value == "CUBICSPLINE")
      {
        sampler.m_Interpolation = glTF::AnimationSampler::CubicSpline;
      }
      else
      {
        sampler.m_Interpolation = glTF::AnimationSampler::Linear;
      }
    }

    p_Animation.samplers = values;
    p_Animation.samplersCount = static_cast<uint32_t>(count);
  }

  jsonArray = p_JsonData.at("channels");
  if (jsonArray.is_array())
  {
    size_t count = jsonArray.size();

    glTF::AnimationChannel* values = (glTF::AnimationChannel*)allocateAndZero(
        p_Allocator, sizeof(glTF::AnimationChannel) * count);

    for (size_t i = 0; i < count; ++i)
    {
      json element = jsonArray.at(i);
      glTF::AnimationChannel& channel = values[i];

      tryLoadInt(element, "sampler", channel.sampler);
      json target = element.at("target");
      tryLoadInt(target, "node", channel.targetNode);

      std::string targetPath = target.value("path", "");
      if (targetPath == "scale")
      {
        channel.targetType = glTF::AnimationChannel::Scale;
      }
      else if (targetPath == "rotation")
      {
        channel.targetType = glTF::AnimationChannel::Rotation;
      }
      else if (targetPath == "translation")
      {
        channel.targetType = glTF::AnimationChannel::Translation;
      }
      else if (targetPath == "weights")
      {
        channel.targetType = glTF::AnimationChannel::Weights;
      }
      else
      {
        assert(false && "Error parsing target path");
        channel.targetType = glTF::AnimationChannel::Count;
      }
    }

    p_Animation.channels = values;
    p_Animation.channelsCount = static_cast<uint32_t>(count);
  }
}

static void loadAnimations(json& p_JsonData, glTF::glTF& p_GltfData, Allocator* p_Allocator)
{
  json array = p_JsonData["animations"];

  size_t arrayCount = array.size();
  p_GltfData.animations =
      (glTF::Animation*)allocateAndZero(p_Allocator, sizeof(glTF::Animation) * arrayCount);
  p_GltfData.animationsCount = static_cast<uint32_t>(arrayCount);

  for (size_t i = 0; i < arrayCount; ++i)
  {
    loadAnimation(array[i], p_GltfData.animations[i], p_Allocator);
  }
}

glTF::glTF gltfLoadFile(const char* p_FilePath)
{
  glTF::glTF result{};

  if (!fileExists(p_FilePath))
  {
    char msg[256];
    sprintf(msg, "Error: file %s does not exists.\n", p_FilePath);
    OutputDebugStringA(msg);
    return result;
  }

  Allocator* heapAllocator = &MemoryService::instance()->m_SystemAllocator;

  FileReadResult readResult = fileReadText(p_FilePath, heapAllocator);

  json gltfData = json::parse(readResult.data);

  result.allocator.init(FRAMEWORK_MEGA(2));
  Allocator* allocator = &result.allocator;

  for (auto properties : gltfData.items())
  {
    if (properties.key() == "asset")
    {
      loadAsset(gltfData, result.asset, allocator);
    }
    else if (properties.key() == "scene")
    {
      tryLoadInt(gltfData, "scene", result.scene);
    }
    else if (properties.key() == "scenes")
    {
      loadScenes(gltfData, result, allocator);
    }
    else if (properties.key() == "buffers")
    {
      loadBuffers(gltfData, result, allocator);
    }
    else if (properties.key() == "bufferViews")
    {
      loadBufferViews(gltfData, result, allocator);
    }
    else if (properties.key() == "nodes")
    {
      load_nodes(gltfData, result, allocator);
    }
    else if (properties.key() == "meshes")
    {
      loadMeshes(gltfData, result, allocator);
    }
    else if (properties.key() == "accessors")
    {
      loadAccessors(gltfData, result, allocator);
    }
    else if (properties.key() == "materials")
    {
      loadMaterials(gltfData, result, allocator);
    }
    else if (properties.key() == "textures")
    {
      loadTextures(gltfData, result, allocator);
    }
    else if (properties.key() == "images")
    {
      loadImages(gltfData, result, allocator);
    }
    else if (properties.key() == "samplers")
    {
      loadSamplers(gltfData, result, allocator);
    }
    else if (properties.key() == "skins")
    {
      loadSkins(gltfData, result, allocator);
    }
    else if (properties.key() == "animations")
    {
      loadAnimations(gltfData, result, allocator);
    }
  }

  heapAllocator->deallocate(readResult.data);

  return result;
}

void gltfFree(glTF::glTF& scene) { scene.allocator.shutdown(); }

int gltfGetAttributeAccessorIndex(
    glTF::MeshPrimitive::Attribute* p_Attributes,
    uint32_t p_AttributeCount,
    const char* p_AttributeName)
{
  for (uint32_t index = 0; index < p_AttributeCount; ++index)
  {
    glTF::MeshPrimitive::Attribute& attribute = p_Attributes[index];
    if (strcmp(attribute.key.m_Data, p_AttributeName) == 0)
    {
      return attribute.accessorIndex;
    }
  }

  return -1;
}

} // namespace Framework

int Framework::glTF::getDataOffset(int p_AccessorOffset, int p_BufferViewOffset)
{

  int byteOffset = p_BufferViewOffset == INVALID_INT_VALUE ? 0 : p_BufferViewOffset;
  byteOffset += p_AccessorOffset == INVALID_INT_VALUE ? 0 : p_AccessorOffset;
  return byteOffset;
}
