#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 worldPosition;
layout(location = 2) out vec3 worldNormal;
layout(location = 3) out vec3 worldTangent;
layout(location = 4) out vec4 instanceColor;

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

void main() {
    Instance instance = scene.instances[gl_InstanceIndex];
    vec4 world = instance.model * vec4(inPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(instance.model)));
    worldNormal = normalize(normalMatrix * inNormal);
    vec3 tangent = mat3(instance.model) * inTangent;
    worldTangent = normalize(tangent - worldNormal * dot(worldNormal, tangent));
    worldPosition = world.xyz;
    uv = inUv;
    instanceColor = instance.color;
    gl_Position = scene.viewProjection * world;
}
