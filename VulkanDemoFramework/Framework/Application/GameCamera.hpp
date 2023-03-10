#pragma once

#include "Foundation/camera.hpp"
#include "Application/keys.hpp"
#include "Application/input.hpp"

namespace Framework
{
//
//
struct GameCamera
{
  void init(
      bool enabled = true,
      float rotationSpeed = 10.f,
      float movementSpeed = 10.f,
      float movementDelta = 0.1f);
  void reset();

  void update(Framework::InputService* input, uint32_t width, uint32_t height, float deltaTime);
  void applyJittering(float x, float y);

  Camera camera;

  float targetYaw;
  float targetPitch;

  float mouseSensitivity;
  float movementDelta;
  uint32_t ignoreDraggingFrames;

  vec3s targetMovement;

  bool enabled;
  bool mouseDragging;

  float rotationSpeed;
  float movementSpeed;
}; // struct GameCamera

} // namespace Framework