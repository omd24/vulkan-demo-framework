#include "Window.hpp"
#include "Foundation/Numerics.hpp"

#include <SDL.h>
#include <SDL_vulkan.h>

#include "Externals/imgui/imgui.h"
#include "Externals/imgui/imgui_impl_sdl.h"

static SDL_Window* g_Window = nullptr;

namespace Framework
{

static float sdlGetMonitorRefresh()
{
  SDL_DisplayMode current;
  int should_be_zero = SDL_GetCurrentDisplayMode(0, &current);
  assert(!should_be_zero);
  return 1.0f / current.refresh_rate;
}

void Window::init(void* p_Configuration)
{
  OutputDebugStringA("Window service init\n");

  if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
  {
    char msg[256];
    sprintf(msg, "SDL Init error: %s\n", SDL_GetError());
    OutputDebugStringA(msg);

    return;
  }

  SDL_DisplayMode current;
  SDL_GetCurrentDisplayMode(0, &current);

  WindowConfiguration& configuration = *(WindowConfiguration*)p_Configuration;

  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

  g_Window = SDL_CreateWindow(
      configuration.m_Name,
      SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED,
      configuration.m_Width,
      configuration.m_Height,
      window_flags);

  OutputDebugStringA("Window created successfully\n");

  int windowWidth, windowHeight;
  SDL_Vulkan_GetDrawableSize(g_Window, &windowWidth, &windowHeight);

  m_Width = (uint32_t)windowWidth;
  m_Height = (uint32_t)windowHeight;

  // Assing this so it can be accessed from outside.
  m_PlatformHandle = g_Window;

  // Callbacks
  m_OSMessagesCallbacks.init(configuration.m_Allocator, 4);
  m_OSMessagesCallbacksData.init(configuration.m_Allocator, 4);

  m_DisplayRefresh = sdlGetMonitorRefresh();
}

void Window::shutdown()
{

  m_OSMessagesCallbacksData.shutdown();
  m_OSMessagesCallbacks.shutdown();

  SDL_DestroyWindow(g_Window);
  SDL_Quit();

  OutputDebugStringA("Window service shutdown\n");
}

void Window::handleOSMessages()
{
  SDL_Event event;
  while (SDL_PollEvent(&event))
  {

    ImGui_ImplSDL2_ProcessEvent(&event);

    switch (event.type)
    {
    case SDL_QUIT: {
      m_RequestedExit = true;
      goto propagateEvent;
      break;
    }

    // Handle subevent
    case SDL_WINDOWEVENT: {
      switch (event.window.event)
      {
      case SDL_WINDOWEVENT_SIZE_CHANGED:
      case SDL_WINDOWEVENT_RESIZED: {
        {
          uint32_t newWidth = (uint32_t)(event.window.data1);
          uint32_t newHeight = (uint32_t)(event.window.data2);

          // Update only if needed.
          if (newWidth != m_Width || newHeight != m_Height)
          {
            m_Resized = true;
            m_Width = newWidth;
            m_Height = newHeight;

            {
              char msg[256];
              sprintf(msg, "Resizing to %u, %u\n", m_Width, m_Height);
              OutputDebugStringA(msg);
            }
          }
        }

        break;
      }

      case SDL_WINDOWEVENT_FOCUS_GAINED: {
        OutputDebugStringA("Focus Gained\n");
        break;
      }
      case SDL_WINDOWEVENT_FOCUS_LOST: {
        OutputDebugStringA("Focus Lost\n");
        break;
      }
      case SDL_WINDOWEVENT_MAXIMIZED: {
        OutputDebugStringA("Maximized\n");
        m_Minimized = false;
        break;
      }
      case SDL_WINDOWEVENT_MINIMIZED: {
        OutputDebugStringA("Minimized\n");
        m_Minimized = true;
        break;
      }
      case SDL_WINDOWEVENT_RESTORED: {
        OutputDebugStringA("Restored\n");
        m_Minimized = false;
        break;
      }
      case SDL_WINDOWEVENT_TAKE_FOCUS: {
        OutputDebugStringA("Take Focus\n");
        break;
      }
      case SDL_WINDOWEVENT_EXPOSED: {
        OutputDebugStringA("Exposed\n");
        break;
      }

      case SDL_WINDOWEVENT_CLOSE: {
        m_RequestedExit = true;
        OutputDebugStringA("Window close event received.\n");
        break;
      }
      default: {
        m_DisplayRefresh = sdlGetMonitorRefresh();
        break;
      }
      }
      goto propagateEvent;
      break;
    }
    }
  // Maverick:
  propagateEvent:
    // Callbacks
    for (uint32_t i = 0; i < m_OSMessagesCallbacks.m_Size; ++i)
    {
      OSMessagesCallback callback = m_OSMessagesCallbacks[i];
      callback(&event, m_OSMessagesCallbacksData[i]);
    }
  }
}

void Window::setFullscreen(bool p_Value)
{
  if (p_Value)
    SDL_SetWindowFullscreen(g_Window, SDL_WINDOW_FULLSCREEN_DESKTOP);
  else
  {
    SDL_SetWindowFullscreen(g_Window, 0);
  }
}

void Window::registerOSMessagesCallback(OSMessagesCallback p_Callback, void* p_UserData)
{

  m_OSMessagesCallbacks.push(p_Callback);
  m_OSMessagesCallbacksData.push(p_UserData);
}

void Window::unregisterOSMessagesCallback(OSMessagesCallback p_Callback)
{
  assert(
      m_OSMessagesCallbacks.m_Size < 8 &&
      "This array is too big for a linear search. Consider using something different!");

  for (uint32_t i = 0; i < m_OSMessagesCallbacks.m_Size; ++i)
  {
    if (m_OSMessagesCallbacks[i] == p_Callback)
    {
      m_OSMessagesCallbacks.deleteSwap(i);
      m_OSMessagesCallbacksData.deleteSwap(i);
    }
  }
}

void Window::centerMouse(bool p_Dragging)
{
  if (p_Dragging)
  {
    SDL_WarpMouseInWindow(
        g_Window, Framework::roundU32(m_Width / 2.f), Framework::roundU32(m_Height / 2.f));
    SDL_SetWindowGrab(g_Window, SDL_TRUE);
    SDL_SetRelativeMouseMode(SDL_TRUE);
  }
  else
  {
    SDL_SetWindowGrab(g_Window, SDL_FALSE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
  }
}

} // namespace Framework
