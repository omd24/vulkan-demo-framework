#pragma once

namespace Framework
{
struct Service
{
  virtual void init(void* p_Configuration) {}
  virtual void shutdown() {}
}; // struct Service

#define FRAMEWORK_DECLARE_SERVICE(Type) static Type* instance();

} // namespace Framework
