#pragma once

#include "Foundation/Array.hpp"
#include "Foundation/Bit.hpp"

#include "Externals/cglm/struct/mat4.h"

namespace Graphics
{
//---------------------------------------------------------------------------//
struct Hierarchy
{
  int parent : 24;
  int level : 8;
}; // struct Hierarchy
//---------------------------------------------------------------------------//
struct SceneGraphNodeDebugData
{
  const char* name;
}; // struct SceneGraphNodeDebugData
//---------------------------------------------------------------------------//
struct SceneGraph
{

  void init(Framework::Allocator* residentAllocator, uint32_t numNodes);
  void shutdown();

  void resize(uint32_t numNodes);
  void updateMatrices();

  void setHierarchy(uint32_t nodeIndex, uint32_t parentIndex, uint32_t level);
  void setLocalMatrix(uint32_t nodeIndex, const mat4s& localMatrix);
  void setDebugData(uint32_t nodeIndex, const char* name);

  Framework::Array<mat4s> localMatrices;
  Framework::Array<mat4s> worldMatrices;
  Framework::Array<Hierarchy> nodesHierarchy;
  Framework::Array<SceneGraphNodeDebugData> nodesDebugData;

  Framework::BitSet updatedNodes;

  bool sortUpdateOrder = true;

}; // struct SceneGraph
//---------------------------------------------------------------------------//
} // namespace Graphics
