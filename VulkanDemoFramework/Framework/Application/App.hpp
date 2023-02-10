#pragma once

#include "Foundation/Prerequisites.hpp"

namespace Framework
{

struct ServiceManager;

struct ApplicationConfiguration
{

  uint32_t m_Width;
  uint32_t m_Height;

  const char* m_Name = nullptr;

  bool m_InitBaseServices = false;

  ApplicationConfiguration& setWidth(uint32_t p_Value)
  {
    m_Width = p_Value;
    return *this;
  }
  ApplicationConfiguration& setHeight(uint32_t p_Value)
  {
    m_Height = p_Value;
    return *this;
  }
  ApplicationConfiguration& setName(const char* p_Value)
  {
    m_Name = p_Value;
    return *this;
  }

}; // struct ApplicationConfiguration

struct App
{
  //
  virtual void create(const ApplicationConfiguration& configuration) {}
  virtual void destroy() {}
  virtual bool mainLoop() { return false; }

  // Fixed update. Can be called more than once compared to rendering.
  virtual void fixedUpdate(float delta) {}
  // Variable time update. Called only once per frame.
  virtual void variableUpdate(float delta) {}
  // Rendering with optional interpolation factor.
  virtual void render(float interpolation) {}
  // Per frame begin/end.
  virtual void frameBegin() {}
  virtual void frameEnd() {}

  void run(const ApplicationConfiguration& configuration);

  ServiceManager* service_manager = nullptr;

}; // struct Application

} // namespace Framework
