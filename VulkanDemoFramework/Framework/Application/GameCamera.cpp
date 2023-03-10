#include "GameCamera.hpp"

#include "Foundation/Prerequisites.hpp"
#include "Foundation/Numerics.hpp"

#include "Externals/cglm/struct/affine.h"
#include "Externals/cglm/struct/cam.h"
#include "Externals/imgui/imgui.h"

namespace Framework
{
// GameCamera //////////////////////////////////////////////////////////////////
void GameCamera::init(
    bool enabled_, float rotationSpeed_, float movementSpeed_, float movementDelta_)
{

  reset();
  enabled = enabled_;

  rotationSpeed = rotationSpeed_;
  movementSpeed = movementSpeed_;
  movementDelta = movementDelta_;
}

void GameCamera::reset()
{

  targetYaw = 0.0f;
  targetPitch = 0.0f;

  targetMovement = camera.position;

  mouseDragging = false;
  ignoreDraggingFrames = 3;
  mouseSensitivity = 1.0f;
}

// Taken from this article:
// http://www.rorydriscoll.com/2016/03/07/frame-rate-independent-damping-using-lerp/
//
float lerp(float a, float b, float t, float dt) { return glm_lerp(a, b, 1.f - powf(1 - t, dt)); }

vec3s lerp3(const vec3s& from, const vec3s& to, float t, float dt)
{
  return vec3s{lerp(from.x, to.x, t, dt), lerp(from.y, to.y, t, dt), lerp(from.z, to.z, t, dt)};
}

void GameCamera::update(
    InputService* input, uint32_t windowWidth, uint32_t windowHeight, float deltaTime)
{

  if (!enabled)
    return;

  camera.update();

  // Ignore first dragging frames for mouse movement waiting the cursor to be placed at the center
  // of the screen.

  if (input->isMouseDragging(MOUSE_BUTTONS_RIGHT) && !ImGui::IsAnyItemHovered())
  {

    if (ignoreDraggingFrames == 0)
    {
      targetYaw -=
          (input->m_MousePosition.x - roundU32(windowWidth / 2.f)) * mouseSensitivity * deltaTime;
      targetPitch -=
          (input->m_MousePosition.y - roundU32(windowHeight / 2.f)) * mouseSensitivity * deltaTime;
    }
    else
    {
      --ignoreDraggingFrames;
    }
    mouseDragging = true;
  }
  else
  {
    mouseDragging = false;

    ignoreDraggingFrames = 3;
  }

  vec3s cameraMovement{0, 0, 0};
  float cameraMovementDelta = movementDelta;

  if (input->isKeyDown(KEY_RSHIFT) || input->isKeyDown(KEY_LSHIFT))
  {
    cameraMovementDelta *= 10.0f;
  }

  if (input->isKeyDown(KEY_RALT) || input->isKeyDown(KEY_LALT))
  {
    cameraMovementDelta *= 100.0f;
  }

  if (input->isKeyDown(KEY_RCTRL) || input->isKeyDown(KEY_LCTRL))
  {
    cameraMovementDelta *= 0.1f;
  }

  if (input->isKeyDown(KEY_LEFT) || input->isKeyDown(KEY_A))
  {
    cameraMovement =
        glms_vec3_add(cameraMovement, glms_vec3_scale(camera.right, -cameraMovementDelta));
  }
  else if (input->isKeyDown(KEY_RIGHT) || input->isKeyDown(KEY_D))
  {
    cameraMovement =
        glms_vec3_add(cameraMovement, glms_vec3_scale(camera.right, cameraMovementDelta));
  }

  if (input->isKeyDown(KEY_PAGEDOWN) || input->isKeyDown(KEY_E))
  {
    cameraMovement =
        glms_vec3_add(cameraMovement, glms_vec3_scale(camera.up, -cameraMovementDelta));
  }
  else if (input->isKeyDown(KEY_PAGEUP) || input->isKeyDown(KEY_Q))
  {
    cameraMovement = glms_vec3_add(cameraMovement, glms_vec3_scale(camera.up, cameraMovementDelta));
  }

  if (input->isKeyDown(KEY_UP) || input->isKeyDown(KEY_W))
  {
    cameraMovement =
        glms_vec3_add(cameraMovement, glms_vec3_scale(camera.direction, cameraMovementDelta));
  }
  else if (input->isKeyDown(KEY_DOWN) || input->isKeyDown(KEY_S))
  {
    cameraMovement =
        glms_vec3_add(cameraMovement, glms_vec3_scale(camera.direction, -cameraMovementDelta));
  }

  targetMovement = glms_vec3_add((vec3s&)targetMovement, cameraMovement);

  {
    // Update camera rotation
    const float tweenSpeed = rotationSpeed * deltaTime;
    camera.rotate((targetPitch - camera.pitch) * tweenSpeed, (targetYaw - camera.yaw) * tweenSpeed);

    // Update camera position
    const float tweenPositionSpeed = movementSpeed * deltaTime;
    camera.position = lerp3(camera.position, targetMovement, 0.9f, tweenPositionSpeed);
  }
}

void GameCamera::applyJittering(float x, float y)
{
  // Reset camera projection
  camera.calculateProjectionMatrix();

  // camera.projection.m20 += x;
  // camera.projection.m21 += y;
  mat4s jitteringMatrix = glms_translate_make({x, y, 0.0f});
  camera.projection = glms_mat4_mul(jitteringMatrix, camera.projection);
  camera.calculateViewProjection();
}

} // namespace Framework