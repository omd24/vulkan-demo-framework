#pragma once

#include "Foundation/Prerequisites.hpp"

#include "Externals/cglm/struct/mat4.h"

namespace Framework
{
//
// Camera struct - can be both perspective and orthographic.
//
struct Camera
{

  void initPerspective(float nearPlane, float farPlane, float fovY, float aspectRatio);
  void initOrthographic(
      float nearPlane, float farPlane, float viewportWidth, float viewportHeight, float zoom);

  void reset();

  void setViewportSize(float width, float height);
  void setZoom(float zoom);
  void setAspectRatio(float aspectRatio);
  void setFovY(float fovY);

  void update();
  void rotate(float deltaPitch, float deltaYaw);

  void calculateProjectionMatrix();
  void calculateViewProjection();

  // Project/unproject
  vec3s unproject(const vec3s& screenCoordinates);

  // Unproject by inverting the y of the screen coordinate.
  vec3s unprojectInvertedY(const vec3s& screenCoordinates);

  void getProjectionOrtho2d(mat4& out_matrix);

  static void yawPitchFromDirection(const vec3s& direction, float& yaw, float& pitch);

  mat4s view;
  mat4s projection;
  mat4s viewProjection;

  vec3s position;
  vec3s right;
  vec3s direction;
  vec3s up;

  float yaw;
  float pitch;

  float nearPlane;
  float farPlane;

  float fieldOfViewY;
  float aspectRatio;

  float zoom;
  float viewportWidth;
  float viewportHeight;

  bool perspective;
  bool updateProjection;

}; // struct Camera

} // namespace Framework
