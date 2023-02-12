#include "Foundation/ResourcePool.hpp"

#include <string.h>

namespace Framework
{
static const uint32_t kInvalidIndex = 0xffffffff;

/// Resource Pool

void ResourcePool::init(Allocator* p_Allocator, uint32_t p_PoolSize, uint32_t p_ResourceSize)
{

  m_Allocator = p_Allocator;
  m_PoolSize = p_PoolSize;
  m_ResourceSize = p_ResourceSize;

  // Group allocate ( resource size + uint32_t )
  size_t allocation_size = m_PoolSize * (m_ResourceSize + sizeof(uint32_t));
  m_Memory = (uint8_t*)m_Allocator->allocate(allocation_size, 1);
  memset(m_Memory, 0, allocation_size);

  // Allocate and add free indices
  m_FreeIndices = (uint32_t*)(m_Memory + m_PoolSize * m_ResourceSize);
  m_FreeIndicesHead = 0;

  for (uint32_t i = 0; i < m_PoolSize; ++i)
  {
    m_FreeIndices[i] = i;
  }

  m_UsedIndices = 0;
}

void ResourcePool::shutdown()
{

  if (m_FreeIndicesHead != 0)
  {
    OutputDebugStringA("Resource pool has unfreed resources.\n");

    for (uint32_t i = 0; i < m_FreeIndicesHead; ++i)
    {
      char msg[256];
      sprintf(msg, "\tResource %u\n", m_FreeIndices[i]);
      OutputDebugStringA(msg);
    }
  }

  assert(m_UsedIndices == 0);

  m_Allocator->deallocate(m_Memory);
}

void ResourcePool::freeAllResources()
{
  m_FreeIndicesHead = 0;
  m_UsedIndices = 0;

  for (uint32_t i = 0; i < m_PoolSize; ++i)
  {
    m_FreeIndices[i] = i;
  }
}

uint32_t ResourcePool::obtainResource()
{
  // TODO: add bits for checking if resource is alive and use bitmasks.
  if (m_FreeIndicesHead < m_PoolSize)
  {
    const uint32_t freeIndex = m_FreeIndices[m_FreeIndicesHead++];
    ++m_UsedIndices;
    return freeIndex;
  }
  // Error: no more resources left!
  assert(false);
  return kInvalidIndex;
}

void ResourcePool::releaseResource(uint32_t handle)
{
  // TODO: add bits for checking if resource is alive and use bitmasks.
  m_FreeIndices[--m_FreeIndicesHead] = handle;
  --m_UsedIndices;
}

void* ResourcePool::accessResource(uint32_t handle)
{
  if (handle != kInvalidIndex)
  {
    return &m_Memory[handle * m_ResourceSize];
  }
  return nullptr;
}

const void* ResourcePool::accessResource(uint32_t handle) const
{
  if (handle != kInvalidIndex)
  {
    return &m_Memory[handle * m_ResourceSize];
  }
  return nullptr;
}

} // namespace Framework
