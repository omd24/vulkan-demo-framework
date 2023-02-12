#pragma once

#include "Foundation/Memory.hpp"

#include <assert.h>

namespace Framework
{
struct ResourcePool
{
  void init(Allocator* p_Allocator, uint32_t p_PoolSize, uint32_t p_ResourceSize);
  void shutdown();

  uint32_t obtainResource(); // Returns an index to the resource
  void releaseResource(uint32_t index);
  void freeAllResources();

  void* accessResource(uint32_t index);
  const void* accessResource(uint32_t index) const;

  uint8_t* m_Memory = nullptr;
  uint32_t* m_FreeIndices = nullptr;
  Allocator* m_Allocator = nullptr;

  uint32_t m_FreeIndicesHead = 0;
  uint32_t m_PoolSize = 16;
  uint32_t m_ResourceSize = 4;
  uint32_t m_UsedIndices = 0;

}; // struct ResourcePool

template <typename T> struct ResourcePoolTyped : public ResourcePool
{

  void init(Allocator* allocator, uint32_t pool_size);
  void shutdown();

  T* obtain();
  void release(T* resource);

  T* get(uint32_t index);
  const T* get(uint32_t index) const;

}; // struct ResourcePoolTyped

template <typename T>
inline void ResourcePoolTyped<T>::init(Allocator* p_Allocator, uint32_t p_PoolSize)
{
  ResourcePool::init(p_Allocator, p_PoolSize, sizeof(T));
}

template <typename T> inline void ResourcePoolTyped<T>::shutdown()
{
  if (m_FreeIndicesHead != 0)
  {
    OutputDebugStringA("Resource pool has unfreed resources.\n");

    for (uint32_t i = 0; i < m_FreeIndicesHead; ++i)
    {
      char msg[256]{};
      sprintf(msg, "\tResource %u, %s\n", m_FreeIndices[i], get(m_FreeIndices[i])->name);
      OutputDebugStringA(msg);
    }
  }
  ResourcePool::shutdown();
}

template <typename T> inline T* ResourcePoolTyped<T>::obtain()
{
  uint32_t resourceIndex = ResourcePool::obtainResource();
  if (resourceIndex != UINT32_MAX)
  {
    T* resource = get(resourceIndex);
    resource->m_PoolIndex = resourceIndex;
    return resource;
  }

  return nullptr;
}

template <typename T> inline void ResourcePoolTyped<T>::release(T* resource)
{
  ResourcePool::releaseResource(resource->m_PoolIndex);
}

template <typename T> inline T* ResourcePoolTyped<T>::get(uint32_t index)
{
  return (T*)ResourcePool::accessResource(index);
}

template <typename T> inline const T* ResourcePoolTyped<T>::get(uint32_t index) const
{
  return (const T*)ResourcePool::accessResource(index);
}

} // namespace Framework
