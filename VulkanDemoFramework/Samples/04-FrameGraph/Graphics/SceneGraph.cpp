#include "Graphics/SceneGraph.hpp"

namespace Graphics
{
//---------------------------------------------------------------------------//
void SceneGraph::init(Framework::Allocator* residentAllocator, uint32_t numNodes)
{
  nodesHierarchy.init(residentAllocator, numNodes, numNodes);
  localMatrices.init(residentAllocator, numNodes, numNodes);
  worldMatrices.init(residentAllocator, numNodes, numNodes);
  updatedNodes.init(residentAllocator, numNodes);
}
//---------------------------------------------------------------------------//
void SceneGraph::shutdown()
{
  nodesHierarchy.shutdown();
  localMatrices.shutdown();
  worldMatrices.shutdown();
  updatedNodes.shutdown();
}
//---------------------------------------------------------------------------//
void SceneGraph::resize(uint32_t numNodes)
{
  nodesHierarchy.setSize(numNodes);
  localMatrices.setSize(numNodes);
  worldMatrices.setSize(numNodes);

  updatedNodes.resize(numNodes);

  memset(nodesHierarchy.m_Data, 0, numNodes * 4);

  for (uint32_t i = 0; i < numNodes; ++i)
  {
    nodesHierarchy[i].parent = -1;
  }
}
//---------------------------------------------------------------------------//
void SceneGraph::updateMatrices()
{
  // TODO: per level update
  uint32_t maxLevel = 0;
  for (uint32_t i = 0; i < nodesHierarchy.size; ++i)
  {
    maxLevel = max(maxLevel, (uint32_t)nodesHierarchy[i].level);
  }
  uint32_t currentLevel = 0;
  uint32_t nodesVisited = 0;

  while (currentLevel <= maxLevel)
  {

    for (uint32_t i = 0; i < nodesHierarchy.m_Size; ++i)
    {

      if (nodesHierarchy[i].level != currentLevel)
      {
        continue;
      }

      if (updatedNodes.getBit(i) == 0)
      {
        continue;
      }

      updatedNodes.clearBit(i);

      if (nodesHierarchy[i].parent == -1)
      {
        worldMatrices[i] = localMatrices[i];
      }
      else
      {
        const mat4s& parent_matrix = worldMatrices[nodesHierarchy[i].parent];
        worldMatrices[i] = glms_mat4_mul(parent_matrix, localMatrices[i]);
      }

      ++nodesVisited;
    }

    ++currentLevel;
  }
}
//---------------------------------------------------------------------------//
void SceneGraph::setHierarchy(uint32_t nodeIndex, uint32_t parentIndex, uint32_t level)
{
  // Mark node as updated
  updatedNodes.setBit(nodeIndex);
  nodesHierarchy[nodeIndex].parent = parentIndex;
  nodesHierarchy[nodeIndex].level = level;

  sortUpdateOrder = true;
}
//---------------------------------------------------------------------------//
void SceneGraph::setLocalMatrix(uint32_t p_NodeIndex, const mat4s& p_LocalMatrix)
{
  // Mark node as updated
  updatedNodes.setBit(p_NodeIndex);
  localMatrices[p_NodeIndex] = p_LocalMatrix;
}
//---------------------------------------------------------------------------//
} // namespace Graphics
