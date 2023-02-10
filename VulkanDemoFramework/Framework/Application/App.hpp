#pragma once

#include "Foundation/Prerequisites.hpp"

namespace Framework
{
struct ServiceManager;
//---------------------------------------------------------------------------//
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
//---------------------------------------------------------------------------//
struct App
{
  //
  virtual void create(const ApplicationConfiguration& p_Configuration) {}
  virtual void destroy() {}
  virtual bool mainLoop() { return false; }

  // Fixed update. Can be called more than once compared to rendering.
  virtual void fixedUpdate(float p_Delta) {}
  // Variable time update. Called only once per frame.
  virtual void variableUpdate(float p_Delta) {}
  // Rendering with optional interpolation factor.
  virtual void render(float p_Interpolation) {}
  // Per frame begin/end.
  virtual void frameBegin() {}
  virtual void frameEnd() {}

  void run(const ApplicationConfiguration& p_Configuration);

  ServiceManager* service_manager = nullptr;

}; // struct Application
} // namespace Framework
