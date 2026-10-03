#version 450
layout(location = 0) in vec2 uv;
layout(location = 4) in vec4 instanceColor;
layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 1) uniform Material {
    vec4 baseColor;
    vec4 factors;
    vec4 emissive;
    ivec4 flags;
} material;
layout(set = 0, binding = 2) uniform sampler2D baseTexture;

void main() {
    vec4 base = material.baseColor * instanceColor * texture(baseTexture, uv);
    if (material.flags.y == 1 && base.a < material.emissive.a)
        discard;
    float alpha = material.flags.y == 2 ? base.a : 1.0;
    vec3 color = material.flags.z == 1 ? base.rgb : pow(max(base.rgb, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, alpha);
}
