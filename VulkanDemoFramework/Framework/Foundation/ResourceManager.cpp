#include "ResourceManager.hpp"

namespace Framework
{
void ResourceManager::init(Allocator* p_Allocator, ResourceFilenameResolver* p_Resolver)
{
  this->m_Allocator = p_Allocator;
  this->m_FilenameResolver = p_Resolver;

  m_Loaders.init(m_Allocator, 8);
  m_Compilers.init(m_Allocator, 8);
}

void ResourceManager::shutdown()
{
  m_Loaders.shutdown();
  m_Compilers.shutdown();
}

void ResourceManager::setLoader(const char* p_ResourceType, ResourceLoader* p_Loader)
{
  const uint64_t hashedName = hashCalculate(p_ResourceType);
  m_Loaders.insert(hashedName, p_Loader);
}

void ResourceManager::setCompiler(const char* p_ResourceType, ResourceCompiler* p_Compiler)
{
  const uint64_t hashedName = hashCalculate(p_ResourceType);
  m_Compilers.insert(hashedName, p_Compiler);
}

} // namespace Framework
