#pragma once

#include "Foundation/Service.hpp"
#include "Foundation/Array.hpp"

namespace Framework
{

struct WindowConfiguration
{
  uint32_t m_Width;
  uint32_t m_Height;

  const char* m_Name;

  Allocator* m_Allocator;
}; // struct WindowConfiguration

typedef void (*OSMessagesCallback)(void* p_OSEvent, void* p_UserData);

struct Window : public Service
{

  void init(void* p_Configuration) override;
  void shutdown() override;

  void handleOSMessages();

  void setFullscreen(bool p_Value);

  void registerOSMessagesCallback(OSMessagesCallback p_Callback, void* p_UserData);
  void unregisterOSMessagesCallback(OSMessagesCallback p_Callback);

  void centerMouse(bool p_Dragging);

  Array<OSMessagesCallback> m_OSMessagesCallbacks;
  Array<void*> m_OSMessagesCallbacksData;

  void* m_PlatformHandle = nullptr;
  bool m_RequestedExit = false;
  bool m_Resized = false;
  bool m_Minimized = false;
  uint32_t m_Width = 0;
  uint32_t m_Height = 0;
  float m_DisplayRefresh = 1.0f / 60.0f;

  static constexpr const char* ms_Name = "Framework Window Service";

}; // struct Window

} // namespace Framework
