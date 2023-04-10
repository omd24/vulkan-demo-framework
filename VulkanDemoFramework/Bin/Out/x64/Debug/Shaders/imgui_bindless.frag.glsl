#version 450
#extension GL_EXT_nonuniform_qualifier : enable
layout (location = 0) in vec2 Frag_UV;
layout (location = 1) in vec4 Frag_Color;
layout (location = 2) flat in uint texture_id;
layout (location = 0) out vec4 Out_Color;
layout (set = 1, binding = 10) uniform sampler2D textures[];
void main()
{
    Out_Color = Frag_Color * texture(textures[nonuniformEXT(texture_id)], Frag_UV.st);
}