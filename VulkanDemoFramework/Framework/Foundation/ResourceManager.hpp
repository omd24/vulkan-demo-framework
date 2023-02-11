#pragma once

#include "Foundation/Prerequisites.hpp"
#include "Foundation/HashMap.hpp"

#include <assert.h>

namespace Framework
{

struct ResourceManager;

// Reference counting and named resource.
struct Resource
{
  void addReference() { ++m_References; }
  void removeReference()
  {
    assert(m_References != 0);
    --m_References;
  }

  uint64_t m_References = 0;
  const char* m_Name = nullptr;

}; // struct Resource

//
//
struct ResourceCompiler
{

}; // struct ResourceCompiler

//
//
struct ResourceLoader
{

  virtual Resource* get(const char* p_Name) = 0;
  virtual Resource* get(uint64_t p_HashedName) = 0;

  virtual Resource* unload(const char* p_Name) = 0;

  virtual Resource* createFromFile(
      const char* p_Name, const char* p_Filename, Framework::ResourceManager* p_ResourceManager)
  {
    return nullptr;
  }

}; // struct ResourceLoader

//
//
struct ResourceFilenameResolver
{

  virtual const char* getBinaryPathFromName(const char* p_Name) = 0;

}; // struct ResourceFilenameResolver

//
//
struct ResourceManager
{

  void init(Allocator* p_Allocator, ResourceFilenameResolver* p_Resolver);
  void shutdown();

  template <typename T> T* load(const char* p_Name);

  template <typename T> T* get(const char* p_Name);

  template <typename T> T* get(uint64_t p_HashedName);

  template <typename T> T* reload(const char* p_Name);

  void setLoader(const char* resource_type, ResourceLoader* p_Loader);
  void setCompiler(const char* resource_type, ResourceCompiler* p_Compiler);

  FlatHashMap<uint64_t, ResourceLoader*> m_Loaders;
  FlatHashMap<uint64_t, ResourceCompiler*> m_Compilers;

  Allocator* m_Allocator;
  ResourceFilenameResolver* m_FilenameResolver;

}; // struct ResourceManager

template <typename T> inline T* ResourceManager::load(const char* p_Name)
{
  ResourceLoader* loader = m_Loaders.get(T::kTypeHash);
  if (loader)
  {
    // Search if the resource is already in cache
    T* resource = (T*)loader->get(p_Name);
    if (resource)
      return resource;

    // Resource not in cache, create from file
    const char* path = m_FilenameResolver->getBinaryPathFromName(p_Name);
    return (T*)loader->createFromFile(p_Name, path, this);
  }
  return nullptr;
}

template <typename T> inline T* ResourceManager::get(const char* p_Name)
{
  ResourceLoader* loader = m_Loaders.get(T::kTypeHash);
  if (loader)
  {
    return (T*)loader->get(p_Name);
  }
  return nullptr;
}

template <typename T> inline T* ResourceManager::get(uint64_t p_HashedName)
{
  ResourceLoader* loader = m_Loaders.get(T::kTypeHash);
  if (loader)
  {
    return (T*)loader->get(p_HashedName);
  }
  return nullptr;
}

template <typename T> inline T* ResourceManager::reload(const char* p_Name)
{
  ResourceLoader* loader = m_Loaders.get(T::kTypeHash);
  if (loader)
  {
    T* resource = (T*)loader->get(p_Name);
    if (resource)
    {
      loader->unload(p_Name);

      // Resource not in cache, create from file
      const char* path = m_FilenameResolver->getBinaryPathFromName(p_Name);
      return (T*)loader->createFromFile(p_Name, path, this);
    }
  }
  return nullptr;
}

} // namespace Framework
