#include "Foundation/Camera.hpp"

#include "Externals/cglm/struct/cam.h"
#include "Externals/cglm/struct/affine.h"
#include "Externals/cglm/struct/quat.h"
#include "Externals/cglm/struct/project.h"

namespace Framework
{
// Camera ///////////////////////////////////////////////////////////////////////

void Camera::initPerspective(float nearPlane_, float farPlane_, float fovY, float aspectRatio_)
{
  perspective = true;

  nearPlane = nearPlane_;
  farPlane = farPlane_;
  fieldOfViewY = fovY;
  aspectRatio = aspectRatio_;

  reset();
}

void Camera::initOrthographic(
    float nearPlane_, float farPlane_, float viewportWidth_, float viewportHeight_, float zoom_)
{
  perspective = false;

  nearPlane = nearPlane_;
  farPlane = farPlane_;

  viewportWidth = viewportWidth_;
  viewportHeight = viewportHeight_;
  zoom = zoom_;

  reset();
}

void Camera::reset()
{
  position = glms_vec3_zero();
  yaw = 0;
  pitch = 0;
  view = glms_mat4_identity();
  projection = glms_mat4_identity();

  updateProjection = true;
}

void Camera::setViewportSize(float width_, float height_)
{
  viewportWidth = width_;
  viewportHeight = height_;

  updateProjection = true;
}

void Camera::setZoom(float zoom_)
{
  zoom = zoom_;

  updateProjection = true;
}

void Camera::setAspectRatio(float aspectRatio_)
{
  aspectRatio = aspectRatio_;

  updateProjection = true;
}

void Camera::setFovY(float fovY_)
{
  fieldOfViewY = fovY_;

  updateProjection = true;
}

void Camera::update()
{
  // Quaternion based rotation.
  // https://stackoverflow.com/questions/49609654/quaternion-based-first-person-view-camera
  const versors pitchRotation = glms_quat(pitch, 1, 0, 0);
  const versors yawRotation = glms_quat(yaw, 0, 1, 0);
  const versors rotation = glms_quat_normalize(glms_quat_mul(pitchRotation, yawRotation));

  const mat4s translation = glms_translate_make(glms_vec3_scale(position, -1.f));
  view = glms_mat4_mul(glms_quat_mat4(rotation), translation);

  // Update the vectors used for movement
  right = {view.m00, view.m10, view.m20};
  up = {view.m01, view.m11, view.m21};
  direction = {view.m02, view.m12, view.m22};

  if (updateProjection)
  {
    updateProjection = false;

    calculateProjectionMatrix();
  }

  // Calculate final view projection matrix
  calculateViewProjection();
}

void Camera::rotate(float deltaPitch, float deltaYaw)
{

  pitch += deltaPitch;
  yaw += deltaYaw;
}

void Camera::calculateProjectionMatrix()
{
  if (perspective)
  {
    projection = glms_perspective(glm_rad(fieldOfViewY), aspectRatio, nearPlane, farPlane);
  }
  else
  {
    projection = glms_ortho(
        zoom * -viewportWidth / 2.f,
        zoom * viewportWidth / 2.f,
        zoom * -viewportHeight / 2.f,
        zoom * viewportHeight / 2.f,
        nearPlane,
        farPlane);
  }
}

void Camera::calculateViewProjection() { viewProjection = glms_mat4_mul(projection, view); }

vec3s Camera::unproject(const vec3s& screenCoordinates)
{
  return glms_unproject(screenCoordinates, viewProjection, {0, 0, viewportWidth, viewportHeight});
}

vec3s Camera::unprojectInvertedY(const vec3s& screenCoordinates)
{
  const vec3s screenCoordinatesYInv{
      screenCoordinates.x, viewportHeight - screenCoordinates.y, screenCoordinates.z};
  return unproject(screenCoordinatesYInv);
}

void Camera::getProjectionOrtho2d(mat4& outMatrix)
{
  glm_ortho(0, viewportWidth * zoom, 0, viewportHeight * zoom, -1.f, 1.f, outMatrix);
}

void Camera::yawPitchFromDirection(const vec3s& direction, float& yaw, float& pitch)
{

  yaw = glm_deg(atan2f(direction.z, direction.x));
  pitch = glm_deg(asinf(direction.y));
}

} // namespace Framework
