#pragma once

#include "Memory.hpp"
#include "Prerequisites.hpp"
#include "String.hpp"

namespace Framework
{
namespace glTF
{
static const int INVALID_INT_VALUE = 2147483647;
static_assert(INVALID_INT_VALUE == INT32_MAX, "Mismatch between invalid int and int32 max");
static const float INVALID_FLOAT_VALUE = 3.402823466e+38F;

struct Asset
{
  StringBuffer copyright;
  StringBuffer generator;
  StringBuffer minVersion;
  StringBuffer version;
};

struct CameraOrthographic
{
  float xmag;
  float ymag;
  float zfar;
  float znear;
};

struct AccessorSparse
{
  int count;
  int indices;
  int values;
};

struct Camera
{
  int orthographic;
  int perspective;
  StringBuffer type;
};

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
};

struct AnimationSampler
{
  int m_InputKeyframeBufferIndex;  //"The index of an accessor containing keyframe timestamps."
  int m_OutputKeyframeBufferIndex; // "The index of an accessor, containing keyframe output
                                   // values."

  enum Interpolation
  {
    Linear,
    Step,
    CubicSpline,
    Count
  };
  // LINEAR The animated values are linearly interpolated between keyframes. When targeting a
  // rotation, spherical linear interpolation (slerp) **SHOULD** be used to interpolate quaternions.
  // The float of output elements **MUST** equal the float of input elements. STEP The animated
  // values remain constant to the output of the first keyframe, until the next keyframe. The float
  // of output elements **MUST** equal the float of input elements. CUBICSPLINE The animation's
  // interpolation is computed using a cubic spline with specified tangents. The float of output
  // elements **MUST** equal three times the float of input elements. For each input element, the
  // output stores three elements, an in-tangent, a spline vertex, and an out-tangent. There
  // **MUST** be at least two keyframes when using this interpolation.
  Interpolation m_Interpolation;
};

struct Skin
{
  int inverseBindMatricesBufferIndex;
  int skeletonRootNodeIndex;
  uint32_t jointsCount;
  int* joints;
};

struct BufferView
{
  enum Target
  {
    ARRAY_BUFFER = 34962 /* Vertex Data */,
    ELEMENT_ARRAY_BUFFER = 34963 /* Index Data */
  };

  int buffer;
  int byteLength;
  int byteOffset;
  int byteStride;
  int target;
  StringBuffer name;
};

struct Image
{
  int bufferView;
  // image/jpeg
  // image/png
  StringBuffer mimeType;
  StringBuffer uri;
};

struct Node
{
  int camera;
  uint32_t childrenCount;
  int* children;
  uint32_t matrixCount;
  float* matrix;
  int mesh;
  uint32_t rotationCount;
  float* rotation;
  uint32_t scaleCount;
  float* scale;
  int skin;
  uint32_t translationCount;
  float* translation;
  uint32_t weightsCount;
  float* weights;
  StringBuffer name;
};

struct TextureInfo
{
  int index;
  int texCoord;
};

struct MaterialPBRMetallicRoughness
{
  uint32_t baseColorFactorCount;
  float* baseColorFactor;
  TextureInfo* baseColorTexture;
  float metallicFactor;
  TextureInfo* metallicRoughnessTexture;
  float roughnessFactor;
};

struct MeshPrimitive
{
  struct Attribute
  {
    StringBuffer key;
    int accessorIndex;
  };

  uint32_t attributeCount;
  Attribute* attributes;
  int indices;
  int material;
  // 0 POINTS
  // 1 LINES
  // 2 LINE_LOOP
  // 3 LINE_STRIP
  // 4 TRIANGLES
  // 5 TRIANGLE_STRIP
  // 6 TRIANGLE_FAN
  int mode;
  // uint32_t targets_count;
  // object* targets; // TODO: this is a json object
};

struct AccessorSparseIndices
{
  int bufferView;
  int byteOffset;
  // 5121 UNSIGNED_BYTE
  // 5123 UNSIGNED_SHORT
  // 5125 UNSIGNED_INT
  int componentType;
};

struct Accessor
{
  enum ComponentType
  {
    BYTE = 5120,
    UNSIGNED_BYTE = 5121,
    SHORT = 5122,
    UNSIGNED_SHORT = 5123,
    UNSIGNED_INT = 5125,
    FLOAT = 5126
  };

  enum Type
  {
    Scalar,
    Vec2,
    Vec3,
    Vec4,
    Mat2,
    Mat3,
    Mat4
  };

  int bufferView;
  int byteOffset;

  int componentType;
  int count;
  uint32_t maxCount;
  float* max;
  uint32_t minCount;
  float* min;
  bool normalized;
  int sparse;
  Type type;
};

struct Texture
{
  int sampler;
  int source;
  StringBuffer name;
};

struct MaterialNormalTextureInfo
{
  int index;
  int texCoord;
  float scale;
};

struct Mesh
{
  uint32_t primitivesCount;
  MeshPrimitive* primitives;
  uint32_t weightsCount;
  float* weights;
  StringBuffer name;
};

struct MaterialOcclusionTextureInfo
{
  int index;
  int texCoord;
  float strength;
};

struct Material
{
  float alpha_cutoff;
  // OPAQUE The alpha value is ignored, and the rendered output is fully opaque.
  // MASK The rendered output is either fully opaque or fully transparent depending on the alpha
  // value and the specified `alphaCutoff` value; the exact appearance of the edges **MAY** be
  // subject to implementation-specific techniques such as "`Alpha-to-Coverage`". BLEND The alpha
  // value is used to composite the source and destination areas. The rendered output is combined
  // with the background using the normal painting operation (i.e. the Porter and Duff over
  // operator).
  StringBuffer alphaMode;
  bool doubleSided;
  uint32_t emissiveFactorCount;
  float* emissiveFactor;
  TextureInfo* emissiveTexture;
  MaterialNormalTextureInfo* normalTexture;
  MaterialOcclusionTextureInfo* occlusionTexture;
  MaterialPBRMetallicRoughness* pbrMetallicRoughness;
  StringBuffer name;
};

struct Buffer
{
  int byteLength;
  StringBuffer uri;
  StringBuffer name;
};

struct CameraPerspective
{
  float aspectRatio;
  float yfov;
  float zfar;
  float znear;
};

struct Animation
{
  uint32_t channelsCount;
  AnimationChannel* channels;
  uint32_t samplersCount;
  AnimationSampler* samplers;
};

struct AccessorSparseValues
{
  int bufferView;
  int byteOffset;
};

struct Scene
{
  uint32_t nodesCount;
  int* nodes;
};

struct Sampler
{
  enum Filter
  {
    NEAREST = 9728,
    LINEAR = 9729,
    NEAREST_MIPMAP_NEAREST = 9984,
    LINEAR_MIPMAP_NEAREST = 9985,
    NEAREST_MIPMAP_LINEAR = 9986,
    LINEAR_MIPMAP_LINEAR = 9987
  };

  enum Wrap
  {
    CLAMP_TO_EDGE = 33071,
    MIRRORED_REPEAT = 33648,
    REPEAT = 10497
  };

  int magFilter;
  int minFilter;
  int wrapS;
  int wrapT;
};

struct glTF
{
  uint32_t accessorsCount;
  Accessor* accessors;
  uint32_t animationsCount;
  Animation* animations;
  Asset asset;
  uint32_t bufferViewsCount;
  BufferView* bufferViews;
  uint32_t buffersCount;
  Buffer* buffers;
  uint32_t camerasCount;
  Camera* cameras;
  uint32_t extensionsRequiredCount;
  StringBuffer* extensionsRequired;
  uint32_t extensionsUsedCount;
  StringBuffer* extensionsUsed;
  uint32_t imagesCount;
  Image* images;
  uint32_t materialsCount;
  Material* materials;
  uint32_t meshesCount;
  Mesh* meshes;
  uint32_t nodesCount;
  Node* nodes;
  uint32_t samplersCount;
  Sampler* samplers;
  int scene;
  uint32_t scenesCount;
  Scene* scenes;
  uint32_t skinsCount;
  Skin* skins;
  uint32_t texturesCount;
  Texture* textures;

  LinearAllocator allocator;
};

int getDataOffset(int p_AccessorOffset, int p_BufferViewOffset);

} // namespace glTF

glTF::glTF gltfLoadFile(const char* p_FilePath);

void gltfFree(glTF::glTF& scene);

int gltfGetAttributeAccessorIndex(
    glTF::MeshPrimitive::Attribute* p_Attributes,
    uint32_t p_AttributeCount,
    const char* p_AttributeName);

} // namespace Framework
