#version 450
layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 worldPosition;
layout(location = 2) in vec3 worldNormal;
layout(location = 3) in vec3 worldTangent;
layout(location = 4) in vec4 instanceColor;
layout(location = 0) out vec4 outColor;

struct DirectionalLight { vec4 directionIntensity; vec4 color; };
struct PointLight { vec4 positionRange; vec4 colorIntensity; };
struct Instance { mat4 model; vec4 color; };
layout(std430, set = 0, binding = 0) readonly buffer Scene {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    ivec4 counts;
    DirectionalLight directional[4];
    PointLight point[8];
    Instance instances[];
} scene;
layout(std140, set = 0, binding = 1) uniform Material {
    vec4 baseColor;
    vec4 factors;
    vec4 emissive;
    ivec4 flags;
} material;
layout(set = 0, binding = 2) uniform sampler2D baseTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 4) uniform sampler2D normalTexture;
layout(set = 0, binding = 5) uniform sampler2D occlusionTexture;
layout(set = 0, binding = 6) uniform sampler2D emissiveTexture;

const float pi = 3.141592653589793;

vec3 directBrdf(vec3 N, vec3 V, vec3 L, vec3 base, float metallic, float roughness) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    float d = a2 / max(pi * pow(NdotH * NdotH * (a2 - 1.0) + 1.0, 2.0), 0.0001);
    float k = pow(roughness + 1.0, 2.0) / 8.0;
    float gV = NdotV / (NdotV * (1.0 - k) + k);
    float gL = NdotL / max(NdotL * (1.0 - k) + k, 0.0001);
    vec3 f0 = mix(vec3(0.04), base, metallic);
    vec3 f = f0 + (1.0 - f0) * pow(1.0 - VdotH, 5.0);
    vec3 specular = d * gV * gL * f / max(4.0 * NdotV * NdotL, 0.001);
    vec3 diffuse = (1.0 - f) * (1.0 - metallic) * base / pi;
    return (diffuse + specular) * NdotL;
}

void main() {
    vec4 base = material.baseColor * instanceColor * texture(baseTexture, uv);
    if (material.flags.y == 1 && base.a < material.emissive.a)
        discard;
    float alpha = material.flags.y == 2 ? base.a : 1.0;
    vec4 mr = texture(metallicRoughnessTexture, uv);
    float metallic = clamp(material.factors.x * mr.b, 0.0, 1.0);
    float roughness = clamp(material.factors.y * mr.g, 0.04, 1.0);
    vec3 N = normalize(worldNormal);
    vec3 T = normalize(worldTangent);
    vec3 B = normalize(cross(N, T));
    vec3 mapNormal = texture(normalTexture, uv).xyz * 2.0 - 1.0;
    mapNormal.xy *= material.factors.z;
    N = normalize(mat3(T, B, N) * normalize(mapNormal));
    vec3 V = normalize(scene.cameraPosition.xyz - worldPosition);
    vec3 radiance = vec3(0.0);
    for (int i = 0; i < scene.counts.x; ++i) {
        vec3 L = normalize(-scene.directional[i].directionIntensity.xyz);
        radiance += directBrdf(N, V, L, base.rgb, metallic, roughness) *
                    scene.directional[i].color.rgb * scene.directional[i].directionIntensity.w;
    }
    for (int i = 0; i < scene.counts.y; ++i) {
        vec3 delta = scene.point[i].positionRange.xyz - worldPosition;
        float distance = length(delta);
        float range = scene.point[i].positionRange.w;
        if (distance >= range)
            continue;
        float fade = max(1.0 - pow(distance / range, 4.0), 0.0);
        float attenuation = fade * fade / (1.0 + distance * distance);
        radiance += directBrdf(N, V, delta / max(distance, 0.0001), base.rgb, metallic, roughness) *
                    scene.point[i].colorIntensity.rgb * scene.point[i].colorIntensity.w * attenuation;
    }
    float ao = mix(1.0, texture(occlusionTexture, uv).r, material.factors.w);
    vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
    radiance += scene.ambient.rgb * scene.ambient.w *
                (base.rgb * (1.0 - metallic) + f0 * (1.0 - roughness * 0.5)) * ao;
    radiance += material.emissive.rgb * texture(emissiveTexture, uv).rgb;
    vec3 mapped = radiance / (radiance + vec3(1.0));
    outColor = vec4(pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2)), alpha);
}
