#version 450
layout(std140, binding = 0) uniform LocalConstants {
    mat4 m;
    mat4 vp;
    mat4 mInverse;
    vec4 eye;
    vec4 light;
};

layout(location=0) in vec3 position;
layout(location=1) in vec4 tangent;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 texCoord0;

layout (location = 0) out vec2 vTexcoord0;
layout (location = 1) out vec3 vNormal;
layout (location = 2) out vec4 vTangent;
layout (location = 3) out vec4 vPosition;

void main() {
    gl_Position = vp * m * vec4(position, 1);
    vPosition = m * vec4(position, 1.0);
    vTexcoord0 = texCoord0;
    vNormal = mat3(mInverse) * normal;
    vTangent = tangent;
}