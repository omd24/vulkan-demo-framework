#version 450
layout( location = 0 ) in vec2 Position;
layout( location = 1 ) in vec2 UV;
layout( location = 2 ) in uvec4 Color;
layout( location = 0 ) out vec2 Frag_UV;
layout( location = 1 ) out vec4 Frag_Color;
layout (location = 2) flat out uint texture_id;
layout( std140, set = 1, binding = 0 ) uniform LocalConstants { mat4 ProjMtx; };
void main()
{
  Frag_UV = UV;
  Frag_Color = Color / 255.0f;
  texture_id = gl_InstanceIndex;
  gl_Position = ProjMtx * vec4(Position.xy, 0, 1);
}