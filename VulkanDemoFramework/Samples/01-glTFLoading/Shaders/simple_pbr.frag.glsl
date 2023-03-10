
#version 450
layout(std140, binding = 0) uniform LocalConstants {
    mat4 m;
    mat4 vp;
    mat4 mInverse;
    vec4 eye;
    vec4 light;
};

layout(std140, binding = 4) uniform MaterialConstant {
    vec4 base_color_factor;
};

layout (binding = 1) uniform sampler2D diffuseTexture;
layout (binding = 2) uniform sampler2D occlusionRoughnessMetalnessTexture;
layout (binding = 3) uniform sampler2D normalTexture;

layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec4 vTangent;
layout (location = 3) in vec4 vPosition;

layout (location = 0) out vec4 frag_color;

#define PI 3.1415926538

vec3 decode_srgb( vec3 c ) {
    vec3 result;
    if ( c.r <= 0.04045) {
        result.r = c.r / 12.92;
    } else {
        result.r = pow( ( c.r + 0.055 ) / 1.055, 2.4 );
    }

    if ( c.g <= 0.04045) {
        result.g = c.g / 12.92;
    } else {
        result.g = pow( ( c.g + 0.055 ) / 1.055, 2.4 );
    }

    if ( c.b <= 0.04045) {
        result.b = c.b / 12.92;
    } else {
        result.b = pow( ( c.b + 0.055 ) / 1.055, 2.4 );
    }

    return clamp( result, 0.0, 1.0 );
}

vec3 encode_srgb( vec3 c ) {
    vec3 result;
    if ( c.r <= 0.0031308) {
        result.r = c.r * 12.92;
    } else {
        result.r = 1.055 * pow( c.r, 1.0 / 2.4 ) - 0.055;
    }

    if ( c.g <= 0.0031308) {
        result.g = c.g * 12.92;
    } else {
        result.g = 1.055 * pow( c.g, 1.0 / 2.4 ) - 0.055;
    }

    if ( c.b <= 0.0031308) {
        result.b = c.b * 12.92;
    } else {
        result.b = 1.055 * pow( c.b, 1.0 / 2.4 ) - 0.055;
    }

    return clamp( result, 0.0, 1.0 );
}

float heaviside( float v ) {
    if ( v > 0.0 ) return 1.0;
    else return 0.0;
}

void main() {
    // NOTE: normal textures are encoded to [0, 1] but need to be mapped to [-1, 1] value
    vec3 bump_normal = normalize( texture(normalTexture, vTexcoord0).rgb * 2.0 - 1.0 );
    vec3 tangent = normalize( vTangent.xyz );
    vec3 bitangent = cross( normalize( vNormal ), tangent ) * vTangent.w;

    mat3 TBN = transpose(mat3(
        tangent,
        bitangent,
        normalize( vNormal )
    ));

    // vec3 V = normalize(eye.xyz - vPosition.xyz);
    // vec3 L = normalize(light.xyz - vPosition.xyz);
    // vec3 N = normalize(vNormal);
    // vec3 H = normalize(L + V);

    vec3 V = normalize( TBN * ( eye.xyz - vPosition.xyz ) );
    vec3 L = normalize( TBN * ( light.xyz - vPosition.xyz ) );
    vec3 N = bump_normal;
    vec3 H = normalize( L + V );

    vec4 rmo = texture(occlusionRoughnessMetalnessTexture, vTexcoord0);

    // Green channel contains roughness values
    float roughness = rmo.g;
    float alpha = pow(roughness, 2.0);

    // Blue channel contains metalness
    float metalness = rmo.b;

    // Red channel for occlusion value

    vec4 base_colour = texture(diffuseTexture, vTexcoord0) * base_color_factor;
    base_colour.rgb = decode_srgb( base_colour.rgb );

    // https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#specular-brdf
    float NdotH = dot(N, H);
    float alpha_squared = alpha * alpha;
    float d_denom = ( NdotH * NdotH ) * ( alpha_squared - 1.0 ) + 1.0;
    float distribution = ( alpha_squared * heaviside( NdotH ) ) / ( PI * d_denom * d_denom );

    float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    float HdotL = dot(H, L);
    float HdotV = dot(H, V);

    float visibility = ( heaviside( HdotL ) / ( abs( NdotL ) + sqrt( alpha_squared + ( 1.0 - alpha_squared ) * ( NdotL * NdotL ) ) ) ) * ( heaviside( HdotV ) / ( abs( NdotV ) + sqrt( alpha_squared + ( 1.0 - alpha_squared ) * ( NdotV * NdotV ) ) ) );

    float specular_brdf = visibility * distribution;

    vec3 diffuse_brdf = (1 / PI) * base_colour.rgb;

    // NOTE: f0 in the formula notation refers to the base colour here
    vec3 conductor_fresnel = specular_brdf * ( base_colour.rgb + ( 1.0 - base_colour.rgb ) * pow( 1.0 - abs( HdotV ), 5 ) );

    // NOTE: f0 in the formula notation refers to the value derived from ior = 1.5
    float f0 = 0.04; // pow( ( 1 - ior ) / ( 1 + ior ), 2 )
    float fr = f0 + ( 1 - f0 ) * pow(1 - abs( HdotV ), 5 );
    vec3 fresnel_mix = mix( diffuse_brdf, vec3( specular_brdf ), fr );

    vec3 material_colour = mix( fresnel_mix, conductor_fresnel, metalness );

    frag_color = vec4( encode_srgb( material_colour ), base_colour.a );
}