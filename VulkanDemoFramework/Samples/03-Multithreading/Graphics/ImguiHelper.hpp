#pragma once

#include "Foundation/Service.hpp"
#include "Graphics/Renderer.hpp"

namespace Graphics
{
//---------------------------------------------------------------------------//
namespace ImguiUtil
{
//---------------------------------------------------------------------------//
enum ImguiStyles
{
  kDefault = 0,
  kGreenBlue,
  kDarkRed,
  kDarkGold
};
//---------------------------------------------------------------------------//
struct ImguiServiceConfiguration
{
  GpuDevice* gpuDevice;
  void* windowHandle;
};
struct ImguiService : public Framework::Service
{
  static ImguiService* instance();

  void init(void* p_Configuration) override;
  void shutdown() override;

  void newFrame();
  void render(CommandBuffer& p_Commands);

  // Removes the Texture from the Cache and destroy the associated Descriptor Set.
  void removeCachedTexture(TextureHandle& p_Texture);

  void setStyle(ImguiStyles p_Style);

  GpuDevice* m_GpuDevice;

  static constexpr const char* ms_Name = "Graphics_imgui_service";

}; // ImGuiService
//---------------------------------------------------------------------------//
void imguiLogInit();
void imguiLogShutdown();
void imguiLogDraw();

// FPS graph
void fpsInit();
void fpsShutdown();
void fpsAdd(float p_DeltaTime);
void fpsDraw();
//---------------------------------------------------------------------------//
} // namespace ImguiUtil
//---------------------------------------------------------------------------//
} // namespace Graphics
